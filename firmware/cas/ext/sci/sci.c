/*
 * sci.c -- the science part of the advanced CAS extension (casx.h): vector
 * calculus in curvilinear coordinates, potentials, line and surface
 * integrals (veccalc.c); partial differential equations in closed form (pde.c) and by finite differences (pdenum.c).  Part of the "casx" module like cas/ext/adv; its
 * init is called from casx_adv_init.
 *
 * The core tier links cas/ext into flash bank 2; there these commands are
 * left out unless CONFIG_CASX_CORE_SCI is set (Kconfig), and their names
 * report "needs the advanced CAS module" (they are in casx.c's known list).
 */
#include "casx_mod.h"

int casx_sci_init(XP) { return casx_vec_register(X) | casx_pde_register(X) | casx_pdenum_register(X); }
