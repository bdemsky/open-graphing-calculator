# CAS engine (firmware/cas)

A computer algebra system written from scratch in portable C11 for the
graphing calculator (STM32H743, 32 MB SDRAM).  No code from Giac, Eigenmath,
SymPy, Maxima or other CAS projects was used; algorithms come from the
standard literature (Knuth vol. 2, von zur Gathen & Gerhard "Modern Computer
Algebra", Geddes/Czapor/Labahn, Bronstein "Symbolic Integration I", Cohen
"Computer Algebra and Symbolic Computation").

* Public interface: `include/cas.h` (unchanged signatures; added
  `cas_latex()`, `cas_set_stack_limit()`, `cas_eval_quiet()` (evaluate without
  changing `ans`), and session persistence: `cas_save()` / `cas_restore()`
  write and read variables, user functions, assumptions and `ans` as text
  records (`src/session.c`; values are restored without variable
  substitution, independent of angle/approx mode), `cas_vars()` lists user
  names).
* No `malloc`, no stdio, no OS calls in the library.  All memory comes from
  the arena passed to `cas_init`.
* Builds for the host (Apple clang) and for Cortex-M7
  (`arm-none-eabi-gcc -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -O2 -ffreestanding`),
  warning-free with `-Wall -Wextra`.  ARM code size ≈ 211 KB (`make arm-check`).

## Build targets

| target | what it does |
|---|---|
| `make repl` | host library + `build/cas_repl` command-line front end |
| `make unit` | C white-box unit tests (bignum, rationals, polys, memory, API) |
| `make fuzz` | parser/engine fuzzer built with AddressSanitizer + UBSan |
| `make difftest` | differential test against SymPy (`tests/difftest.py`) |
| `make test` | unit + fuzz + difftest |
| `make bench` | host benchmarks (`tests/bench.py`) |
| `make arm-check` | cross-compile the library for Cortex-M7 and print its size |
| `make stress` | resource-exhaustion test: Wester, a suite sample, the fuzz corpus and adversarial inputs on the sanitizer build with a 2 MB and a 256 KB arena and a 16 KB stack limit (`tests/stress/run_stress.py`) |

`cas_repl` usage: interactive by default; `-b` batch mode (one output line per
input line, errors as `ERROR <code>: <msg>`), `-a` approximate mode, `-d`
degrees, `-r` real mode (real odd roots of negatives, COMPLEX = REAL), `-t <ms>` per-line timeout (uses `cas_interrupt`), `-T` timings to
stderr, `-m <MB>` arena size, `-k <KB>` arena size in KiB, `-s <KB>` stack limit (the engine then
runs on a thread whose stack is the limit + 512 KB, so an unchecked recursion really overflows).  REPL commands: `:approx`, `:exact`, `:deg`,
`:rad`, `:real`, `:complex`, `:mem`, `:latex`, `:reset`.

## Architecture

```
include/cas.h      public API
src/cas_int.h      internal declarations, node layout, builtin symbol table
src/mem.c          arena layout, scratch allocator, persistent heap, errors
src/big.c          bignums: Karatsuba, Knuth D division, gcd, primality, rho
src/num.c          integers / rationals / doubles
src/expr.c         symbols, constructors, canonical order, symbol-table GC
src/simp.c         automatic simplification (canonical forms), function rules
src/parse.c        parser (implicit multiplication, :=, =>, f'(a), lists, ...)
src/print.c        linear text printer (round-trips) and LaTeX printer
src/eval.c         evaluator: variables, user functions, command dispatch
src/cmds.c         ~150 user commands
src/poly.c         recursive dense multivariate polynomials over Q, gcd, expand,
                   rational normal form, collect, coefficients
src/factor.c       factorisation over Q (Zassenhaus/Hensel, cyclotomic,
                   multivariate via homogenisation/Kronecker)
src/upoly.c        univariate polys with expression coefficients, partial
                   fractions, resultants, numeric roots (Aberth), exact()
src/diff.c         differentiation
src/integ.c        integration (see below), definite integrals, quadrature
src/heurisch.c     parallel-Risch (heurisch) ansatz integration
src/series.c       Puiseux/Laurent series engine, limits
src/solve.c        equations, inequalities, systems
src/sum.c          sums and products
src/ode.c          dsolve, Laplace / inverse Laplace
src/gbasis.c       Groebner bases
src/bf.c           arbitrary-precision binary floats for approx(e, n)
src/linalg.c       exact linear algebra
src/xcmds.c        extension commands dispatched by name (ext_dispatch in eval.c,
                   no BUILTIN_SYMS entry needed), top-level hooks ext_pre/ext_post
                   (cas.c), matrix row operations, base-n/bit commands, digits,
                   pade, fourier_an/bn, randpoly, egcd, expexpand, piecewise
src/units.c        units and physical constants (quantities are value*unit
                   symbols; dimension checks and conversions after evaluation)
src/unc.c          uncertainty: 9.81±0.02 / pm(v,s) as v + s*_uncN (one source
                   symbol per measurement), first-order linearisation after
                   evaluation, value±unc printing, nominal/uncert/ucov
src/trig.c         trig rewriting (exp form, linearisation, expansion, simplify)
src/simplify.c     user-level simplify()
src/forms.c        forms(): alternate forms (rewritings up to two deep, numeric
                   spot check, ranking by size, work budget)
src/numeric.c      complex-double evaluation, evalf, float formatting,
                   cas_compile bytecode
tools/cas_repl.c   host REPL
tests/             unit tests, fuzzer, SymPy differential harness, benchmarks
```

### Memory model

The arena is split once at `cas_init`:

```
[ctx][symbol records + names + permanent symbol nodes][constants]
[persistent heap (1/4 of the rest)][scratch (3/4)]
```

* **Scratch** is a bump allocator reset at the start of every `cas_eval`.  All
  intermediate expressions live there.  Long computations release memory of
  failed attempts (`smark`/`srelease`) and compact live results with
  `skeep(mark, expr)`, a Cheney-style copy with forwarding pointers followed
  by a slide-down, so loops (gcd, Hensel lifting, products, quadrature) run
  in bounded memory.
