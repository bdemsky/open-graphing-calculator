# sympy suite results

binary: `build/cas_repl`; full per-case records: `build/suites/sympy/results.json`

| group | PASS | WRONG | CRASH | TIMEOUT | ERROR | UNEVAL | UNSUPPORTED | TRANSLATION-UNCERTAIN | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|---|
| test_integrals | 226 | 0 | 0 | 0 | 0 | 22 | 47 | 0 | 0 | 173 | 468 |
| test_heurisch | 74 | 0 | 0 | 0 | 0 | 0 | 9 | 0 | 0 | 18 | 101 |
| test_manual | 147 | 0 | 0 | 0 | 0 | 13 | 3 | 0 | 0 | 21 | 184 |
| test_risch | 10 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 0 | 88 | 100 |
| test_rationaltools | 64 | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 10 | 76 |
| test_trigonometry | 37 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 37 |
| test_meijerint | 16 | 0 | 0 | 0 | 0 | 8 | 21 | 0 | 0 | 98 | 143 |
| test_failing_integrals | 4 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 3 | 8 |
| test_laplace | 4 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 231 | 236 |
| test_limits | 279 | 0 | 0 | 0 | 0 | 132 | 41 | 9 | 0 | 40 | 501 |
| test_gruntz | 36 | 0 | 0 | 0 | 0 | 21 | 4 | 1 | 0 | 95 | 157 |
| test_demidovich | 40 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12 | 52 |
| test_series | 21 | 0 | 0 | 0 | 0 | 3 | 22 | 0 | 0 | 38 | 84 |
| test_nseries | 40 | 0 | 0 | 0 | 0 | 1 | 9 | 0 | 0 | 137 | 187 |
| test_limitseq | 5 | 0 | 0 | 0 | 0 | 15 | 11 | 0 | 0 | 23 | 54 |
| test_residues | 32 | 0 | 0 | 0 | 0 | 2 | 1 | 0 | 0 | 2 | 37 |
| test_solvers | 107 | 0 | 0 | 0 | 2 | 16 | 97 | 17 | 0 | 325 | 564 |
| test_solveset | 50 | 0 | 0 | 0 | 0 | 2 | 40 | 1 | 0 | 435 | 528 |
| test_polysys | 9 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 82 | 92 |
| test_simplify | 72 | 0 | 0 | 0 | 1 | 2 | 85 | 1 | 1 | 131 | 293 |
| test_trigsimp | 110 | 0 | 0 | 0 | 0 | 0 | 34 | 0 | 0 | 36 | 180 |
| test_ratsimp | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 7 | 8 |
| test_radsimp | 33 | 0 | 0 | 0 | 0 | 0 | 61 | 0 | 0 | 57 | 151 |
| test_powsimp | 76 | 0 | 0 | 0 | 0 | 0 | 52 | 0 | 1 | 11 | 140 |
| test_sqrtdenest | 51 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 8 | 60 |
| test_combsimp | 19 | 0 | 0 | 0 | 0 | 0 | 9 | 0 | 0 | 0 | 28 |
| test_gammasimp | 40 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 6 | 46 |
| test_fu | 16 | 0 | 0 | 0 | 0 | 0 | 2 | 0 | 0 | 173 | 191 |
| test_polytools | 131 | 0 | 0 | 0 | 1 | 4 | 46 | 5 | 1 | 975 | 1163 |
| test_partfrac | 8 | 0 | 0 | 0 | 0 | 0 | 15 | 0 | 0 | 11 | 34 |
| test_polyroots | 23 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 94 | 118 |
| test_polyfuncs | 0 | 0 | 0 | 0 | 0 | 0 | 11 | 0 | 0 | 32 | 43 |
| test_matrices | 33 | 0 | 0 | 0 | 0 | 2 | 11 | 0 | 0 | 340 | 386 |
| test_determinant | 11 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 38 | 50 |
| test_eigen | 3 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 43 | 47 |
| test_commonmatrix | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 133 | 134 |
| test_matrixbase | 26 | 0 | 0 | 0 | 0 | 2 | 11 | 0 | 0 | 392 | 431 |
| test_reductions | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 39 | 47 |
| test_sums_products | 58 | 0 | 0 | 0 | 0 | 9 | 41 | 0 | 0 | 225 | 333 |
| test_products | 15 | 0 | 0 | 0 | 0 | 0 | 11 | 0 | 0 | 48 | 74 |
| test_gosper | 9 | 0 | 0 | 0 | 0 | 8 | 0 | 0 | 0 | 8 | 25 |
| test_complexes | 110 | 0 | 0 | 0 | 1 | 30 | 56 | 0 | 0 | 132 | 329 |
| test_exponential | 89 | 0 | 0 | 0 | 0 | 12 | 43 | 0 | 0 | 128 | 272 |
| test_hyperbolic | 342 | 0 | 0 | 0 | 0 | 70 | 4 | 11 | 0 | 198 | 625 |
| test_integers | 82 | 0 | 0 | 0 | 0 | 54 | 0 | 0 | 0 | 78 | 214 |
| test_miscellaneous | 18 | 0 | 0 | 0 | 0 | 67 | 22 | 0 | 0 | 39 | 146 |
| test_piecewise | 25 | 0 | 0 | 0 | 0 | 48 | 72 | 0 | 10 | 188 | 343 |
| test_trigonometric | 393 | 0 | 0 | 0 | 0 | 73 | 44 | 4 | 0 | 424 | 938 |
| test_comb_factorials | 45 | 0 | 0 | 0 | 9 | 15 | 47 | 0 | 0 | 47 | 163 |
| test_gamma_functions | 16 | 0 | 0 | 0 | 0 | 1 | 82 | 0 | 0 | 126 | 225 |
| test_error_functions | 29 | 0 | 0 | 0 | 0 | 5 | 106 | 0 | 0 | 127 | 267 |
| test_zeta_functions | 11 | 0 | 0 | 0 | 0 | 2 | 44 | 0 | 0 | 16 | 73 |
| test_delta_functions | 10 | 0 | 0 | 0 | 0 | 7 | 11 | 0 | 0 | 33 | 61 |
| test_arit | 35 | 0 | 0 | 0 | 8 | 17 | 0 | 0 | 11 | 97 | 168 |
| test_power | 29 | 0 | 0 | 0 | 0 | 17 | 0 | 0 | 0 | 76 | 122 |
| test_expand | 38 | 0 | 0 | 0 | 0 | 0 | 10 | 0 | 0 | 64 | 112 |
| test_evalf | 12 | 0 | 0 | 0 | 0 | 2 | 14 | 0 | 0 | 179 | 207 |
| test_numbers | 64 | 0 | 0 | 0 | 0 | 0 | 29 | 0 | 0 | 345 | 438 |
| test_function | 12 | 0 | 0 | 0 | 0 | 0 | 99 | 0 | 0 | 173 | 284 |
| test_diff | 23 | 0 | 0 | 0 | 1 | 2 | 2 | 0 | 0 | 14 | 42 |
| test_subs | 17 | 0 | 0 | 0 | 0 | 0 | 5 | 0 | 0 | 293 | 315 |
| test_complex | 16 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 57 | 74 |
| test_eval | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 7 |
| test_exprtools | 1 | 0 | 0 | 0 | 0 | 0 | 28 | 0 | 0 | 119 | 148 |
| test_primetest | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 14 | 14 |
| test_generate | 15 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 99 | 115 |
| test_factor_ | 10 | 0 | 0 | 0 | 0 | 0 | 55 | 0 | 0 | 187 | 252 |
| **total** | **3394** | **0** | **0** | **0** | **23** | **691** | **1473** | **49** | **24** | **7891** | **13545** |

