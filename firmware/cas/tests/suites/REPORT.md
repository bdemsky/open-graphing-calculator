# External CAS test suites: report

`make -C firmware/cas suites` fetches and translates the suites (only when
the translators have changed), then runs Rubi, SymPy, Maxima and Giac
against `build/cas_repl`.  Use `SUITES=maxima` (or another list) to pick a subset.

- Per-suite details, with every WRONG/ERROR/TIMEOUT case: `<suite>/results.md`.
- Full per-case records: `build/suites/<suite>/results.json`.

Sources:
- Rubi: RuleBasedIntegration/MaximaSyntaxTestSuite, cloned into
  `build/suites/src/rubi`.
- Maxima: the `tests/rtest*.mac` files of the local Maxima 5.49 install
  (`MAXIMA_TESTS=` overrides the path).
- SymPy: the 1.14 tests in the project venv.
- Giac: the `check/` directory of the giac 2.1.0 source tarball
  (`giac_stable.tgz`), fetched by `giac/fetch.sh` into `build/suites/src/giac`.
  Only `check/` and `COPYING` are extracted, and nothing in it is run.

## Totals (full runs, all cases)

| suite | cases | PASS | WRONG | ERROR | TIMEOUT | UNEVAL | UNSUPPORTED | TRANSL.-UNCERTAIN | UNGRADED | NA |
|---|---|---|---|---|---|---|---|---|---|---|
| Rubi   | 72,254 | 48,854 (was 45,721) | **0**          | 0  | 0 | 23,240 | 65    | 0   | 1  | 94    |
| SymPy  | 13,545 | 3,394 (was 3,393)   | **0**          | 23 | 0 | 691    | 1,473 | 49  | 24 | 7,891 |
| Maxima | 11,136 | 2,229 (was 1,882)   | **0**          | 17 | 0 | 984    | 2,750 | 221 | 9  | 4,926 |
| Giac   | 13,800 | 10,788 (was 10,742) | **0**          | 2  | 0 | 2,818  | 108   | 2   | 12 | 70    |

Antiderivative forms (`cas/ext/int/tidyrad.c`, `tidytrig.c`; see docs/QSPI_CONTENT.md):
`tests/intsize.py` compares leaf counts with Rubi's optimal antiderivatives. On the 1,560
solved problems of Rubi's independent (textbook) suites the ratio ours/optimal went from
median 1.00, 90th percentile 2.43, mean 1.51 (204 results over 2x) to 1.00 / 1.58 / 1.18 (90
over 2x); 375 results got smaller, 1 larger. Over 4,255 solved problems of the textbook-like
chapters (`--rubi student`): p90 3.00 -> 2.41, mean 1.97 -> 1.64, 871 smaller, 13 larger.
Against SymPy on difftest's integrals: p90 1.12 -> 1.00, 48 smaller, none larger. Full Rubi
run: 0 WRONG, PASS unchanged (two elliptic cases with unchanged outputs were UNGRADED once by
a stuck grader and PASS on a regrade).

CAS round 6 (casx module: `cas/ext/int/intalg.c`, `cas/ext/alg/hyper.c`, `cas/ext/alg/odex.c`).
Rubi, measured by running the round-5 binary and the new one over all 72,254 cases and grading
every case whose output changed (4,200 cases, both sides): +3,133 PASS, 0 lost, 0 WRONG (one
former TIMEOUT is now a quick UNEVAL).

| group | PASS before | PASS after | gained |
|---|---|---|---|
| 0 Independent test suites | 1,560 | 1,566 | +6 |
| 1 Algebraic functions | 18,473 | 20,160 | +1,687 |
| 4 Trig functions | 14,613 | 15,991 | +1,378 |
| 6 Hyperbolic functions | 3,356 | 3,417 | +61 |
| 8 Special functions | 975 | 976 | +1 |

