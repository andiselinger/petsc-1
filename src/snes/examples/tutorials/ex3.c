/*$Id: ex3.c,v 1.80 2001/03/13 04:58:12 bsmith Exp bsmith $*/

static char help[] = "Uses Newton-like methods to solve u'' + u^{2} = f in parallel.\n\
This example employs a user-defined monitoring routine and optionally a user-defined\n\
routine to check candidate iterates produced by line search routines.  This code also\n\
demonstrates use of the macro __FUNC__ to define routine names for use in error handling.\n\
The command line options include:\n\
  -check_iterates : activate checking of iterates\n\
  -check_tol <tol>: set tolerance for iterate checking\n\n";

/*T
   Concepts: SNES^basic parallel example
   Concepts: SNES^setting a user-defined monitoring routine
   Concepts: error handling^using the macro __FUNC__ to define routine names;
   Processors: n
T*/

/* 
   Include "petscdraw.h" so that we can use distributed arrays (DAs).
   Include "petscdraw.h" so that we can use PETSc drawing routines.
   Include "petscsnes.h" so that we can use SNES solvers.  Note that this
   file automatically includes:
     petsc.h       - base PETSc routines   petscvec.h - vectors
     petscsys.h    - system routines       petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners
     petscsles.h   - linear solvers
*/

#include "petscda.h"
#include "petscsnes.h"

/* 
   User-defined routines.  Note that immediately before each routine below,
   we define the macro __FUNC__ to be a string containing the routine name.
   If defined, this macro is used in the PETSc error handlers to provide a
   complete traceback of routine names.  All PETSc library routines use this
   macro, and users can optionally employ it as well in their application
   codes.  Note that users can get a traceback of PETSc errors regardless of
   whether they define __FUNC__ in application codes; this macro merely
   provides the added traceback detail of the application routine names.
*/
int FormJacobian(SNES,Vec,Mat*,Mat*,MatStructure*,void*);
int FormFunction(SNES,Vec,Vec,void*);
int FormInitialGuess(Vec);
int Monitor(SNES,int,double,void *);
int StepCheck(SNES,void *,Vec,PetscTruth *);

/* 
   User-defined application context
*/
typedef struct {
   DA     da;     /* distributed array */
   Vec    F;      /* right-hand-side of PDE */
   int    rank;   /* rank of processor */
   int    size;   /* size of communicator */
   double h;      /* mesh spacing */
} ApplicationCtx;

/*
   User-defined context for monitoring
*/
typedef struct {
   PetscViewer viewer; 
} MonitorCtx;

/*
   User-defined context for checking candidate iterates that are 
   determined by line search methods
*/
typedef struct {
   Vec    last_step;  /* previous iterate */
   double tolerance;  /* tolerance for changes between successive iterates */
} StepCheckCtx;