* **Persistent heap** (first-fit free list with coalescing) holds variable
  values, user functions, `ans` and compiled functions.  Each stored value is
  a deep, sharing-preserving copy in a single block; reassigning frees the old
  block, so long sessions do not leak (unit test: 2000 reassignments of a
  20th-degree polynomial leave usage unchanged).
* **Symbols** are interned in a fixed table; when it is ¾ full a symbol GC at
  the start of `cas_eval` keeps only builtins and symbols reachable from stored
  values and renumbers them (unit test: 30 000 distinct identifiers).
* `cas_mem` reports fixed + heap + scratch use and the peak.
* Out-of-memory, interrupts (`cas_interrupt` sets a volatile flag polled in the
  allocator and in long loops), syntax/domain errors and excessive recursion
  are raised with `longjmp` to the innermost handler; `cas_eval` returns the
  status and leaves the context usable.  Recursion depth is guarded by
  measuring the C stack (`cas_set_stack_limit`, default 48 KB on ARM, 2 MB on
  the host) — deep inputs fail with `CAS_ERR_MEMORY`, never crash.
* **Never crash, always bail out** (the calculator has a fixed arena and a small
  stack):
  - every recursive function calls `stack_check()` (or is bounded otherwise:
    the symbol GC walks only values that `hstore` copied, whose depth its
    checked first pass bounded; `hs_copy` mirrors that pass; Karatsuba and
    merge sort recurse log n deep).  `stack_check` also polls the
    application's callback and the interrupt flag;
  - size prechecks refuse results that cannot fit before computing them
    (`CAS_ERR_MEMORY`): integer powers above ~4 million bits, factorials above
    20 000, polynomial powers whose dense size (prod (k deg_v + 1) / nv!
    coefficient slots) exceeds the free scratch ("result too large"), matrix
    and sequence sizes, series orders, digit counts;
  - long loops without allocation poll (Karatsuba nodes, long division rows,
    trial division, bigfloat series); `cas_eval` honours an interrupt within
    milliseconds of the request (`make stress` checks every case against its
    timeout);
  - recovery: each evaluation starts from a clean state (scratch, deferred
    frees, interrupt flag, the file-scope budgets and flags of the algorithms,
    `cas_reset_statics`); hidden variables and temporary assumptions are
    restored by local handlers; while evaluating, the last 1/16 of the scratch
    (at most 32 KB, `s_reserve`) is kept back, so printing the result and
    storing `ans` still work after a near-exhaustion (unit test `test_robust`:
    eighteen exhaustions in a row, then normal work, an interrupt request and a
    session save).
* The library keeps a global "current context" pointer and a few static
  scratch variables, so it is single-threaded (one evaluation at a time).
  Compiled functions are invalidated by `cas_reset`.

### Expressions and canonical form

Nodes (`E`) are immutable: integers (sign + 32-bit limbs), rationals, doubles,
symbols, n-ary sums and products, powers, function applications (functions
are identified by their symbol id, so builtins and user functions share one
namespace), lists (matrices are lists of rows) and strings.

Automatic simplification produces a canonical form (after Cohen): flattened
and sorted operands under a total order, numeric folding, like terms and
like bases combined, `c*(a+b)` distributed for numeric c, exact rational
powers reduced to `A*B^(s/t)` with B not a perfect power and radicals with the
same index merged (`sqrt(2)*sqrt(3) = sqrt(6)`), Gaussian (complex rational)
arithmetic, `i^n`, `exp(ln u) = u`, special values of trig/inverse trig at
rational multiples of π (denominators 1,2,3,4,5,6,8,10,12), parity, periodic
shifts, inverse-function cancellation, `log(x) = ln(x)/ln(10)`, `inf`/`undef`
arithmetic.  Numbers: exact by default; decimals in the input produce doubles
("approximate").  In approximate mode (`exact = 0`) results are converted with
`evalf` and printed with `digits` significant digits (exact binary→decimal
conversion with round-half-even, no printf).

### Printing

`print_expr` produces linear text that the parser reads back (fuzz-checked):
`x^2+2*x+1`, `sqrt(2)/2`, `exp(x)`, `(x-1)*(x+1)`, `[[1,2],[3,4]]`.
Sums are printed by descending degree in alphabetically ordered variables.
`cas_latex()` renders the last result as LaTeX (`\frac`, `\sqrt[n]{}`,
`pmatrix`, `\int_a^b`), intended as the structure for 2D display.

### Algorithms

* **Bignums**: 32-bit limbs; schoolbook + Karatsuba (≥32 limbs) multiply;
  Knuth algorithm D division; Euclid with 64-bit fast path; Newton integer
  roots; Miller–Rabin (deterministic below 3.3·10²⁴, plus random bases);
  trial division + Pollard–Brent rho factoring.
* **Polynomials**: recursive dense representation over Q in "kernels"
  (symbols or non-polynomial subexpressions, e.g. `sin(x)`, `sqrt(2)`);
  exact division, pseudo-remainders, primitive PRS gcd (multivariate),
  Yun squarefree decomposition.  `rat_normal` (normal/ratsimp/together)
  cancels gcds; symbols under radicals are handled by substituting
  x = Tᵠ so `(x-1)/(sqrt(x)-1) → sqrt(x)+1`.
* **Factorisation over Q**: content + squarefree; univariate by
  Cantor–Zassenhaus mod p (best of 10 primes), linear Hensel lifting past the
  Mignotte bound, subset recombination with a constant-term pre-test
  (Swinnerton–Dyer degree-16 in < 1 ms); x^n±1 via cyclotomic polynomials;
  multivariate by content in the main variable, dehomogenisation for
  homogeneous polynomials, otherwise Kronecker substitution.