Largest chapter gains: 1.1.1.3 (a+bx)^m (c+dx)^n (e+fx)^p +381, 4.3.0 trig powers with tan
+203, 1.2.1.3 +202, 4.2.4.2 +179, 1.2.2.2 (a+bx^2+cx^4)^p +169, 4.5.4.2 +169, 1.1.2.4 +153,
4.1.0 +144, 1.2.2.3 +143, 4.2.2.1 +139, 1.1.2.3 (a+bx^2)^p (c+dx^2)^q +126.  Of the 8,955
cases whose optimal antiderivative uses an elliptic function, 3,453 passed before and 6,258
after.  What is left there: products with cube roots ((a+bx^2)^(1/3) in Rubi's elliptic
forms), pseudo-elliptic third-kind integrals of cubics, radicals of quadratics in sin/cos with
fractional powers of a cosine, and quartics with two complex pairs.  Wester: 332 PASS (was 330),
0 WRONG (P42 now uses `wronskianmat`, R10 Vandermonde with upper bound r).
Other checks: unit 1823/1823 (tests/unit_round6.inc), difftest 0 wrong (new categories
gosper_sum, summation, ode_more, ode_series, wronskian, integrate_ell), fuzz clean (a negative
allocation in poly_quo_rem fixed), make test ok (all tiers). Flash bank 2: standard 83.6 % ->
83.7 %, core tier 82.2 % -> 82.4 % (bank 1 unchanged); the casx module payload is 169.2 KB (was
140.8 KB).

CAS round 5 (all new code in the casx module, `cas/ext/int`; the core tier leaves it out).
Gains per step, each measured on a full run of all four suites with a per-case diff against
the previous step (no PASS lost, 0 WRONG):

| step | what | Rubi | Maxima | Giac | SymPy |
|---|---|---|---|---|---|
| 1 | translation unlocks: polylog, Bessel (Maxima test_bessel graded numerically), new functions lambertw, fresnels/fresnelc, Shi/Chi, expint, li (sfn.c) with by-parts rules and the lambertw substitution (intsfn.c), Fresnel forms for sin/cos of quadratics | +755 | +192 | +1 | 0 |
| 2 | tidy.c: the final antiderivative in a smaller form (argument normalisation, sin^2 = 1 - cos^2 forms, constants dropped, atan(tan u) = u, distributed constants, trigsimp of Weierstrass results), checked by differentiation; Rubi PASS outputs 10.7 % shorter | 0 | +3 | 0 | 0 |
| 3 | elliptic_f/e/pi (ellip.c, Carlson) and half-integer powers of a + b cos u (sin u, cosh u, sin^2-only via cos 2u) times rational functions of the cosine, with Pi terms for linear denominators (intell.c) | +2,526 | +66 | 0 | 0 |
| 4 | uppergamma and hyp2f1 (hyp.c) and the integrals x^q e^(bx), x^m (a + b x^n)^p, (a + b x)^m (c + d x)^n (inthyp.c); Rubi GAMMA, Maxima gamma_incomplete / hypergeometric translated | +1,721 | +61 | +6 | 0 |
| 5 | polylogarithms (intpl.c): ln(t)^j R(t) by partial fractions, P(x) R(e^(c x)) (also trig and through u = atan c x), R(x) ln(c (a+bx)^n)^k, G(ln x) ln(a + b x^q)/x | +1,170 | +25 | +38 | +1 |
| 6 | Wester gaps: chain rule for unknown functions, Leibniz rule, fourier, 2*2^n = 2^(n+1) in simplify, dense sums in rat_normal / expand | 0 | +1 | +1 | 0 |

Grader changes: the antiderivative check gives a huge antiderivative more digits when it
cancels in the central difference (Rubi 8.6/86: x^101 Gamma(n, a x) against
Gamma(n+101, a x)/a^101); Maxima rtest_taylor/94 is NA (taylor keeps terms up to z^9, our
series(f, z, 0, 10) exponents below 10, so the Puiseux series has one more term). The one
Rubi UNGRADED case is the SymPy grader stuck in C code (watchdog).

Other checks: Wester 330 PASS (was 321), 0 WRONG; unit 1648/1648 (tests/unit_round5.inc);
difftest 0 wrong; stress ok; make test ok (all tiers). Flash bank 2: standard 77.5 %,
advanced 77.5 %, core 77.9 %; the casx module is 122.4 KB (was 84.8 KB).