#undef __FUNC__
#define __FUNC__ "main"
int main(int argc,char **argv)
{
  SNES           snes;                 /* SNES context */
  Mat            J;                    /* Jacobian matrix */
  ApplicationCtx ctx;                  /* user-defined context */
  Vec            x,r,U,F;           /* vectors */
  MonitorCtx     monP;                 /* monitoring context */
  StepCheckCtx   checkP;               /* step-checking context */
  PetscTruth     step_check;           /* flag indicating whether we're checking
                                          candidate iterates */
  Scalar         xp,*FF,*UU,none = -1.0;
  int            ierr,its,N = 5,i,maxit,maxf,xs,xm;
  double         atol,rtol,stol,norm;

  PetscInitialize(&argc,&argv,(char *)0,help);
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&ctx.rank);CHKERRQ(ierr);
  ierr = MPI_Comm_size(PETSC_COMM_WORLD,&ctx.size);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(PETSC_NULL,"-n",&N,PETSC_NULL);CHKERRQ(ierr);
  ctx.h = 1.0/(N-1);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create nonlinear solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = SNESCreate(PETSC_COMM_WORLD,SNES_NONLINEAR_EQUATIONS,&snes);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create vector data structures; set function evaluation routine
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*
     Create distributed array (DA) to manage parallel grid and vectors
  */
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,N,1,1,PETSC_NULL,&ctx.da);CHKERRQ(ierr);

  /*
     Extract global and local vectors from DA; then duplicate for remaining
     vectors that are the same types
  */
  ierr = DACreateGlobalVector(ctx.da,&x);CHKERRQ(ierr);
  ierr = VecDuplicate(x,&r);CHKERRQ(ierr);
  ierr = VecDuplicate(x,&F);CHKERRQ(ierr); ctx.F = F;
  ierr = VecDuplicate(x,&U);CHKERRQ(ierr); 

  /* 
     Set function evaluation routine and vector.  Whenever the nonlinear
     solver needs to compute the nonlinear function, it will call this
     routine.
      - Note that the final routine argument is the user-defined
        context that provides application-specific data for the
        function evaluation routine.
  */
  ierr = SNESSetFunction(snes,r,FormFunction,&ctx);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create matrix data structure; set Jacobian evaluation routine
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  ierr = MatCreate(PETSC_COMM_WORLD,PETSC_DECIDE,PETSC_DECIDE,N,N,&J);CHKERRQ(ierr);
  ierr = MatSetFromOptions(J);CHKERRQ(ierr);

  /* 
     Set Jacobian matrix data structure and default Jacobian evaluation
     routine.  Whenever the nonlinear solver needs to compute the
     Jacobian matrix, it will call this routine.
      - Note that the final routine argument is the user-defined
        context that provides application-specific data for the
        Jacobian evaluation routine.
  */
  ierr = SNESSetJacobian(snes,J,J,FormJacobian,&ctx);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Customize nonlinear solver; set runtime options
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* 
     Set an optional user-defined monitoring routine
  */
  ierr = PetscViewerDrawOpen(PETSC_COMM_WORLD,0,0,0,0,400,400,&monP.viewer);CHKERRQ(ierr);
  ierr = SNESSetMonitor(snes,Monitor,&monP,0);CHKERRQ(ierr); 

  /*
     Set names for some vectors to facilitate monitoring (optional)
  */
  ierr = PetscObjectSetName((PetscObject)x,"Approximate Solution");CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)U,"Exact Solution");CHKERRQ(ierr);

  /* 
     Set SNES/SLES/KSP/PC runtime options, e.g.,
         -snes_view -snes_monitor -ksp_type <ksp> -pc_type <pc>
  */
  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  /* 
     Set an optional user-defined routine to check the validity of candidate 
     iterates that are determined by line search methods
  */
  ierr = PetscOptionsHasName(PETSC_NULL,"-check_iterates",&step_check);CHKERRQ(ierr);
  if (step_check) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Activating step checking routine\n");CHKERRQ(ierr);
    ierr = SNESSetLineSearchCheck(snes,StepCheck,&checkP);CHKERRQ(ierr); 
    ierr = VecDuplicate(x,&(checkP.last_step));CHKERRQ(ierr); 
    checkP.tolerance = 1.0;
    ierr = PetscOptionsGetDouble(PETSC_NULL,"-check_tol",&checkP.tolerance,PETSC_NULL);CHKERRQ(ierr);
  }


  /* 
     Print parameters used for convergence testing (optional) ... just
     to demonstrate this routine; this information is also printed with
     the option -snes_view
  */
  ierr = SNESGetTolerances(snes,&atol,&rtol,&stol,&maxit,&maxf);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"atol=%g, rtol=%g, stol=%g, maxit=%d, maxf=%d\n",atol,rtol,stol,maxit,maxf);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize application:
     Store right-hand-side of PDE and exact solution
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*
     Get local grid boundaries (for 1-dimensional DA):
       xs, xm - starting grid index, width of local grid (no ghost points)
  */
  ierr = DAGetCorners(ctx.da,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  /*
     Get pointers to vector data
  */
  ierr = DAVecGetArray(ctx.da,F,(void**)&FF);CHKERRQ(ierr);
  ierr = DAVecGetArray(ctx.da,U,(void**)&UU);CHKERRQ(ierr);

  /*
     Compute local vector entries
  */
  xp = ctx.h*xs;
  for (i=xs; i<xs+xm; i++) {
    FF[i] = 6.0*xp + PetscPowScalar(xp+1.e-12,6.0); /* +1.e-12 is to prevent 0^6 */
    UU[i] = xp*xp*xp;
    xp   += ctx.h;
  }

  /*
     Restore vectors
  */
  ierr = DAVecRestoreArray(ctx.da,F,(void**)&FF);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(ctx.da,U,(void**)&UU);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Evaluate initial guess; then solve nonlinear system
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /*
     Note: The user should initialize the vector, x, with the initial guess
     for the nonlinear solver prior to calling SNESSolve().  In particular,
     to employ an initial guess of zero, the user should explicitly set
     this vector to zero by calling VecSet().
  */
  ierr = FormInitialGuess(x);CHKERRQ(ierr);
  ierr = SNESSolve(snes,x,&its);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Number of Newton iterations = %d\n\n",its);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Check solution and clean up
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  /* 
     Check the error
  */
  ierr = VecAXPY(&none,U,x);CHKERRQ(ierr);
  ierr  = VecNorm(x,NORM_2,&norm);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Norm of error %A, Iterations %d\n",norm,its);CHKERRQ(ierr);

  /*
     Free work space.  All PETSc objects should be destroyed when they
     are no longer needed.
  */
  ierr = PetscViewerDestroy(monP.viewer);CHKERRQ(ierr);
  if (step_check) {ierr = VecDestroy(checkP.last_step);CHKERRQ(ierr);}
  ierr = VecDestroy(x);CHKERRQ(ierr);
  ierr = VecDestroy(r);CHKERRQ(ierr);
  ierr = VecDestroy(U);CHKERRQ(ierr);
  ierr = VecDestroy(F);CHKERRQ(ierr);
  ierr = MatDestroy(J);CHKERRQ(ierr);
  ierr = SNESDestroy(snes);CHKERRQ(ierr);
  ierr = DADestroy(ctx.da);CHKERRQ(ierr);
  ierr = PetscFinalize();CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "FormInitialGuess"
/*
   FormInitialGuess - Computes initial guess.

   Input/Output Parameter:
.  x - the solution vector
*/
int FormInitialGuess(Vec x)
{
   int    ierr;
   Scalar pfive = .50;

   PetscFunctionBegin;
   ierr = VecSet(&pfive,x);CHKERRQ(ierr);
   PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "FormFunction"
/* 
   FormFunction - Evaluates nonlinear function, F(x).

   Input Parameters:
.  snes - the SNES context
.  x - input vector
.  ctx - optional user-defined context, as set by SNESSetFunction()

   Output Parameter:
.  f - function vector

   Note:
   The user-defined context can contain any application-specific
   data needed for the function evaluation.
*/
int FormFunction(SNES snes,Vec x,Vec f,void *ctx)
{
  ApplicationCtx *user = (ApplicationCtx*) ctx;
  DA             da = user->da;
  Scalar         *xx,*ff,*FF,d;
  int            i,ierr,N,xs,xm;
  Vec            xlocal;

  PetscFunctionBegin;
  ierr = DAGetLocalVector(da,&xlocal);CHKERRQ(ierr);
  /*
     Scatter ghost points to local vector, using the 2-step process
        DAGlobalToLocalBegin(), DAGlobalToLocalEnd().
     By placing code between these two statements, computations can
     be done while messages are in transition.
  */
  ierr = DAGlobalToLocalBegin(da,x,INSERT_VALUES,xlocal);CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(da,x,INSERT_VALUES,xlocal);CHKERRQ(ierr);

  /*
     Get pointers to vector data.
       - The vector xlocal includes ghost point; the vectors x and f do
         NOT include ghost points.
       - Using DAVecGetArray() allows accessing the values using global ordering
  */
  ierr = DAVecGetArray(da,xlocal,(void**)&xx);CHKERRQ(ierr);
  ierr = DAVecGetArray(da,f,(void**)&ff);CHKERRQ(ierr);
  ierr = DAVecGetArray(da,user->F,(void**)&FF);CHKERRQ(ierr);

  /*
     Get local grid boundaries (for 1-dimensional DA):
       xs, xm  - starting grid index, width of local grid (no ghost points)
  */
  ierr = DAGetCorners(da,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);
  ierr = VecGetSize(f,&N);CHKERRQ(ierr);

  /*
     Set function values for boundary points; define local interior grid point range:
        xsi - starting interior grid index
        xei - ending interior grid index
  */
  if (xs == 0) { /* left boundary */
    ff[0] = xx[0];
    xs++;xm--;
  }
  if (xs+xm == N) {  /* right boundary */
    ff[xs+xm-1] = xx[xs+xm-1] - 1.0;
    xm--;
  }

  /*
     Compute function over locally owned part of the grid (interior points only)
  */
  d = 1.0/(user->h*user->h);
  for (i=xs; i<xs+xm; i++) {
    ff[i] = d*(xx[i-1] - 2.0*xx[i] + xx[i+1]) + xx[i]*xx[i] - FF[i];
  }

  /*
     Restore vectors
  */
  ierr = DAVecRestoreArray(da,xlocal,(void**)&xx);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(da,f,(void**)&ff);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(da,user->F,(void**)&FF);CHKERRQ(ierr);
  ierr = DARestoreLocalVector(da,&xlocal);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "FormJacobian"
/*
   FormJacobian - Evaluates Jacobian matrix.

   Input Parameters:
.  snes - the SNES context
.  x - input vector
.  dummy - optional user-defined context (not used here)

   Output Parameters:
.  jac - Jacobian matrix
.  B - optionally different preconditioning matrix
.  flag - flag indicating matrix structure
*/
int FormJacobian(SNES snes,Vec x,Mat *jac,Mat *B,MatStructure*flag,void *ctx)
{
  ApplicationCtx *user = (ApplicationCtx*) ctx;
  Scalar         *xx,d,A[3];
  int            i,j[3],ierr,M,xs,xm;
  DA             da = user->da;

  PetscFunctionBegin;
  /*
     Get pointer to vector data
  */
  ierr = DAVecGetArray(da,x,(void**)&xx);CHKERRQ(ierr);
  ierr = DAGetCorners(da,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  /*
    Get range of locally owned matrix
  */
  ierr = DAGetInfo(da,PETSC_NULL,&M,PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL,
                   PETSC_NULL,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  /*
     Determine starting and ending local indices for interior grid points.
     Set Jacobian entries for boundary points.
  */

  if (xs == 0) {  /* left boundary */
    i = 0; A[0] = 1.0; 
    ierr = MatSetValues(*jac,1,&i,1,&i,A,INSERT_VALUES);CHKERRQ(ierr);
    xs++;xm--;
  }
  if (xs+xm == M) { /* right boundary */
    i = M-1; A[0] = 1.0; 
    ierr = MatSetValues(*jac,1,&i,1,&i,A,INSERT_VALUES);CHKERRQ(ierr);
    xm--;
  }

  /*
     Interior grid points
      - Note that in this case we set all elements for a particular
        row at once.
  */
  d = 1.0/(user->h*user->h);
  for (i=xs; i<xs+xm; i++) {
    j[0] = i - 1; j[1] = i; j[2] = i + 1; 
    A[0] = A[2] = d; A[1] = -2.0*d + 2.0*xx[i];
    ierr = MatSetValues(*jac,1,&i,3,j,A,INSERT_VALUES);CHKERRQ(ierr);
  }

  /* 
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.

     Also, restore vector.
  */

  ierr = MatAssemblyBegin(*jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(da,x,(void**)&xx);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  *flag = SAME_NONZERO_PATTERN;
  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "Monitor"
/*
   Monitor - Optional user-defined monitoring routine that views the
   current iterate with an x-window plot. Set by SNESSetMonitor().

   Input Parameters:
   snes - the SNES context
   its - iteration number
   norm - 2-norm function value (may be estimated)
   ctx - optional user-defined context for private data for the 
         monitor routine, as set by SNESSetMonitor()

   Note:
   See the manpage for PetscViewerDrawOpen() for useful runtime options,
   such as -nox to deactivate all x-window output.
 */
int Monitor(SNES snes,int its,double fnorm,void *ctx)
{
  int        ierr;
  MonitorCtx *monP = (MonitorCtx*) ctx;
  Vec        x;

  PetscFunctionBegin;
  ierr = PetscPrintf(PETSC_COMM_WORLD,"iter = %d,SNES Function norm %g\n",its,fnorm);CHKERRQ(ierr);
  ierr = SNESGetSolution(snes,&x);CHKERRQ(ierr);
  ierr = VecView(x,monP->viewer);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNC__
#define __FUNC__ "StepCheck"
/*
   StepCheck - Optional user-defined routine that checks the validity of
   candidate steps of a line search method.  Set by SNESSetLineSearchCheck().

   Input Parameters:
   snes - the SNES context
   ctx  - optional user-defined context for private data for the 
          monitor routine, as set by SNESSetLineSearchCheck()
   x    - the new candidate iterate

   Output Parameters:
   x    - current iterate (possibly modified)
   flg - flag indicating whether x has been modified (either
          PETSC_TRUE of PETSC_FALSE)
 */
int StepCheck(SNES snes,void *ctx,Vec x,PetscTruth *flg)
{
  int            ierr,i,iter,xs,xm;
  ApplicationCtx *user;
  StepCheckCtx   *check = (StepCheckCtx*) ctx;
  Scalar         *xa,*xa_last,tmp;
  double         rdiff;
  DA             da = user->da;

  PetscFunctionBegin;
  *flg = PETSC_FALSE;
  ierr = SNESGetIterationNumber(snes,&iter);CHKERRQ(ierr);

  if (iter > 1) {
    ierr = SNESGetApplicationContext(snes,(void**)&user);CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Checking candidate step at iteration %d with tolerance %g\n",
       iter,check->tolerance);CHKERRQ(ierr);

    /* Access local array data */
    ierr = DAVecGetArray(da,check->last_step,(void**)&xa_last);CHKERRQ(ierr);
    ierr = DAVecGetArray(da,x,(void**)&xa);CHKERRQ(ierr);
    ierr = DAGetCorners(da,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

    /* 
       If we fail the user-defined check for validity of the candidate iterate,
       then modify the iterate as we like.  (Note that the particular modification 
       below is intended simply to demonstrate how to manipulate this data, not
       as a meaningful or appropriate choice.)
    */
    for (i=xs; i<xs+xm; i++) {
      rdiff = PetscAbsScalar((xa[i] - xa_last[i])/xa[i]);
      if (rdiff > check->tolerance) {
        tmp   = xa[i];
        xa[i] = (xa[i] + xa_last[i])/2.0;
        *flg = PETSC_TRUE;
        ierr = PetscPrintf(PETSC_COMM_WORLD,"  Altering entry %d: x=%g, x_last=%g, diff=%g, x_new=%g\n",
                    i,PetscAbsScalar(tmp),PetscAbsScalar(xa_last[i]),rdiff,PetscAbsScalar(xa[i]));CHKERRQ(ierr);
      }
    }
    ierr = DAVecRestoreArray(da,check->last_step,(void**)&xa_last);CHKERRQ(ierr);
    ierr = DAVecRestoreArray(da,x,(void**)&xa);CHKERRQ(ierr);
  }
  ierr = VecCopy(x,check->last_step);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}





