/* $Id: petscmath.h,v 1.16 1999/05/27 19:41:20 balay Exp bsmith $ */
/*
   
      PETSc mathematics include file. Defines certain basic mathematical 
    constants and functions for working with single and double precision
    floating point numbers as well as complex and integers.

    This file is included by petsc.h and should not be used directly.

*/

#if !defined(__PETSCMATH_H)
#define __PETSCMATH_H
#include <math.h>

/*

     Defines operations that are different for complex and real numbers;
   note that one cannot really mix the use of complex and real in the same 
   PETSc program. All PETSc objects in one program are built around the object
   Scalar which is either always a double or a complex.

*/
#if defined(PETSC_USE_COMPLEX)

#if defined (PETSC_HAVE_STD_COMPLEX)
#include <complex>
#elif defined(PETSC_HAVE_NONSTANDARD_COMPLEX_H)
#include PETSC_HAVE_NONSTANDARD_COMPLEX_H
#else
#include <complex.h>
#endif

extern  MPI_Datatype        MPIU_COMPLEX;
#define MPIU_SCALAR         MPIU_COMPLEX
#if defined (PETSC_HAVE_STD_COMPLEX)
#define PetscReal(a)        (a).real()
#define PetscImaginary(a)   (a).imag()
#define PetscAbsScalar(a)   std::abs(a)
#define PetscConj(a)        std::conj(a)
#define PetscSqrtScalar(a)  std::sqrt(a)
#define PetscPowScalar(a,b) std::pow(a,b)
#define PetscExpScalar(a)   std::exp(a)
#define PetscSinScalar(a)   std::sin(a)
#define PetscCosScalar(a)   std::cos(a)
#else
#define PetscReal(a)        real(a)
#define PetscImaginary(a)   imag(a)
#define PetscAbsScalar(a)   abs(a)
#define PetscConj(a)        conj(a)
#define PetscSqrtScalar(a)  sqrt(a)
#define PetscPowScalar(a,b) pow(a,b)
#define PetscExpScalar(a)   exp(a)
#define PetscSinScalar(a)   sin(a)
#define PetscCosScalar(a)   cos(a)
#endif
/*
  The new complex class for GNU C++ is based on templates and is not backward
  compatible with all previous complex class libraries.
*/
#if defined(PETSC_HAVE_STD_COMPLEX)
#define Scalar             std::complex<double>
#elif defined(PETSC_HAVE_TEMPLATED_COMPLEX)
#define Scalar             complex<double>
#else
#define Scalar             complex
#endif

/* Compiling for real numbers only */
#else
#define MPIU_SCALAR         MPI_DOUBLE
#define PetscReal(a)        (a)
#define PetscImaginary(a)   (a)
#define PetscAbsScalar(a)   ( ((a)<0.0)   ? -(a) : (a) )
#define Scalar              double
#define PetscConj(a)        (a)
#define PetscSqrtScalar(a)  sqrt(a)
#define PetscPowScalar(a,b) pow(a,b)
#define PetscExpScalar(a)   exp(a)
#define PetscSinScalar(a)   sin(a)
#define PetscCosScalar(a)   cos(a)
#endif

/*
       Allows compiling PETSc so that matrix values are stored in 
   single precision but all other objects still use double
   precision. This does not work for complex numbers in that case
   it remains double

          EXPERIMENTAL! NOT YET COMPLETELY WORKING
*/
#if defined(PETSC_USE_COMPLEX)

#define MatScalar Scalar 
#define MatFloat  double

#elif defined(PETSC_USE_MAT_SINGLE)

#define MatScalar float
#define MatFloat  float

#else

#define MatScalar Scalar
#define MatFloat  double

#endif


/* --------------------------------------------------------------------------*/

/*
   Certain objects may be created using either single
  or double precision.
*/
typedef enum { SCALAR_DOUBLE, SCALAR_SINGLE } ScalarPrecision;

/* PETSC_i is the imaginary number, i */
extern  Scalar            PETSC_i;

#define PetscMin(a,b)      ( ((a)<(b)) ? (a) : (b) )
#define PetscMax(a,b)      ( ((a)<(b)) ? (b) : (a) )
#define PetscAbsInt(a)     ( ((a)<0)   ? -(a) : (a) )
#define PetscAbsDouble(a)  ( ((a)<0)   ? -(a) : (a) )

/* ----------------------------------------------------------------------------*/
/*
     Basic constants
*/
#define PETSC_PI                 3.14159265358979323846264
#define PETSC_DEGREES_TO_RADIANS 0.01745329251994
#define PETSC_MAX                1.e300
#define PETSC_MIN                -1.e300
#define PETSC_MAX_INT            1000000000;
#define PETSC_MIN_INT            -1000000000;

/* ----------------------------------------------------------------------------*/
/*
    PLogDouble variables are used to contain double precision numbers
  that are not used in the numerical computations, but rather in logging,
  timing etc.
*/
typedef double PLogDouble;
/*
      Once PETSc is compiling with a ADIC enhanced version of MPI
   we will create a new MPI_Datatype for the inactive double variables.
*/
#if defined(AD_DERIV_H)
/* extern  MPI_Datatype  MPIU_PLOGDOUBLE; */
#else
#if !defined(USING_MPIUNI)
#define MPIU_PLOGDOUBLE MPI_DOUBLE
#endif
#endif


#endif