CAS round 4 (the casx integrator `cas/ext/int`, behind the core's integrate through the slot
`CASX_FN_INTEGRATE`; every antiderivative it returns is checked by differentiation at sample
points). Rubi gains per method, each measured on the full run with no PASS lost:

| method | Rubi PASS gained |
|---|---|
| radicals of one or two linear forms (t = L1^(1/k) L2^(-1/k)), exponentials of atanh / atan, binomial and trinomial denominators with symbolic coefficients | 4,530 |
| Ei / Si / Ci for R(x) F(a + b x), erf / erfi for Gaussians, x^m / ln, symbolic bases f^u | 500 |
| u = asin(c x + d) and the other inverse functions | 1,041 |
| commensurable exponentials, dilogarithms for R(x) ln(a + b x), polylog / Bessel as functions | 65 |
| u = x^n (Chebyshev binomials), three linear forms, u = sin / cos / tan by symmetry | 5,365 |
| u = sin / cos without symmetry (cos w = K sqrt(1 - u^2), K locally constant), u = x^n with symbolic n | 2,812 |

The Giac, SymPy and Maxima gains are mostly integrals too, plus the new commands of
`cas/ext/int/spfn.c` (orthogonal polynomials, digamma, residue, minimize / maximize) and the
block eigenvalues. Two cases were reviewed and overridden: Maxima rtest3/142 (our
antiderivative differs from the reference by a constant, which the case's equivalence check
cannot allow) and SymPy test_residues/test_expressions_failing/1 (an XFAIL test whose asserted
value is the residue at the pole, not at the regular point it names; ours is 0).

CAS round 3 (vector calculus, PDEs, modular gcd and multivariate Hensel factorisation, sums,
products and Stirling limits in the casx module): the Rubi and Giac gains come mostly from the
modular gcd (Rubi rational integrands with symbolic coefficients no longer exhaust memory in the
remainder sequence) and the shortcut A = c f' for logarithmic parts.

The antiderivative grader skips a sample point whose mismatch comes from an
ill-conditioned central difference (F changes by more than 1/1000 of itself over the
step, as e^(e^(2 e^3/x)) near x = 0.15 does); it only ever turns such a mismatch
into "no evidence", never a mismatch into a pass.

No suite has a CRASH.

