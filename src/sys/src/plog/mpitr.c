#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: mpitr.c,v 1.18 1999/05/12 03:27:27 bsmith Exp bsmith $";
#endif

/*
    Code for tracing mistakes in MPI usage. For example, sends that are never received,
  nonblocking messages that are not correctly waited for, etc.
*/

#include "petsc.h"           /*I "petsc.h" I*/

#if defined(PETSC_USE_LOG) && !defined(USING_MPIUNI)

#undef __FUNC__  
#define __FUNC__ "PetscMPIDump"
/*@C
   PetscMPIDump - Dumps a listing of incomplete MPI operations, such as sends that
   have never been received, etc.

   Collective on PETSC_COMM_WORLD

   Input Parameter:
.  fp - file pointer.  If fp is NULL, stderr is assumed.

   Options Database Key:
.  -mpidump - Dumps MPI incompleteness during call to PetscFinalize()

    Level: developer

.keywords: MPI errors

.seealso:  PetscTrDump()
 @*/
int PetscMPIDump(FILE *fd)
{
  int    rank,ierr;
  double tsends,trecvs,work;

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  if (fd == 0) fd = stderr;
   
  /* Did we wait on all the non-blocking sends and receives? */
  ierr = PetscSequentialPhaseBegin(PETSC_COMM_WORLD,1 );CHKERRQ(ierr);
  if (irecv_ct + isend_ct != sum_of_waits_ct) {
    fprintf(fd,"[%d]You have not waited on all non-blocking sends and receives",rank);
    fprintf(fd,"[%d]Number non-blocking sends %g receives %g number of waits %g\n",rank,isend_ct,
            irecv_ct,sum_of_waits_ct);
    fflush(fd);
  }
  ierr = PetscSequentialPhaseEnd(PETSC_COMM_WORLD,1 );CHKERRQ(ierr);
  /* Did we receive all the messages that we sent? */
  work = irecv_ct + recv_ct;
  ierr = MPI_Reduce(&work,&trecvs,1,MPI_DOUBLE,MPI_SUM,0,PETSC_COMM_WORLD);CHKERRQ(ierr);
  work = isend_ct + send_ct;
  ierr = MPI_Reduce(&work,&tsends,1,MPI_DOUBLE,MPI_SUM,0,PETSC_COMM_WORLD);CHKERRQ(ierr);
  if (!rank && tsends != trecvs) {
    fprintf(fd,"Total number sends %g not equal receives %g\n",tsends,trecvs);
    fflush(fd);
  }
  PetscFunctionReturn(0);
}

#else

#undef __FUNC__  
#define __FUNC__ "PetscMPIDump"
int PetscMPIDump(FILE *fd)
{
  PetscFunctionBegin;
  PetscFunctionReturn(0);
}

#endif