* **Integration** (heuristic, verified by differentiation in the tests):
  table with linear inner arguments; rational functions by Hermite reduction
  (Mack's linear version) + partial fractions over Q (log/atan terms; real
  quadratic splitting of quartics via the resolvent cubic; Rothstein–Trager
  for other irreducible factors); integration by parts (tabular for
  polynomial × exp/trig/hyperbolic, LIATE for logs/inverse trig and their
  powers); derivative-divides substitution over candidate subexpressions
  (including sub-powers x²⊂x⁸); trig linearisation and the Weierstrass
  substitution; inverse substitutions (x = ln T/a for exponentials, x = Tᵐ for
  radicals of x, T = (a+b e^{cx})^{1/n}); `P(x)/sqrt(Q)` by undetermined
  coefficients; Euler substitutions; `P(x)·e^{g(x)}` by solving `R'+g'R = P`;
  hyperbolics via exponentials; finally a heurisch (parallel Risch) ansatz.
  Integrals that cannot be done return unevaluated (partially integrated when
  the integrand is a sum).  Definite integrals: antiderivative evaluated with
  limits at the endpoints, poles inside the interval detected (divergence
  error), result cross-checked against adaptive Gauss–Kronrod quadrature
  (global, budgeted; infinite intervals by variable transformation); numeric
  fallback.
* **Series**: generalised power series in h→0⁺ with rational exponents and a
  symbolic `ln h`, relative-precision arithmetic, composition with
  elementary functions, Taylor coefficients by symbolic differentiation for
  other functions, automatic precision retry.
* **Limits**: direct evaluation when continuous; series at x=a±h or x=±1/h;
  then the **Gruntz algorithm** (MRV sets, rewriting in ω→0, leading term via
  the series engine, recursion on lower comparability classes, a real-exponent
  leading-term fallback for rationally independent rates such as 3ˣ vs eˣ);
  finally a recursive limit calculus with signed infinities, bounded oscillation
  (squeeze), dominant-term factoring, `exp(Σ ln)` for 0·∞ and l'Hôpital (only
  while the form stays 0/0 or ∞/∞).
* **Solve**: numerator of the normal form factored over Q; degree 1–2 exact,
  cubics (Cardano / trigonometric form), binomials, polynomials in xᵏ,
  higher degrees numerically (Aberth); kernel isolation with inverse
  functions (principal solutions for trig), radicals isolated and raised,
  sums of exponentials via T = e^{gx}, trig via half-angle substitution;
  real-root scan as last resort; all candidates verified.  Inequalities by
  sign charts.  Systems: exact linear (Gauss–Jordan), polynomial by
  substitution/resultant elimination.