Other checks:
- Wester (`make wester`): 321 PASS (was 302), 0 WRONG; no PASS lost (Giac 297, SymPy 304).
- `make unit`: 1475 tests, including `tests/unit_round4.inc` (this round's capabilities).
- Every case that passed before this round still passes (per-case diff of the full runs of
  all four suites against the run before the round).
- SymPy difftest: 0 wrong. `make stress`: every case a result or a clean error.
- `make -C firmware test` passes (all tiers).
- `make fw`: banks at 76.2 % and 76.9 % (core tier: 75.2 % and 77.5 %); the casx module is
  84.8 KB (was 59.5 KB).

## Memory limits

`lib/suitelib.py` has a memory guard (`MEMW`):
- A thread polls `ps` every 0.1 s. It watches every registered child: the
  cas_repl processes and the forked SymPy graders.
- A child above 2 GB RSS is killed.
- When the children plus the runner pass 6 GB, the largest child is killed.
- A killed case is recorded as `ERROR(memory)`.
- At most 3 jobs run in parallel (`MAX_JOBS`; `-j` is capped).

The translators are guarded too:
- They call `S.self_memguard()`, which aborts the process above 2 GB.
- The Maxima translator evaluates each expected value in a forked child
  under the guard (`S.isolated`).

Grader children send one result per case. A child that sends nothing for
150 s (`CASE_WALL`) is stuck in C code, where the SIGALRM timeout never
fires (SymPy/mpmath on exp(exp(exp(25))) and similar towers in the Giac
lists). It is killed, the case becomes UNGRADED, and a new child grades the
rest of the batch. Before this, the first Giac grading pass sat for over an
hour on two such cases. A grader child killed by the memory guard also gives
UNGRADED, since it is SymPy and not our CAS that ran out of memory.

This guard catches the earlier 15 GB blow-up. `rtest16/550` makes SymPy
build a huge integer in C code, where the SIGALRM timeout never fires. That
case now has no reference instead of eating the machine.

## Root causes of the 53 WRONG cases

Real CAS bugs, all fixed or made honest. The regression tests are R1-R17 in
`tests/unit_suites.inc`.

| # | where | bug | cases | status |
|---|---|---|---|---|
| R1 | integ.c `try_sub` | the substitution u = x^2 used x = u^(1/2), which holds only for x > 0: int sqrt(x^2) gave x^2/2, and int (u^2)^p u' was wrong too | Rubi 3.5/149, 151 | fixed: a substitution with even n is accepted only if both branches give the same integrand |
| R2 | integ.c `sqrt_pos` | sqrt((a-b)^2) was pulled out as a-b in int 1/sqrt(-x^2+...) | Rubi Apostol/104, 105, 1.3.2/842, 843 | fixed: only bases that are positive when their symbols are (a, a+b) are pulled out |
| R3 | integ.c | the integral over [inf, inf] gave undef | SymPy test_basics/13, 14 | fixed: equal bounds give 0 |
| R4 | integ.c `eval_at`, series.c `try_direct` | Ci(3x) - Ci(x) at 0 was substituted as Ci(0) - Ci(0) = 0 | SymPy issue_14078 (Frullani) | fixed: singular function values force the limit path |
| R5 | integ.c `integrate_def` | a Weierstrass antiderivative with a parameter jumps at pi: int 1/(1+a cos x) over [0, 2 pi] gave 0 | SymPy issue_20370 | honest: results with parameters are checked against quadrature at sample values; a mismatch returns the integral unevaluated |
| R6 | sum.c | sum(0, n, 0, inf) became 0*inf = undef | SymPy Sum_interface/2 | fixed |
| R7 | solve.c | candidates were checked only against the simplified f (x + 1/x - 1/x = x); assumptions were ignored; -inf/ln 2 was accepted | SymPy 8666/1, 2, 4793/6, 12, checking/4, 4463/1, 5197/3, 4, issue 21 | fixed: domain check on the original sides of the equation, an assume() filter, non-finite candidates dropped |
| R8 | solve.c `invert` | 0^x = 100 gave x = 0; (-2)^x = 4 gave [] in real mode | SymPy 11536, solveset_real_exp/1, 4, 6 | fixed: zero bases, and integer exponents of negative bases |
| R9 | solve.c numeric scan | rounding noise (cosh + sinh at -99, tanh*tanh - 1) and tiny complex values ((-99.5)^(-99.5)) were taken as roots | SymPy 4463/2, 6056, solve_hyperbolic | honest: the noise is rejected, and these now give "cannot solve" |
| R10 | solve.c | (1-i-x)^4 was solved numerically (4 inaccurate roots); csolve of a symbolic or complex cubic gave 1 root | SymPy 17454, 14642 | fixed: a perfect-power test, and all 3 Cardano roots in complex mode |
| R11 | series.c | a limit on a branch cut gave the principal value even when approached from the other side: ln(-2+ix), asin(3+ix), atan2(e,-1) for e -> 0- | Maxima limit_extra/6, 7, 8, 41, 56, 61, atan2/41 | honest: the cut crossing is detected; the result is kept only if it matches a numeric check from the requested side, else "could not be determined" |
| R12 | series.c `lead_limit` | ln of a negative infinitesimal or a negative infinity (i pi + ln h) was reported as +-inf | Maxima limit/225, 226, 232, limit_extra/1, 29 | fixed: the result is complex infinity (undef); the real-domain convention is kept for ln(x) at 0 (-inf) |
| R13 | series.c `limit_expr` | a two-sided limit took the right side's value when the left side overflowed (exp(-1/x) sin(1/x)) | Maxima limit/250 | fixed |
| R14 | series.c | a denominator that vanishes where rat_normal cannot see it made 0/0 into 0: 1 - sin(a)^2 - cos(a)^2, or a quadratic at an irrational root | SymPy 6364, 21550 | fixed: a numeric zero test of sums with functions, at sample points |
| R15 | series.c | Ei(u) was expanded with ln u instead of ln abs(u) for u < 0, adding an extra i pi | SymPy 17671 | fixed |
| R16 | series.c `sr_exp` | e^(c) with c = -10 ln h ln(-ln h) was kept as a "constant" coefficient, so exp(A)/exp(B) gave 1 | Maxima gruntz/15 | honest: such series now fail; the case gives an ERROR (the arena runs out in Gruntz) instead of 1 |
| R17 | solve.c systems | a nonlinear system with a parameter returned [] (two circles, biquadratic) | SymPy solve_biquadratic/1 | honest: "cannot solve this system" instead of [] |

Translation and grading problems, fixed in the translators or the overrides:
- Rubi acot: our `acot(x)` is pi/2 - atan(x), but SymPy's is atan(1/x).
  `rubi/translate.py` now grades acot integrands in our convention, so
  5.4.1/157 and 170 now PASS.
- Maxima limit_extra/63 and 64: the limit depends on the integration
  constant of Maxima's own antiderivative (`uncertain` override). The
  override already existed, but the case file was older than the override.
- Maxima limit_extra/2, ln(x) at 0: we use the real-domain convention (-inf);
  Maxima answers complex infinity (`na`).
- SymPy uselogcombine/2: the equation is an identity, so the reference []
  is wrong (`na`).
- Overrides added after the fixes:
  - SymPy exponential_symbols/4: the reference contradicts assume(y>0).
  - SymPy issue_10868/3: ln(x) + acosh(1/x) at oo is complex infinity, but
    SymPy says oo.

Side effects of the honest fixes: some former PASS cases are now UNEVAL or
ERROR.
- R11: 4 SymPy and 2 Maxima limits involving branch cuts, for example
  asinh(1/x) + ln(x) at 0-, abs(ln x)^x at 0-, and z^v ln z with a symbolic
  v.
- R16: 3 Gruntz nested-log limits. One of them runs out of the 24 MB arena,
  like gruntz/15.
- R1: 28 Rubi cases. Their old answers, such as `atanh(a*sqrt(x^2))`, were
  valid only for x > 0. The grader samples only positive x, so it missed
  this.

## Giac check suite

`giac/translate.py` reads two kinds of files in `check/`:
- The `testX` / `X.out` pairs (limit, integrate, cas, normalize, factor,
  partfrac, other), aligned command by command. Indefinite integrals are
  graded by differentiation, everything else against giac's printed result.
- The integration lists `intind*`, `intheb1-4` and `intmit` (13,357 distinct
  integrands, after duplicates are dropped), graded by differentiation.

The first full run gave 72 WRONG and 17 ERROR. All 72 WRONG were
translation problems, and no CAS bug was found:
- 68 integrals: the lists write pi as `Pi`. Our CAS read it as pi, but the
  reference integrand had a free symbol `Pi`. Pi and PI now map to pi.
- 4 `isprime` cases: giac's old check output is 0 / 1 / 2 (2 = proven
  prime). It is now graded as a truth value.
