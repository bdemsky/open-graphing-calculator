/*
 * pde.h -- shared by the PDE solvers of the science extension: pde.c
 * (pdesolve: characteristics, separation of variables, d'Alembert) and
 * pdenum.c (pdenum: finite differences; gridval).
 *
 * The unknown is u(x, t) (two variables; for Laplace's equation u(x, y)).
 * Partial derivatives are written diff(u(x,t),x), diff(u(x,t),x,2), ... or
 * with subscripts u_x(x,t), u_xx(x,t), u_t(x,t), u_tt(x,t), u_xt(x,t).
 * Conditions are equations whose left side is u or a first derivative at a
 * point of one variable: u(0,t)=0, u_x(L,t)=0, u(x,0)=f(x), u_t(x,0)=g(x).
 */
#ifndef PDE_H
#define PDE_H
#include "casx_mod.h"

/* derivative codes: u, u_x, u_t, u_xx, u_xt, u_tt */
enum { D_U, D_X, D_T, D_XX, D_XT, D_TT, D_N };
/* equation kinds */
enum { PK_NONE, PK_FIRST, PK_HEAT, PK_WAVE, PK_LAPLACE };

typedef struct {
    E *u;        /* u(x, t) */
    E *v[2];     /* x, t */
    int fid;     /* function id of u */
    E *ph[D_N];  /* placeholder symbols */
    E *co[D_N];  /* sum co[k] D_k = rhs (pde_parse) */
    E *rhs;
    E *param;    /* heat: k (u_t = k u_xx); wave: c^2; laplace: the source rho (u_xx + u_yy = rho) */
} pde_t;

/* one condition: d (D_U, D_X or D_T) of u at v[var] = at equals val */
typedef struct {
    int var, d;
    E *at, *val;
} pcond_t;

#define PDE_MAXC 8

int pde_init(XP, pde_t *p, E *u);
int pde_parse(XP, pde_t *p, E *eq);
int pde_kind(XP, pde_t *p);
int pde_conds(XP, pde_t *p, E *L, pcond_t *c);
E *pde_simp(XP, E *e);

#endif
