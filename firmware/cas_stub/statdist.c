/* The stub CAS build uses the real library's distribution functions
 * (cas/include/cas_stat.h): they are plain double-precision C.
 *
 * firmware/Makefile compiles every .c file in cas_stub/ when CAS=stub, so
 * this one-line translation unit pulls cas/src/statdist.c (which depends only
 * on <math.h> and cas_stat.h, not on the CAS context) into the stub build.
 * The statistics app therefore links and produces the same numbers with
 * either CAS. */
#include "../cas/src/statdist.c"