- 2 of the ERRORs, `ichinrem`: giac answers "No Integer Solution Error", and so do
  we (`ERROR 2: no solution`). They are now graded as expected errors.

What is left:
- TRANSLATION-UNCERTAIN (2): `ker` gives [[-2,1]] where giac gives [[2,-1]]
  (and the same for a 3x3 matrix). Both are valid kernel bases.
- ERROR (7), all `ERROR 4: out of memory` in the 24 MB arena:
  - 2 Gruntz limits with deeply nested exp/ln (limit/50, 52).
  - normal(((x+y)^2-(z+1)^2)^8/(x+y+z+1)^16) and the ^10/^20 version.
  - Two products of trivariate polynomials (factor/23, 25).
  - factor((x^202+x^101+1)/(x^2+x+1)).
  These are capacity limits; the answers are honest errors.
- UNGRADED (12): antiderivatives of exponential towers such as
  exp(exp(exp(x+25))). SymPy cannot evaluate them at the sample points.
  All 12 were checked offline: diff(ours) - integrand simplifies to 0 for 11.
  intheb3/2420 matches at 50 digits near x = -0.9, where the tower is finite.
- UNEVAL (3,558), by file: intheb1-4 2,644, intind* 837, intmit 58,
  testlimit 12, testintegrate 7. All but 15 are unevaluated integrals; the
  rest are 13 undetermined limits and 2 numeric results.
