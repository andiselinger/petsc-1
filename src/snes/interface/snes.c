#ifdef PETSC_RCS_HEADER
static char vcid[] = "$Id: snes.c,v 1.195 1999/09/20 19:08:41 bsmith Exp bsmith $";
#endif

#include "src/snes/snesimpl.h"      /*I "snes.h"  I*/

int SNESRegisterAllCalled = 0;
FList SNESList = 0;

#undef __FUNC__  
#define __FUNC__ "SNESView"
/*@ 
   SNESView - Prints the SNES data structure.

   Collective on SNES, unless Viewer is VIEWER_STDOUT_SELF

   Input Parameters:
+  SNES - the SNES context
-  viewer - visualization context

   Options Database Key:
.  -snes_view - Calls SNESView() at end of SNESSolve()

   Notes:
   The available visualization contexts include
+     VIEWER_STDOUT_SELF - standard output (default)
-     VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their 
         data to the first processor to print. 

   The user can open an alternative visualization context with
   ViewerASCIIOpen() - output to a specified file.

   Level: beginner

.keywords: SNES, view

.seealso: ViewerASCIIOpen()
@*/
int SNESView(SNES snes,Viewer viewer)
{
  SNES_KSP_EW_ConvCtx *kctx;
  int                 ierr;
  SLES                sles;
  char                *method;
  ViewerType          vtype;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (viewer) {PetscValidHeader(viewer);}
  else { viewer = VIEWER_STDOUT_SELF; }

  ierr = ViewerGetType(viewer,&vtype);CHKERRQ(ierr);
  if (PetscTypeCompare(vtype,ASCII_VIEWER)) {
    ierr = ViewerASCIIPrintf(viewer,"SNES Object:\n");CHKERRQ(ierr);
    ierr = SNESGetType(snes,&method);CHKERRQ(ierr);
    if (method) {
      ierr = ViewerASCIIPrintf(viewer,"  method: %s\n",method);CHKERRQ(ierr);
    } else {
      ierr = ViewerASCIIPrintf(viewer,"  method: not set yet\n");CHKERRQ(ierr);
    }
    if (snes->view) {
      ierr = ViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      ierr = (*snes->view)(snes,viewer);CHKERRQ(ierr);
      ierr = ViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    ierr = ViewerASCIIPrintf(viewer,"  maximum iterations=%d, maximum function evaluations=%d\n",snes->max_its,snes->max_funcs);CHKERRQ(ierr);
    ierr = ViewerASCIIPrintf(viewer,"  tolerances: relative=%g, absolute=%g, truncation=%g, solution=%g\n",
                 snes->rtol, snes->atol, snes->trunctol, snes->xtol);CHKERRQ(ierr);
    ierr = ViewerASCIIPrintf(viewer,"  total number of linear solver iterations=%d\n",snes->linear_its);CHKERRQ(ierr);
    ierr = ViewerASCIIPrintf(viewer,"  total number of function evaluations=%d\n",snes->nfuncs);CHKERRQ(ierr);
    if (snes->method_class == SNES_UNCONSTRAINED_MINIMIZATION) {
      ierr = ViewerASCIIPrintf(viewer,"  min function tolerance=%g\n",snes->fmin);CHKERRQ(ierr);
    }
    if (snes->ksp_ewconv) {
      kctx = (SNES_KSP_EW_ConvCtx *)snes->kspconvctx;
      if (kctx) {
        ierr = ViewerASCIIPrintf(viewer,"  Eisenstat-Walker computation of KSP relative tolerance (version %d)\n",kctx->version);CHKERRQ(ierr);
        ierr = ViewerASCIIPrintf(viewer,"    rtol_0=%g, rtol_max=%g, threshold=%g\n",kctx->rtol_0,kctx->rtol_max,kctx->threshold);CHKERRQ(ierr);
        ierr = ViewerASCIIPrintf(viewer,"    gamma=%g, alpha=%g, alpha2=%g\n",kctx->gamma,kctx->alpha,kctx->alpha2);CHKERRQ(ierr);
      }
    }
  } else if (PetscTypeCompare(vtype,STRING_VIEWER)) {
    ierr = SNESGetType(snes,&method);CHKERRQ(ierr);
    ierr = ViewerStringSPrintf(viewer," %-3.3s",method);CHKERRQ(ierr);
  }
  ierr = SNESGetSLES(snes,&sles);CHKERRQ(ierr);
  ierr = ViewerASCIIPushTab(viewer);CHKERRQ(ierr);
  ierr = SLESView(sles,viewer);CHKERRQ(ierr);
  ierr = ViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
       We retain a list of functions that also take SNES command 
    line options. These are called at the end SNESSetFromOptions()
*/
#define MAXSETFROMOPTIONS 5
static int numberofsetfromoptions;
static int (*othersetfromoptions[MAXSETFROMOPTIONS])(SNES);

#undef __FUNC__  
#define __FUNC__ "SNESAddOptionsChecker"
/*@
    SNESAddOptionsChecker - Adds an additional function to check for SNES options.

    Not Collective

    Input Parameter:
.   snescheck - function that checks for options

    Level: developer

.seealso: SNESSetFromOptions()
@*/
int SNESAddOptionsChecker(int (*snescheck)(SNES) )
{
  PetscFunctionBegin;
  if (numberofsetfromoptions >= MAXSETFROMOPTIONS) {
    SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Too many options checkers, only 5 allowed");
  }

  othersetfromoptions[numberofsetfromoptions++] = snescheck;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetTypeFromOptions"
/*@
   SNESSetTypeFromOptions - Sets the SNES solver type from the options database,
        or sets a default if none is give.

   Collective on SNES

   Input Parameter:
.  snes - the SNES context

   Options Database Keys:
.  -snes_type <type> - SNES_EQ_LS, SNES_EQ_TR, SNES_UM_TR, SNES_UM_LS etc

   Level: beginner

.keywords: SNES, nonlinear, set, options, database

.seealso: SNESPrintHelp(), SNESSetOptionsPrefix(), SNESSetFromOptions()
@*/
int SNESSetTypeFromOptions(SNES snes)
{
  char     method[256];
  int      ierr, flg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->setupcalled) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call prior to SNESSetUp()");
  ierr = OptionsGetString(snes->prefix,"-snes_type",method,256,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = SNESSetType(snes,(SNESType) method);CHKERRQ(ierr);
  }
  /*
      If SNES type has not yet been set, set it now
  */
  if (!snes->type_name) {
    if (snes->method_class == SNES_NONLINEAR_EQUATIONS) {
      ierr = SNESSetType(snes,SNES_EQ_LS);CHKERRQ(ierr);
    } else {
      ierr = SNESSetType(snes,SNES_UM_TR);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetFromOptions"
/*@
   SNESSetFromOptions - Sets various SNES and SLES parameters from user options.

   Collective on SNES

   Input Parameter:
.  snes - the SNES context

   Options Database Keys:
+  -snes_type <type> - SNES_EQ_LS, SNES_EQ_TR, SNES_UM_TR, SNES_UM_LS etc
.  -snes_stol - convergence tolerance in terms of the norm
                of the change in the solution between steps
.  -snes_atol <atol> - absolute tolerance of residual norm
.  -snes_rtol <rtol> - relative decrease in tolerance norm from initial
.  -snes_max_it <max_it> - maximum number of iterations
.  -snes_max_funcs <max_funcs> - maximum number of function evaluations
.  -snes_trtol <trtol> - trust region tolerance
.  -snes_no_convergence_test - skip convergence test in nonlinear or minimization 
                               solver; hence iterations will continue until max_it
                               or some other criterion is reached. Saves expense
                               of convergence test
.  -snes_monitor - prints residual norm at each iteration 
.  -snes_vecmonitor - plots solution at each iteration
.  -snes_vecmonitor_update - plots update to solution at each iteration 
.  -snes_xmonitor - plots residual norm at each iteration 
.  -snes_fd - use finite differences to compute Jacobian; very slow, only for testing
-  -snes_mf_ksp_monitor - if using matrix-free multiply then print h at each KSP iteration

    Options Database for Eisenstat-Walker method:
+  -snes_ksp_eq_conv - use Eisenstat-Walker method for determining linear system convergence
.  -snes_ksp_eq_version ver - version of  Eisenstat-Walker method
.  -snes_ksp_ew_rtol0 <rtol0> - Sets rtol0
.  -snes_ksp_ew_rtolmax <rtolmax> - Sets rtolmax
.  -snes_ksp_ew_gamma <gamma> - Sets gamma
.  -snes_ksp_ew_alpha <alpha> - Sets alpha
.  -snes_ksp_ew_alpha2 <alpha2> - Sets alpha2 
-  -snes_ksp_ew_threshold <threshold> - Sets threshold

   Notes:
   To see all options, run your program with the -help option or consult
   the users manual.

   Level: beginner

.keywords: SNES, nonlinear, set, options, database

.seealso: SNESPrintHelp(), SNESSetOptionsPrefix(), SNESSetTypeFromOptions()
@*/
int SNESSetFromOptions(SNES snes)
{
  double   tmp;
  SLES     sles;
  int      ierr, flg,i,loc[4],nmax = 4;
  int      version   = PETSC_DEFAULT;
  double   rtol_0    = PETSC_DEFAULT;
  double   rtol_max  = PETSC_DEFAULT;
  double   gamma2    = PETSC_DEFAULT;
  double   alpha     = PETSC_DEFAULT;
  double   alpha2    = PETSC_DEFAULT;
  double   threshold = PETSC_DEFAULT;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  ierr = SNESSetTypeFromOptions(snes);CHKERRQ(ierr);

  loc[0] = PETSC_DECIDE; loc[1] = PETSC_DECIDE; loc[2] = 300; loc[3] = 300;
  ierr = OptionsGetDouble(snes->prefix,"-snes_stol",&tmp, &flg);CHKERRQ(ierr);
  if (flg) {
    ierr = SNESSetTolerances(snes,PETSC_DEFAULT,PETSC_DEFAULT,tmp,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
  }
  ierr = OptionsGetDouble(snes->prefix,"-snes_atol",&tmp, &flg);CHKERRQ(ierr);
  if (flg) {
    ierr = SNESSetTolerances(snes,tmp,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
  }
  ierr = OptionsGetDouble(snes->prefix,"-snes_rtol",&tmp, &flg);CHKERRQ(ierr);
  if (flg) {
    ierr = SNESSetTolerances(snes,PETSC_DEFAULT,tmp,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
  }
  ierr = OptionsGetInt(snes->prefix,"-snes_max_it",&snes->max_its, &flg);CHKERRQ(ierr);
  ierr = OptionsGetInt(snes->prefix,"-snes_max_funcs",&snes->max_funcs, &flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_trtol",&tmp, &flg);CHKERRQ(ierr);
  if (flg) { ierr = SNESSetTrustRegionTolerance(snes,tmp);CHKERRQ(ierr); }
  ierr = OptionsGetDouble(snes->prefix,"-snes_fmin",&tmp, &flg);CHKERRQ(ierr);
  if (flg) { ierr = SNESSetMinimizationFunctionTolerance(snes,tmp);CHKERRQ(ierr);}
  ierr = OptionsHasName(snes->prefix,"-snes_ksp_ew_conv", &flg);CHKERRQ(ierr);
  if (flg) { snes->ksp_ewconv = 1; }
  ierr = OptionsGetInt(snes->prefix,"-snes_ksp_ew_version",&version,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_rtol0",&rtol_0,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_rtolmax",&rtol_max,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_gamma",&gamma2,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_alpha",&alpha,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_alpha2",&alpha2,&flg);CHKERRQ(ierr);
  ierr = OptionsGetDouble(snes->prefix,"-snes_ksp_ew_threshold",&threshold,&flg);CHKERRQ(ierr);

  ierr = OptionsHasName(snes->prefix,"-snes_no_convergence_test",&flg);CHKERRQ(ierr);
  if (flg) {snes->converged = 0;}

  ierr = SNES_KSP_SetParametersEW(snes,version,rtol_0,rtol_max,gamma2,alpha,
                                  alpha2,threshold);CHKERRQ(ierr);
  ierr = OptionsHasName(snes->prefix,"-snes_cancelmonitors",&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESClearMonitor(snes);CHKERRQ(ierr);}
  ierr = OptionsHasName(snes->prefix,"-snes_monitor",&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESSetMonitor(snes,SNESDefaultMonitor,0,0);CHKERRQ(ierr);}
  ierr = OptionsHasName(snes->prefix,"-snes_smonitor",&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESSetMonitor(snes,SNESDefaultSMonitor,0,0);CHKERRQ(ierr);}
  ierr = OptionsHasName(snes->prefix,"-snes_vecmonitor",&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESSetMonitor(snes,SNESVecViewMonitor,0,0);CHKERRQ(ierr);}
  ierr = OptionsHasName(snes->prefix,"-snes_vecmonitor_update",&flg);CHKERRQ(ierr);
  if (flg) {ierr = SNESSetMonitor(snes,SNESVecViewMonitorUpdate,0,0);CHKERRQ(ierr);}
  ierr = OptionsGetIntArray(snes->prefix,"-snes_xmonitor",loc,&nmax,&flg);CHKERRQ(ierr);
  if (flg) {
    int    rank = 0;
    DrawLG lg;
    MPI_Initialized(&rank);
    if (rank) {ierr = MPI_Comm_rank(snes->comm,&rank);CHKERRQ(ierr);}
    if (!rank) {
      ierr = SNESLGMonitorCreate(0,0,loc[0],loc[1],loc[2],loc[3],&lg);CHKERRQ(ierr);
      ierr = SNESSetMonitor(snes,SNESLGMonitor,lg,( int (*)(void *))SNESLGMonitorDestroy);CHKERRQ(ierr);  
      PLogObjectParent(snes,lg);
    }
  }

  ierr = OptionsHasName(snes->prefix,"-snes_fd", &flg);CHKERRQ(ierr);
  if (flg && snes->method_class == SNES_NONLINEAR_EQUATIONS) {
    ierr = SNESSetJacobian(snes,snes->jacobian,snes->jacobian_pre,
                 SNESDefaultComputeJacobian,snes->funP);CHKERRQ(ierr);
    PLogInfo(snes,"SNESSetFromOptions: Setting default finite difference Jacobian matrix\n");
  } else if (flg && snes->method_class == SNES_UNCONSTRAINED_MINIMIZATION) {
    ierr = SNESSetHessian(snes,snes->jacobian,snes->jacobian_pre,
                 SNESDefaultComputeHessian,snes->funP);CHKERRQ(ierr);
    PLogInfo(snes,"SNESSetFromOptions: Setting default finite difference Hessian matrix\n");
  }

  for ( i=0; i<numberofsetfromoptions; i++ ) {
    ierr = (*othersetfromoptions[i])(snes);CHKERRQ(ierr);
  }

  ierr = SNESGetSLES(snes,&sles);CHKERRQ(ierr);
  ierr = SLESSetFromOptions(sles);CHKERRQ(ierr);

  /* set the special KSP monitor for matrix-free application */
  ierr = OptionsHasName(snes->prefix,"-snes_mf_ksp_monitor",&flg);CHKERRQ(ierr);
  if (flg) {
    KSP ksp;
    ierr = SLESGetKSP(sles,&ksp);CHKERRQ(ierr);
    ierr = KSPSetMonitor(ksp,MatSNESMFKSPMonitor,PETSC_NULL,0);CHKERRQ(ierr);
  }

  ierr = OptionsHasName(PETSC_NULL,"-help", &flg);CHKERRQ(ierr);
  if (flg) { ierr = SNESPrintHelp(snes);CHKERRQ(ierr);} 

  if (snes->setfromoptions) {
    ierr = (*snes->setfromoptions)(snes);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0); 
}


#undef __FUNC__  
#define __FUNC__ "SNESSetApplicationContext"
/*@
   SNESSetApplicationContext - Sets the optional user-defined context for 
   the nonlinear solvers.  

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  usrP - optional user context

   Level: intermediate

.keywords: SNES, nonlinear, set, application, context

.seealso: SNESGetApplicationContext()
@*/
int SNESSetApplicationContext(SNES snes,void *usrP)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  snes->user		= usrP;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetApplicationContext"
/*@C
   SNESGetApplicationContext - Gets the user-defined context for the 
   nonlinear solvers.  

   Not Collective

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  usrP - user context

   Level: intermediate

.keywords: SNES, nonlinear, get, application, context

.seealso: SNESSetApplicationContext()
@*/
int SNESGetApplicationContext( SNES snes,  void **usrP )
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *usrP = snes->user;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetIterationNumber"
/*@
   SNESGetIterationNumber - Gets the number of nonlinear iterations completed
   at this time.

   Not Collective

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  iter - iteration number

   Notes:
   For example, during the computation of iteration 2 this would return 1.

   This is useful for using lagged Jacobians (where one does not recompute the 
   Jacobian at each SNES iteration). For example, the code
.vb
      ierr = SNESGetIterationNumber(snes,&it);
      if (!(it % 2)) {
        [compute Jacobian here]
      }
.ve
   can be used in your ComputeJacobian() function to cause the Jacobian to be
   recomputed every second SNES iteration.

   Level: intermediate

.keywords: SNES, nonlinear, get, iteration, number
@*/
int SNESGetIterationNumber(SNES snes,int* iter)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidIntPointer(iter);
  *iter = snes->iter;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetFunctionNorm"
/*@
   SNESGetFunctionNorm - Gets the norm of the current function that was set
   with SNESSSetFunction().

   Collective on SNES

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  fnorm - 2-norm of function

   Note:
   SNESGetFunctionNorm() is valid for SNES_NONLINEAR_EQUATIONS methods only.
   A related routine for SNES_UNCONSTRAINED_MINIMIZATION methods is
   SNESGetGradientNorm().

   Level: intermediate

.keywords: SNES, nonlinear, get, function, norm

.seealso: SNESGetFunction()
@*/
int SNESGetFunctionNorm(SNES snes,Scalar *fnorm)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidScalarPointer(fnorm);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"For SNES_NONLINEAR_EQUATIONS only");
  }
  *fnorm = snes->norm;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetGradientNorm"
/*@
   SNESGetGradientNorm - Gets the norm of the current gradient that was set
   with SNESSSetGradient().

   Collective on SNES

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  fnorm - 2-norm of gradient

   Note:
   SNESGetGradientNorm() is valid for SNES_UNCONSTRAINED_MINIMIZATION 
   methods only.  A related routine for SNES_NONLINEAR_EQUATIONS methods
   is SNESGetFunctionNorm().

   Level: intermediate

.keywords: SNES, nonlinear, get, gradient, norm

.seelso: SNESSetGradient()
@*/
int SNESGetGradientNorm(SNES snes,Scalar *gnorm)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidScalarPointer(gnorm);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  *gnorm = snes->norm;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetNumberUnsuccessfulSteps"
/*@
   SNESGetNumberUnsuccessfulSteps - Gets the number of unsuccessful steps
   attempted by the nonlinear solver.

   Not Collective

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  nfails - number of unsuccessful steps attempted

   Notes:
   This counter is reset to zero for each successive call to SNESSolve().

   Level: intermediate

.keywords: SNES, nonlinear, get, number, unsuccessful, steps
@*/
int SNESGetNumberUnsuccessfulSteps(SNES snes,int* nfails)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidIntPointer(nfails);
  *nfails = snes->nfailures;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetNumberLinearIterations"
/*@
   SNESGetNumberLinearIterations - Gets the total number of linear iterations
   used by the nonlinear solver.

   Not Collective

   Input Parameter:
.  snes - SNES context

   Output Parameter:
.  lits - number of linear iterations

   Notes:
   This counter is reset to zero for each successive call to SNESSolve().

   Level: intermediate

.keywords: SNES, nonlinear, get, number, linear, iterations
@*/
int SNESGetNumberLinearIterations(SNES snes,int* lits)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidIntPointer(lits);
  *lits = snes->linear_its;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetSLES"
/*@C
   SNESGetSLES - Returns the SLES context for a SNES solver.

   Not Collective, but if SNES object is parallel, then SLES object is parallel

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
.  sles - the SLES context

   Notes:
   The user can then directly manipulate the SLES context to set various
   options, etc.  Likewise, the user can then extract and manipulate the 
   KSP and PC contexts as well.

   Level: beginner

.keywords: SNES, nonlinear, get, SLES, context

.seealso: SLESGetPC(), SLESGetKSP()
@*/
int SNESGetSLES(SNES snes,SLES *sles)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *sles = snes->sles;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESPublish_Petsc"
static int SNESPublish_Petsc(PetscObject object)
{
#if defined(PETSC_HAVE_AMS)
  SNES          v = (SNES) object;
  int          ierr;
#endif

  PetscFunctionBegin;

#if defined(PETSC_HAVE_AMS)
  /* if it is already published then return */
  if (v->amem >=0 ) PetscFunctionReturn(0);

  ierr = PetscObjectPublishBaseBegin(object);CHKERRQ(ierr);
  ierr = AMS_Memory_add_field((AMS_Memory)v->amem,"Iteration",&v->iter,1,AMS_INT,AMS_READ,
                                AMS_COMMON,AMS_REDUCT_UNDEF);CHKERRQ(ierr);
  ierr = AMS_Memory_add_field((AMS_Memory)v->amem,"Residual",&v->norm,1,AMS_DOUBLE,AMS_READ,
                                AMS_COMMON,AMS_REDUCT_UNDEF);CHKERRQ(ierr);
  ierr = PetscObjectPublishBaseEnd(object);CHKERRQ(ierr);
#endif
  PetscFunctionReturn(0);
}

/* -----------------------------------------------------------*/
#undef __FUNC__  
#define __FUNC__ "SNESCreate"
/*@C
   SNESCreate - Creates a nonlinear solver context.

   Collective on MPI_Comm

   Input Parameters:
+  comm - MPI communicator
-  type - type of method, either 
   SNES_NONLINEAR_EQUATIONS (for systems of nonlinear equations) 
   or SNES_UNCONSTRAINED_MINIMIZATION (for unconstrained minimization)

   Output Parameter:
.  outsnes - the new SNES context

   Options Database Keys:
+   -snes_mf - Activates default matrix-free Jacobian-vector products,
               and no preconditioning matrix
.   -snes_mf_operator - Activates default matrix-free Jacobian-vector
               products, and a user-provided preconditioning matrix
               as set by SNESSetJacobian()
-   -snes_fd - Uses (slow!) finite differences to compute Jacobian

   Level: beginner

.keywords: SNES, nonlinear, create, context

.seealso: SNESSolve(), SNESDestroy()
@*/
int SNESCreate(MPI_Comm comm,SNESProblemType type,SNES *outsnes)
{
  int                 ierr;
  SNES                snes;
  SNES_KSP_EW_ConvCtx *kctx;

  PetscFunctionBegin;
  *outsnes = 0;
  if (type != SNES_UNCONSTRAINED_MINIMIZATION && type != SNES_NONLINEAR_EQUATIONS){
    SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"incorrect method type"); 
  }
  PetscHeaderCreate(snes,_p_SNES,int,SNES_COOKIE,0,"SNES",comm,SNESDestroy,SNESView);
  PLogObjectCreate(snes);
  snes->bops->publish     = SNESPublish_Petsc;
  snes->max_its           = 50;
  snes->max_funcs	  = 10000;
  snes->norm		  = 0.0;
  if (type == SNES_UNCONSTRAINED_MINIMIZATION) {
    snes->rtol		  = 1.e-8;
    snes->ttol            = 0.0;
    snes->atol		  = 1.e-10;
  } else {
    snes->rtol		  = 1.e-8;
    snes->ttol            = 0.0;
    snes->atol		  = 1.e-50;
  }
  snes->xtol		  = 1.e-8;
  snes->trunctol	  = 1.e-12; /* no longer used */
  snes->nfuncs            = 0;
  snes->nfailures         = 0;
  snes->linear_its        = 0;
  snes->numbermonitors    = 0;
  snes->data              = 0;
  snes->view              = 0;
  snes->computeumfunction = 0;
  snes->umfunP            = 0;
  snes->fc                = 0;
  snes->deltatol          = 1.e-12;
  snes->fmin              = -1.e30;
  snes->method_class      = type;
  snes->set_method_called = 0;
  snes->setupcalled      = 0;
  snes->ksp_ewconv        = 0;
  snes->vwork             = 0;
  snes->nwork             = 0;
  snes->conv_hist_len     = 0;
  snes->conv_hist_max     = 0;
  snes->conv_hist         = PETSC_NULL;
  snes->conv_hist_its     = PETSC_NULL;
  snes->conv_hist_reset   = PETSC_TRUE;
  snes->reason            = SNES_CONVERGED_ITERATING;

  /* Create context to compute Eisenstat-Walker relative tolerance for KSP */
  kctx = PetscNew(SNES_KSP_EW_ConvCtx);CHKPTRQ(kctx);
  PLogObjectMemory(snes,sizeof(SNES_KSP_EW_ConvCtx));
  snes->kspconvctx  = (void*)kctx;
  kctx->version     = 2;
  kctx->rtol_0      = .3; /* Eisenstat and Walker suggest rtol_0=.5, but 
                             this was too large for some test cases */
  kctx->rtol_last   = 0;
  kctx->rtol_max    = .9;
  kctx->gamma       = 1.0;
  kctx->alpha2      = .5*(1.0 + sqrt(5.0));
  kctx->alpha       = kctx->alpha2;
  kctx->threshold   = .1;
  kctx->lresid_last = 0;
  kctx->norm_last   = 0;

  ierr = SLESCreate(comm,&snes->sles);CHKERRQ(ierr);
  PLogObjectParent(snes,snes->sles)

  *outsnes = snes;
  PetscPublishAll(snes);
  PetscFunctionReturn(0);
}

/* --------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ "SNESSetFunction"
/*@C
   SNESSetFunction - Sets the function evaluation routine and function 
   vector for use by the SNES routines in solving systems of nonlinear
   equations.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  func - function evaluation routine
.  r - vector to store function value
-  ctx - [optional] user-defined context for private data for the 
         function evaluation routine (may be PETSC_NULL)

   Calling sequence of func:
$    func (SNES snes,Vec x,Vec f,void *ctx);

.  f - function vector
-  ctx - optional user-defined function context 

   Notes:
   The Newton-like methods typically solve linear systems of the form
$      f'(x) x = -f(x),
   where f'(x) denotes the Jacobian matrix and f(x) is the function.

   SNESSetFunction() is valid for SNES_NONLINEAR_EQUATIONS methods only.
   Analogous routines for SNES_UNCONSTRAINED_MINIMIZATION methods are
   SNESSetMinimizationFunction() and SNESSetGradient();

   Level: beginner

.keywords: SNES, nonlinear, set, function

.seealso: SNESGetFunction(), SNESComputeFunction(), SNESSetJacobian()
@*/
int SNESSetFunction( SNES snes, Vec r, int (*func)(SNES,Vec,Vec,void*),void *ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(r,VEC_COOKIE);
  PetscCheckSameComm(snes,r);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }

  snes->computefunction     = func; 
  snes->vec_func            = snes->vec_func_always = r;
  snes->funP                = ctx;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESComputeFunction"
/*@
   SNESComputeFunction - Calls the function that has been set with
                         SNESSetFunction().  

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  x - input vector

   Output Parameter:
.  y - function vector, as set by SNESSetFunction()

   Notes:
   SNESComputeFunction() is valid for SNES_NONLINEAR_EQUATIONS methods only.
   Analogous routines for SNES_UNCONSTRAINED_MINIMIZATION methods are
   SNESComputeMinimizationFunction() and SNESComputeGradient();

   SNESComputeFunction() is typically used within nonlinear solvers
   implementations, so most users would not generally call this routine
   themselves.

   Level: developer

.keywords: SNES, nonlinear, compute, function

.seealso: SNESSetFunction(), SNESGetFunction()
@*/
int SNESComputeFunction(SNES snes,Vec x, Vec y)
{
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscValidHeaderSpecific(y,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  PetscCheckSameComm(snes,y);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }

  PLogEventBegin(SNES_FunctionEval,snes,x,y,0);
  PetscStackPush("SNES user function");
  ierr = (*snes->computefunction)(snes,x,y,snes->funP);CHKERRQ(ierr);
  PetscStackPop;
  snes->nfuncs++;
  PLogEventEnd(SNES_FunctionEval,snes,x,y,0);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetMinimizationFunction"
/*@C
   SNESSetMinimizationFunction - Sets the function evaluation routine for 
   unconstrained minimization.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  func - function evaluation routine
-  ctx - [optional] user-defined context for private data for the 
         function evaluation routine (may be PETSC_NULL)

   Calling sequence of func:
$     func (SNES snes,Vec x,double *f,void *ctx);

+  x - input vector
.  f - function
-  ctx - [optional] user-defined function context 

   Level: beginner

   Notes:
   SNESSetMinimizationFunction() is valid for SNES_UNCONSTRAINED_MINIMIZATION
   methods only. An analogous routine for SNES_NONLINEAR_EQUATIONS methods is
   SNESSetFunction().

.keywords: SNES, nonlinear, set, minimization, function

.seealso:  SNESGetMinimizationFunction(), SNESComputeMinimizationFunction(),
           SNESSetHessian(), SNESSetGradient()
@*/
int SNESSetMinimizationFunction(SNES snes,int (*func)(SNES,Vec,double*,void*),
                      void *ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"Only for SNES_UNCONSTRAINED_MINIMIZATION");
  }
  snes->computeumfunction   = func; 
  snes->umfunP              = ctx;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESComputeMinimizationFunction"
/*@
   SNESComputeMinimizationFunction - Computes the function that has been
   set with SNESSetMinimizationFunction().

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  x - input vector

   Output Parameter:
.  y - function value

   Notes:
   SNESComputeMinimizationFunction() is valid only for 
   SNES_UNCONSTRAINED_MINIMIZATION methods. An analogous routine for 
   SNES_NONLINEAR_EQUATIONS methods is SNESComputeFunction().

   SNESComputeMinimizationFunction() is typically used within minimization
   implementations, so most users would not generally call this routine
   themselves.

   Level: developer

.keywords: SNES, nonlinear, compute, minimization, function

.seealso: SNESSetMinimizationFunction(), SNESGetMinimizationFunction(),
          SNESComputeGradient(), SNESComputeHessian()
@*/
int SNESComputeMinimizationFunction(SNES snes,Vec x,double *y)
{
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"Only for SNES_UNCONSTRAINED_MINIMIZATION");
  }

  PLogEventBegin(SNES_MinimizationFunctionEval,snes,x,y,0);
  PetscStackPush("SNES user minimzation function");
  ierr = (*snes->computeumfunction)(snes,x,y,snes->umfunP);CHKERRQ(ierr);
  PetscStackPop;
  snes->nfuncs++;
  PLogEventEnd(SNES_MinimizationFunctionEval,snes,x,y,0);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetGradient"
/*@C
   SNESSetGradient - Sets the gradient evaluation routine and gradient
   vector for use by the SNES routines.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  func - function evaluation routine
.  ctx - optional user-defined context for private data for the 
         gradient evaluation routine (may be PETSC_NULL)
-  r - vector to store gradient value

   Calling sequence of func:
$     func (SNES, Vec x, Vec g, void *ctx);

+  x - input vector
.  g - gradient vector
-  ctx - optional user-defined gradient context 

   Notes:
   SNESSetMinimizationFunction() is valid for SNES_UNCONSTRAINED_MINIMIZATION
   methods only. An analogous routine for SNES_NONLINEAR_EQUATIONS methods is
   SNESSetFunction().

   Level: beginner

.keywords: SNES, nonlinear, set, function

.seealso: SNESGetGradient(), SNESComputeGradient(), SNESSetHessian(),
          SNESSetMinimizationFunction(),
@*/
int SNESSetGradient(SNES snes,Vec r,int (*func)(SNES,Vec,Vec,void*),void *ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(r,VEC_COOKIE);
  PetscCheckSameComm(snes,r);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  snes->computefunction     = func;
  snes->vec_func            = snes->vec_func_always = r;
  snes->funP                = ctx;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESComputeGradient"
/*@
   SNESComputeGradient - Computes the gradient that has been set with
   SNESSetGradient().

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  x - input vector

   Output Parameter:
.  y - gradient vector

   Notes:
   SNESComputeGradient() is valid only for 
   SNES_UNCONSTRAINED_MINIMIZATION methods. An analogous routine for 
   SNES_NONLINEAR_EQUATIONS methods is SNESComputeFunction().

   SNESComputeGradient() is typically used within minimization
   implementations, so most users would not generally call this routine
   themselves.

   Level: developer

.keywords: SNES, nonlinear, compute, gradient

.seealso:  SNESSetGradient(), SNESGetGradient(), 
           SNESComputeMinimizationFunction(), SNESComputeHessian()
@*/
int SNESComputeGradient(SNES snes,Vec x, Vec y)
{
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscValidHeaderSpecific(y,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  PetscCheckSameComm(snes,y);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  PLogEventBegin(SNES_GradientEval,snes,x,y,0);
  PetscStackPush("SNES user gradient function");
  ierr = (*snes->computefunction)(snes,x,y,snes->funP);CHKERRQ(ierr);
  PetscStackPop;
  PLogEventEnd(SNES_GradientEval,snes,x,y,0);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESComputeJacobian"
/*@
   SNESComputeJacobian - Computes the Jacobian matrix that has been
   set with SNESSetJacobian().

   Collective on SNES and Mat

   Input Parameters:
+  snes - the SNES context
-  x - input vector

   Output Parameters:
+  A - Jacobian matrix
.  B - optional preconditioning matrix
-  flag - flag indicating matrix structure

   Notes: 
   Most users should not need to explicitly call this routine, as it 
   is used internally within the nonlinear solvers. 

   See SLESSetOperators() for important information about setting the
   flag parameter.

   SNESComputeJacobian() is valid only for SNES_NONLINEAR_EQUATIONS
   methods. An analogous routine for SNES_UNCONSTRAINED_MINIMIZATION 
   methods is SNESComputeHessian().

   SNESComputeJacobian() is typically used within nonlinear solver
   implementations, so most users would not generally call this routine
   themselves.

   Level: developer

.keywords: SNES, compute, Jacobian, matrix

.seealso:  SNESSetJacobian(), SLESSetOperators()
@*/
int SNESComputeJacobian(SNES snes,Vec X,Mat *A,Mat *B,MatStructure *flg)
{
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(X,VEC_COOKIE);
  PetscCheckSameComm(snes,X);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }
  if (!snes->computejacobian) PetscFunctionReturn(0);
  PLogEventBegin(SNES_JacobianEval,snes,X,*A,*B);
  *flg = DIFFERENT_NONZERO_PATTERN;
  PetscStackPush("SNES user Jacobian function");
  ierr = (*snes->computejacobian)(snes,X,A,B,flg,snes->jacP);CHKERRQ(ierr);
  PetscStackPop;
  PLogEventEnd(SNES_JacobianEval,snes,X,*A,*B);
  /* make sure user returned a correct Jacobian and preconditioner */
  PetscValidHeaderSpecific(*A,MAT_COOKIE);
  PetscValidHeaderSpecific(*B,MAT_COOKIE);  
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESComputeHessian"
/*@
   SNESComputeHessian - Computes the Hessian matrix that has been
   set with SNESSetHessian().

   Collective on SNES and Mat

   Input Parameters:
+  snes - the SNES context
-  x - input vector

   Output Parameters:
+  A - Hessian matrix
.  B - optional preconditioning matrix
-  flag - flag indicating matrix structure

   Notes: 
   Most users should not need to explicitly call this routine, as it
   is used internally within the nonlinear solvers. 

   See SLESSetOperators() for important information about setting the
   flag parameter.

   SNESComputeHessian() is valid only for 
   SNES_UNCONSTRAINED_MINIMIZATION methods. An analogous routine for 
   SNES_NONLINEAR_EQUATIONS methods is SNESComputeJacobian().

   SNESComputeHessian() is typically used within minimization
   implementations, so most users would not generally call this routine
   themselves.

   Level: developer

.keywords: SNES, compute, Hessian, matrix

.seealso:  SNESSetHessian(), SLESSetOperators(), SNESComputeGradient(),
           SNESComputeMinimizationFunction()
@*/
int SNESComputeHessian(SNES snes,Vec x,Mat *A,Mat *B,MatStructure *flag)
{
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  if (!snes->computejacobian) PetscFunctionReturn(0);
  PLogEventBegin(SNES_HessianEval,snes,x,*A,*B);
  *flag = DIFFERENT_NONZERO_PATTERN;
  PetscStackPush("SNES user Hessian function");
  ierr = (*snes->computejacobian)(snes,x,A,B,flag,snes->jacP);CHKERRQ(ierr);
  PetscStackPop;
  PLogEventEnd(SNES_HessianEval,snes,x,*A,*B);
  /* make sure user returned a correct Jacobian and preconditioner */
  PetscValidHeaderSpecific(*A,MAT_COOKIE);
  PetscValidHeaderSpecific(*B,MAT_COOKIE);  
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetJacobian"
/*@C
   SNESSetJacobian - Sets the function to compute Jacobian as well as the
   location to store the matrix.

   Collective on SNES and Mat

   Input Parameters:
+  snes - the SNES context
.  A - Jacobian matrix
.  B - preconditioner matrix (usually same as the Jacobian)
.  func - Jacobian evaluation routine
-  ctx - [optional] user-defined context for private data for the 
         Jacobian evaluation routine (may be PETSC_NULL)

   Calling sequence of func:
$     func (SNES snes,Vec x,Mat *A,Mat *B,int *flag,void *ctx);

+  x - input vector
.  A - Jacobian matrix
.  B - preconditioner matrix, usually the same as A
.  flag - flag indicating information about the preconditioner matrix
   structure (same as flag in SLESSetOperators())
-  ctx - [optional] user-defined Jacobian context

   Notes: 
   See SLESSetOperators() for important information about setting the flag
   output parameter in the routine func().  Be sure to read this information!

   The routine func() takes Mat * as the matrix arguments rather than Mat.  
   This allows the Jacobian evaluation routine to replace A and/or B with a 
   completely new new matrix structure (not just different matrix elements)
   when appropriate, for instance, if the nonzero structure is changing
   throughout the global iterations.

   Level: beginner

.keywords: SNES, nonlinear, set, Jacobian, matrix

.seealso: SLESSetOperators(), SNESSetFunction()
@*/
int SNESSetJacobian(SNES snes,Mat A,Mat B,int (*func)(SNES,Vec,Mat*,Mat*,
                    MatStructure*,void*),void *ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(A,MAT_COOKIE);
  PetscValidHeaderSpecific(B,MAT_COOKIE);
  PetscCheckSameComm(snes,A);
  PetscCheckSameComm(snes,B);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }

  snes->computejacobian = func;
  snes->jacP            = ctx;
  snes->jacobian        = A;
  snes->jacobian_pre    = B;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetJacobian"
/*@C
   SNESGetJacobian - Returns the Jacobian matrix and optionally the user 
   provided context for evaluating the Jacobian.

   Not Collective, but Mat object will be parallel if SNES object is

   Input Parameter:
.  snes - the nonlinear solver context

   Output Parameters:
+  A - location to stash Jacobian matrix (or PETSC_NULL)
.  B - location to stash preconditioner matrix (or PETSC_NULL)
-  ctx - location to stash Jacobian ctx (or PETSC_NULL)

   Level: advanced

.seealso: SNESSetJacobian(), SNESComputeJacobian()
@*/
int SNESGetJacobian(SNES snes,Mat *A,Mat *B, void **ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }
  if (A)   *A = snes->jacobian;
  if (B)   *B = snes->jacobian_pre;
  if (ctx) *ctx = snes->jacP;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetHessian"
/*@C
   SNESSetHessian - Sets the function to compute Hessian as well as the
   location to store the matrix.

   Collective on SNES and Mat

   Input Parameters:
+  snes - the SNES context
.  A - Hessian matrix
.  B - preconditioner matrix (usually same as the Hessian)
.  func - Jacobian evaluation routine
-  ctx - [optional] user-defined context for private data for the 
         Hessian evaluation routine (may be PETSC_NULL)

   Calling sequence of func:
$    func (SNES snes,Vec x,Mat *A,Mat *B,int *flag,void *ctx);

+  x - input vector
.  A - Hessian matrix
.  B - preconditioner matrix, usually the same as A
.  flag - flag indicating information about the preconditioner matrix
   structure (same as flag in SLESSetOperators())
-  ctx - [optional] user-defined Hessian context

   Notes: 
   See SLESSetOperators() for important information about setting the flag
   output parameter in the routine func().  Be sure to read this information!

   The function func() takes Mat * as the matrix arguments rather than Mat.  
   This allows the Hessian evaluation routine to replace A and/or B with a 
   completely new new matrix structure (not just different matrix elements)
   when appropriate, for instance, if the nonzero structure is changing
   throughout the global iterations.

   Level: beginner

.keywords: SNES, nonlinear, set, Hessian, matrix

.seealso: SNESSetMinimizationFunction(), SNESSetGradient(), SLESSetOperators()
@*/
int SNESSetHessian(SNES snes,Mat A,Mat B,int (*func)(SNES,Vec,Mat*,Mat*,
                    MatStructure*,void*),void *ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(A,MAT_COOKIE);
  PetscValidHeaderSpecific(B,MAT_COOKIE);
  PetscCheckSameComm(snes,A);
  PetscCheckSameComm(snes,B);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  snes->computejacobian = func;
  snes->jacP            = ctx;
  snes->jacobian        = A;
  snes->jacobian_pre    = B;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetHessian"
/*@
   SNESGetHessian - Returns the Hessian matrix and optionally the user 
   provided context for evaluating the Hessian.

   Not Collective, but Mat object is parallel if SNES object is parallel

   Input Parameter:
.  snes - the nonlinear solver context

   Output Parameters:
+  A - location to stash Hessian matrix (or PETSC_NULL)
.  B - location to stash preconditioner matrix (or PETSC_NULL)
-  ctx - location to stash Hessian ctx (or PETSC_NULL)

   Level: advanced

.seealso: SNESSetHessian(), SNESComputeHessian()

.keywords: SNES, get, Hessian
@*/
int SNESGetHessian(SNES snes,Mat *A,Mat *B, void **ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION){
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  if (A)   *A = snes->jacobian;
  if (B)   *B = snes->jacobian_pre;
  if (ctx) *ctx = snes->jacP;
  PetscFunctionReturn(0);
}

/* ----- Routines to initialize and destroy a nonlinear solver ---- */

#undef __FUNC__  
#define __FUNC__ "SNESSetUp"
/*@
   SNESSetUp - Sets up the internal data structures for the later use
   of a nonlinear solver.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  x - the solution vector

   Notes:
   For basic use of the SNES solvers the user need not explicitly call
   SNESSetUp(), since these actions will automatically occur during
   the call to SNESSolve().  However, if one wishes to control this
   phase separately, SNESSetUp() should be called after SNESCreate()
   and optional routines of the form SNESSetXXX(), but before SNESSolve().  

   Level: advanced

.keywords: SNES, nonlinear, setup

.seealso: SNESCreate(), SNESSolve(), SNESDestroy()
@*/
int SNESSetUp(SNES snes,Vec x)
{
  int ierr, flg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  snes->vec_sol = snes->vec_sol_always = x;

  ierr = OptionsHasName(snes->prefix,"-snes_mf_operator", &flg);CHKERRQ(ierr); 
  /*
      This version replaces the user provided Jacobian matrix with a
      matrix-free version but still employs the user-provided preconditioner matrix
  */
  if (flg) {
    Mat J;
    ierr = MatCreateSNESMF(snes,snes->vec_sol,&J);CHKERRQ(ierr);
    PLogObjectParent(snes,J);
    snes->mfshell = J;
    snes->jacobian = J;
    if (snes->method_class == SNES_NONLINEAR_EQUATIONS) {
      PLogInfo(snes,"SNESSetUp: Setting default matrix-free operator Jacobian routines\n");
    } else if (snes->method_class == SNES_UNCONSTRAINED_MINIMIZATION) {
      PLogInfo(snes,"SNESSetUp: Setting default matrix-free operator Hessian routines\n");
    } else {
      SETERRQ(PETSC_ERR_SUP,0,"Method class doesn't support matrix-free operator option");
    }
    ierr = MatSNESMFSetFromOptions(J);CHKERRQ(ierr);
  }
  ierr = OptionsHasName(snes->prefix,"-snes_mf", &flg);CHKERRQ(ierr); 
  /*
      This version replaces both the user-provided Jacobian and the user-
      provided preconditioner matrix with the default matrix free version.
   */
  if (flg) {
    Mat J;
    ierr = MatCreateSNESMF(snes,snes->vec_sol,&J);CHKERRQ(ierr);
    PLogObjectParent(snes,J);
    snes->mfshell = J;
    if (snes->method_class == SNES_NONLINEAR_EQUATIONS) {
      ierr = SNESSetJacobian(snes,J,J,0,snes->funP);CHKERRQ(ierr);
      PLogInfo(snes,"SNESSetUp: Setting default matrix-free Jacobian routines\n");
    } else if (snes->method_class == SNES_UNCONSTRAINED_MINIMIZATION) {
      ierr = SNESSetHessian(snes,J,J,0,snes->funP);CHKERRQ(ierr);
      PLogInfo(snes,"SNESSetUp: Setting default matrix-free Hessian routines\n");
    } else {
      SETERRQ(PETSC_ERR_SUP,0,"Method class doesn't support matrix-free option");
    }
    ierr = MatSNESMFSetFromOptions(J);CHKERRQ(ierr);
  }
  if ((snes->method_class == SNES_NONLINEAR_EQUATIONS)) {
    if (!snes->vec_func) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetFunction() first");
    if (!snes->computefunction) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetFunction() first");
    if (!snes->jacobian) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetJacobian() first \n or use -snes_mf option");
    if (snes->vec_func == snes->vec_sol) {  
      SETERRQ(PETSC_ERR_ARG_IDN,0,"Solution vector cannot be function vector");
    }

    /* Set the KSP stopping criterion to use the Eisenstat-Walker method */
    if (snes->ksp_ewconv && PetscStrcmp(snes->type_name,SNES_EQ_TR)) {
      SLES sles; KSP ksp;
      ierr = SNESGetSLES(snes,&sles);CHKERRQ(ierr);
      ierr = SLESGetKSP(sles,&ksp);CHKERRQ(ierr);
      ierr = KSPSetConvergenceTest(ksp,SNES_KSP_EW_Converged_Private,(void *)snes);CHKERRQ(ierr);
    }
  } else if ((snes->method_class == SNES_UNCONSTRAINED_MINIMIZATION)) {
    if (!snes->vec_func) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetGradient() first");
    if (!snes->computefunction) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetGradient() first");
    if (!snes->computeumfunction) {
      SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetMinimizationFunction() first");
    }
    if (!snes->jacobian) SETERRQ(PETSC_ERR_ARG_WRONGSTATE,0,"Must call SNESSetHessian()");
  } else {
    SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Unknown method class");
  }
  if (snes->setup) {ierr = (*snes->setup)(snes);CHKERRQ(ierr);}
  snes->setupcalled = 1;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESDestroy"
/*@C
   SNESDestroy - Destroys the nonlinear solver context that was created
   with SNESCreate().

   Collective on SNES

   Input Parameter:
.  snes - the SNES context

   Level: beginner

.keywords: SNES, nonlinear, destroy

.seealso: SNESCreate(), SNESSolve()
@*/
int SNESDestroy(SNES snes)
{
  int i,ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (--snes->refct > 0) PetscFunctionReturn(0);

  /* if memory was published with AMS then destroy it */
  ierr = PetscAMSDestroy(snes);CHKERRQ(ierr);

  if (snes->destroy) {ierr = (*(snes)->destroy)(snes);CHKERRQ(ierr);}
  if (snes->kspconvctx) {ierr = PetscFree(snes->kspconvctx);CHKERRQ(ierr);}
  if (snes->mfshell) {ierr = MatDestroy(snes->mfshell);CHKERRQ(ierr);}
  ierr = SLESDestroy(snes->sles);CHKERRQ(ierr);
  if (snes->vwork) {ierr = VecDestroyVecs(snes->vwork,snes->nvwork);CHKERRQ(ierr);}
  for (i=0; i<snes->numbermonitors; i++ ) {
    if (snes->monitordestroy[i]) {
      ierr = (*snes->monitordestroy[i])(snes->monitorcontext[i]);CHKERRQ(ierr);
    }
  }
  PLogObjectDestroy((PetscObject)snes);
  PetscHeaderDestroy((PetscObject)snes);
  PetscFunctionReturn(0);
}

/* ----------- Routines to set solver parameters ---------- */

#undef __FUNC__  
#define __FUNC__ "SNESSetTolerances"
/*@
   SNESSetTolerances - Sets various parameters used in convergence tests.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  atol - absolute convergence tolerance
.  rtol - relative convergence tolerance
.  stol -  convergence tolerance in terms of the norm
           of the change in the solution between steps
.  maxit - maximum number of iterations
-  maxf - maximum number of function evaluations

   Options Database Keys: 
+    -snes_atol <atol> - Sets atol
.    -snes_rtol <rtol> - Sets rtol
.    -snes_stol <stol> - Sets stol
.    -snes_max_it <maxit> - Sets maxit
-    -snes_max_funcs <maxf> - Sets maxf

   Notes:
   The default maximum number of iterations is 50.
   The default maximum number of function evaluations is 1000.

   Level: intermediate

.keywords: SNES, nonlinear, set, convergence, tolerances

.seealso: SNESSetTrustRegionTolerance(), SNESSetMinimizationFunctionTolerance()
@*/
int SNESSetTolerances(SNES snes,double atol,double rtol,double stol,int maxit,int maxf)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (atol != PETSC_DEFAULT)  snes->atol      = atol;
  if (rtol != PETSC_DEFAULT)  snes->rtol      = rtol;
  if (stol != PETSC_DEFAULT)  snes->xtol      = stol;
  if (maxit != PETSC_DEFAULT) snes->max_its   = maxit;
  if (maxf != PETSC_DEFAULT)  snes->max_funcs = maxf;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetTolerances"
/*@
   SNESGetTolerances - Gets various parameters used in convergence tests.

   Not Collective

   Input Parameters:
+  snes - the SNES context
.  atol - absolute convergence tolerance
.  rtol - relative convergence tolerance
.  stol -  convergence tolerance in terms of the norm
           of the change in the solution between steps
.  maxit - maximum number of iterations
-  maxf - maximum number of function evaluations

   Notes:
   The user can specify PETSC_NULL for any parameter that is not needed.

   Level: intermediate

.keywords: SNES, nonlinear, get, convergence, tolerances

.seealso: SNESSetTolerances()
@*/
int SNESGetTolerances(SNES snes,double *atol,double *rtol,double *stol,int *maxit,int *maxf)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (atol)  *atol  = snes->atol;
  if (rtol)  *rtol  = snes->rtol;
  if (stol)  *stol  = snes->xtol;
  if (maxit) *maxit = snes->max_its;
  if (maxf)  *maxf  = snes->max_funcs;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetTrustRegionTolerance"
/*@
   SNESSetTrustRegionTolerance - Sets the trust region parameter tolerance.  

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  tol - tolerance
   
   Options Database Key: 
.  -snes_trtol <tol> - Sets tol

   Level: intermediate

.keywords: SNES, nonlinear, set, trust region, tolerance

.seealso: SNESSetTolerances(), SNESSetMinimizationFunctionTolerance()
@*/
int SNESSetTrustRegionTolerance(SNES snes,double tol)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  snes->deltatol = tol;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetMinimizationFunctionTolerance"
/*@
   SNESSetMinimizationFunctionTolerance - Sets the minimum allowable function tolerance
   for unconstrained minimization solvers.
   
   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  ftol - minimum function tolerance

   Options Database Key: 
.  -snes_fmin <ftol> - Sets ftol

   Note:
   SNESSetMinimizationFunctionTolerance() is valid for SNES_UNCONSTRAINED_MINIMIZATION
   methods only.

   Level: intermediate

.keywords: SNES, nonlinear, set, minimum, convergence, function, tolerance

.seealso: SNESSetTolerances(), SNESSetTrustRegionTolerance()
@*/
int SNESSetMinimizationFunctionTolerance(SNES snes,double ftol)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  snes->fmin = ftol;
  PetscFunctionReturn(0);
}
/* 
   Duplicate the lg monitors for SNES from KSP; for some reason with 
   dynamic libraries things don't work under Sun4 if we just use 
   macros instead of functions
*/
#undef __FUNC__  
#define __FUNC__ "SNESLGMonitor"
int SNESLGMonitor(SNES snes,int it,double norm,void *ctx)
{
  int ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  ierr = KSPLGMonitor((KSP)snes,it,norm,ctx);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESLGMonitorCreate"
int SNESLGMonitorCreate(char *host,char *label,int x,int y,int m,int n, DrawLG *draw)
{
  int ierr;

  PetscFunctionBegin;
  ierr = KSPLGMonitorCreate(host,label,x,y,m,n,draw);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESLGMonitorDestroy"
int SNESLGMonitorDestroy(DrawLG draw)
{
  int ierr;

  PetscFunctionBegin;
  ierr = KSPLGMonitorDestroy(draw);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* ------------ Routines to set performance monitoring options ----------- */

#undef __FUNC__  
#define __FUNC__ "SNESSetMonitor"
/*@C
   SNESSetMonitor - Sets an ADDITIONAL function that is to be used at every
   iteration of the nonlinear solver to display the iteration's 
   progress.   

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  func - monitoring routine
.  mctx - [optional] user-defined context for private data for the 
          monitor routine (may be PETSC_NULL)
-  monitordestroy - options routine that frees monitor context

   Calling sequence of func:
$     int func(SNES snes,int its, double norm,void *mctx)

+    snes - the SNES context
.    its - iteration number
.    norm - 2-norm function value (may be estimated)
            for SNES_NONLINEAR_EQUATIONS methods
.    norm - 2-norm gradient value (may be estimated)
            for SNES_UNCONSTRAINED_MINIMIZATION methods
-    mctx - [optional] monitoring context

   Options Database Keys:
+    -snes_monitor        - sets SNESDefaultMonitor()
.    -snes_xmonitor       - sets line graph monitor,
                            uses SNESLGMonitorCreate()
_    -snes_cancelmonitors - cancels all monitors that have
                            been hardwired into a code by 
                            calls to SNESSetMonitor(), but
                            does not cancel those set via
                            the options database.

   Notes: 
   Several different monitoring routines may be set by calling
   SNESSetMonitor() multiple times; all will be called in the 
   order in which they were set.

   Level: intermediate

.keywords: SNES, nonlinear, set, monitor

.seealso: SNESDefaultMonitor(), SNESClearMonitor()
@*/
int SNESSetMonitor( SNES snes, int (*func)(SNES,int,double,void*),void *mctx,int (*monitordestroy)(void *))
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->numbermonitors >= MAXSNESMONITORS) {
    SETERRQ(PETSC_ERR_ARG_OUTOFRANGE,0,"Too many monitors set");
  }

  snes->monitor[snes->numbermonitors]           = func;
  snes->monitordestroy[snes->numbermonitors]    = monitordestroy;
  snes->monitorcontext[snes->numbermonitors++]  = (void*)mctx;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESClearMonitor"
/*@C
   SNESClearMonitor - Clears all the monitor functions for a SNES object.

   Collective on SNES

   Input Parameters:
.  snes - the SNES context

   Options Database:
.  -snes_cancelmonitors - cancels all monitors that have been hardwired
    into a code by calls to SNESSetMonitor(), but does not cancel those 
    set via the options database

   Notes: 
   There is no way to clear one specific monitor from a SNES object.

   Level: intermediate

.keywords: SNES, nonlinear, set, monitor

.seealso: SNESDefaultMonitor(), SNESSetMonitor()
@*/
int SNESClearMonitor( SNES snes )
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  snes->numbermonitors = 0;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetConvergenceTest"
/*@C
   SNESSetConvergenceTest - Sets the function that is to be used 
   to test for convergence of the nonlinear iterative solution.   

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
.  func - routine to test for convergence
-  cctx - [optional] context for private data for the convergence routine 
          (may be PETSC_NULL)

   Calling sequence of func:
$     int func (SNES snes,double xnorm,double gnorm,double f,SNESConvergedReason *reason,void *cctx)

+    snes - the SNES context
.    cctx - [optional] convergence context
.    reason - reason for convergence/divergence
.    xnorm - 2-norm of current iterate
.    gnorm - 2-norm of current step (SNES_NONLINEAR_EQUATIONS methods)
.    f - 2-norm of function (SNES_NONLINEAR_EQUATIONS methods)
.    gnorm - 2-norm of current gradient (SNES_UNCONSTRAINED_MINIMIZATION methods)
-    f - function value (SNES_UNCONSTRAINED_MINIMIZATION methods)

   Level: advanced

.keywords: SNES, nonlinear, set, convergence, test

.seealso: SNESConverged_EQ_LS(), SNESConverged_EQ_TR(), 
          SNESConverged_UM_LS(), SNESConverged_UM_TR()
@*/
int SNESSetConvergenceTest(SNES snes,int (*func)(SNES,double,double,double,SNESConvergedReason*,void*),void *cctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  (snes)->converged = func;
  (snes)->cnvP      = cctx;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetConvergedReason"
/*@C
   SNESGetConvergedReason - Gets the reason the SNES iteration was stopped.

   Not Collective

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
.  reason - negative value indicates diverged, positive value converged, see snes.h or the 
            manual pages for the individual convergence tests for complete lists

   Level: intermediate

   Notes: Can only be called after the call the SNESSolve() is complete.

.keywords: SNES, nonlinear, set, convergence, test

.seealso: SNESSetConvergenceTest(), SNESConverged_EQ_LS(), SNESConverged_EQ_TR(), 
          SNESConverged_UM_LS(), SNESConverged_UM_TR()
@*/
int SNESGetConvergedReason(SNES snes,SNESConvergedReason *reason)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *reason = snes->reason;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSetConvergenceHistory"
/*@
   SNESSetConvergenceHistory - Sets the array used to hold the convergence history.

   Collective on SNES

   Input Parameters:
+  snes - iterative context obtained from SNESCreate()
.  a   - array to hold history
.  its - integer array holds the number of linear iterations (or
         negative if not converged) for each solve.
.  na  - size of a and its
-  reset - PETSC_TRUTH indicates each new nonlinear solve resets the history counter to zero,
           else it continues storing new values for new nonlinear solves after the old ones

   Notes:
   If set, this array will contain the function norms (for
   SNES_NONLINEAR_EQUATIONS methods) or gradient norms
   (for SNES_UNCONSTRAINED_MINIMIZATION methods) computed
   at each step.

   This routine is useful, e.g., when running a code for purposes
   of accurate performance monitoring, when no I/O should be done
   during the section of code that is being timed.

   Level: intermediate

.keywords: SNES, set, convergence, history

.seealso: SNESGetConvergenceHistory()

@*/
int SNESSetConvergenceHistory(SNES snes, double *a, int *its,int na,PetscTruth reset)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (na) PetscValidScalarPointer(a);
  snes->conv_hist       = a;
  snes->conv_hist_its   = its;
  snes->conv_hist_max   = na;
  snes->conv_hist_reset = reset;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetConvergenceHistory"
/*@C
   SNESGetConvergenceHistory - Gets the array used to hold the convergence history.

   Collective on SNES

   Input Parameter:
.  snes - iterative context obtained from SNESCreate()

   Output Parameters:
.  a   - array to hold history
.  its - integer array holds the number of linear iterations (or
         negative if not converged) for each solve.
-  na  - size of a and its

   Notes:
    The calling sequence for this routine in Fortran is
$   call SNESGetConvergenceHistory(SNES snes, integer na, integer ierr)

   This routine is useful, e.g., when running a code for purposes
   of accurate performance monitoring, when no I/O should be done
   during the section of code that is being timed.

   Level: intermediate

.keywords: SNES, get, convergence, history

.seealso: SNESSetConvergencHistory()

@*/
int SNESGetConvergenceHistory(SNES snes, double **a, int **its,int *na)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (a)   *a   = snes->conv_hist;
  if (its) *its = snes->conv_hist_its;
  if (na) *na   = snes->conv_hist_len;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESScaleStep_Private"
/*
   SNESScaleStep_Private - Scales a step so that its length is less than the
   positive parameter delta.

    Input Parameters:
+   snes - the SNES context
.   y - approximate solution of linear system
.   fnorm - 2-norm of current function
-   delta - trust region size

    Output Parameters:
+   gpnorm - predicted function norm at the new point, assuming local 
    linearization.  The value is zero if the step lies within the trust 
    region, and exceeds zero otherwise.
-   ynorm - 2-norm of the step

    Note:
    For non-trust region methods such as SNES_EQ_LS, the parameter delta 
    is set to be the maximum allowable step size.  

.keywords: SNES, nonlinear, scale, step
*/
int SNESScaleStep_Private(SNES snes,Vec y,double *fnorm,double *delta, 
                          double *gpnorm,double *ynorm)
{
  double norm;
  Scalar cnorm;
  int    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(y,VEC_COOKIE);
  PetscCheckSameComm(snes,y);

  ierr = VecNorm(y,NORM_2, &norm );CHKERRQ(ierr);
  if (norm > *delta) {
     norm = *delta/norm;
     *gpnorm = (1.0 - norm)*(*fnorm);
     cnorm = norm;
     VecScale( &cnorm, y );
     *ynorm = *delta;
  } else {
     *gpnorm = 0.0;
     *ynorm = norm;
  }
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESSolve"
/*@
   SNESSolve - Solves a nonlinear system.  Call SNESSolve after calling 
   SNESCreate() and optional routines of the form SNESSetXXX().

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  x - the solution vector

   Output Parameter:
.  its - number of iterations until termination

   Notes:
   The user should initialize the vector, x, with the initial guess
   for the nonlinear solve prior to calling SNESSolve.  In particular,
   to employ an initial guess of zero, the user should explicitly set
   this vector to zero by calling VecSet().

   Level: beginner

.keywords: SNES, nonlinear, solve

.seealso: SNESCreate(), SNESDestroy()
@*/
int SNESSolve(SNES snes,Vec x,int *its)
{
  int ierr, flg;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidHeaderSpecific(x,VEC_COOKIE);
  PetscCheckSameComm(snes,x);
  PetscValidIntPointer(its);
  if (!snes->setupcalled) {ierr = SNESSetUp(snes,x);CHKERRQ(ierr);}
  else {snes->vec_sol = snes->vec_sol_always = x;}
  if (snes->conv_hist_reset == PETSC_TRUE) snes->conv_hist_len = 0;
  PLogEventBegin(SNES_Solve,snes,0,0,0);
  snes->nfuncs = 0; snes->linear_its = 0; snes->nfailures = 0;
  ierr = (*(snes)->solve)(snes,its);CHKERRQ(ierr);
  PLogEventEnd(SNES_Solve,snes,0,0,0);
  ierr = OptionsHasName(PETSC_NULL,"-snes_view", &flg);CHKERRQ(ierr);
  if (flg) { ierr = SNESView(snes,VIEWER_STDOUT_WORLD);CHKERRQ(ierr); }
  PetscFunctionReturn(0);
}

/* --------- Internal routines for SNES Package --------- */

#undef __FUNC__  
#define __FUNC__ "SNESSetType"
/*@C
   SNESSetType - Sets the method for the nonlinear solver.  

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  method - a known method

   Options Database Key:
.  -snes_type <method> - Sets the method; use -help for a list
   of available methods (for instance, ls or tr)

   Notes:
   See "petsc/include/snes.h" for available methods (for instance)
+    SNES_EQ_LS - Newton's method with line search
     (systems of nonlinear equations)
.    SNES_EQ_TR - Newton's method with trust region
     (systems of nonlinear equations)
.    SNES_UM_TR - Newton's method with trust region 
     (unconstrained minimization)
-    SNES_UM_LS - Newton's method with line search
     (unconstrained minimization)

  Normally, it is best to use the SNESSetFromOptions() command and then
  set the SNES solver type from the options database rather than by using
  this routine.  Using the options database provides the user with
  maximum flexibility in evaluating the many nonlinear solvers.
  The SNESSetType() routine is provided for those situations where it
  is necessary to set the nonlinear solver independently of the command
  line or options database.  This might be the case, for example, when
  the choice of solver changes during the execution of the program,
  and the user's application is taking responsibility for choosing the
  appropriate method.  In other words, this routine is not for beginners.

  Level: intermediate

.keywords: SNES, set, method
@*/
int SNESSetType(SNES snes,SNESType method)
{
  int ierr;
  int (*r)(SNES);

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);

  if (PetscTypeCompare(snes->type_name,method)) PetscFunctionReturn(0);

  if (snes->setupcalled) {
    ierr       = (*(snes)->destroy)(snes);CHKERRQ(ierr);
    snes->data = 0;
  }

  /* Get the function pointers for the iterative method requested */
  if (!SNESRegisterAllCalled) {ierr = SNESRegisterAll(PETSC_NULL);CHKERRQ(ierr);}

  ierr =  FListFind(snes->comm, SNESList, method,(int (**)(void *)) &r );CHKERRQ(ierr);

  if (!r) SETERRQ1(1,1,"Unable to find requested SNES type %s",method);

  if (snes->data) {ierr = PetscFree(snes->data);CHKERRQ(ierr);}
  snes->data = 0;
  ierr = (*r)(snes);CHKERRQ(ierr);

  ierr = PetscObjectChangeTypeName((PetscObject)snes,method);CHKERRQ(ierr);
  snes->set_method_called = 1;

  PetscFunctionReturn(0); 
}


/* --------------------------------------------------------------------- */
#undef __FUNC__  
#define __FUNC__ "SNESRegisterDestroy"
/*@C
   SNESRegisterDestroy - Frees the list of nonlinear solvers that were
   registered by SNESRegister().

   Not Collective

   Level: advanced

.keywords: SNES, nonlinear, register, destroy

.seealso: SNESRegisterAll(), SNESRegisterAll()
@*/
int SNESRegisterDestroy(void)
{
  int ierr;

  PetscFunctionBegin;
  if (SNESList) {
    ierr = FListDestroy( SNESList );CHKERRQ(ierr);
    SNESList = 0;
  }
  SNESRegisterAllCalled = 0;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetType"
/*@C
   SNESGetType - Gets the SNES method type and name (as a string).

   Not Collective

   Input Parameter:
.  snes - nonlinear solver context

   Output Parameter:
.  method - SNES method (a charactor string)

   Level: intermediate

.keywords: SNES, nonlinear, get, method, name
@*/
int SNESGetType(SNES snes, SNESType *method)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *method = snes->type_name;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetSolution"
/*@C
   SNESGetSolution - Returns the vector where the approximate solution is
   stored.

   Not Collective, but Vec is parallel if SNES is parallel

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
.  x - the solution

   Level: advanced

.keywords: SNES, nonlinear, get, solution

.seealso: SNESGetFunction(), SNESGetGradient(), SNESGetSolutionUpdate()
@*/
int SNESGetSolution(SNES snes,Vec *x)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *x = snes->vec_sol_always;
  PetscFunctionReturn(0);
}  

#undef __FUNC__  
#define __FUNC__ "SNESGetSolutionUpdate"
/*@C
   SNESGetSolutionUpdate - Returns the vector where the solution update is
   stored. 

   Not Collective, but Vec is parallel if SNES is parallel

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
.  x - the solution update

   Level: advanced

.keywords: SNES, nonlinear, get, solution, update

.seealso: SNESGetSolution(), SNESGetFunction
@*/
int SNESGetSolutionUpdate(SNES snes,Vec *x)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  *x = snes->vec_sol_update_always;
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetFunction"
/*@C
   SNESGetFunction - Returns the vector where the function is stored.

   Not Collective, but Vec is parallel if SNES is parallel

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
+  r - the function (or PETSC_NULL)
-  ctx - the function context (or PETSC_NULL)

   Notes:
   SNESGetFunction() is valid for SNES_NONLINEAR_EQUATIONS methods only
   Analogous routines for SNES_UNCONSTRAINED_MINIMIZATION methods are
   SNESGetMinimizationFunction() and SNESGetGradient();

   Level: advanced

.keywords: SNES, nonlinear, get, function

.seealso: SNESSetFunction(), SNESGetSolution(), SNESGetMinimizationFunction(),
          SNESGetGradient()

@*/
int SNESGetFunction(SNES snes,Vec *r,void **ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->method_class != SNES_NONLINEAR_EQUATIONS) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_NONLINEAR_EQUATIONS only");
  }
  if (r)   *r = snes->vec_func_always;
  if (ctx) *ctx = snes->funP;
  PetscFunctionReturn(0);
}  

#undef __FUNC__  
#define __FUNC__ "SNESGetGradient"
/*@C
   SNESGetGradient - Returns the vector where the gradient is stored.

   Not Collective, but Vec is parallel if SNES is parallel

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
+  r - the gradient (or PETSC_NULL)
-  ctx - the gradient context (or PETSC_NULL)

   Notes:
   SNESGetGradient() is valid for SNES_UNCONSTRAINED_MINIMIZATION methods 
   only.  An analogous routine for SNES_NONLINEAR_EQUATIONS methods is
   SNESGetFunction().

   Level: advanced

.keywords: SNES, nonlinear, get, gradient

.seealso: SNESGetMinimizationFunction(), SNESGetSolution(), SNESGetFunction(),
          SNESSetGradient(), SNESSetFunction()

@*/
int SNESGetGradient(SNES snes,Vec *r,void **ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  if (r)   *r = snes->vec_func_always;
  if (ctx) *ctx = snes->funP;
  PetscFunctionReturn(0);
}  

#undef __FUNC__  
#define __FUNC__ "SNESGetMinimizationFunction"
/*@C
   SNESGetMinimizationFunction - Returns the scalar function value for 
   unconstrained minimization problems.

   Not Collective

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
+  r - the function (or PETSC_NULL)
-  ctx - the function context (or PETSC_NULL)

   Notes:
   SNESGetMinimizationFunction() is valid for SNES_UNCONSTRAINED_MINIMIZATION
   methods only.  An analogous routine for SNES_NONLINEAR_EQUATIONS methods is
   SNESGetFunction().

   Level: advanced

.keywords: SNES, nonlinear, get, function

.seealso: SNESGetGradient(), SNESGetSolution(), SNESGetFunction(), SNESSetFunction()

@*/
int SNESGetMinimizationFunction(SNES snes,double *r,void **ctx)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  PetscValidScalarPointer(r);
  if (snes->method_class != SNES_UNCONSTRAINED_MINIMIZATION) {
    SETERRQ(PETSC_ERR_ARG_WRONG,0,"For SNES_UNCONSTRAINED_MINIMIZATION only");
  }
  if (r)   *r = snes->fc;
  if (ctx) *ctx = snes->umfunP;
  PetscFunctionReturn(0);
}  

#undef __FUNC__  
#define __FUNC__ "SNESSetOptionsPrefix"
/*@C
   SNESSetOptionsPrefix - Sets the prefix used for searching for all 
   SNES options in the database.

   Collective on SNES

   Input Parameter:
+  snes - the SNES context
-  prefix - the prefix to prepend to all option names

   Notes:
   A hyphen (-) must NOT be given at the beginning of the prefix name.
   The first character of all runtime options is AUTOMATICALLY the hyphen.

   Level: advanced

.keywords: SNES, set, options, prefix, database

.seealso: SNESSetFromOptions()
@*/
int SNESSetOptionsPrefix(SNES snes,char *prefix)
{
  int ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  ierr = PetscObjectSetOptionsPrefix((PetscObject)snes, prefix);CHKERRQ(ierr);
  ierr = SLESSetOptionsPrefix(snes->sles,prefix);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESAppendOptionsPrefix"
/*@C
   SNESAppendOptionsPrefix - Appends to the prefix used for searching for all 
   SNES options in the database.

   Collective on SNES

   Input Parameters:
+  snes - the SNES context
-  prefix - the prefix to prepend to all option names

   Notes:
   A hyphen (-) must NOT be given at the beginning of the prefix name.
   The first character of all runtime options is AUTOMATICALLY the hyphen.

   Level: advanced

.keywords: SNES, append, options, prefix, database

.seealso: SNESGetOptionsPrefix()
@*/
int SNESAppendOptionsPrefix(SNES snes,char *prefix)
{
  int ierr;
  
  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  ierr = PetscObjectAppendOptionsPrefix((PetscObject)snes, prefix);CHKERRQ(ierr);
  ierr = SLESAppendOptionsPrefix(snes->sles,prefix);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESGetOptionsPrefix"
/*@C
   SNESGetOptionsPrefix - Sets the prefix used for searching for all 
   SNES options in the database.

   Not Collective

   Input Parameter:
.  snes - the SNES context

   Output Parameter:
.  prefix - pointer to the prefix string used

   Notes: On the fortran side, the user should pass in a string 'prifix' of
   sufficient length to hold the prefix.

   Level: advanced

.keywords: SNES, get, options, prefix, database

.seealso: SNESAppendOptionsPrefix()
@*/
int SNESGetOptionsPrefix(SNES snes,char **prefix)
{
  int ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);
  ierr = PetscObjectGetOptionsPrefix((PetscObject)snes, prefix);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNC__  
#define __FUNC__ "SNESPrintHelp"
/*@
   SNESPrintHelp - Prints all options for the SNES component.

   Collective on SNES

   Input Parameter:
.  snes - the SNES context

   Options Database Keys:
+  -help - Prints SNES options
-  -h - Prints SNES options

   Level: beginner

.keywords: SNES, nonlinear, help

.seealso: SNESSetFromOptions()
@*/
int SNESPrintHelp(SNES snes)
{
  char                p[64];
  SNES_KSP_EW_ConvCtx *kctx;
  int                 ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(snes,SNES_COOKIE);

  ierr = PetscStrcpy(p,"-");CHKERRQ(ierr);
  if (snes->prefix) PetscStrcat(p, snes->prefix);

  kctx = (SNES_KSP_EW_ConvCtx *)snes->kspconvctx;

  if (!SNESRegisterAllCalled) {ierr = SNESRegisterAll(PETSC_NULL);CHKERRQ(ierr);}
  ierr = (*PetscHelpPrintf)(snes->comm,"SNES options ------------------------------------------------\n");CHKERRQ(ierr);
  ierr = FListPrintTypes(snes->comm,stdout,snes->prefix,"snes_type",SNESList);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_view: view SNES info after each nonlinear solve\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_max_it <its>: max iterations (default %d)\n",p,snes->max_its);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_max_funcs <maxf>: max function evals (default %d)\n",p,snes->max_funcs);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_stol <stol>: successive step tolerance (default %g)\n",p,snes->xtol);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_atol <atol>: absolute tolerance (default %g)\n",p,snes->atol);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_rtol <rtol>: relative tolerance (default %g)\n",p,snes->rtol);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," %ssnes_trtol <trtol>: trust region parameter tolerance (default %g)\n",p,snes->deltatol);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm," SNES Monitoring Options: Choose any of the following\n");CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_cancelmonitors: cancels all monitors hardwired in code\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_monitor: use default SNES convergence monitor, prints\n\
    residual norm at each iteration.\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_smonitor: same as the above, but prints fewer digits of the\n\
    residual norm for small residual norms. This is useful to conceal\n\
    meaningless digits that may be different on different machines.\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_xmonitor [x,y,w,h]: use X graphics convergence monitor\n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_vecmonitor: plots solution at each iteration \n",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_vecmonitor_update: plots update to solution at each iteration \n",p);CHKERRQ(ierr);
  if (snes->type == SNES_NONLINEAR_EQUATIONS) {
    ierr = (*PetscHelpPrintf)(snes->comm,
     " Options for solving systems of nonlinear equations only:\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_fd: use finite differences for Jacobian\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_mf: use matrix-free Jacobian\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_mf_operator:use matrix-free Jacobian and user-provided preconditioning matrix\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_mf_ksp_monitor - if using matrix-free multiply then prints h at each KSP iteration\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_no_convergence_test: Do not test for convergence, always run to SNES max its\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_ksp_ew_conv: use Eisenstat-Walker computation of KSP rtol. Params are:\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_version <version> (1 or 2, default is %d)\n",p,kctx->version);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_rtol0 <rtol0> (0 <= rtol0 < 1, default %g)\n",p,kctx->rtol_0);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_rtolmax <rtolmax> (0 <= rtolmax < 1, default %g)\n",p,kctx->rtol_max);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_gamma <gamma> (0 <= gamma <= 1, default %g)\n",p,kctx->gamma);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_alpha <alpha> (1 < alpha <= 2, default %g)\n",p,kctx->alpha);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_alpha2 <alpha2> (default %g)\n",p,kctx->alpha2);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,
     "     %ssnes_ksp_ew_threshold <threshold> (0 < threshold < 1, default %g)\n",p,kctx->threshold);CHKERRQ(ierr);
  } else if (snes->type == SNES_UNCONSTRAINED_MINIMIZATION) {
    ierr = (*PetscHelpPrintf)(snes->comm," Options for solving unconstrained minimization problems only:\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_fmin <ftol>: minimum function tolerance (default %g)\n",p,snes->fmin);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_fd: use finite differences for Hessian\n",p);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(snes->comm,"   %ssnes_mf: use matrix-free Hessian\n",p);CHKERRQ(ierr);
  }
  ierr = (*PetscHelpPrintf)(snes->comm," Run program with -help %ssnes_type <method> for help on ",p);CHKERRQ(ierr);
  ierr = (*PetscHelpPrintf)(snes->comm,"a particular method\n");CHKERRQ(ierr);
  if (snes->printhelp) {
    ierr = (*snes->printhelp)(snes,p);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*MC
   SNESRegister - Adds a method to the nonlinear solver package.

   Synopsis:
   SNESRegister(char *name_solver,char *path,char *name_create,int (*routine_create)(SNES))

   Not collective

   Input Parameters:
+  name_solver - name of a new user-defined solver
.  path - path (either absolute or relative) the library containing this solver
.  name_create - name of routine to create method context
-  routine_create - routine to create method context

   Notes:
   SNESRegister() may be called multiple times to add several user-defined solvers.

   If dynamic libraries are used, then the fourth input argument (routine_create)
   is ignored.

   Sample usage:
.vb
   SNESRegister("my_solver",/home/username/my_lib/lib/libg/solaris/mylib.a,
                "MySolverCreate",MySolverCreate);
.ve

   Then, your solver can be chosen with the procedural interface via
$     SNESSetType(snes,"my_solver")
   or at runtime via the option
$     -snes_type my_solver

   Level: advanced

   $PETSC_ARCH, $PETSC_DIR, $PETSC_LDIR, and $BOPT occuring in pathname will be replaced with appropriate values.

.keywords: SNES, nonlinear, register

.seealso: SNESRegisterAll(), SNESRegisterDestroy()
M*/

#undef __FUNC__  
#define __FUNC__ "SNESRegister_Private"
int SNESRegister_Private(char *sname,char *path,char *name,int (*function)(SNES))
{
  char fullname[256];
  int  ierr;

  PetscFunctionBegin;
  ierr = PetscStrcpy(fullname,path);CHKERRQ(ierr);
  ierr = PetscStrcat(fullname,":");CHKERRQ(ierr);
  ierr = PetscStrcat(fullname,name);CHKERRQ(ierr);
  ierr = FListAdd_Private(&SNESList,sname,fullname, (int (*)(void*))function);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
