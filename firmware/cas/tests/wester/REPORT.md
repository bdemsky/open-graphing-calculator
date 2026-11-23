# Wester benchmark: results for the calculator CAS

Michael Wester, "A Critique of the Mathematical Abilities of CA Systems"
(1999), run against `firmware/cas` (`build/cas_repl`, default 24 MiB arena,
10 s per line). The first run found 18 WRONG answers; all of them have since
been fixed or turned into an honest error or unevaluated result (see "Fixed
WRONG results" below, regression tests in `tests/unit_wester.inc`).

- Problems: `tests/wester/problems.txt`, 497 entries. Provenance: Wester's own
  Macsyma transcripts (`math.unm.edu/~wester/demos/<Section>/Macsyma.problems`)
  are the primary source. They were cross-checked against SymPy's
  `test_wester.py` (SymPy 1.14.0). The header of `problems.txt` has the
  details and the corrections made to the sources.
- Runner: `tests/wester/run_wester.py`. Run it with `make -C firmware/cas wester`
  (about 7 s), or with `make -C firmware/cas wester-asan` for the
  AddressSanitizer + UBSan build. Each problem runs in a fresh process. Answers
  are graded for equivalence with SymPy: numeric spot checks at random points
  in a per-problem domain, falling back to `simplify(diff) == 0`. String
  matching is never used for grading. The `simp`, `form`, `factor` and `rect`
  checks also require the requested form. Solution sets are compared without
  regard to order. ODE answers are substituted back into the equation, and
  implicit solutions are checked by implicit differentiation. Linear and
  nonlinear systems (M35-M37) are substituted back into the system.
- Raw results: `results.md` and `results.json`, plus `results_asan.*` for the
  sanitizer build.

## Totals

479 problems are scored. The 18 NA problems are out of scope for a calculator:
quantifier elimination, Fortran/C code generation, noncommutative algebra,
differential forms and tensors.

| category | count | share of scored |
|---|---:|---:|
| PASS | 190 | 39.7 % |
| **WRONG** | **0** | 0.0 % |
| UNEVAL (unevaluated, numeric-only, not simplified, or "cannot solve") | 144 | 30.1 % |
| UNSUPPORTED (no such function or feature) | 136 | 28.4 % |
| ERROR (all are `ERROR 4: out of memory`) | 9 | 1.9 % |
| TIMEOUT | 0 | |
| CRASH | 0 | |
| NA | 18 | (excluded) |

| section | PASS | WRONG | UNEVAL | UNSUPP | ERROR | NA | total |
|---|---:|---:|---:|---:|---:|---:|---:|
| A Boolean logic | 1 | 0 | 4 | 3 | 0 | 5 | 13 |
| B Set theory | 0 | 0 | 0 | 4 | 0 | 0 | 4 |
| C Numbers | 16 | 0 | 6 | 1 | 0 | 1 | 24 |
| D Numerical analysis | 5 | 0 | 0 | 3 | 0 | 6 | 14 |
| E Statistics | 8 | 0 | 0 | 7 | 0 | 0 | 15 |
| F Combinatorics | 2 | 0 | 2 | 4 | 0 | 0 | 8 |
| G Number theory | 2 | 0 | 0 | 18 | 0 | 0 | 20 |
| H Algebra | 16 | 0 | 7 | 3 | 6 | 2 | 34 |
| I Trigonometry | 6 | 0 | 6 | 2 | 0 | 0 | 14 |
| J Special functions | 1 | 0 | 3 | 15 | 0 | 0 | 19 |
| K Complex domain | 10 | 0 | 9 | 5 | 0 | 0 | 24 |
| L Zero equivalence | 4 | 0 | 5 | 0 | 0 | 0 | 9 |
| M Equations | 25 | 0 | 12 | 1 | 1 | 0 | 39 |
| N Inequalities | 6 | 0 | 6 | 6 | 0 | 0 | 18 |
| O Vector analysis | 3 | 0 | 0 | 7 | 0 | 0 | 10 |
| P Matrix theory | 21 | 0 | 6 | 17 | 2 | 0 | 46 |
| Q Tensors | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 3 | 0 | 16 | 5 | 0 | 0 | 24 |
| S Products | 3 | 0 | 7 | 0 | 0 | 0 | 10 |
| T Limits | 8 | 0 | 8 | 0 | 0 | 0 | 16 |
| U Calculus | 5 | 0 | 5 | 7 | 0 | 2 | 19 |
| V Indefinite integrals | 11 | 0 | 6 | 1 | 0 | 0 | 18 |
| W Definite integrals | 8 | 0 | 20 | 6 | 0 | 0 | 34 |
| X Series | 17 | 0 | 4 | 4 | 0 | 1 | 26 |
| Y Transforms | 2 | 0 | 6 | 6 | 0 | 0 | 14 |
| Z Difference/differential eqs | 7 | 0 | 6 | 11 | 0 | 0 | 24 |

**Sanitizer build.** `make wester-asan` builds `build/cas_repl_asan` with
`-fsanitize=address,undefined -fno-sanitize-recover=all` and gives exactly the
same categories and outputs on all 497 problems: no ASan or UBSan report and
no crash. A second sanitizer run with a 512 MiB arena and a 120 s timeout,
which reaches deeper into the expensive cases, was also clean.

**Arena size.** With `--mem 512` (and a 120 s timeout), H8, H27, H28 and M27
also pass (194 PASS, 0 WRONG). H9, H10, H11, P22 and P26 still run out of
memory at 512 MiB.

## Fixed WRONG results

There were no crashes in either build. The first run had the 18 WRONG answers
below (plus P25 at 512 MiB), each checked by hand. All are fixed now; the last
column says how. "cannot solve" is `ERROR 6`, graded UNEVAL.

| id | now | fix |
|---|---|---|
| W1 | `ERROR 2: integral diverges (pole inside the interval)` | integ.c `sym_pole_check`: zeros of the denominator between symbolic bounds (s - a, b - s positive constants) are tested like numeric poles. The principal value is not computed (UNEVAL via an `alt:` line) |
| W2 | same error | same (PASS) |
| K13 | `-exp(3*i)` | simp.c: (e^z)^p = e^(p z) only for z free of i; a constant z is shifted into -pi < Im z <= pi by 2 pi i k, otherwise unevaluated |
| M9 | `ERROR 6: cannot solve` | solve.c `g_incomplete`: an empty result after a numeric scan or a failed inversion is "cannot solve", not `[]` (no Lambert W) |
| M10 | `[-1,1]` | solve.c: x^a = x^b reduces to x^(a-b) = 1 (or x = 0 if a, b > 0 there); b^u = 1 gives b = 1, u = 0, or b = -1 with u even |
| M22 | both roots | numeric.c `cpow_`: small integer powers by squaring, so (iy)^2 stays real and the verification's sqrt stays on the right side of its cut |
| M24 | `[(ln(a)-ln(c))/(ln(d)-ln(b))]` | solve.c `ln_monomial`: two terms c b^u = d e^v with x only in exponents are solved by taking logs |
| M33 | `[2+3*i]` | eval.c: csolve with conj/re/im of the unknown solves re = 0, im = 0 for z = _u + i _v |
| N15 | `ERROR 6: cannot solve` | solve.c `solve_ineq`: a sign chart is refused when f has trig in x and zeros (periodic families) or when a critical point came from a numeric scan |
| P17 | `1` | linalg.c `pick_pivot`: a trig pivot candidate that simplify_full reduces to 0 is a zero |
| P24 | 7 values | solve.c verification also accepts |f(s)| <= 1e-11 times the sum of the absolute terms (rounding level of an expanded charpoly) |
| P25 | 7 floats | cmds.c eigenvals: a float matrix uses the exact charpoly of exact(A), roots evaluated afterwards (PASS at 24 MiB too) |
| X24 | `2*((-1)^n-1)*p/(n^2*pi^2)` | integ.c `sign_jumps`: F(b) - F(a) gets F(c-) - F(c+) for each zero c of a sign() in F inside the interval; unevaluated if c cannot be placed |
| U4 | `n!` | cmds.c: diff(f, x, k) with k assumed integer is k-th order: x^k gives k!, anything else stays unevaluated |
| H12 | `x^n` | cmds.c `symexp_gcd`: x^(c+s) -> x^c T, both sides T^p A0(x); unevaluated when that does not decide the gcd |
| J16 | `subst(diff(zeta(x),x),x,0)` | simp.c `subst_rec`: the variable of an unevaluated diff (or antiderivative) is not replaced by a non-symbol |
| U3b | `subst(diff(piecewise(...),x),x,1)` | same |
| D2b | `3.29683147808856e-434295` | numeric.c evalf: an under/overflowing constant is redone with bigfloats; bf.c `bigflt_parts` scales huge binary exponents by a power of ten before printing |
| D2 | `3.29683147809e-434295` | simp.c: e^float beyond the double range goes through bf_evalf |

The original WRONG list, for reference:

| id | input | our output | expected | what is wrong |
|---|---|---|---|---|
| W1 | `integrate(1/(x-a),x,a-1,a+1)` | `-i*pi` | 0 (principal value), or divergent | A real integrand gives an imaginary result. The pole at x = a inside the interval is not detected. |
| W2 | `integrate(1/(x-a)^2,x,a-1,a+1)` | `-2` | divergent | A positive integrand gives a negative value: the antiderivative was evaluated across the pole. The help text promises an error here. |
| K13 | `sqrt(exp(6*i))` | `exp(3*i)` | `-exp(3*i)` | `sqrt(exp(z))` is rewritten to `exp(z/2)` without the principal-branch check (Im 6 > pi). |
| M9 | `csolve(exp(x)=x,x)` | `[]` | `[-LambertW(-1)]` | Claims there is no complex solution. |
| M10 | `solve(x^x=x,x)` | `[1.0]` | `[-1,1]` | Misses x = -1, and the 1 is numeric. |
| M22 | `csolve(x=1/sqrt(1+x^2),x)` | `[sqrt(2*sqrt(5)-2)/2]` | also `-i*sqrt((sqrt(5)+1)/2)` | csolve drops the complex root (verified by substitution). |
| M24 | `solve(a*b^x=c*d^x,x)` | `[]` | `[ln(c/a)/ln(b/d)]` | Claims there is no solution. |
| M33 | `csolve((1+i)*z+(2-i)*conj(z)=-3*i,z)` | `[-i]` | `[2+3*i]` | `conj(z)` is treated as `z` (symbols are real). The answer does not satisfy the equation. |
| N15 | `solve(sin(x)<1,x)` | `[x<pi/2,x>pi/2]` | x != pi/2 + 2 n pi | The periodic exclusions are lost, so x = 5 pi/2 is accepted. |
| P17 | `rank([[sin(2*t),cos(2*t)],[2*(1-cos(t)^2)*cos(t),(1-2*sin(t)^2)*sin(t)]])` | `2` | `1` | Row 2 is sin(t) times row 1. Zero recognition in the elimination fails. |
| P24 | `eigenvals(<Rosser 8x8>)` | `[-10*sqrt(10405),0,510-100*sqrt(26),1000]` | 7 distinct values | `510+100*sqrt(26)`, `1020` and `10*sqrt(10405)` are dropped silently, even though the conjugate root of each quadratic is returned. |
| X24 | `fourier_an(abs(x),x,2*p,n,-p)` | `2*(-1)^n*p/(n^2*pi^2)` | `2*p*((-1)^n-1)/(pi^2*n^2)` | The `-2p/(n^2 pi^2)` term is missing. For p = pi, n = 1 the result is -0.637 instead of -1.273. |
| U4 | `assume(n,integer)`, then `diff(x^n,x,n)` | `ln(x)*n*x^(n-1)+x^(n-1)` | `n!` | A symbolic differentiation order is accepted and produces nonsense. |
| H12 | `gcd(2*x^(n+4)-x^(n+2),4*x^(n+1)+3*x^n)` | `1` | `x^n` | gcd of expressions with symbolic exponents returns 1 instead of factoring out x^n or refusing. |
| J16 | `subst(diff(zeta(x),x),x,0)` | `diff(-1/2,0)` | `-ln(2*pi)/2` | zeta' is not known, and `subst` then also replaces the differentiation variable of the unevaluated `diff`, which is meaningless. |
| U3b | `f(x):=piecewise(x=1,x^2-1,x^3)`, then `subst(diff(f(x),x),x,1)` | `diff(0,1)` | `3` | The same `subst`-into-`diff` bug, combined with diff not handling piecewise. |
| D2b | `approx(exp(-1000000),15)` | `0.0` | `3.29683147808856e-434295` | The arbitrary-precision path underflows: the bigfloat result is routed through a double. |
| D2 | `exp(-1000000.0)` | `0.0` | `3.29683e-434295` | Double underflow, as Wester expects of most systems. Low severity. |

Only with a larger arena (`--mem 512`), before the fix:

| id | input | our output | expected | what is wrong |
|---|---|---|---|---|
| P25 | `eigenvals(approx(<Rosser>))` | `[undef+undef*i, ...]` (24 entries) | 7 floats | Garbage output. At the default 24 MiB it is `ERROR 4: out of memory`. |

Answers where Wester's or SymPy's expected value was wrong, and our CAS was
right, were fixed in `problems.txt` (with a note on each):

- E8: the normal-distribution probability is 0.5881860; Wester has 0.5867.
- M27: there is a fourth root at 29.4545.
- M37: the system has 16 solutions (the lex Groebner basis has a degree-8
  factor in z). SymPy lists 6.
- M26: the +- answer is accepted.

## Gaps by capability

Effort is a rough estimate for one engineer who knows the code base: S is
about a day, M a few days, L a week or more. Value is for calculator users
(students and engineers) and reflects how often the gap is hit and how
visibly it fails.

| # | capability gap | problems | effort | value |
|---|---|---|---|---|
| 1 | **Definite integrals across singularities**: detect poles and branch points in [a, b]; report divergence; principal value optional | W1, W2 (were WRONG, fixed), W9 | S-M | very high |
| 2 | **Solver completeness and honesty**: report "cannot solve" instead of `[]` when no method applies; handle `conj`; keep complex roots in csolve; `a*b^x=c*d^x` by taking logs; x^x = x by cases; Lambert W; solving for f(x) | M9, M10, M22, M24, M33 (were WRONG, fixed), M34 | M | very high |
| 3 | **Exact roots instead of floats**: cyclotomic and binomial polynomials, biquadratic and quartic radicals; rational roots after the piecewise split of abs/max (M29-M31 return `[-7.0,-1.0]`); recover exact roots by rationalizing numeric roots and verifying them exactly; `rootof` for the rest | M3-M6, M8, M16, M17, M25, M29-M32 | M | high |
| 4 | **Branch-correct simplification and use of the assumptions that already exist**: sqrt(exp z) (K13, fixed); integer n in (-1)^(n(n+1)), cos(n pi), sin(k pi), (xy)^n; z < 0 in sqrt(1/z); y >= 0 in sqrt(x y abs(z)^2); atan(tan(10)) and ln(exp(10 i)) reductions | K13, H3, I3-I5, K7, K10, K16, K19, K22, K23 | M | high |
| 5 | **Assumption system** with intervals and relations (-3 pi < x < -5 pi/2, x > y, abs(x) < 1), a sign/`is` decision for polynomial inequalities (x^4 - x + 1 > 0), and parameters passed through to limits and integrals | I6, K21, N2-N9, T9, T10, W7, W8, W12 | L | high |
| 6 | **Linear-algebra robustness**: zero recognition in rank (P17, fixed); eigenvals must not drop roots (P24, fixed); numeric eigenvalues by QR/Hessenberg rather than the characteristic polynomial (P25 now uses the exact charpoly of exact(A)); diagonal and sparse fast paths ((2-a)*identity(100) runs out of memory) | P17, P22, P24, P25, P26 | M | high |
| 7 | **Sparse multivariate gcd and factorization**: the dense recursive representation runs out of memory on Liska's 3-variable degree-22 polynomials even at 512 MiB; resultant reports "degree too large"; multivariate factor stops early (P19 leaves (x-w)(x-y)(x-z) as one cubic factor) | H7, H9, H10, H11, P19, plus H8, H27, H28 at 24 MiB | L | medium-high (robustness on textbook-size inputs) |
| 8 | **Indefinite integration**: rational functions with irreducible denominators (`1/(x^3+2)`; Lazard-Rioboo-Trager or a binomial/Cardano special case); a^x substitutions (`2^x/sqrt(1+4^x)`); `ln(abs(...))`; piecewise, abs and Heaviside integrands | V3-V6, V15 | M | high (V5 is a calculus-course staple) |
| 9 | **Exact definite integrals** instead of the numeric fallback: split at zeros of abs and sqrt arguments (W3-W6, W32), algebraic integrands (W11), parametric Gaussian integrals (W12), Euler-gamma types (W13, W15), sinc/Fourier types (W14), piecewise, Heaviside and parametric iterated integrals (W22-W24, W26, W28, W31), Mellin types (Y11) | W3-W8, W11-W15, W22-W24, W26, W28, W31, W32, Y11 | L | medium-high |
| 10 | **Symbolic summation**: telescoping; the binomial-theorem family (k^2 C(n,k), C(n,k)/(k+1), Vandermonde, C(2n,k)^2); trig sums (sin kx); polylog and zeta values (1/(2^k k^2)); partial fractions of rational terms at infinity (R19, R21); Gosper/Zeilberger later | R3, R5, R6, R8-R10, R13-R15, R18-R24 | M (patterns) to L (Gosper) | medium |
| 11 | **Symbolic products**: gamma multiplication/reflection (S1), (2k-1)/(2k) as a binomial, infinite products via gamma (S7-S10), trig products (S6) | S1, S5-S10, F4, F5 | M | low-medium |
| 12 | **Limits beyond series**: gamma/factorial asymptotics (Stirling), Gruntz-type nested exponentials (T5), zeta near 1, products and parameters; all of these currently give "limit could not be determined" | T5, T7-T13 | M-L | medium |
| 13 | **Special functions**: Bessel J (values, derivatives, half-integer closed forms, numeric), Chebyshev/Legendre/associated Legendre polynomials, digamma/polygamma, 2F1 with closed forms, elliptic integrals, polylog | J2, J3, J5-J15, W16-W21, Y12, Z15 | S (polynomial families) to M (Bessel) | medium |
| 14 | **Integer and combinatorial functions**: double factorial (`n!!` currently parses as `(n!)!`), pochhammer, Bernoulli, Fibonacci, Stirling numbers, partitions, primitive root, continued fractions, best rational approximation | C3, F1, F3, F6, F7, J1, R11, R12, G2, G15-G20 | S | medium (cheap wins) |
| 15 | **Modular arithmetic**: linear and polynomial congruences, systems mod n, factorization over GF(p) (the Zassenhaus code already factors mod p internally), Diophantine equations | G3-G7, G9-G14, H24, H25, H30 | S (GF(p) factor, congruences) to M (Diophantine) | medium |
| 16 | **Matrix functions and structure**: expm (also the basis for linear ODE systems), sinm, and sqrtm (`M^(1/2)` stays `sqrt([[...]])`); Jordan form, minimal polynomial, Smith form, SVD, Gram-Schmidt, wronskian; `norm(M,inf)`; elementwise `mod`; diag/minor/submatrix/block extraction; list indexing (`L[2]` multiplies) | P1-P5, P8, P20, P28-P39, P42, O10 | M | medium |
| 17 | **ODEs**: linear systems (via expm or eigenvectors), Heaviside forcing (Y5), autonomous second-order reduction (Z11, Z12), Bessel-type (Z15), BVP eigenvalue conditions (Z14 returns only 0), recurrences (`rsolve`) | Z1-Z4, Z11, Z12, Z14, Z15, Z17, Z19-Z24, Y5 | M-L | medium-high (rsolve S-M; systems M once expm exists) |
| 18 | **Transforms**: Laplace table gaps (sinh*cosh, erf(c/sqrt t)); ilaplace with repeated quadratic factors (`2/(s^2+4)^2`, the textbook resonance case) and exp(-a s) shifts; Fourier and Z transforms | Y3, Y4, Y6, Y7, Z6, Y8-Y10, Y13, Y14 | S-M | medium (Z6 and Y6 are common coursework) |
| 19 | **Calculus with unknown functions**: chain rule for f(g(t)); Leibniz rule for variable integration limits; g'(x^2+y^2); integrals against dirac'; diff of piecewise. `subst` into `diff` must not touch the differentiation variable (J16, U3b, fixed), and a symbolic order must be rejected (U4, fixed) | U2, U4-U6, U9, J16, J17, J17b, U3b, V18, X7 | M | medium |
| 20 | **Radicals and algebraic simplification**: denesting with several radicals (C16); cube and fifth roots of a + b sqrt c (C19-C21); products of conjugate radicals (C18); logs of algebraic numbers (C22); algebraic cancellation (H21); half-angle values (I1b, I2); the exp(x/2) substitution (H15); zero equivalence (L5-L9, K24) | C16, C18-C22, H15, H21, I1b, I2, L5-L9, K24 | M | medium |
| 21 | **Series**: asymptotic expansions of binomial/gamma and Ei at infinity (X16, X17); non-integer powers at the expansion point (X14); multivariate expansion (X18); formal power series and series reversion | X14, X16-X21 | M | low-medium |
| 22 | **Inequalities with parameters and systems**: symbolic critical points (N16, N17), 2-variable systems (N18), periodic solution sets (N15 now "cannot solve") | N15-N18 | M | medium |
| 23 | **Statistics in the CAS**: quartiles, mode, sample (n-1) standard deviation, linear and multiple regression. These exist in the Stat app; expose them as CAS commands | E3-E5, E12-E15 | S | medium |
| 24 | **Logic and sets**: boolean simplification (`x or not(x)`, absorption, `x and false`), logical xor/implies/iff, union/intersection/difference/product of lists | A2-A7, A12, B1-B4 | S | low |
| 25 | **Complex declarations** (`assume(z,complex)`): symbols are always real, so Wester's "for complex z" variants cannot be posed; no rectangular-form command (K4, K5) | K1, K4, K5, K11, K14, K17, V2 | M | low-medium |
| 26 | **Misc**: bigfloat underflow (D2, D2b, fixed), Horner form, splines, interval arithmetic, optimization (min/max, LP), residue, scalar/vector potentials, curvilinear coordinates | D2, D2b, D5, D8, D12, U10, U13-U17, O3, O5-O9 | S each | low-medium |

## Recommended fix order

1. **Silent wrong answers** come first, because a calculator that is
   confidently wrong is worse than one that says "cannot" (done: all of the
   following now give a right answer, unevaluated, or an error):
   - pole detection in definite integrals (W1, W2)
   - "cannot solve" instead of `[]` (M9, M24)
   - complex roots and conj in csolve (M22, M33)
   - the principal-branch check for sqrt(exp(z)) (K13)
   - eigenvals dropping roots (P24)
   - zero recognition in rank (P17)
   - `fourier_an(abs(x))` (X24)
   - `subst` into the variable of an unevaluated `diff` (J16, U3b)
   - rejecting a symbolic diff order (U4)
   - gcd with symbolic exponents (H12)
   - periodic inequality sets (N15)
   - bigfloat underflow (D2b)

   Each item is S, except P24 and M22/M33, which are S-M.
2. **Exact roots instead of floats** (gap 3), M. This is high value: students
   see `[-1.0,2.0]` for problems with integer answers.
3. **Use the existing assumptions in simplification** (gap 4), M.
4. **Numeric eigenvalues by QR and a diagonal fast path** (gap 6), M. This
   also removes out-of-memory errors.
5. **Integration of rational functions with irreducible denominators**,
   abs/piecewise/Heaviside, and a^x substitution (gap 8), M.
6. **Laplace/ilaplace gaps**: repeated quadratics, exp shifts, table entries
   (gap 18), S-M.
7. **Cheap breadth**: integer/combinatorial functions, continued fractions,
   GF(p) factoring and congruences, and statistics commands (gaps 14, 15, 23).
   Each is S, and together they cover about 35 problems.
8. **Symbolic sums**: telescoping and the binomial-theorem patterns first,
   then Gosper (gap 10).
9. **expm, followed by linear ODE systems and rsolve** (gaps 16, 17).
10. **Exact definite integrals** by splitting at abs/sqrt zeros, plus
    parametric Gaussian integrals (gap 9).
11. **Interval and relational assumptions** (gap 5), L. This unlocks N4-N9,
    I6, K21 and parametric limits and integrals.
12. **Sparse multivariate gcd and factorization** (gap 7), L.
13. **Limits with gamma asymptotics** (gap 12), and **special functions**
    (gap 13): Chebyshev/Legendre first (S), then Bessel (M).
14. The rest: radicals, series extras, logic/sets, complex declarations and
    misc (gaps 20-26).

## Notes on grading

- UNEVAL covers four cases: echoed or unevaluated commands; `ERROR 6`
  ("valid input the engine cannot handle yet"); floats where Wester asks for
  an exact result (W3-W6, W11, W13, W15 and W32 come from quadrature, and W3
  gives 1.33333332244 for 4/3); and equivalent but unsimplified output on
  simplification problems.
- UNSUPPORTED covers three cases: an unknown function name is echoed (for
  example `besselj(2.0,1.0+i)`); a command rejects the form, as in "dsolve:
  expected names y, x" or "assume: only comparisons with 0 are supported"; or
  `problems.txt` records that no input is possible (`unsupported:`).
- Names that collide with units: `psi` is pounds per square inch and `J` is
  joules, so `J(p,q):=...` is rejected. J5 therefore uses `digamma`.
- Outputs the grader cannot decide automatically, such as unknown-function
  calculus and divergence reported as `undef`, have an `alt:` line in
  `problems.txt`. It pins the exact current output to a category with a
  reason. If the output changes, the problem is graded automatically again.
- `--mem MB` sets the cas_repl arena size, `--asan` uses the sanitizer build,
  and `--only H,M37` selects sections or problem ids.