- UNSUPPORTED (108): giac-only commands such as jordan, egv/egvl, pcar,
  smod, lin, r2e/e2r, peval, cyclotomic, companion and fdistrib, plus
  commands that use giac-specific assignments.

Known but not fixed: limit(x^(1/ln x), x, 0, 1) gives 1, but the answer is
e. This was found while testing, and none of the suites hits it.

## UNEVAL / UNSUPPORTED by capability

UNEVAL means the command returned unevaluated or said it could not decide.
UNSUPPORTED means a function or feature is missing in our CAS.

- **Rubi**
  - UNEVAL: 31,215, all `integrate`. What is left is mostly results Rubi writes with
    hypergeometric, Appell, elliptic or incomplete-gamma functions (about 15,000 of them),
    trigonometric integrands with radicals of a + b tan or of quadratics in sin / cos, and
    products of inverse functions with logarithms. By group:

    | group | UNEVAL |
    |---|---|
    | 4 Trig | 10,578 |
    | 1 Algebraic | 8,502 |
    | 7 Inverse hyperbolic | 3,323 |
    | 5 Inverse trig | 3,315 |
    | 3 Logarithms | 2,138 |
    | 6 Hyperbolic | 2,089 |
    | 2 Exponentials | 543 |
    | 8 Special | 409 |
    | 0 Independent | 318 |

  - UNSUPPORTED (1,395; polylog and the Bessel functions now exist, so the cases that use them
    can be translated in the next round):

    | function | cases |
    |---|---|
    | ProductLog | 398 |
    | polylog | 224 |
    | incomplete GAMMA | 208 |
    | two-argument Ei | 134 |
    | FresnelS | 109 |
    | FresnelC | 109 |
    | Shi | 68 |
    | Chi | 68 |

    Psi, Li and Zeta account for a few more.
- **SymPy**
  - UNEVAL (764):

    | command | cases |
    |---|---|
    | limit | 178 |
    | integrate | 138 |
    | min/max | 61 |
    | floor/ceil with symbolic arguments | 52 |
    | acosh | 23 |
    | solve | 21 |
    | mod | 20 |
    | sum | 18 |

    sqrt, ln and cot simplification accounts for a few dozen more.
  - UNSUPPORTED (1,507):

    | feature | cases |
    |---|---|
    | undefined functions | 85 |
    | Piecewise (incl. Equality conditions) | 69 |
    | derivatives w.r.t. non-symbols | 58 |
    | collect | 56 |
    | factorint | 51 |
    | series at infinity | 45 |
    | nsimplify | 39 |
    | residue | 34 |
    | adjoint | 29 |
    | AccumBounds | 29 |
    | integer_nthroot | 29 |
    | factor_terms | 28 |
    | multiple integrals | 24 |
- **Maxima**
  - UNEVAL (1,093):

    | command | cases |
    |---|---|
    | integrate | 437 |
    | limit | 133 |
    | floor/ceil/round | 117 |
    | max/min | 91 |
    | abs | 49 |
    | factor | 24 |
    | arg | 22 |
    | laplace | 21 |
    | ratsimp | 18 |
  - UNSUPPORTED (3,486):

    | function | cases |
    |---|---|
    | hgfred | 362 |
    | specint | 283 |
    | Bessel J/I/Y/K | 237 |
    | gamma_incomplete | 216 |
    | hypergeometric | 81 |
    | multi-argument limit | 76 |
    | expintegral_e | 68 |
    | beta_incomplete | 62 |
    | double_factorial | 58 |
    | hstep | 50 |
    | algsys | 41 |
