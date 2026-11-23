/*
 * adv.c -- the advanced CAS extension (cas/src/casx.h): number theory,
 * combinatorics and list statistics (ntcmds.c), matrix functions, Jordan
 * form, minimal polynomial and linear recurrences (matcmds.c), radical and
 * algebraic simplification hooked into simplify (radsimp.c); cas/ext/sci
 * (sci.c: vector calculus, PDEs), cas/ext/alg (modular gcd, factorisation, sums,
 * block eigenvalues), cas/ext/applied (numeric linear algebra, transforms, linear programming,
 * logic, splines) and cas/ext/int (the integrator behind integrate, special
 * functions, orthogonal polynomials).
 *
 * On the calculator this is the code module "casx" (modules/casx/casx_mod.c
 * is its modx entry; it runs in place from the QSPI); in the core tier and
 * in every host build it is linked in and registered at the first extension
 * lookup (casx.c).
 */
#include "casx_mod.h"

/* cas/ext/sci, alg and int (vector calculus, PDEs, integrator, ...) are left out of the core tier's flash unless
 * CONFIG_CASX_CORE_SCI (Kconfig): the linker then drops its code */
#if defined(CONFIG_CONTENT_INTERNAL) && !defined(CONFIG_CASX_CORE_SCI)
#define SCI_INIT(X) 0
#define APPLIED_INIT(X) 0
#else
#define APPLIED_INIT(X) casx_applied_init(X)
#define SCI_INIT(X) (casx_sci_init(X) | casx_alg_init(X) | casx_int_register(X))
#endif

int casx_adv_init(XP) {
    if (X->abi != CASX_ABI || X->size < sizeof(casx_api_t)) return -1;
    int r = casx_nt_register(X) | casx_mat_register(X) | casx_rad_register(X) | casx_lap_register(X);
    r |= APPLIED_INIT(X); /* cas/ext/applied, before cas/ext/int: its fourier is the one found */
    return r | SCI_INIT(X);
}
