/*
 * alg.c -- the algebra part of the advanced CAS extension (casx.h): the modular
 * multivariate gcd behind the core's p_gcd (mgcd.c) and the multivariate factorisation
 * behind its factor (mfactor.c).  Part of the "casx" module; its
 * init is called from casx_adv_init and, like cas/ext/sci, it is left out of the core
 * tier's flash unless CONFIG_CASX_CORE_SCI (the core then keeps its remainder sequence).
 */
#include "casx_mod.h"

int casx_alg_init(XP) { return casx_mgcd_register(X) | casx_mfactor_register(X) | casx_sums_register(X) | casx_eig_register(X) |
                             casx_trigsolve_register(X) | casx_odex_register(X); }
