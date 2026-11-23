/*
 * casx_mod.h -- included (first) by the source files of a CAS extension
 * (cas/src/casx.h).  Every function of an extension takes the table as its
 * parameter `const casx_api_t *X`; the core names below then expand to
 * calls through it, so the code reads like core code but never references a
 * core symbol directly (an XIP image cannot: it has no GOT, and the image
 * link fails on any undefined symbol).  Nothing static and writable either:
 * per-call state goes in locals or scratch (salloc).
 */
#ifndef CASX_MOD_H
#define CASX_MOD_H
#include "casx.h"

#define XP const casx_api_t *X /* the parameter every extension function takes */

#undef smark
#define K (*X->ctx)
#define salloc X->salloc
#define cas_throw(st, msg) (X->cas_throw(st, msg), __builtin_unreachable())
#define mk_int X->mk_int
#define mk_frac X->mk_frac
#define mk_sym X->mk_sym
#define mk_sym_name X->mk_sym_name
#define s_list X->s_list
#define s_add X->s_add
#define s_add2 X->s_add2
#define s_sub X->s_sub
#define s_mul X->s_mul
#define s_mul2 X->s_mul2
#define s_div X->s_div
#define s_inv X->s_inv
#define s_neg X->s_neg
#define s_pow X->s_pow
#define s_powi X->s_powi
#define s_sqrt X->s_sqrt
#define s_exp X->s_exp
#define s_fn1 X->s_fn1
#define s_fn2 X->s_fn2
#define num_is_small_int X->num_is_small_int
#define int_mul_si X->int_mul_si
#define int_add X->int_add
#define int_sub X->int_sub
#define int_neg X->int_neg
#define int_isprime X->int_isprime
#define int_factor X->int_factor
#define int_mod X->int_mod
#define num_floor X->num_floor
#define num_sub X->num_sub
#define num_is_zero X->num_is_zero
#define num_inv X->num_inv
#define to_double X->to_double
#define evalf_digits X->evalf_digits
#define bernoulli X->bernoulli
#define is_const_expr X->is_const_expr
#define expr_eq X->expr_eq
#define is_zero X->is_zero
#define freeof X->freeof
#define has_float X->has_float
#define subst X->subst
#define subst_many X->subst_many
#define diff X->diff
#define expand X->expand
#define rat_normal X->rat_normal
#define factor_expr X->factor_expr
#define default_var X->default_var
#define eval_cmd X->eval_cmd
#define poly_degree_in X->poly_degree_in
#define poly_coeff X->poly_coeff
#define up_from X->up_from
#define is_matrix X->is_matrix
#define mat_mul_lists X->mat_mul_lists
#define mat_identity X->mat_identity
#define mat_rref X->mat_rref
#define mat_kernel X->mat_kernel
#define mat_charpoly X->mat_charpoly
#define mat_fun X->mat_fun
#define mat_rpow X->mat_rpow
#define roots_mult X->roots_mult
#define lin_fit X->lin_fit
#define stack_check X->stack_check
#define mk_fn X->mk_fn
#define mk_flt X->mk_flt
#define sym_name X->sym_name
#define try_cmd X->try_cmd
#define p_mk X->p_mk
#define p_norm X->p_norm
#define p_const X->p_const
#define p_divexact X->p_divexact
#define p_lnum X->p_lnum
#define int_mul X->int_mul
#define int_gcd X->int_gcd
#define num_cmp X->num_cmp
#define num_sgn X->num_sgn
#define p_var X->p_var
#define p_content X->p_content
#define p_eval X->p_eval
#define p_factor X->p_factor
#define up_mul X->up_mul
#define up_add X->up_add
#define up_divrem X->up_divrem
#define up_egcd X->up_egcd
#define up_scale X->up_scale
#define p_intprim X->p_intprim

/* cas_int.h poll_intr, through the table */
#define poll_intr() casx_poll(X)
static inline void casx_poll(XP) {
    if (K->poll) X->cas_poll_step();
    if (K->intr) cas_throw(CAS_ERR_INTERRUPTED, "interrupted");
}

/* the extensions linked or built here: init registers the commands */
int casx_nt_register(XP);  /* ntcmds.c */
int casx_mat_register(XP); /* matcmds.c */
int casx_lap_register(XP); /* lapl.c */
int casx_sci_init(XP);     /* cas/ext/sci/sci.c */
int casx_vec_register(XP); /* cas/ext/sci/veccalc.c */
int casx_pde_register(XP); /* cas/ext/sci/pde.c */
int casx_pdenum_register(XP); /* cas/ext/sci/pdenum.c */
int casx_alg_init(XP);       /* cas/ext/alg/alg.c */
int casx_mgcd_register(XP);  /* cas/ext/alg/mgcd.c */
int casx_mfactor_register(XP); /* cas/ext/alg/mfactor.c */
int casx_sums_register(XP);  /* cas/ext/alg/sums.c */
int casx_eig_register(XP);   /* cas/ext/alg/eigblk.c */
int casx_trigsolve_register(XP); /* cas/ext/alg/trigsolve.c */
int casx_odex_register(XP);  /* cas/ext/alg/odex.c */
int casx_int_register(XP);   /* cas/ext/int/intx.c */
int casx_applied_init(XP);   /* cas/ext/applied/applied.c */

#endif
