#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: da1.c,v 1.70 1997/10/28 14:25:01 bsmith Exp bsmith $";
#endif

/* 
   Code for manipulating distributed regular 1d arrays in parallel.
   This file was created by Peter Mell   6/30/95    
*/

#include "src/da/daimpl.h"     /*I  "da.h"   I*/
#include "pinclude/pviewer.h"   
#include <math.h>

#undef __FUNC__  
#define __FUNC__ "DAView_1d"
int DAView_1d(PetscObject pobj,Viewer viewer)
{
  DA          da  = (DA) pobj;
  int         rank, ierr;
  ViewerType  vtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(da,DA_COOKIE);

  MPI_Comm_rank(da->comm,&rank); 

  if (!viewer) { 
    viewer = VIEWER_STDOUT_SELF; 
  }

  ierr = ViewerGetType(viewer,&vtype); CHKERRQ(ierr);


  if (vtype == ASCII_FILE_VIEWER) {
    FILE *fd;
    ierr = ViewerASCIIGetPointer(viewer,&fd); CHKERRQ(ierr);
    PetscSequentialPhaseBegin(da->comm,1);
    fprintf(fd,"Processor [%d] M %d m %d w %d s %d\n",rank,da->M,
                 da->m,da->w,da->s);
    fprintf(fd,"X range: %d %d\n",da->xs,da->xe);
    fflush(fd);
    PetscSequentialPhaseEnd(da->comm,1);
  } else if (vtype == DRAW_VIEWER) {
    Draw       draw;
    double     ymin = -1,ymax = 1,xmin = -1,xmax = da->M,x;
    int        base;
    char       node[10];
    PetscTruth isnull;

    ierr = ViewerDrawGetDraw(viewer,&draw); CHKERRQ(ierr);
    ierr = DrawIsNull(draw,&isnull); CHKERRQ(ierr); if (isnull) PetscFunctionReturn(0);

    ierr = DrawSetCoordinates(draw,xmin,ymin,xmax,ymax);CHKERRQ(ierr);

    /* first processor draws all node lines */

    if (!rank) {
      ymin = 0.0; ymax = 0.3;

      for ( xmin=0; xmin<da->M; xmin++ ) {
         ierr = DrawLine(draw,xmin,ymin,xmin,ymax,DRAW_BLACK);CHKERRQ(ierr);
      }

      xmin = 0.0; xmax = da->M - 1;
      ierr = DrawLine(draw,xmin,ymin,xmax,ymin,DRAW_BLACK);CHKERRQ(ierr);
      ierr = DrawLine(draw,xmin,ymax,xmax,ymax,DRAW_BLACK);CHKERRQ(ierr);
    }

    ierr = DrawSynchronizedFlush(draw); CHKERRQ(ierr);
    ierr = DrawPause(draw);CHKERRQ(ierr);
    ierr = MPI_Barrier(da->comm);CHKERRQ(ierr);

    /* draw my box */
    ymin = 0; ymax = 0.3; xmin = da->xs / da->w; xmax = (da->xe / da->w)  - 1;
    DrawLine(draw,xmin,ymin,xmax,ymin,DRAW_RED);
    DrawLine(draw,xmin,ymin,xmin,ymax,DRAW_RED);
    DrawLine(draw,xmin,ymax,xmax,ymax,DRAW_RED);
    DrawLine(draw,xmax,ymin,xmax,ymax,DRAW_RED);

    /* Put in index numbers */
    base = da->base / da->w;
    for ( x=xmin; x<=xmax; x++ ) {
      sprintf(node,"%d",base++);
      ierr = DrawString(draw,x,ymin,DRAW_RED,node);CHKERRQ(ierr);
    }

    ierr = DrawSynchronizedFlush(draw);CHKERRQ(ierr);
    ierr = DrawPause(draw); CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "DACreate1d"
/*@C
    DACreate1d - Creates a one-dimensional regular array that is
    distributed across some processors.

   Input Parameters:
.  comm - MPI communicator
.  wrap - Do you want ghost points to wrap around? Use one of
$         DA_NONPERIODIC, DA_XPERIODIC
.  M - global dimension of the array
.  w - number of degrees of freedom per node
.  s - stencil width  
.  lc - array containing number of nodes in the X direction on each processor, or PETSC_NULL.
$       If non-null, must be of length as m.

   Output Parameter:
.  inra - the resulting distributed array object

   Options Database Key:
$  -da_view : call DAView() at the conclusion of DACreate1d()

.keywords: distributed array, create, one-dimensional

.seealso: DADestroy(), DAView(), DACreate2d(), DACreate3d()
@*/
int DACreate1d(MPI_Comm comm,DAPeriodicType wrap,int M,int w,int s,int *lc,DA *inra)
{
  int        rank, size,xs,xe,x,Xs,Xe,ierr,start,end,m;
  int        i,*idx,nn,j,count,left,flg1,flg2,gdim;
  DA         da;
  Vec        local,global;
  VecScatter ltog,gtol;
  IS         to,from;
  DF         df_local;

  PetscFunctionBegin;
  *inra = 0;

  if (w < 1) SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Must have 1 or more degrees of freedom per node");
  if (s < 0) SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Stencil width cannot be negative");

  PetscHeaderCreate(da,_p_DA,DA_COOKIE,0,comm,DADestroy,DAView);
  PLogObjectCreate(da);
  PLogObjectMemory(da,sizeof(struct _p_DA));
  da->dim   = 1;
  da->gtog1 = 0;

  MPI_Comm_size(comm,&size); 
  MPI_Comm_rank(comm,&rank); 

  m = size;

  if (M < m)     SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"More processors than data points!");
  if ((M-1) < s) SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Array is too small for stencil!");

  /* 
     Determine locally owned region 
     xs is the first local node number, x is the number of local nodes 
  */
  if (lc == PETSC_NULL) {
    ierr = OptionsHasName(PETSC_NULL,"-da_partition_blockcomm",&flg1);CHKERRQ(ierr);
    ierr = OptionsHasName(PETSC_NULL,"-da_partition_nodes_at_end",&flg2);CHKERRQ(ierr);
    if (flg1) {      /* Block Comm type Distribution */
      xs = rank*M/m;
      x  = (rank + 1)*M/m - xs;
    }
    else if (flg2) { /* The odd nodes are evenly distributed across last nodes */
      x = (M + rank)/m;
      if (M/m == x) { xs = rank*x; }
      else          { xs = rank*(x-1) + (M+rank)%(x*m); }
    }
    else { /* The odd nodes are evenly distributed across the first k nodes */
      /* Regular PETSc Distribution */
      x = M/m + ((M % m) > rank);
      if (rank >= (M % m)) {xs = (rank * (int) (M/m) + M % m);}
      else                 {xs = rank * (int)(M/m) + rank;}
    }
  } else {
    x  = lc[rank];
    xs = 0;
    for ( i=0; i<rank; i++ ) {
      xs += lc[i];
    }
    /* verify that data user provided is consistent */
    left = xs;
    for ( i=rank; i<size; i++ ) {
      left += lc[i];
    }
    if (left != M) {
      SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,1,"Sum of lc across processors not equal to M");
    }
  }

  /* From now on x,s,xs,xe,Xs,Xe are the exact location in the array */
  x  *= w;
  s  *= w;  /* NOTE: here change s to be absolute stencil distance */
  xs *= w;
  xe = xs + x;

  /* determine ghost region */
  if (wrap == DA_XPERIODIC) {
    Xs = xs - s; 
    Xe = xe + s;
  }
  else
  {
    if ((xs-s) >= 0)   Xs = xs-s;  else Xs = 0; 
    if ((xe+s) <= M*w) Xe = xe+s;  else Xe = M*w;    
  }

  /* allocate the base parallel and sequential vectors */
  ierr = VecCreateMPI(comm,x,PETSC_DECIDE,&global); CHKERRQ(ierr);
  ierr = VecCreateSeq(PETSC_COMM_SELF,(Xe-Xs),&local); CHKERRQ(ierr);
    
  /* Create Local to Global Vector Scatter Context */
  /* local to global inserts non-ghost point region into global */
  VecGetOwnershipRange(global,&start,&end);
  ierr = ISCreateStride(comm,x,start,1,&to);CHKERRQ(ierr);
  ierr = ISCreateStride(comm,x,xs-Xs,1,&from);CHKERRQ(ierr);
  ierr = VecScatterCreate(local,from,global,to,&ltog); CHKERRQ(ierr);
  PLogObjectParent(da,to);
  PLogObjectParent(da,from);
  PLogObjectParent(da,ltog);
  ISDestroy(from); ISDestroy(to);

  /* Create Global to Local Vector Scatter Context */
  /* global to local must retrieve ghost points */
  ierr=ISCreateStride(comm,(Xe-Xs),0,1,&to);CHKERRQ(ierr);
 
  idx = (int *) PetscMalloc( (x+2*s)*sizeof(int) ); CHKPTRQ(idx);  
  PLogObjectMemory(da,(x+2*s)*sizeof(int));

  nn = 0;
  if (wrap == DA_XPERIODIC) {    /* Handle all cases with wrap first */

    for (i=0; i<s; i++) {  /* Left ghost points */
      if ((xs-s+i)>=0) { idx[nn++] = xs-s+i;}
      else             { idx[nn++] = M*w+(xs-s+i);}
    }

    for (i=0; i<x; i++) { idx [nn++] = xs + i;}  /* Non-ghost points */
    
    for (i=0; i<s; i++) { /* Right ghost points */
      if ((xe+i)<M*w) { idx [nn++] =  xe+i; }
      else            { idx [nn++] = (xe+i) - M*w;}
    }
  }

  else {      /* Now do all cases with no wrapping */

    if (s <= xs) {for (i=0; i<s; i++) {idx[nn++] = xs - s + i;}}
    else         {for (i=0; i<xs;  i++) {idx[nn++] = i;}}

    for (i=0; i<x; i++) { idx [nn++] = xs + i;}
    
    if ((xe+s)<=M*w) {for (i=0;  i<s;     i++) {idx[nn++]=xe+i;}}
    else             {for (i=xe; i<(M*w); i++) {idx[nn++]=i;   }}
  }

  ierr = ISCreateGeneral(comm,nn,idx,&from); CHKERRQ(ierr);
  ierr = VecScatterCreate(global,from,local,to,&gtol); CHKERRQ(ierr);
  PLogObjectParent(da,to);
  PLogObjectParent(da,from);
  PLogObjectParent(da,gtol);
  ISDestroy(to); ISDestroy(from);

  da->M  = M;  da->N  = 1;  da->m  = m; da->n = 1;
  da->xs = xs; da->xe = xe; da->ys = 0; da->ye = 0; da->zs = 0; da->ze = 0;
  da->Xs = Xs; da->Xe = Xe; da->Ys = 0; da->Ye = 0; da->Zs = 0; da->Ze = 0;
  da->P  = 1;  da->p  = 1;  da->w = w; da->s = s/w;

  PLogObjectParent(da,global);
  PLogObjectParent(da,local);

  da->global = global; 
  da->local  = local;
  da->gtol   = gtol;
  da->ltog   = ltog;
  da->idx    = idx;
  da->Nl     = nn;
  da->base   = xs;
  da->view   = DAView_1d;
  da->wrap   = wrap;
  da->stencil_type = DA_STENCIL_STAR;

  /* 
     Set the local to global ordering in the global vector, this allows use
     of VecSetValuesLocal().
  */
  {
    ISLocalToGlobalMapping isltog;
    ierr        = ISLocalToGlobalMappingCreate(comm,nn,idx,&isltog); CHKERRQ(ierr);
    ierr        = VecSetLocalToGlobalMapping(da->global,isltog); CHKERRQ(ierr);
    da->ltogmap = isltog; PetscObjectReference((PetscObject)isltog);
    PLogObjectParent(da,isltog);
    ierr = ISLocalToGlobalMappingDestroy(isltog); CHKERRQ(ierr);
  }

  /* construct the local to local scatter context */
  /* 
      We simply remap the values in the from part of 
    global to local to read from an array with the ghost values 
    rather then from the plain array.
  */
  ierr = VecScatterCopy(gtol,&da->ltol); CHKERRQ(ierr);
  PLogObjectParent(da,da->ltol);
  left  = xs - Xs;
  idx   = (int *) PetscMalloc( x*sizeof(int) ); CHKPTRQ(idx);
  count = 0;
  for ( j=0; j<x; j++ ) {
    idx[count++] = left + j;
  }  
  ierr = VecScatterRemap(da->ltol,idx,PETSC_NULL); CHKERRQ(ierr); 
  PetscFree(idx);

  /* 
     Build the natural ordering to PETSc ordering mappings.
  */
  {
    IS is;
    
    ierr = ISCreateStride(comm,da->xe-da->xs,da->base,1,&is);CHKERRQ(ierr);
    ierr = AOCreateBasicIS(is,is,&da->ao); CHKERRQ(ierr);
    PLogObjectParent(da,da->ao);
    ierr = ISDestroy(is); CHKERRQ(ierr);
  }

  /*
     Note the following will be removed soon. Since the functionality 
    is replaced by the above.
  */
  /* Construct the mapping from current global ordering to global
     ordering that would be used if only 1 processor were employed.
     This mapping is intended only for internal use by discrete
     function and matrix viewers.

     We don't really need this for 1D distributed arrays, since the
     ordering is the same regardless.  But for now we form it anyway
     so that the DFVec routines can all be used seamlessly.  Maybe
     we'll change in the near future.
   */
  ierr = VecGetSize(global,&gdim); CHKERRQ(ierr);
  da->gtog1 = (int *)PetscMalloc(gdim*sizeof(int)); CHKPTRQ(da->gtog1);
  PLogObjectMemory(da,gdim*sizeof(int));
  for (i=0; i<gdim; i++) da->gtog1[i] = i;

  /* Create discrete function shell and associate with vectors in DA */
  /* Eventually will pass in optional labels for each component */
  ierr = DFShellCreateDA_Private(comm,PETSC_NULL,da,&da->dfshell); CHKERRQ(ierr);
  PLogObjectParent(da,da->dfshell);
  ierr = DFShellGetLocalDFShell(da->dfshell,&df_local);
  ierr = DFVecShellAssociate(da->dfshell,global); CHKERRQ(ierr);
  ierr = DFVecShellAssociate(df_local,local); CHKERRQ(ierr);

  ierr = OptionsHasName(PETSC_NULL,"-da_view",&flg1); CHKERRQ(ierr);
  if (flg1) {ierr = DAView(da,VIEWER_STDOUT_SELF); CHKERRQ(ierr);}
  ierr = OptionsHasName(PETSC_NULL,"-da_view_draw",&flg1); CHKERRQ(ierr);
  if (flg1) {ierr = DAView(da,VIEWER_DRAWX_(da->comm)); CHKERRQ(ierr);}
  ierr = OptionsHasName(PETSC_NULL,"-help",&flg1); CHKERRQ(ierr);
  if (flg1) {ierr = DAPrintHelp(da); CHKERRQ(ierr);}
  *inra = da;
  PetscFunctionReturn(0);
}