## TRANSLATION-UNCERTAIN (49)

- **sympy/test_limits/test_basic1/17** `limit(1/sqrt(x),x,0,-1)` -> `undef` (expected `-oo*I`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_limits/test_branch_cuts/18** `limit(acot(x + i/2),x,0,-1)` -> `pi/2-atan(i/2)` (expected `-I*acoth(1/2)`) WRONG?  [acot branch convention (ours: range (0,pi)); complex-valued limit (branch cuts)]
- **sympy/test_limits/test_branch_cuts/20** `limit(acot(x - i/2),x,0,-1)` -> `atan(i/2)+pi/2` (expected `-pi + I*acoth(1/2)`) WRONG?  [acot branch convention (ours: range (0,pi)); complex-valued limit (branch cuts)]
- **sympy/test_limits/test_branch_cuts/22** `limit(acot(x),x,0,-1)` -> `pi/2` (expected `-pi/2`) WRONG?  [acot branch convention (ours: range (0,pi))]
- **sympy/test_limits/test_issue_10868/3** `limit(acosh(1/(x)) + ln(x),x,inf)` -> `undef` (expected `oo`) WRONG?  [complex infinity vs oo convention]
- **sympy/test_limits/test_issue_22220/5** `limit(-ln(-i - x) + ln(x - i),x,pi,1)` -> `ln(pi-i)-ln(-i-pi)` (expected `2*I*pi`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_limits/test_issue_22220/6** `limit(-ln(-i - x) + ln(x - i),x,pi,-1)` -> `ln(pi-i)-ln(-i-pi)` (expected `0`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_limits/test_issue_23752/1** `limit(sqrt(-3 + x - i*x^2),x,0,1)` -> `sqrt(3)*i` (expected `-sqrt(3)*I`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_limits/test_issue_23752/2** `limit(sqrt(-3 + x - i*x^2),x,0,-1)` -> `sqrt(3)*i` (expected `-sqrt(3)*I`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_gruntz/test_gruntz_I/1** `limit(i*x,x,inf)` -> `undef` (expected `oo*I`) WRONG?  [complex-valued limit (branch cuts)]
- **sympy/test_solvers/test_solve_transcendental/3** `solve(-y + cos(x)=0,x)` -> `[acos(y),-acos(y)]` (expected `[-acos(y) + 2*pi, acos(y)]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/4** `solve(-y + 2*cos(x)=0,x)` -> `[acos(y/2),-acos(y/2)]` (expected `[-acos(y/2) + 2*pi, acos(y/2)]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/5** `solve(cos(x)=sin(x),x)` -> `[-2*atan(sqrt(2)+1),2*atan(sqrt(2)-1)]` (expected `[pi/4]`) WRONG? 1 unexpected / 0 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/19** `solve(-y + z*cos(x)=0,x)` -> `[acos(y/z),-acos(y/z)]` (expected `[-acos(y/z) + 2*pi, acos(y/z)]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/20** `solve(-y + z*cos(2*x)=0,x)` -> `[-acos(y/z)/2,acos(y/z)/2]` (expected `[pi - acos(y/z)/2, acos(y/z)/2]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/21** `csolve(-y + z*cos(sin(x))=0,x)` -> `[asin(acos(y/z)),-asin(acos(y/z)),asin(acos(y/z))+pi,pi-asin(acos(y/z))]` (expected `[pi - asin(acos(y/z)), asin(acos(y/z) - 2*pi) + pi, -asin(acos(y/z) - 2*pi), asin(acos(y/z))]`) WRONG? 2 unexpected / 2 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/22** `solve(z*cos(x)=0,x)` -> `[-pi/2,pi/2]` (expected `[pi/2, 3*pi/2]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/41** `solve(-1/2 + cos(1000*x)=0,x)` -> `[-pi/3000,pi/3000]` (expected `[pi/3000, pi/600]`) WRONG? 1 unexpected / 1 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/42** `solve(-1 + cos(x - 1000*y)=0,x)` -> `[1000*y]` (expected `[1000*y, 1000*y + 2*pi]`) WRONG? missing 1 solution(s) [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_solve_transcendental/43** `solve(-1 + cos(x + y + z)=0,x)` -> `[-y-z]` (expected `[-y - z, -y - z + 2*pi]`) WRONG? missing 1 solution(s) [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_issue_4671_4463_4467/3** `solve((1 + a^2)*(cos(a*x) + sin(a*x))=0,x)` -> `[-2*atan(sqrt(2)-1)/a,2*atan(sqrt(2)+1)/a]` (expected `[-pi/(4*a)]`) WRONG? 1 unexpected / 0 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_unrad_fail/1** `solve(1 + (x^3 - 3*x^2)^(1/3) - x=0,x)` -> `[]` (expected `[1/3]`) WRONG? missing 1 solution(s) [real vs principal cube root convention]
- **sympy/test_solvers/test_rewrite_trig/2** `csolve(sec(x) + sin(x)=0,x)` -> `[]` (expected `[-2*atan(-1/2 + sqrt(2)*sqrt(1 - sqrt(3)*I)/2 + sqrt(3)*I/2), 2*atan(1/2 - sqrt(2)*sqrt(1 + sqrt(3)*I)/2 + sqrt(3)*I/2), 2*atan(1/2 + sqrt(2)*sqrt(1 + sqrt(3)*I)/2 + sqrt(3)*I/2), 2*atan(1/2 - sqrt(3)*I/2 + sqrt(2)*sqrt(1 - sqrt(3)*I)/2)]`) WRONG? missing 4 solution(s) [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_rewrite_trig/4** `solve(-cos(x) + 2*sin(x)=0,x)` -> `[-2*atan(sqrt(5)+2),2*atan(sqrt(5)-2)]` (expected `[atan(1/2)]`) WRONG? 1 unexpected / 0 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_rewrite_trigh/1** `csolve((1/cosh(x)) + sinh(x)=0,x)` -> `[-0.721817737589]` (expected `[-2*atanh(-sqrt(5)/2 + 1/2 + sqrt(2 - 2*sqrt(5))/2), 2*atanh(-1/2 + sqrt(5)/2 + sqrt(2 - 2*sqrt(5))/2), -2*atanh(-sqrt(2 + 2*sqrt(5))/2 + 1/2 + sqrt(5)/2), -2*atanh(1/2 + sqrt(5)/2 + sqrt(2 + 2*sqrt(5))/2)]`) WRONG? missing 3 solution(s) [periodic in the complex plane: solution representatives may differ]
- **sympy/test_solvers/test_issue_19113_19102/1** `csolve(-2*sin(x)^3 + 4*cos(x)^3=0,x)` -> `[-2.24168430551,0.899908348082,2*atan(0.378574702309*i-0.486535877596),-2*atan(0.486535877596+0.378574702309*i),-2*atan(0.996154934689*i-1.28023640358),2*atan(1.28023640358+0.996154934689*i)]` (expected `[atan(2^(1/3)), -atan(2^(1/3)*(1 - sqrt(3)*I)/2), -atan(2^(1/3)*(1 + sqrt(3)*I)/2)]`) WRONG? 3 unexpected / 0 missing [periodic equation: solution representatives may differ]
- **sympy/test_solvers/test_issue_19113_19102/3** `solve(-sin(x) + 3*cos(x)=0,x)` -> `[-2*atan(sqrt(10)/3+1/3),2*atan(sqrt(10)/3-1/3)]` (expected `[atan(3)]`) WRONG? 1 unexpected / 0 missing [periodic equation: solution representatives may differ]
- **sympy/test_solveset/test_issue_9778/3** `solve(y + x^3=0,x)` -> `[(-y)^(1/3)]` (expected `[-Abs(y)^(1/3)*sign(y)]`) WRONG? 1 unexpected / 1 missing [real vs principal cube root convention]
- **sympy/test_simplify/test_issue_22020/1** `simplify(-inf + pi*i/2)` -> `-inf` (expected `-oo + I*pi/2`) WRONG?  [infinity arithmetic convention]
- **sympy/test_polytools/test_gcd/23** `gcd(-1.0 + 1.0*x^2,-1.0 + 1.0*x)` -> `1` (expected `1.0*x - 1.0`) WRONG?  [gcd/lcm content convention (rational or float coefficients)]
- **sympy/test_polytools/test_gcd/24** `lcm(-1.0 + 1.0*x^2,-1.0 + 1.0*x)` -> `x^3-x^2-x+1.0` (expected `1.0*x^2 - 1.0`) WRONG?  [gcd/lcm content convention (rational or float coefficients)]
- **sympy/test_polytools/test_gcd/26** `gcd(-1.0 + 1.0*x^2,-1.0 + 1.0*x)` -> `1` (expected `1.0*x - 1.0`) WRONG?  [gcd/lcm content convention (rational or float coefficients)]
- **sympy/test_polytools/test_gcd/27** `lcm(-1.0 + 1.0*x^2,-1.0 + 1.0*x)` -> `x^3-x^2-x+1.0` (expected `1.0*x^2 - 1.0`) WRONG?  [gcd/lcm content convention (rational or float coefficients)]
- **sympy/test_polytools/test_gcd_numbers_vs_polys/4** `gcd(3*x/2,9/4)` -> `3/4` (expected `1`) WRONG?  [gcd/lcm content convention (rational or float coefficients)]
- **sympy/test_hyperbolic/test_coth/5** `1/tanh(pi*i)` -> `1/tanh(i*pi)` (expected ``) WRONG? expected undefined (-I * cot(pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/6** `1/tanh(-pi*i)` -> `-1/tanh(i*pi)` (expected ``) WRONG? expected undefined (cot(pi) * I) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/8** `1/tanh(pi*i)` -> `1/tanh(i*pi)` (expected ``) WRONG? expected undefined (-I * cot(pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/9** `1/tanh(-pi*i)` -> `-1/tanh(i*pi)` (expected ``) WRONG? expected undefined (I * cot(pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/10** `1/tanh(2*pi*i)` -> `1/tanh(2*i*pi)` (expected ``) WRONG? expected undefined (-I * cot(2 * pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/11** `1/tanh(-2*pi*i)` -> `-1/tanh(2*i*pi)` (expected ``) WRONG? expected undefined (I * cot(2 * pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/12** `1/tanh(-30000000000000000000000000000000000000000000000000000000000000000000000000*pi*i)` -> `-1/tanh(30000000000000000000000000000000000000000000000000000000000000000000000000*i*pi)` (expected ``) WRONG? expected undefined (I * cot(3 * 10 ** 73 * pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/13** `1/tanh(70000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000*pi*i)` -> `1/tanh(70000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000*i*pi)` (expected ``) WRONG? expected undefined (-I * cot(7 * 10 ** 103 * pi)) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/31** `assume(k,integer) ; 1/tanh(pi*i*k)` -> `1/tanh(i*k*pi)` (expected ``) WRONG? expected undefined (-cot(k * pi) * I) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/32** `assume(k,integer) ; 1/tanh(17*pi*i*k)` -> `1/tanh(17*i*k*pi)` (expected ``) WRONG? expected undefined (-cot(17 * k * pi) * I) [SymPy zoo (complex infinity)]
- **sympy/test_hyperbolic/test_coth/33** `assume(k,integer) ; 1/tanh(pi*i*k)` -> `1/tanh(i*k*pi)` (expected ``) WRONG? expected undefined (-cot(k * pi) * I) [SymPy zoo (complex infinity)]
- **sympy/test_trigonometric/test_acot/1** `acot(-inf)` -> `pi` (expected `0`) WRONG?  [acot branch convention (ours: range (0,pi))]
- **sympy/test_trigonometric/test_acot/8** `acot(-sqrt(3)/3)` -> `2*pi/3` (expected `-pi/3`) WRONG?  [acot branch convention (ours: range (0,pi))]
- **sympy/test_trigonometric/test_acot/11** `acot(-2*i)` -> `atan(2*i)+pi/2` (expected `I*acoth(2)`) WRONG?  [acot branch convention (ours: range (0,pi))]
- **sympy/test_trigonometric/test_acot/13** `acot(-tan(1/4))` -> `pi/2+1/4` (expected `1/4 - pi/2`) WRONG?  [acot branch convention (ours: range (0,pi))]

## ERROR (23)

- **sympy/test_solvers/test_unrad1/41** `solve(-2 + x^(1/3) + (1 + x)^(1/4)=0,x)` -> `ERROR 4: out of memory` (expected `[(-sqrt(-2*(-1/16 + sqrt(6913)/16)^(1/3) + 6/(-1/16 + sqrt(6913)/16)^(1/3) + 17/2 + 121/(4*sqrt(-6/(-1/16 + sqrt(6913)/16)^(1/3) + 2*(-1/16 + sqrt(6913)/16)^(1/3) + 17/4)))/2 + sqrt(-6/(-1/16 + sqrt(6913)/16)^(1/3) + 2*(-1/16 + sqrt(6913)/16)^(1/3) + 17/4)/2 + 9/4)^3]`) ERROR 4: out of memory
- **sympy/test_solvers/test_unrad1/43** `csolve(-x + (-45 + sqrt(-39304/27 + (90 - 3*x^3 + x*(-34 + 3*x^2))^2/4) + 3*x^3/2 - x*(-34 + 3*x^2)/2)^(1/3)*(1/2 - i*sqrt(3)/2) + 34/((3/2 - 3*i*sqrt(3)/2)*(-45 + sqrt(-39304/27 + (90 - 3*x^3 + x*(-34 + 3*x^2))^2/4) + 3*x^3/2 - x*(-34 + 3*x^2)/2)^(1/3))=0,x)` -> `ERROR 4: out of memory` (expected `[]`) ERROR 4: out of memory
- **sympy/test_simplify/test_simplify_expr/13** `simplify(y*(-1 + x^2)*(2*x*y/(a*(x^2 - y^2)) - 2*x*y*(-1 + x^2)/(a*(x^2 - y^2)^2))/(a*(x^2 - y^2)) - x*(-1 + y^2)*((-1 + x^2)/(a*(x^2 - y^2)) + 2*y^2*(-1 + x^2)/(a*(x^2 - y^2)^2))/(a*(x^2 - y^2)) + x*sqrt(-1 + x^2 + y^2 - x^2*y^2)*((sqrt((1 - x^2)*(-1 + y^2))*(sqrt(-1 + x^2 + y^2 - x^2*y^2) + x*(x - x*y^2)/sqrt(-1 + x^2 + y^2 - x^2*y^2))*sin(z) + x^2*sqrt((1 - x^2)*(-1 + y^2))*sqrt(-1 + x^2 + y^2 ` -> `ERROR 4: out of memory` (expected `2*x/(a^2*(x^2 - y^2))`) ERROR 4: out of memory
- **sympy/test_polytools/test_factor_large/2** `factor((1 + x^2 + 2*x)^3000 + y^2*(1 + x^2 + 2*x)^3000 + 2*y*(1 + x^2 + 2*x)^3000)` -> `ERROR 4: out of memory` (expected `(x + 1)^6000*(y + 1)^2`) ERROR 4: out of memory
- **sympy/test_complexes/test_Abs/12** `abs(-2.0*x)` -> `ERROR 4: expression too deeply nested` (expected `2.0*Abs(x)`) ERROR 4: expression too deeply nested
- **sympy/test_comb_factorials/test_factorial_Mod/3** `mod(factorial(10000004),10000005)` -> `ERROR 4: factorial argument too large` (expected `0`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_factorial_Mod/4** `mod(factorial(33333332),33333333)` -> `ERROR 4: factorial argument too large` (expected `0`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_factorial_Mod/5** `mod(factorial(1000000008),1000000009)` -> `ERROR 4: factorial argument too large` (expected `1000000008`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_factorial_Mod/6** `mod(factorial(1000000032),1000000033)` -> `ERROR 4: factorial argument too large` (expected `1000000032`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_factorial_Mod/7** `mod(factorial(999999959),1000000009)` -> `ERROR 4: factorial argument too large` (expected `854928834`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_factorial_Mod/8** `mod(factorial(999998233),1000000033)` -> `ERROR 4: factorial argument too large` (expected `905504050`) ERROR 4: factorial argument too large
- **sympy/test_comb_factorials/test_binomial_Mod/1** `mod(binomial(156675,4433),100003)` -> `ERROR 4: out of memory` (expected `28625`) ERROR 4: out of memory
- **sympy/test_comb_factorials/test_binomial_Mod_slow/4** `mod(binomial(123456,43253),100003)` -> `ERROR 4: out of memory` (expected `0`) ERROR 4: out of memory
- **sympy/test_comb_factorials/test_binomial_Mod_slow/9** `mod(binomial(-15733,4458),1000000033)` -> `ERROR 4: out of memory` (expected `459357332`) ERROR 4: out of memory
- **sympy/test_arit/test_Mod_Pow/2** `mod(2^10000000000,3)` -> `ERROR 4: exponent too large` (expected `1`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/4** `mod(33284959323^653440920647673032339831833360231128809762590119869151981883991034268771817893563777774717555477241773043861790009199771567658669721637319114241415393311586981618244931016289976483034849279709789063377372737397333700854872705623251409842043378193061582328505786949620003739291138293481110540397325690117305798489235461804150302504629159526209109229876576624004367838539158485152207487` -> `ERROR 4: exponent too large` (expected `0`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/5** `mod(78789849597^904793758405842302339751965847805693334010290411675875418030355789427373435763817503082109776255706185115107806847944763840444251368842202661277523806419324093838302288198389692330847261436882254363833187822797189461857135662722771810812479186466957665077333766955613562098310623316349444477668620650395605833254480899232946109622709375640168300906968586037520775130955293897179331961` -> `ERROR 4: exponent too large` (expected `698175693`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/9** `mod(2^(2^(2^(2^(2^2)))),59049)` -> `ERROR 4: exponent too large` (expected `18016`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/10** `mod(2^(2^(2^(2^(2^(2^2))))),59049)` -> `ERROR 4: exponent too large` (expected `5137`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/18** `mod((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2))),59049)` -> `ERROR 4: exponent too large` (expected `25708`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/19** `mod(((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2))))^((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2)))),59049)` -> `ERROR 4: exponent too large` (expected `26608`) ERROR 4: exponent too large
- **sympy/test_arit/test_Mod_Pow/20** `mod((((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2))))^((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2)))))^(((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2))))^((((2^2)^(2^2))^((2^2)^(2^2)))^(((2^2)^(2^2))^((2^2)^(2^2))))),59049)` -> `ERROR 4: exponent too large` (expected `1966`) ERROR 4: exponent too large
- **sympy/test_diff/test_speed/1** `diff(x,x,100000000)` -> `ERROR 2: bad derivative order` (expected `0`) ERROR 2: bad derivative order

## UNGRADED (24)

- **sympy/test_simplify/test_factorial_simplify/2** `simplify(factorial(factorial(x)))` -> `x!!` (expected `factorial(factorial(x))`) equivalence undecided
- **sympy/test_powsimp/test_powdenest_polar/3** `simplify((x^(a*b)*y^c)^c)` -> `(x^(a*b)*y^c)^c` (expected `x^(a*b*c)*y^(c^2)`) equivalence undecided
- **sympy/test_polytools/test_sqf/32** `sqrfree((1 + x^2 + 2*x)^20000000000)` -> `(x^2+2*x+1)^20000000000` (expected `(x + 1)^40000000000`) equivalence undecided
- **sympy/test_piecewise/test_piecewise_simplify/15** `simplify(piecewise(x<2,1,(x>1) and (x<3),2,3))` -> `piecewise(x<2,1,x>1 and x<3,2,3)` (expected `Piecewise((1, x < 2), (2, x < 3), (3, True))`) cannot parse our output: cannot determine truth value of Relational: x > 1
- **sympy/test_piecewise/test_piecewise_simplify/16** `simplify(piecewise(x<2,1,(x<=3) and (x>1),2,3))` -> `piecewise(x<2,1,x<=3 and x>1,2,3)` (expected `Piecewise((1, x < 2), (2, x <= 3), (3, True))`) cannot parse our output: cannot determine truth value of Relational: x <= 3
- **sympy/test_piecewise/test_piecewise_simplify/17** `simplify(piecewise(x<2,1,(x>2) and (x<3),2,3))` -> `piecewise(x<2,1,x>2 and x<3,2,3)` (expected `Piecewise((1, x < 2), (2, (x > 2) & (x < 3)), (3, True))`) cannot parse our output: cannot determine truth value of Relational: x > 2
- **sympy/test_piecewise/test_piecewise_simplify/18** `simplify(piecewise(x<2,1,(x>=1) and (x<=3),2,3))` -> `piecewise(x<2,1,x>=1 and x<=3,2,3)` (expected `Piecewise((1, x < 2), (2, x <= 3), (3, True))`) cannot parse our output: cannot determine truth value of Relational: x >= 1
- **sympy/test_piecewise/test_piecewise_simplify/19** `simplify(piecewise(x<1,1,(x>=2) and (x<=3),2,3))` -> `piecewise(x<1,1,x>=2 and x<=3,2,3)` (expected `Piecewise((1, x < 1), (2, (x >= 2) & (x <= 3)), (3, True))`) cannot parse our output: cannot determine truth value of Relational: x >= 2
- **sympy/test_piecewise/test_piecewise_simplify/20** `simplify(piecewise((x<=5) and (x>3),ln(x),x))` -> `piecewise(x<=5 and x>3,ln(x),x)` (expected `Piecewise((log(x), (x <= 5) & (x > 3)), (x, True))`) cannot parse our output: cannot determine truth value of Relational: x <= 5
- **sympy/test_piecewise/test_piecewise_fold_expand/4** `expand(piecewise((x>=0) and (x<1),1 - x,0))` -> `piecewise(x>=0 and x<1,-x+1,0)` (expected `Piecewise((1 - x, (x >= 0) & (x < 1)), (0, True))`) cannot parse our output: cannot determine truth value of Relational: x >= 0
- **sympy/test_piecewise/test_piecewise_interval/3** `diff(piecewise((x>=0) and (x<=1),x,0),x)` -> `piecewise(x>=0 and x<=1,1,0)` (expected `Piecewise((1, (x >= 0) & (x <= 1)), (0, True))`) cannot parse our output: cannot determine truth value of Relational: x >= 0
- **sympy/test_piecewise/test_piecewise_with_DiracDelta/4** `integrate(piecewise(x<2,dirac(-1 + x),0),x)` -> `heaviside(x-1)-heaviside(x-2)*(heaviside(x-1)-1)` (expected `Piecewise((DiracDelta(x - 1), x < 2), (0, True))`) no numeric sample points
- **sympy/test_piecewise/test_issue_22533/1** `integrate(piecewise(x<=0,-1/x,1/x),x)` -> `2*heaviside(x)*inf-ln(x)` (expected `Piecewise((-1/x, x <= 0), (1/x, True))`) no numeric sample points
- **sympy/test_arit/test_Mod/17** `assume(p>0) ; mod(2,3 + p)` -> `mod(2,p+3)` (expected `2`) equivalence undecided
- **sympy/test_arit/test_Mod/18** `assume(p>0) ; mod(-2,3 + p)` -> `mod(-2,p+3)` (expected `p + 1`) equivalence undecided
- **sympy/test_arit/test_Mod/19** `assume(p>0) ; mod(2,-3 - p)` -> `mod(2,-p-3)` (expected `-p - 1`) equivalence undecided
- **sympy/test_arit/test_Mod/20** `assume(p>0) ; mod(-2,-3 - p)` -> `mod(-2,-p-3)` (expected `-2`) equivalence undecided
- **sympy/test_arit/test_Mod/21** `assume(p>0) ; mod(5 + p,3 + p)` -> `mod(p+5,p+3)` (expected `2`) equivalence undecided
- **sympy/test_arit/test_Mod/22** `assume(p>0) ; mod(-5 - p,3 + p)` -> `mod(-p-5,p+3)` (expected `p + 1`) equivalence undecided
- **sympy/test_arit/test_Mod/23** `assume(p>0) ; mod(5 + p,-3 - p)` -> `mod(p+5,-p-3)` (expected `-p - 1`) equivalence undecided
- **sympy/test_arit/test_Mod/24** `assume(p>0) ; mod(-5 - p,-3 - p)` -> `mod(-p-5,-p-3)` (expected `-2`) equivalence undecided
- **sympy/test_arit/test_Mod/31** `mod(1 + mod(1 + x,2),2)` -> `mod(mod(x+1,2)+1,2)` (expected `Mod(x, 2)`) equivalence undecided
- **sympy/test_arit/test_Mod/32** `assume(x1,integer) ; mod((4 + x1)*(mod(2 + x1,4)),4)` -> `mod(mod(x1+2,4)*(x1+4),4)` (expected `Mod(x1*(x1 + 2), 4)`) equivalence undecided
- **sympy/test_arit/test_Mod/33** `assume(x1,integer) ; mod(4*(mod(2 + x1,4)),4)` -> `mod(4*mod(x1+2,4),4)` (expected `0`) equivalence undecided