* **Sums**: polynomial × geometric by undetermined coefficients; rational
  terms by partial fractions + telescoping; ζ(2n) via Bernoulli numbers
  (ζ(odd) kept symbolic, numeric by Borwein's algorithm), half-integer
  (Hurwitz) shifts, Σ rᵏ/(k+a) via logarithms, e^x series.  Products of
  rational functions with integer-shifted linear factors telescope through
  factorial ratios.
* **Gröbner bases**: Buchberger with product and Gebauer–Möller chain
  criteria, normal selection, reduced output (lex or grevlex); `solve` uses a
  lex basis to triangularise polynomial systems.
* **Arbitrary precision**: binary bigfloats (bignum mantissa) with exp/ln
  (atanh series)/sin/cos/atan by argument reduction, π by Machin; results are
  float nodes carrying the exact mantissa for display.
* **Elliptic integrals** (extension, `ext/int/intalg.c`): R(x) times square
  roots whose product P is a cubic or quartic.  A substitution
  sin²φ = w(x) with parameter m is chosen from the roots of P (even quartics:
  asin, atan, 2·atan and acos forms; real roots: a Möbius map of the
  interval where the radicands are positive; a cubic with a complex pair:
  Byrd & Friedman 239/240), the polynomial part is reduced, and the rest is a
  linear system for the algebraic part and the coefficients of F, E and Π
  (one Π term per pole, and for the points at infinity of a quartic).
  Radical constants become a symbol for the linear algebra.  Trigonometric
  integrands reduce to this through C = cos u (sin u, cosh u);
  sin^p·cos^q with non-integer powers give ₂F₁.
* **Hypergeometric sums** (extension, `ext/alg/hyper.c`): Gosper's algorithm
  (Gosper–Petkovšek form, polynomial solution by linear algebra) and
  Zeilberger's creative telescoping (Petkovšek, Wilf, Zeilberger, "A = B").
* **Special functions**: Si, Ci, Ei (series + asymptotic expansions), erf,
  erfc, erfi; integration table entries (∫sin(x)/x, ∫eˣ/x, ∫1/ln x,
  ∫e^{ax²}); limits at ∞; series at 0.
* **ODEs**: first order (quadrature, linear, separable, Bernoulli), constant
  coefficient linear of any order (real basis), variation of parameters for
  order-2 right-hand sides, initial conditions `y(a)=b`, `y'(a)=c`.  The
  extension (`ext/alg/odex.c`, slot `CASX_FN_DSOLVE`) adds Riccati and Bessel
  equations and power series / Frobenius solutions, and writes general
  solutions as Σ c_i y_i + y_p (checked at sample points); `wronskian`.
* **Linear algebra**: Bareiss determinant, Gauss–Jordan rref/inverse/rank
  with rational-normal-form entries (symbolic matrices work), kernel,
  charpoly, eigenvalues/vectors via solve, matrix power.
* **cas_compile**: expression → stack bytecode (≤64 deep) evaluated with
  doubles; no allocation in `cas_fn_eval`; returns NaN outside the domain;
  rational powers with odd denominators use the real root (graphing).

## Supported input

Operators `+ - * / ^ ** !`, implicit multiplication (`2x`, `2(x+1)`,
`(a+b)(a-b)`, `x y`, `2sin(x)`), relations `= == < > <= >= != <>`, `and or
not`, `:=` (variables and `f(x,y):=...`), `=>` / `▶` store, `;` sequences,
lists `[...]`/`{...}`, `|x|`, `f'(a)`/`f''(x)`, `ans`, decimals and `1e-5`,
Unicode `π √ ∞ ≤ ≥ ≠ × ÷ −`.

Constants: `pi e i inf undef true false`.

Functions (auto-simplified, differentiable, numeric): `sqrt exp ln log(x[,b])
log10 log2 sin cos tan cot sec csc asin acos atan acot asec acsc sinh cosh
tanh asinh acosh atanh abs sign floor ceil round frac re im conj arg atan2
gamma factorial erf erfc erfi Si Ci Ei zeta heaviside dirac max min mod
binomial`; constant `euler_gamma`.  `acot(x) = π/2 − atan(x)` (range (0, π)).
`round` rounds half away from zero.

Commands:
* algebra: `simplify expand factor sqrfree factors collect normal ratsimp
  together partfrac apart subst subs numer denom quo rem divide gcd lcm
  degree coeff coeffs lcoeff content primpart resultant discriminant horner
  lagrange texpand tlin tcollect trigsimp trig2exp exp2trig lnexpand
  lncollect expexpand powexpand exact approx evalf`
* calculus: `diff (diff(f,x,n), diff(f,[x,y]))  integrate/int (indefinite,
  definite, improper) nint nderiv limit (one/two-sided, ±inf) series/taylor
  sum product tangent grad hessian jacobian divergence curl laplace ilaplace
  dsolve/desolve`
* solving: `solve csolve fsolve/nsolve linsolve roots proot gbasis` (also
  inequalities `solve(x^2<4,x)` and polynomial systems); `fsolve([eqs],[vars],[guess])`
  runs Newton's method on nonlinear systems (up to 10 unknowns).  With the
  extension (cas/ext/alg/trigsolve.c, slot `CASX_FN_SOLVE`) periodic equations
  get general solutions `solve(sin(x)=1/2,x)` → `[2*n_1*pi+pi/6,2*n_1*pi+5*pi/6]`
  (`n_1` assumed integer; overlapping families merged: `cos(x)=0` → `[n_1*pi+pi/2]`)
  and `solve(eq,x,a,b)` lists the solutions in [a, b] (at most 100)
* `implicitdiff(F(x,y)=0, y, x)` (dy/dx = -F_x/F_y), `odesolve(f(x,y), [x,y],
  [x0,y0], x1)` (adaptive RK45, returns y(x1)), `propfrac(17/5)`, `propfrac(p/q, x)`
* linear algebra: `det inv transpose/trn rref rank trace identity/idn zeros
  matrix charpoly eigenvals eigenvects ker dot cross norm adjoint size lu qr
  cholesky`, matrix `+ - * ^`
* statistics (the extension, cas/ext/adv/ntcmds.c): `covariance` (n) /
  `covariancep` (n−1, like `variance`/`variancep`), `correlation`; tests
  returning `[statistic, df, p, ..., reject=true|false]` at alpha (default
  0.05): `ttest ttest2` (Welch or pooled) `pairedttest ztest chi2gof chi2test
  anova`; intervals `tinterval zinterval propinterval`; distributions (pdf,
  cdf over [lo,hi], inverse; exact for exact arguments where a closed form
  exists) `exppdf uniformpdf gammapdf betapdf weibullpdf lognormpdf` (and
  `..cdf`, `invExp invUniform invGamma invBeta invWeibull invLognorm`),
  `invBinom invPoisson`; regression returning `[fit, R2=...]`: `polyreg
  expreg powerreg logreg multireg` (exact normal equations for exact data)
  and `nlfit` (Levenberg-Marquardt)
* applied mathematics (the extension, cas/ext/applied; see below):
  `svd singvals pinv`, `norm(A,2) cond(A,p) rank(A,tol)`, numeric
  `eigenvals`/`eigenvects` for matrices with decimals, `fourier ifourier ztrans
  iztrans fft ifft linprog`, `minimize`/`maximize(f, constraints, vars)`,
  `truthtable boolsimplify/logicsimplify dnf cnf satisfiable implies iff`
  (and `xor`, `implies`, `iff` as infix operators), `spline`
* integers: `ifactor isprime nextprime prevprime iquo irem divisors euler
  perm/nPr comb/nCr iegcd invmod powmod ichinrem`
* matrices (common calculator names): `dim fill randMat augment ref rowSwap rowAdd mRow
  mRowAdd cumSum cond setElem redim`
* integers in bases: literals `0x1F 0b101 0o17`; `and or not` of two integers
  are bitwise; `bitand bitor bitxor xor bitnot shl shr rotl rotr hex bin oct
  wordsize(bits, signed)`
* units: `9.8_m/s^2*3_s = 29.4_m/s` (after the first `_`, `*` `/` `^n`
  join unit names without spaces), SI prefixes, `convert(q, _unit)`,
  `convert(q, "SI"|"base"|"US")`, `deltaconvert`, `usimplify`, `uvalue`,
  `uunits`; "incompatible units" on dimension errors; temperatures absolute
  in convert, differences in deltaconvert; constants `_c _hP _hbar _qe _kB
  _NA _R _G _g0 _eps0 _mu0 _me _mp ...` (CODATA 2022) symbolic in exact mode,
  replaced by value*SI units in approx mode / approx()
* also `digits pade fourier_an fourier_bn randpoly egcd`, `piecewise`
  evaluates when its conditions can be decided, bare `restart`
* lists/misc: `seq makelist sort reverse concat map apply mean median
  variance stddev lhs rhs has type purge restart when/ifte`
* assumptions: `assume(a>0)`, `assume(a>=0)`, `assume(a<0)`,
  `assume(n,integer)`, `unassume(a)`, `about(a)` — used by sign analysis
  (`sqrt(a^2)=a`, `abs`, limits and definite integrals with parameters,
  `sin(n*pi)=0`, `cos(n*pi)=(-1)^n`)
* numeric precision: `approx(expr, n)` / `evalf(expr, n)` evaluates with n
  significant digits (arbitrary precision for constant subexpressions);
  settings `digits` up to 1000 in approximate mode
* equations are first-class: `(x+1=3)-1` gives `x=2`, `sqrt(x^2=4)` etc.
* `cfactor` (splits quadratics over C), `d/dx(expr)` notation, `f'(a)`, the del operator `∇f`, `∇·F`, `∇×F`, `∇²f` (`nabla`; grad, divergence, curl, laplacian in x, y, z).

Angle mode: in degree mode trig arguments are scaled by π/180 and inverse
trig results by 180/π.  `log` is base 10 (calculator convention); `ln` is
natural.

### Applied mathematics (cas/ext/applied)

Part of the advanced module like cas/ext/sci (not in the core tier, where the
names report "needs the advanced CAS module"); one registration table
(applied.c).  The core forwards `norm(A,2)`, `rank(A,tol)` and `cond(A,p)` by
name and asks the slot `CASX_FN_NUMLA` for eigenvalues, eigenvectors and the
rank of matrices with decimals (and for eigenvectors when the eigenvalues are
numeric roots).  Doubles only through the API (`X->sqrt`; sin/cos by its own
reduced Taylor polynomials): the module image has no libm.

* numeric linear algebra (nla.c, double precision): SVD by one-sided Jacobi
  (Hestenes) rotations, thin `[U, S, V]`; exact singular values as square roots
  of the eigenvalues of AᵀA when the characteristic polynomial is solved;
  symmetric eigenproblems by cyclic Jacobi; others by balancing, Hessenberg
  reduction (stabilised elimination) and the Francis double-shift QR
  iteration, eigenvectors by inverse iteration (complex LU); numeric rank with
  the tolerance max(m,n)·ε·σ₁; `pinv` exact through the rank factorisation
  A = C F, numeric through the SVD.
* transforms (xform.c): `fourier(f,x,w)` = 1/√(2π) ∫ f e^(−iwx) dx (unitary,
  angular frequency; replaces cas/ext/int/fourier.c, whose table entry is
  registered later and no longer reached) by linearity, shift/scale,
  modulation, xⁿ·g, impulses, Heaviside/sign distributions, Gaussians,
  residues for rational functions (merged to |w| / sign(w) forms) and two
  one-sided Laplace transforms for decaying functions; `ifourier` = fourier at
  −x.  `ztrans` (unilateral) by geometric scaling, (−z d/dz)ᵏ, trig/hyperbolic
  rules, 1/n!, binomial, else `sum`; `iztrans` by partial fractions of F(z)/z
  over the exact poles (unit impulses as `piecewise(n=k,1,0)`, complex pairs
  in real form).  `fft`/`ifft`: radix 2, Bluestein for other lengths.
* optimisation (lp.c): two-phase simplex in exact rationals with Bland's
  rule; `[inf,[]]` for an empty feasible set, `[-inf,[]]` when unbounded;
  equality-constrained `minimize`/`maximize` by Lagrange multipliers (`solve`).
  casx.c picks the table entry by argument count, so the 3-argument
  `minimize` sits beside the 2-argument one of cas/ext/int/spfn.c.
* logic (logic.c): truth tables evaluated 64 rows per machine word; minimal
  DNF/CNF by recursive prime implicants and a minimum cover (essentials,
  greedy bound, branch and bound); `satisfiable` by enumeration (≤ 24 atoms).
* `spline` (spline.c): natural cubic spline by the tridiagonal system in exact
  arithmetic, as a `piecewise`.

## Testing

* `tests/unit_tests.c` — 1961 checks (with the `*.inc` files): randomized bignum identities across
  Karatsuba sizes, division/gcd, primality, rationals, multivariate gcd,
  end-to-end evaluation, settings, compile, memory reuse over long sessions,
  output truncation, deep nesting under a 48 KB stack budget, symbol-table
  GC, reset.
* `tests/fuzz.c` — grammar-based + byte-level + "math expression wrapped in a
  heavy command" fuzzing under ASan/UBSan with a 300 ms interrupt timer;
  also checks that every exact output parses back.  Several million inputs
  run clean.
* `tests/difftest.py` — differential testing against SymPy 1.14 with
  equivalence checks (see the docstring).  Curated textbook problems (two
  tiers) plus random expressions; results are written to
  `tests/results/difftest_report.{md,json}`.

### Differential test results (SymPy 1.14)

`make difftest` (seed 1, 80 random cases per random category; "wrong" = result
mathematically different from SymPy, "unsupported" = unevaluated/"could not be
determined"):

| category | total | pass | wrong | unsupported | error | pass % | avg ms |
|---|---|---|---|---|---|---|---|
| approx | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.00 |
| approx_hp | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.03 |
| arith | 81 | 81 | 0 | 0 | 0 | 100.0 | 0.02 |
| branch | 115 | 115 | 0 | 0 | 0 | 100.0 | 0.00 |
| defint | 33 | 33 | 0 | 0 | 0 | 100.0 | 0.12 |
| diff | 34 | 34 | 0 | 0 | 0 | 100.0 | 0.00 |
| eig_num | 26 | 26 | 0 | 0 | 0 | 100.0 | 0.06 |
| exact_vals | 1410 | 1410 | 0 | 0 | 0 | 100.0 | 0.00 |
| expand | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.01 |
| factor | 40 | 40 | 0 | 0 | 0 | 100.0 | 0.08 |
| fft | 14 | 14 | 0 | 0 | 0 | 100.0 | 0.04 |
| fourier | 29 | 29 | 0 | 0 | 0 | 100.0 | 0.06 |
| gosper_sum | 12 | 12 | 0 | 0 | 0 | 100.0 | 0.09 |
| h_factor | 25 | 25 | 0 | 0 | 0 | 100.0 | 0.05 |
| h_integrate | 80 | 80 | 0 | 0 | 0 | 100.0 | 0.22 |
| h_limit | 35 | 35 | 0 | 0 | 0 | 100.0 | 0.02 |
| h_linalg | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.01 |
| h_series | 15 | 15 | 0 | 0 | 0 | 100.0 | 0.03 |
| h_simplify | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.04 |
| h_solve | 25 | 25 | 0 | 0 | 0 | 100.0 | 0.09 |
| h_sum | 12 | 12 | 0 | 0 | 0 | 100.0 | 0.03 |
| ilaplace | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.05 |
| ineq | 15 | 15 | 0 | 0 | 0 | 100.0 | 0.02 |
| integrate | 124 | 124 | 0 | 0 | 0 | 100.0 | 0.11 |
| integrate_ell | 18 | 18 | 0 | 0 | 0 | 100.0 | 1.11 |
| laplace | 12 | 12 | 0 | 0 | 0 | 100.0 | 0.02 |
| limit | 44 | 44 | 0 | 0 | 0 | 100.0 | 0.02 |
| linalg | 26 | 26 | 0 | 0 | 0 | 100.0 | 0.01 |
| linprog | 34 | 34 | 0 | 0 | 0 | 100.0 | 0.02 |
| logic | 48 | 48 | 0 | 0 | 0 | 100.0 | 0.00 |
| numla | 15 | 15 | 0 | 0 | 0 | 100.0 | 0.26 |
| ode | 33 | 33 | 0 | 0 | 0 | 100.0 | 0.10 |
| ode_more | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.20 |
| ode_series | 12 | 12 | 0 | 0 | 0 | 100.0 | 0.10 |
| pinv | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.01 |
| rand_diff | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.01 |
| rand_eval | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.00 |
| rand_eval_branch | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.00 |
| rand_eval_cplx | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.00 |
| rand_expand | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.01 |
| rand_factor | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.08 |
| rand_factor_mv | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.14 |
| rand_integrate | 56 | 52 | 0 | 4 | 0 | 92.9 | 10.34 |
| rand_limit | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.01 |
| rand_limit_inf | 29 | 28 | 0 | 1 | 0 | 96.6 | 0.10 |
| rand_linalg | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.00 |
| rand_ratint | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.09 |
| rand_series | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.21 |
| rand_solve | 30 | 30 | 0 | 0 | 0 | 100.0 | 0.02 |
| rand_system | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.01 |
| rand_system_nl | 20 | 20 | 0 | 0 | 0 | 100.0 | 0.48 |
| rand_transform_branch | 60 | 60 | 0 | 0 | 0 | 100.0 | 0.01 |
| series | 25 | 25 | 0 | 0 | 0 | 100.0 | 0.02 |
| simplify | 25 | 25 | 0 | 0 | 0 | 100.0 | 0.04 |
| solve | 51 | 51 | 0 | 0 | 0 | 100.0 | 0.04 |
| spline | 5 | 5 | 0 | 0 | 0 | 100.0 | 0.05 |
| sum | 29 | 29 | 0 | 0 | 0 | 100.0 | 0.02 |
| summation | 11 | 10 | 0 | 1 | 0 | 90.9 | 0.32 |
| svd | 22 | 22 | 0 | 0 | 0 | 100.0 | 0.02 |
| system | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.03 |
| t3_integrate | 50 | 50 | 0 | 0 | 0 | 100.0 | 0.36 |
| t3_limit | 40 | 40 | 0 | 0 | 0 | 100.0 | 0.09 |
| t3_simplify | 10 | 10 | 0 | 0 | 0 | 100.0 | 0.08 |
| t3_solve | 14 | 14 | 0 | 0 | 0 | 100.0 | 0.12 |
| t4_misc | 44 | 44 | 0 | 0 | 0 | 100.0 | 0.02 |
| t4_series | 13 | 13 | 0 | 0 | 0 | 100.0 | 0.01 |
| trig_solve | 42 | 42 | 0 | 0 | 0 | 100.0 | 0.13 |
| trig_solve_iv | 16 | 16 | 0 | 0 | 0 | 100.0 | 0.08 |
| wronskian | 7 | 7 | 0 | 0 | 0 | 100.0 | 0.04 |
| ztrans | 34 | 34 | 0 | 0 | 0 | 100.0 | 0.09 |
| **all** | 3620 | 3614 | 0 | 6 | 0 | 99.8 | |


Seeds 2–5: 0 wrong each.
Round 6 categories: `gosper_sum` (against SymPy's `gosper_sum`), `summation`
(definite hypergeometric sums, the closed form against explicit sums for n = 0..8,
since binomial identities hold for integer n only), `ode_more` (Riccati, Bessel,
normal forms), `ode_series` (the residual of a power series / Frobenius solution
has no terms below the truncation order), `wronskian` (SymPy's `wronskian`),
`integrate_ell` (elliptic and hypergeometric antiderivatives, by differentiation).  When
SymPy's own limit at infinity is demonstrably wrong (checked numerically at
x = 10^2..10^4 with 30-digit arithmetic) the case counts as a pass; complex
infinities (`sqrt(-ln x)`) count as unsupported since we return `undef` for real
variables.

Categories: `arith` exact arithmetic/number theory; `exact_vals` ~1400 special
values (trig at kπ/d for d ≤ 12, rational powers, inverse trig, logs, Γ,
binomials, rounding); `branch` / `rand_eval_branch` / `rand_transform_branch`
check the automatic simplifier and simplify/normal/expand/factor at negative and
complex points; `h_*`, `t3_*` and `t4_*` are harder textbook / breadth tiers (`tests/cases_hard.py`);
`rand_*` are random expressions (integrands are derivatives of random
expressions, so an elementary antiderivative always exists; SymPy's `heurisch`/Risch
does somewhat better there).  `svd eig_num pinv numla fourier ztrans fft linprog
logic spline` cover cas/ext/applied (`tests/cases_applied.py`): singular values and
eigenvalues against NumPy (1e-9 relative), U S Vᵀ = A and orthonormality, eigenvector
residuals; `pinv` exactly against SymPy; Fourier transforms against SymPy's
`fourier_transform` converted to the unitary convention or numeric quadrature, at
±w (dirac terms dropped off their support); z-transforms against partial sums,
inverse ones against the Laurent coefficients; `fft` against numpy.fft; linear
programs against scipy (HiGHS); logic by truth-table equivalence and literal count
not above SymPy's `simplify_logic`; splines against scipy's natural CubicSpline.

### Benchmarks (host: Apple M-series, `make bench`, 24 MB arena)

| area | command | ms | peak KB | result (truncated) |
|---|---|---|---|---|
| bignum | `1000!` | 0.2 | 618 | `40238726007709377354370243392300398571937486421071` |
| bignum | `2^100000 mod 997` | 0.2 | 555 | `421` |
| bignum | `500!/(250!*250!)` | 0.0 | 519 | `11674431578827768292093473476217661965923008118031` |
| bignum | `isprime(2^521-1)` | 6.2 | 494 | `true` |
| bignum | `ifactor(2^64+1)` | 0.3 | 556 | `274177*67280421310721` |
| bignum | `ifactor(1000000007*998244353)` | 2.3 | 1275 | `998244353*1000000007` |
| bignum | `gcd(3^5000-1,2^7000-1)` | 0.7 | 517 | `7145813125` |
| expand | `expand((x+y+z+1)^20)` | 20.8 | 8203 | `x^20+20*x^19*y+20*x^19*z+20*x^19+190*x^18*y^2+380*` |
| expand | `expand((x+1)^200)` | 0.6 | 2276 | `x^200+200*x^199+19900*x^198+1313400*x^197+64684950` |
| factor | `factor(x^100-1)` | 0.1 | 643 | `(x-1)*(x+1)*(x^2+1)*(x^4+x^3+x^2+x+1)*(x^20+x^15+x` |
| factor | `factor(x^120-1)` | 0.1 | 731 | `(x-1)*(x+1)*(x^2+1)*(x^4+1)*(x^2+x+1)*(x^2-x+1)*(x` |
| factor | `factor(expand((x^5+x+1)*(x^7-x^2+3)*(2*x^4-x+5)*(x^3+11)))` | 0.5 | 1021 | `(x^3+11)*(x^2+x+1)*(x^3-x^2+1)*(2*x^4-x+5)*(x^7-x^` |
| factor | `factor(Swinnerton-Dyer S4, deg 16)` | 0.2 | 15936 | `x^16-136*x^14+6476*x^12-141912*x^10+1513334*x^8-74` |
| factor | `factor(x^4*y^2-x^2*y^4+x^3-y^3)` | 0.7 | 1405 | `(x-y)*(x^3*y^2+x^2*y^3+x^2+x*y+y^2)` |
| factor | `factor(expand((x+y+z)^3*(x-y+2*z)^2))` | 0.2 | 1007 | `(x+y+z)^3*(x-y+2*z)^2` |
| gcd | `gcd(expand((x+1)^20*(x-2)^15),expand((x+1)^12*(x-3)^10))` | 0.2 | 974 | `x^12+12*x^11+66*x^10+220*x^9+495*x^8+792*x^7+924*x` |
| integrate | `integrate(1/(x^6-1),x)` | 0.1 | 645 | `ln(x-1)/6-sqrt(3)*atan(sqrt(3)*(2*x-1)/3)/6-sqrt(3` |
| integrate | `integrate(x^10*exp(x)*sin(x),x)` | 1.2 | 3135 | `exp(x)*sin(x)*x^10/2-cos(x)*exp(x)*x^10/2+5*cos(x)` |
| integrate | `integrate((x^4+3*x^2+1)/(x^5+x^3-x^2-1),x)` | 0.2 | 734 | `atan(x)/2-2*sqrt(3)*atan(sqrt(3)*(2*x+1)/3)/3+5*ln` |
| integrate | `integrate(sin(x)^7*cos(x)^4,x)` | 0.1 | 664 | `11*cos(5*x)/5120-7*cos(x)/512-cos(3*x)/512-cos(7*x` |
| integrate | `integrate(x^3*atan(x)^2,x)` | 0.1 | 702 | `atan(x)^2*x^4/4-atan(x)*x^3/6+x^2/12+atan(x)*x/2-a` |
| integrate | `integrate(sqrt(x^2+2*x+5),x)` | 0.1 | 549 | `2*ln(2*x+2*sqrt(x^2+2*x+5)+2)+(x/2+1/2)*sqrt(x^2+2` |
| integrate | `integrate(1/(x^4+1),x)` | 0.1 | 804 | `sqrt(2)*atan(sqrt(2)*(2*x+sqrt(2))/2)/4+sqrt(2)*at` |
| integrate | `integrate(exp(-x^2),x,-inf,inf)` | 0.1 | 594 | `sqrt(pi)` |
| series | `series(tan(x),x,0,25)` | 0.6 | 2166 | `113927491862*x^23/2900518163668125+18888466084*x^2` |
| series | `series(exp(sin(x)),x,0,15)` | 0.2 | 974 | `58913*x^14/4151347200+2417*x^13/48648600+181*x^12/` |
| limit | `limit((1+1/x)^(x^2)/exp(x),x,inf)` | 0.6 | 2096 | `exp(-1/2)` |
| solve | `solve(x^5-5*x+1=0,x)` | 0.1 | 564 | `[-1.5416516841,0.20006410263,1.44050039734]` |
| solve | `solve([x^2+y^2=25,x*y=12],[x,y])` | 0.1 | 821 | `[[x=-4,y=-3],[x=-3,y=-4],[x=3,y=4],[x=4,y=3]]` |
| linalg | `det(matrix(10,10,f))` | 0.1 | 638 | `0` |
| linalg | `inv([[1,2,3,4],[2,3,4,1],[3,4,1,2],[4,1,2,3]])` | 0.0 | 505 | `[[-9/40,1/40,1/40,11/40],[1/40,1/40,11/40,-9/40],[` |
| linalg | `det([[a,b,c],[d,e1,f1],[g,h,k]])` | 0.0 | 558 | `a*e1*k-a*f1*h-b*d*k+b*f1*g+c*d*h-c*e1*g` |
| linalg | `det(matrix(12,12,f))` | 0.2 | 899 | `1/379106579436304517151885479034796391880188687864` |
| sum | `sum(k^10,k,1,n)` | 0.5 | 1959 | `n*(n+1)*(2*n+1)*(n^2+n-1)*(3*n^6+9*n^5+2*n^4-11*n^` |
| numeric | `nint(exp(-x^2)*cos(x),x,0,10)` | 0.1 | 514 | `0.690194223522` |

The ARM Cortex-M7 at 480 MHz is expected to be roughly 20–50× slower than the
host per operation; almost everything above is still sub-second there.  Peak
scratch use is shown per command (the persistent heap is unaffected).


## Known limitations

* Integration is heuristic (no full Risch algorithm): some elementary
  integrals are not found; irreducible denominators of degree ≥ 5 whose
  Rothstein–Trager resultant does not split over Q (needs algebraic
  extensions / RootSum) are left unevaluated; quartics are split only when
  the resolvent cubic has a usable real root.  Results can be large:
  the advanced module rewrites antiderivatives into simpler forms
  (square roots of polynomials rationalised, logarithms merged, trig
  rational functions as powers of sin, cos or tan), but nested radicals
  with symbolic or quartic content stay as found, and logarithms are not
  rewritten as inverse hyperbolic functions.
* Algebraic numbers are kernels: `sqrt(2)^2` reduces, but gcds over
  Q(√2) are not computed (e.g. `(x^2-2)/(x-sqrt(2))` is not cancelled by
  `normal`; `simplify` rationalises single square roots in denominators).
* Variables are treated as real in sign analysis, `abs`, `re/im/conj`,
  `ln(exp(x)) = x`, `(e^u)^r = e^(ur)` and when pulling e^u out of radicals;
  other rules are valid on the principal branch for complex values (checked
  at complex points by the harness).  `assume` supports only sign and
  integer facts about single symbols.
* `solve` gives general solutions (`+ p*n_1`) only when every occurrence of the
  variable is inside sin/cos/tan/sec/csc/cot of linear arguments with commensurable
  rates (and only principal solutions without the extension); polynomials of degree ≥ 5 (and non-solvable quartics)
  get numeric roots; polynomial systems go through linear elimination,
  resultants and a plain Buchberger Gröbner basis (no F4, no modular
  methods), so larger systems can be slow.
* Limits: Gruntz is implemented for exp-log functions; limits involving
  oscillating functions at infinity (other than bounded·0) or complex
  infinities are reported as "could not be determined".
* Sums: polynomial·geometric, rational/telescoping, ζ/Hurwitz values, log
  series and binomial-moment sums (Σ k^m·binomial(n,k)·r^k) are handled; with
  the extension also Gosper's algorithm (indefinite hypergeometric sums) and
  Zeilberger's creative telescoping for Σ_{k=0}^{n} F(n,k) when the recurrence
  has order 1 (or order 2 with only even terms).  Higher-order recurrences
  (no Petkovšek "Hyper" solver) and sums without natural boundaries stay
  unevaluated.
* Arbitrary precision is available for evaluating constant expressions
  (`approx(e, n)`); further arithmetic on such results is done in double
  precision.  Gamma/erf/zeta/Si… are evaluated in double precision only.
* Series: coefficients are normalised as rational functions in constant
  kernels without trig identities, so expansions about non-zero points of
  nested trig functions (e.g. `series(atan((2x+sin(x+1))^3),x,0,5)`) can swell
  and run out of memory.
* ODEs: first order (separable, linear, Bernoulli, homogeneous, exact),
  constant-coefficient and Euler–Cauchy linear ODEs, first-order linear
  systems with constant coefficients; with the extension Riccati equations
  (rational particular solution or the linear second-order transform),
  Bessel's equation, and power series solutions `dsolve(eq, y, x, x0, n)` of
  linear equations (ordinary points; Frobenius at regular singular points of
  homogeneous second-order equations).  Variable-coefficient equations without
  such a structure (e.g. Airy's, which has no Airy functions here) are only
  solved as series; `odesolve` is a scalar RK45 only.
* Matrices: exact eigenvalues go through the characteristic polynomial
  (numeric roots for degree ≥ 5); the numeric solvers of the extension (SVD,
  QR iteration, Jacobi) are double precision only, matrices up to 120 × 120
  for eigenvalues and 40 000 entries for the SVD; numeric eigenvectors of a
  non-symmetric matrix are one per distinct eigenvalue (a defective or
  repeated eigenvalue gets a single vector, and close numeric eigenvalues of
  a defective matrix may be listed separately).
* Transforms: `fourier` needs the rules above (no general definite
  integration; rational functions with real poles, products of unrelated
  shifted windows are not transformed); `iztrans` needs a rational F with
  exactly found poles; logic commands work from truth tables (≤ 12 atoms for
  simplification, ≤ 10 for `truthtable`).
* Multivariate factorisation via Kronecker substitution is exponential in the
  worst case and limited to modest degrees.
* Single-threaded (global current context; a few static scratch variables).
* Integration results are correct (verified by differentiation) but not
  always in the simplest form (e.g. log/atan forms with unsimplified radical
  coefficients).  Elliptic integrals carry a locally constant factor of
  square roots (the branch of the integrand's radicals) when the integrand is
  real on more than one interval; third-kind integrals with poles that are
  not symmetric under the substitution (pseudo-elliptic cases such as
  1/((2^(2/3)+x) sqrt(1+x^3))), radicals of degree above 4, and quarter
  powers of trigonometric functions are not done.
