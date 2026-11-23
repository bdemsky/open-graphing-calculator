# Wester benchmark baseline: giac

engine: `giac 2.1.0, icas command line`

Graded with `../run_wester.py` grade(); see `../COMPARISON.md`.

| section | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|
| A Boolean logic | 2 | 1 | 3 | 2 | 0 | 0 | 0 | 0 | 5 | 13 |
| B Set theory | 3 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 4 |
| C Numbers | 19 | 0 | 3 | 1 | 0 | 0 | 0 | 0 | 1 | 24 |
| D Numerical analysis | 5 | 1 | 0 | 2 | 0 | 0 | 0 | 0 | 6 | 14 |
| E Statistics | 10 | 0 | 1 | 4 | 0 | 0 | 0 | 0 | 0 | 15 |
| F Combinatorial theory | 2 | 0 | 2 | 4 | 0 | 0 | 0 | 0 | 0 | 8 |
| G Number theory | 9 | 2 | 1 | 8 | 0 | 0 | 0 | 0 | 0 | 20 |
| H Algebra | 27 | 1 | 4 | 0 | 0 | 0 | 0 | 0 | 2 | 34 |
| I Trigonometry | 9 | 1 | 3 | 1 | 0 | 0 | 0 | 0 | 0 | 14 |
| J Special functions | 4 | 0 | 3 | 7 | 5 | 0 | 0 | 0 | 0 | 19 |
| K Complex domain | 13 | 5 | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 24 |
| L Zero equivalence | 4 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 9 |
| M Equations | 29 | 1 | 8 | 0 | 0 | 1 | 0 | 0 | 0 | 39 |
| N Inequalities | 9 | 2 | 5 | 0 | 2 | 0 | 0 | 0 | 0 | 18 |
| O Vector analysis | 6 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 10 |
| P Matrix theory | 34 | 0 | 3 | 7 | 1 | 0 | 1 | 0 | 0 | 46 |
| Q Tensor analysis | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 7 | 0 | 10 | 5 | 1 | 1 | 0 | 0 | 0 | 24 |
| S Products | 3 | 3 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| T Limits | 13 | 0 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 16 |
| U Calculus | 11 | 1 | 0 | 5 | 0 | 0 | 0 | 0 | 2 | 19 |
| V Indefinite integrals | 15 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 18 |
| W Definite integrals | 22 | 5 | 2 | 2 | 2 | 1 | 0 | 0 | 0 | 34 |
| X Series | 18 | 2 | 1 | 3 | 1 | 0 | 0 | 0 | 1 | 26 |
| Y Transforms | 9 | 1 | 2 | 0 | 2 | 0 | 0 | 0 | 0 | 14 |
| Z Difference and differential equations | 14 | 4 | 2 | 3 | 0 | 1 | 0 | 0 | 0 | 24 |
| **total** | **297** | **31** | **67** | **64** | **15** | **4** | **1** | **0** | **18** | **497** |

## PASS (297)

- **A1** true and false => false -- `true and false` -> `false` (expected `false`) 
- **A7** x and 1 > 2 => false -- `x and 1>2` -> `false` (expected `false`) 
- **B1** union of [a,b,b,c,c,c], [d,c,b], [b,e,b] => {a,b,c,d,e} -- `(set[a,b,b,c,c,c] union set[d,c,b] union set[b,e,b])` -> `set[a,b,c,d,exp(1)]` (expected `[a,b,c,d,e]`) 
- **B2** intersection => {b} -- `(set[a,b,b,c,c,c] intersect set[d,c,b] intersect set[b,e,b])` -> `set[b]` (expected `[b]`) 
- **B3** set difference {a,b,c,d,e} - {b} => {a,c,d,e} -- `(set[a,b,c,d,e] minus set[b])` -> `set[a,c,d,exp(1)]` (expected `[a,c,d,e]`) 
- **C1** 50! (bignum) -- `50!` -> `30414093201713378043612608166064768844377641568960512000000000000` (expected `30414093201713378043612608166064768844377641568960512000000000000`) 
- **C2** factor 50! -- `ifactor(50!)` -> `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47` (expected `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47`) 
- **C4** ABC base 16 => 2748 -- `0xABC` -> `2748` (expected `2748`) 
- **C5** 123 base 10 => 234 base 7 -- `revlist(convert(123,base,7))` -> `[2,3,4]` (expected `[2,3,4]`) 
- **C6** 677 base 8 => 1BF base 16 -- `hex(0o677)` -> `"0x1BF"` (expected `"0x1BF"`) 
- **C7** log base 8 of 32768 => 5 -- `logb(32768,8)` -> `5` (expected `5`) 
- **C8** 5^(-1) mod 7 => 3; 5^(-1) mod 6 => 5 -- `[inv((5) % (7)),inv((5) % (6))]` -> `[3 % 7,-1 % 6]` (expected `[3,5]`) Giac prints the symmetric residue: -1 = 5 (mod 6)
- **C9** gcd(1776, 1554, 5698) => 74 -- `gcd(1776,1554,5698)` -> `74` (expected `74`) 
- **C10** 1/2 + 1/3 + ... + 1/10 => 4861/2520 -- `1/2+1/3+1/4+1/5+1/6+1/7+1/8+1/9+1/10` -> `4861/2520` (expected `4861/2520`) 
- **C11** complete decimal expansion of 1/7 => 0.(142857) -- `evalf(1/7,30)` -> `0.142857142857142857142857142857` (expected `0.142857142857142857142857142857`) 
- **C12** (7/11)*(22/7) => 2 -- `7/11*(22/7)` -> `2` (expected `2`) 
- **C14** denest sqrt(2 sqrt(3) + 4) => 1 + sqrt(3) -- `simplify(sqrt(2*sqrt(3)+4))` -> `sqrt(3)+1` (expected `1+sqrt(3)`) 
- **C15** Putnam denesting => 3 + sqrt(2) -- `simplify(sqrt(14+3*sqrt(3+2*sqrt(5-12*sqrt(3-2*sqrt(2))))))` -> `sqrt(2)+3` (expected `3+sqrt(2)`) 
- **C17** rationalize (sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)) => 5 + 2 sqrt(6) -- `simplify((sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)))` -> `2*sqrt(6)+5` (expected `5+2*sqrt(6)`) 
- **C18** sqrt(-2 + sqrt(-5)) sqrt(-2 - sqrt(-5)) => 3 -- `simplify(sqrt(-2+sqrt(-5))*sqrt(-2-sqrt(-5)))` -> `3` (expected `3`) 
- **C19** (90 + 34 sqrt(7))^(1/3) => 3 + sqrt(7) -- `simplify((90+34*sqrt(7))^(1/3))` -> `sqrt(7)+3` (expected `3+sqrt(7)`) 
- **C20** nontrivial way of writing 12 -- `simplify(((135+78*sqrt(3))^(2/3)+3)*sqrt(3)/(135+78*sqrt(3))^(1/3))` -> `12` (expected `12`) 
- **C21** (41 + 29 sqrt(2))^(1/5) => 1 + sqrt(2) -- `simplify((41+29*sqrt(2))^(1/5))` -> `sqrt(2)+1` (expected `1+sqrt(2)`) 
- **C23** cardinal arithmetic 2 inf - 3 => inf -- `2*inf-3` -> `+infinity` (expected `inf`) 
- **D1** 0.0/sqrt(2) => 0.0 -- `0.0/sqrt(2)` -> `0.0` (expected `0`) 
- **D2b** exp(-1000000) in arbitrary precision -- `evalf(exp(-1000000),15)` -> `0.329683147808856e-434294` (expected `3.29683147808856e-434295`) 
- **D3** exp(pi sqrt(163)) to 50 digits (nearly an integer) -- `evalf(exp(pi*sqrt(163)),50)` -> `0.26253741264076874399999999999925007259719818568872e18` (expected `262537412640768743.99999999999925007259719818568888`) 
- **D4** [floor(-5/3), ceil(-5/3)] => [-2, -1] -- `[floor(-5/3),ceil(-5/3)]` -> `[-2,-1]` (expected `[-2,-1]`) 
- **D5** natural cubic spline through (1,1),(2,4),(4,2),(5,3) at 3 => 27/8 -- `subst(spline([1,2,4,5],[1,4,2,3],x,3)[1],x,3)` -> `27/8` (expected `27/8`)  [translation uncertain: spline() returns the list of cubic pieces; the piece for [2,4] is picked by hand and evaluated at 3]
- **E1** mean([3, 7, 11, 5, 19]) => 9 -- `mean([3,7,11,5,19])` -> `9` (expected `9`) 
- **E3** first quartile of [1..8] => 2 or 5/2 -- `quartile1([1,2,3,4,5,6,7,8])` -> `2.0` (expected `[2,5/2]`) 
- **E5** unbiased sample standard deviation of [1..5] => sqrt(5/2) -- `stddevp([1,2,3,4,5])` -> `sqrt(10)/2` (expected `sqrt(5/2)`) 
- **E6** binomial pdf: 12 of 15 at p = 0.75 => 0.22520 -- `binomial(15,12,0.75)` -> `0.225199065171` (expected `0.225199`) 
- **E7** binomial cdf: up to 12 of 15 => 0.76391 -- `binomial_cdf(15,0.75,12)` -> `0.763912188821` (expected `0.763912`) 
- **E8** normal(4.35, 0.59): P(4 <= X <= 5) => 0.5867 -- `normal_cdf(4.35,0.59,4,5)` -> `0.588185984503` (expected `0.588185984502579`) 
- **E9** Student t test of mean 5 => 0.057567 -- `L:=[1,-2,3,-4,5,-6,7,-8,9,10] ; student_cdf(9,-inf,(mean(L)-5)/(stddev(L)*sqrt(10/9)/sqrt(10)))` -> `0.0575666009177` (expected `0.057567`) 
- **E10** same with the normal distribution => 0.040583 -- `L:=[1,-2,3,-4,5,-6,7,-8,9,10] ; normal_cdf(0,1,-inf,(mean(L)-5)/(stddev(L)*sqrt(10/9)/sqrt(10)))` -> `(erf(-21*sqrt(2)/(2*sqrt(145)))+1)/2` (expected `0.040583`) 
- **E11** chi-square independence test (chi2 = 1153/252) => 0.469859 -- `(1-chisquare_cdf(5,1153/252))` -> `0.469859414477` (expected `0.469859`) 
- **E12** linear regression => y = 0.7365 x + 6.964 -- `linear_regression([3.33,3.25,3.92,3.50,4.33,4.92,6.08,7.42,8.33,8.00,9.25,10.75],[8.61,9.40,9.86,9.91,10.53,10.61,10.59,13.28,12.76,13.44,14.27,14.13])` -> `0.736461128916,6.96411839157` (expected `0.7365*x+6.964`) slope and intercept of the least-squares line y = 0.7365 x + 6.964
- **F2** binomial(n, 3) => n (n-1) (n-2)/6 -- `binomial(n,3)` -> `n!/(6*(n-3)!)` (expected `n*(n-1)*(n-2)/6`) 
- **F8** Euler totient(1776) => 576 -- `euler(1776)` -> `576` (expected `576`) 
- **G1** largest 6-digit and smallest 7-digit prime => [999983, 1000003] -- `[prevprime(10^6),nextprime(10^6)]` -> `[999983,1000003]` (expected `[999983,1000003]`) 
- **G2** primitive root of 191 => 19 -- `znprimroot(191)` -> `19 % 191` (expected `19`) 
- **G4** 9 x = 15 mod 21 => x = 4 mod 7 -- `solve(9*x=15 mod 21,x)` -> `list[4,11,18]` (expected `[4,11,18]`) 
- **G5** 7 x = 22 mod 39 => x = 31 mod 39 -- `solve(7*x=22 mod 39,x)` -> `list[31]` (expected `[31]`) 
- **G8** x = 7 mod 9, 13 mod 23, 1 mod 2 => x = 151 mod 414 -- `ichinrem(ichinrem([7,9],[13,23]),[1,2])` -> `[151,414]` (expected `[151,414]`) 
- **G12** Diophantine x^4 + 9 = y^2 => (2, 5) -- `isolve(x^4+9=y^2)` -> `list[[x=-2,y=-5],[x=-2,y=5],[x=0,y=3],[x=0,y=-3],[x=2,y=-5],[x=2,y=5]]` (expected ``) all integer solutions, including (2,5) and the trivial (0,+-3)
- **G16** continued fraction of pi => [3; 7, 15, 1, 292, ...] -- `mid(dfc(evalf(pi,60),5),0,5)` -> `[3,7,15,1,292]` (expected `[3,7,15,1,292]`) 
- **G17** continued fraction of sqrt(23) => [4; 1, 3, 1, 8, ...] -- `mid(dfc(evalf(sqrt(23),60),5),0,5)` -> `[4,1,3,1,8]` (expected `[4,1,3,1,8]`) 
- **G18** continued fraction of the golden ratio => [1; 1, 1, ...] -- `mid(dfc(evalf((1+sqrt(5))/2,60),5),0,5)` -> `[1,[1]]` (expected `[1,1,1,1,1]`) periodic notation [1; (1)] = [1; 1, 1, ...]
- **H1** 2 2^n => 2^(n+1) -- `simplify(2*2^n)` -> `2^(n+1)` (expected `2^(n+1)`) 
- **H4** factor(6 x - 10) => 2 (3 x - 5) -- `factor(6*x-10)` -> `2*(3*x-5)` (expected `2*(3*x-5)`) 
- **H5** univariate gcd(p1, p2) => 1 (Liska) -- `gcd(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81,72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)` -> `1` (expected `1`) 
- **H6** gcd(p1 q, p2 q) => q -- `gcd(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)))` -> `34*x^19-25*x^16+70*x^7+20*x^3-91*x-86` (expected `[34*x^19-25*x^16+70*x^7+20*x^3-91*x-86,-(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)]`) 
- **H7** resultant(p1 q, p2 q) => 0 -- `resultant(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),x)` -> `0` (expected `0`) 
- **H8** factor(expand(p1 p2)) => p1 p2 -- `factor(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)))` -> `-(16*x^60+21*x^47-64*x^34+126*x^8+46*x^5+81)*(72*x^60-83*x^52-22*x^39-25*x^25-19*x^23+54*x^10+81)` (expected `(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)`) 
- **H9** multivariate gcd(p1, p2) => 1 -- `gcd(24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5,34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)` -> `1` (expected `1`) 
- **H10** multivariate gcd(p1 q, p2 q) => q -- `gcd(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)),expand((34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)))` -> `-47*x^17*y^5*z^8-11*x^12*y^7*z^13+23*x^2*y^8*z^10` (expected `[11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8,-(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)]`) 
- **H11** multivariate factor(expand(p1 p2)) => p1 p2 -- `factor(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)))` -> `2*y^7*z*(17*z^12*y*x^5+10*z^6*x^7+6*z^3*y^9*x^9+40*y^7)*(24*z^8*y^19*x-47*z^8*y^5*x^17+6*z^2*y^9*x^15-3*x^22+5)` (expected `-2*z*y^7*(6*x^9*y^9*z^3+10*x^7*z^6+17*y*x^5*z^12+40*y^7)*(3*x^22+47*x^17*y^5*z^8-6*x^15*y^9*z^2-24*x*y^19*z^8-5)`) 
- **H13** resultant with a common factor => 0 -- `resultant(3*x^4+3*x^3+x^2-x-2,x^3-3*x^2+x+5,x)` -> `0` (expected `0`) 
- **H14** (x^2 - 4)/(x^2 + 4x + 4) => (x - 2)/(x + 2) -- `normal((x^2-4)/(x^2+4*x+4))` -> `(x-2)/(x+2)` (expected `(x-2)/(x+2)`) 
- **H15** (e^x - 1)/(e^(x/2) + 1) => e^(x/2) - 1 -- `simplify((exp(x)-1)/(exp(x/2)+1))` -> `exp(1/2*x)-1` (expected `exp(x/2)-1`) 
- **H16** expand (x+1)^20, differentiate, factor => 20 (x+1)^19 -- `factor(diff(expand((x+1)^20),x))` -> `20*(x+1)^19` (expected `20*(x+1)^19`) 
- **H18** factor(x^100 - 1) => 9 cyclotomic factors -- `factor(x^100-1)` -> `(x-1)*(x+1)*(x^2+1)*(x^4-x^3+x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^8-x^6+x^4-x^2+1)*(x^20-x^15+x^10-x^5+1)*(x^20+x^15+x^10+x^5+1)*(x^40-x^30+x^20-x^10+1)` (expected `(x-1)*(x+1)*(x^2+1)*(x^4-x^3+x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^8-x^6+x^4-x^2+1)*(x^20-x^15+x^10-x^5+1)*(x^20+x^15+x^10+x^5+1)*(x^40-x^30+x^20-x^10+1)`) 
- **H19** factor over Q(i): 4x^4+8x^3+77x^2+18x+153 => (2x+3i)(2x-3i)(x+1+4i)(x+1-4i) -- `cfactor(4*x^4+8*x^3+77*x^2+18*x+153)` -> `(x+1+4*i)*(x+1-4*i)*(2*x+3*i)*(2*x-3*i)` (expected `(2*x+3*i)*(2*x-3*i)*(x+1+4*i)*(x+1-4*i)`) 
- **H20** 1/(sqrt2 - 1) with sqrt2^2 = 2 => sqrt2 + 1 -- `simplify(1/(sqrt(2)-1))` -> `sqrt(2)+1` (expected `sqrt(2)+1`) 
- **H21** (x^3 + (sqrt2-2)x^2 - (2sqrt2+3)x - 3sqrt2)/(x^2-2) => (x^2-2x-3)/(x-sqrt2) -- `simplify((x^3+(sqrt(2)-2)*x^2-(2*sqrt(2)+3)*x-3*sqrt(2))/(x^2-2))` -> `(x^2-2*x-3)/(x-sqrt(2))` (expected `(x^2-2*x-3)/(x-sqrt(2))`) 
- **H23** factor(x^4 - 3x^2 + 1) over Q => (x^2-x-1)(x^2+x-1) -- `factor(x^4-3*x^2+1)` -> `(x^2-x-1)*(x^2+x-1)` (expected `(x^2-x-1)*(x^2+x-1)`) 
- **H24** factor x^4 - 3x^2 + 1 mod 5 => (x-2)^2 (x+2)^2 -- `factor((x^4-3*x^2+1) % 5)` -> `((1 % 5)*x+2 % 5)^2*((1 % 5)*x-2 % 5)^2` (expected `(x-2)^2*(x+2)^2`) 
- **H25** factor x^11 + x + 1 mod 65537 -- `factor((x^11+x+1) % 65537)` -> `((1 % 65537)*x^2+(1 % 65537)*x+1 % 65537)*((1 % 65537)*x^9+(-1 % 65537)*x^8+(1 % 65537)*x^6+(-1 % 65537)*x^5+(1 % 65537)*x^3+(-1 % 65537)*x^2+1 % 65537)` (expected `(x^2+x+1)*(x^9-x^8+x^6-x^5+x^3-x^2+1)`) 
- **H26** factor x^4 - 3x^2 + 1 over Q(phi) => 4 linear factors -- `factor(x^4-3*x^2+1,sqrt(5))` -> `(x+(-sqrt(5)-1)/2)*(x+(-sqrt(5)+1)/2)*(x+(sqrt(5)-1)/2)*(x+(sqrt(5)+1)/2)` (expected `(x-(1+sqrt(5))/2)*(x+(1+sqrt(5))/2)*(x-(1+sqrt(5))/2+1)*(x+(1+sqrt(5))/2-1)`) 
- **H27** factor(expand((x - 2y^2 + 3z^3)^20)) -- `factor(expand((x-2*y^2+3*z^3)^20))` -> `(x-2*y^2+3*z^3)^20` (expected `(x-2*y^2+3*z^3)^20`) 
- **H28** factor(expand((sin x - 2 cos^2 y + 3 tan^3 z)^20)) -- `factor(expand((sin(x)-2*cos(y)^2+3*tan(z)^3)^20))` -> `(sin(x)-2*cos(y)^2+3*tan(z)^3)^20` (expected `(sin(x)-2*cos(y)^2+3*tan(z)^3)^20`) 
- **H29** (1-c^2)^5 (1-s^2)^5 (c^2+s^2)^10 with c^2 + s^2 = 1 => c^10 s^10 -- `factor(rem(expand((1-c^2)^5*(1-s^2)^5*(c^2+s^2)^10),s^2+c^2-1,s))` -> `-c^10*(c-1)^5*(c+1)^5` (expected `-c^10*(c-1)^5*(c+1)^5`) 
- **H30** factor 4x^2 - 21xy + 20y^2 mod 3 => (x + y)(x - y) -- `factor((4*x^2-21*x*y+20*y^2) % 3)` -> `((1 % 3)*x+(1 % 3)*y)*((1 % 3)*x+(-1 % 3)*y)` (expected `(x+y)*(x-y)`) 
- **H31** factor x^3 + y^3 over Q(sqrt(-3)) -- `factor(x^3+y^3,sqrt(-3))` -> `(x+y)*(x+((-i)*sqrt(3)-1)/2*y)*(x+(i*sqrt(3)-1)/2*y)` (expected `(x+y)*(x+y*(-1/2-sqrt(3)/2*i))*(x+y*(-1/2+sqrt(3)/2*i))`) 
- **H32** partial fractions of (x^2+2x+3)/(x^3+4x^2+5x+2) -- `partfrac((x^2+2*x+3)/(x^3+4*x^2+5*x+2),x)` -> `2/(x+1)^2-2/(x+1)+3/(x+2)` (expected `3/(x+2)-2/(x+1)+2/(x+1)^2`) 
- **I1** tan(7 pi/10) => -sqrt(1 + 2/sqrt(5)) -- `tan(7*pi/10)` -> `-1/sqrt(5-2*sqrt(5))` (expected `-sqrt(1+2/sqrt(5))`) 
- **I1b** simplify(tan(7 pi/10)) -- `simplify(tan(7*pi/10))` -> `-sqrt(10*sqrt(5)+25)/5` (expected `-sqrt(1+2/sqrt(5))`) 
- **I2** sqrt((1 + cos 6)/2) => -cos 3 -- `simplify(sqrt((1+cos(6))/2))` -> `-cos(3)` (expected `-cos(3)`) 
- **I6** |cos x|, |sin x| for -3 pi < x < -5/2 pi => -cos x, -sin x -- `assume(x>-3*pi and x<-5*pi/2) ; [abs(cos(x)),abs(sin(x))]` -> `[-cos(x),-sin(x)]` (expected `[-cos(x),-sin(x)]`) 
- **I7** cos(3x)/cos(x) => cos(x)^2 - 3 sin(x)^2 or similar -- `normal(texpand(cos(3*x)/cos(x)))` -> `4*cos(x)^2-3` (expected `cos(x)^2-3*sin(x)^2`) 
- **I8** cos(3x)/cos(x) => 2 cos(2x) - 1 -- `tcollect(normal(texpand(cos(3*x)/cos(x))))` -> `-1+2*cos(2*x)` (expected `2*cos(2*x)-1`) 
- **I8b** simplify(cos(3x)/cos(x)) => 2 cos(2x) - 1 or similar -- `simplify(cos(3*x)/cos(x))` -> `4*cos(x)^2-3` (expected `2*cos(2*x)-1`) 
- **I10** trigsimp((tan^2 x + 1 - sec^2 x)/(sin^2 x + cos^2 x - 1)) => undefined (0/0) -- `trigsimplify((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1))` -> `undef` (expected ``) divergence reported as undef
- **I11** limit of the 0/0 expression at 0 => undefined -- `limit((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1),x,0)` -> `undef` (expected ``) divergence reported as undef
- **J1** Bernoulli B_16 => -3617/510 -- `bernoulli(16)` -> `-3617/510` (expected `-3617/510`) 
- **J4** gamma(-1/2) => -2 sqrt(pi) -- `Gamma(-1/2)` -> `-2*sqrt(pi)` (expected `-2*sqrt(pi)`) 
- **J5** psi(1/3) => -gamma - pi/(2 sqrt 3) - 3/2 log 3 -- `Psi(1/3)` -> `-euler_gamma-ln(6)-pi/2/sqrt(3)-2/2*ln(sqrt(3)/2)` (expected `-euler_gamma-pi/(2*sqrt(3))-3*ln(3)/2`) 
- **J12** T_1008 - 2x T_1007 + T_1006 => 0 -- `normal(tchebyshev1(1008,x)-2*x*tchebyshev1(1007,x)+tchebyshev1(1006,x))` -> `0` (expected `0`) 
- **K1** [Re(x + i y), Im(x + i y)] for complex x, y => [Re x - Im y, Im x + Re y] -- `assume(x,complex) ; assume(y,complex) ; [re(x+i*y),im(x+i*y)]` -> `[re(x)-im(y),im(x)+re(y)]` (expected `[re(x)-im(y),im(x)+re(y)]`) 
- **K3** |1/(a + i/a + i b)| for real a, b => 1/sqrt(a^2 + (1/a + b)^2) -- `abs(1/(a+i/a+i*b))` -> `1/sqrt(a^2+(1/a+b)^2)` (expected `1/sqrt(a^2+(1/a+b)^2)`) 
- **K4** log(3 + 4i) in rectangular form => log 5 + i atan(4/3) -- `evalc(ln(3+4*i))` -> `ln(5)+i*atan(4/3)` (expected `ln(5)+i*atan(4/3)`) 
- **K5** tan(x + i y) in rectangular form -- `evalc(tan(x+i*y))` -> `cosh(y)*sin(x)*cosh(y)*cos(x)/((cosh(y)*cos(x))^2+(-sinh(y)*sin(x))^2)-sinh(y)*cos(x)*sinh(y)*sin(x)/((cosh(y)*cos(x))^2+(-sinh(y)*sin(x))^2)+i*(cosh(y)*sin(x)*sinh(y)*sin(x)/((cosh(y)*cos(x))^2+(-sin` (expected `(sin(2*x)+i*sinh(2*y))/(cos(2*x)+cosh(2*y))`) 
- **K7** same for y >= 0 => sqrt(y) -- `assume(y>=0) ; simplify(sqrt(x*y*abs(z)^2)/(sqrt(x)*abs(z)))` -> `sqrt(y)` (expected `sqrt(y)`) 
- **K9** sqrt(1/z) - 1/sqrt(z) => 0 for z > 0 -- `assume(z>0) ; simplify(sqrt(1/z)-1/sqrt(z))` -> `0` (expected `0`) 
- **K12** sqrt(e^z) - e^(z/2) => 0 for real z -- `simplify(sqrt(exp(z))-exp(z/2))` -> `0` (expected `0`) 
- **K15** log(e^z) => z for real z -- `ln(exp(z))` -> `z` (expected `z`) 
- **K18** (x y)^(1/n) - x^(1/n) y^(1/n) => 0 for y > 0 -- `assume(y>0) ; simplify((x*y)^(1/n)-x^(1/n)*y^(1/n))` -> `0` (expected `0`) 
- **K19** (x y)^n - x^n y^n => 0 for integer n -- `assume(n,integer) ; simplify((x*y)^n-x^n*y^n)` -> `0` (expected `0`) 
- **K20** atan(tan(z)) does not simplify to z for real z -- `simplify(atan(tan(z)))` -> `-pi*floor((pi+2*z)/(2*pi))+z` (expected `atan(tan(z))`) 
- **K21** atan(tan(z)) => z for -pi/2 < z < pi/2 -- `assume(z>-pi/2 and z<pi/2) ; simplify(atan(tan(z)))` -> `z` (expected `z`) 
- **K22** atan(tan(10)) => 10 - 3 pi -- `atan(tan(10))` -> `10-3*pi` (expected `10-3*pi`) 
- **L3** (2^(1/3) + 4^(1/3))^3 - 6 (2^(1/3) + 4^(1/3)) - 6 => 0 -- `simplify((2^(1/3)+4^(1/3))^3-6*(2^(1/3)+4^(1/3))-6)` -> `0` (expected `0`) 
- **L4** cos^3 x + cos x sin^2 x - cos x => 0 -- `trigsimplify(cos(x)^3+cos(x)*sin(x)^2-cos(x))` -> `0` (expected `0`) 
- **L7** log((2 sqrt(r) + 1)/sqrt(4r + 4 sqrt(r) + 1)) => 0 -- `simplify(ln((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1)))` -> `0` (expected `0`) 
- **L8** (4r + 4 sqrt r + 1)^(sqrt r/(2 sqrt r + 1)) (2 sqrt r + 1)^(1/(2 sqrt r + 1)) - 2 sqrt r - 1 => 0 -- `simplify((4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))*(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))-2*sqrt(r)-1)` -> `0` (expected `0`) 
- **M1** (x = 2)/2 + (1 = 1) => x/2 + 1 = 2 -- `(x=2)/2+(1=1)` -> `x/2+1=2` (expected `x/2+1=2`) 
- **M3** x^4 + x^3 + x^2 + x + 1 = 0 (exact roots) -- `csolve(x^4+x^3+x^2+x+1=0,x)` -> `list[i/4*sqrt(2*(sqrt(5)+5))+1/4*(sqrt(5)-1),i/4*sqrt(2*(-sqrt(5)+5))-1/4*(sqrt(5)+1),(-i)/4*sqrt(2*(sqrt(5)+5))+1/4*(sqrt(5)-1),(-i)/4*sqrt(2*(-sqrt(5)+5))-1/4*(sqrt(5)+1)]` (expected `[exp(2*pi*i/5),exp(4*pi*i/5),exp(6*pi*i/5),exp(8*pi*i/5)]`) 
- **M5** x^7 - 1 = 0 => 7th roots of unity -- `csolve(x^7-1=0,x)` -> `list[1,exp(i*2*pi/7),exp(i*2*pi*2/7),exp(i*2*pi*3/7),exp(i*2*pi*4/7),exp(i*2*pi*5/7),exp(i*2*pi*6/7)]` (expected `[1,exp(2*pi*i/7),exp(4*pi*i/7),exp(6*pi*i/7),exp(8*pi*i/7),exp(10*pi*i/7),exp(12*pi*i/7)]`) 
- **M6** Liska's degree-8 polynomial => 1 +- sqrt(+-sqrt(+-4 sqrt(3) - 3) - 3)/sqrt(2) -- `csolve(x^8-8*x^7+34*x^6-92*x^5+175*x^4-236*x^3+226*x^2-140*x+46=0,x)` -> `list[-1/2*(i*sqrt(-1/4*(-4*sqrt(3)-3))/(sqrt(sqrt(3)+3)-3/2)+1)*sqrt(2*(sqrt(sqrt(3)+3)-3/2))+1,-1/2*((-i)*sqrt(-1/4*(-4*sqrt(3)-3))/(sqrt(1/4*(4*sqrt(3)+12))-3/2)+1)*sqrt(2*(sqrt(1/4*(4*sqrt(3)+12))-` (expected `[1+sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2)]`) 
- **M7** e^(2x) + 2 e^x + 1 = z => x = log(sqrt z - 1), log(sqrt z + 1) + i pi (+ 2 n pi i) -- `csolve(exp(2*x)+2*exp(x)+1=z,x)` -> `list[ln(sqrt(z)-1),ln(-sqrt(z)-1)]` (expected `[ln(sqrt(z)-1),ln(sqrt(z)+1)+i*pi]`) 
- **M8** exp(2 - x^2) = exp(-x) => real solutions {-1, 2} -- `solve(exp(2-x^2)=exp(-x),x)` -> `list[-1,2]` (expected `[-1,2]`) 
- **M11** (x + 1)(sin^2 x + 1)^2 cos^3(3x) = 0 => -1, pi/6 + n pi/3 (real) -- `solve((x+1)*(sin(x)^2+1)^2*cos(3*x)^3=0,x)` -> `list[-1,-1/6*pi,1/6*pi]` (expected ``) particular solutions
- **M12** sin x = cos x => pi/4 (+ n pi) -- `solve(sin(x)=cos(x),x)` -> `list[-3/4*pi,1/4*pi]` (expected ``) particular solutions
- **M13** tan x = 1 => pi/4 (+ n pi) -- `solve(tan(x)=1,x)` -> `list[1/4*pi]` (expected ``) particular solutions
- **M14** sin x = 1/2 => pi/6, 5 pi/6 (+ 2 n pi) -- `solve(sin(x)=1/2,x)` -> `list[1/6*pi,5/6*pi]` (expected ``) particular solutions
- **M15** sin x = tan x => 0 (+ n pi) -- `solve(sin(x)=tan(x),x)` -> `list[0,pi]` (expected ``) particular solutions
- **M18** (x - 2)/x^(1/3) = 0 => 2 -- `solve((x-2)/x^(1/3)=0,x)` -> `list[2]` (expected `[2]`) 
- **M19** sqrt(x^2 + 1) = x - 2 => no solutions -- `solve(sqrt(x^2+1)=x-2,x)` -> `[]` (expected `[]`) 
- **M20** x + sqrt(x) = 2 => 1 -- `solve(x+sqrt(x)=2,x)` -> `list[1]` (expected `[1]`) 
- **M21** 2 sqrt(x) + 3 x^(1/4) - 2 = 0 => 1/16 -- `solve(2*sqrt(x)+3*x^(1/4)-2=0,x)` -> `list[1/16]` (expected `[1/16]`) 
- **M22** x = 1/sqrt(1 + x^2) => sqrt((sqrt5-1)/2), -i sqrt((sqrt5+1)/2) -- `csolve(x=1/sqrt(1+x^2),x)` -> `list[sqrt(-1/2*(-sqrt(5)+1)),(-i)*sqrt(1/2*(sqrt(5)+1))]` (expected `[sqrt((sqrt(5)-1)/2),-i*sqrt((sqrt(5)+1)/2)]`) 
- **M23** binomial(m, 2) 2^k = 1 => k = 1 - log2(m (m - 1)) -- `solve(binomial(m,2)*2^k=1,k)` -> `list[(ln(2)-ln(m^2-m))/ln(2)]` (expected `[1-ln(m*(m-1))/ln(2)]`) 
- **M24** a b^x = c d^x => x = log(c/a)/log(b/d) -- `solve(a*b^x=c*d^x,x)` -> `list[(-ln(a)+ln(c))/(ln(b)-ln(d))]` (expected `[ln(c/a)/ln(b/d)]`) 
- **M25** sqrt(log x) = log(sqrt x) => {1, e^4} -- `solve(sqrt(ln(x))=ln(sqrt(x)),x)` -> `list[1,exp(4)]` (expected `[1,exp(4)]`) 
- **M26** log(acos(asin(x^(2/3) - b)) - 1) + 2 = 0 => x = (b + sin(cos(e^-2) + 1))^(3/2) -- `solve(ln(acos(asin(x^(2/3)-b))-1)+2=0,x)` -> `list[b*sqrt(sin(cos((exp(2)+1)/exp(2)))+b)+sin(cos((exp(2)+1)/exp(2)))*sqrt(sin(cos((exp(2)+1)/exp(2)))+b)]` (expected `[[(b+sin(cos(exp(-2)+1)))^(3/2)],[-(b+sin(cos(exp(-2)+1)))^(3/2),(b+sin(cos(exp(-2)+1)))^(3/2)],[(b+sin(cos(exp(-2)+1)))^(3/2),-(b+sin(cos(exp(-2)+1)))^(3/2)]]`) 
- **M27** 5x + exp((x - 5)/2) = 8x^3 => {-0.784966, -0.016291, 0.802557, 29.454528} -- `solve(5*x+exp((x-5)/2)=8*x^3,x)` -> `list[-0.784966146486,-0.0162907377299,0.802556701914,29.4545275721]` (expected `[-0.78496614648634752,-0.016290737729935251,0.8025567019143254,29.454527572105314]`) 
- **M28** |x - 1| = 2 => {-1, 3} -- `solve(abs(x-1)=2,x)` -> `list[-1,3]` (expected `[-1,3]`) 
- **M29** |2x + 5| = |x - 2| => {-1, -7} -- `solve(abs(2*x+5)=abs(x-2),x)` -> `list[-7,-1]` (expected `[-1,-7]`) 
- **M33** (1 + i) z + (2 - i) conj(z) = -3 i => z = 2 + 3 i -- `csolve((1+i)*z+(2-i)*conj(z)=-3*i,z)` -> `list[2+3*i]` (expected `[2+3*i]`) 
- **M33b** same with z = x + i y, x, y real => x = 2, y = 3 -- `solve([re((1+i)*(x+i*y)+(2-i)*(x-i*y)+3*i)=0,im((1+i)*(x+i*y)+(2-i)*(x-i*y)+3*i)=0],[x,y])` -> `list[[2,3]]` (expected `[[x=2,y=3]]`) 
- **M34** f(x)^2 + f(x) - 2 = 0 solved for f(x) => {1, -2} -- `solve(f(x)^2+f(x)-2=0,f(x))` -> `list[-2,1]` (expected `[1,-2]`) 
- **M35** 3x3 linear system with a parametric solution => x = 4 - z, y = 2 -- `solve([x+y+z=6,2*x+y+2*z=10,x+3*y+z=10],[x,y,z])` -> `list[[-z+4,2,z]]` (expected ``) 
- **M36** Steinberg: 189 linear equations in 49 unknowns, 3 free -- `solve([-b*k8/a+c*k8/a=0,-b*k11/a+c*k11/a=0,-b*k10/a+c*k10/a+k2=0,-k3-b*k9/a+c*k9/a=0,-b*k14/a+c*k14/a=0,-b*k15/a+c*k15/a=0,-b*k18/a+c*k18/a-k2=0,-b*k17/a+c*k17/a=0,-b*k16/a+c*k16/a+k4=0,-b*k13/a+c*k13/a-b*k21/a+c*k21/a+b*k5/a-c*k5/a=0,b*k44/a-c*k44/a=0,-b*k45/a+c*k45/a=0,-b*k20/a+c*k20/a=0,-b*k44/a+` -> `list[[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,k39,0,0,a*k42/c,0,0,0,0,k39,0,0,b*k42/c,0,0,0,0,k39,0,0,k42,k43,0,0,0,0,0,0]]` (expected ``) 
- **M37b** same, real solutions => (+-1, 1, 1) and (+-0.5332, 0.6172, 2.0655) -- `solve([x^2*y+3*y*z-4=0,-3*x^2*z+2*y^2+1=0,2*y*z^2-z^2-1=0],[x,y,z])` -> `list[[1,1,1],[-1,1,1],[0.533220825491,0.617192842272,2.06554403714],[-0.533220825491,0.617192842272,2.06554403714]]` (expected ``) 
- **N1** e^pi > pi^e => true -- `e^pi>pi^e` -> `true` (expected `true`) 
- **N2** x^4 - x + 1 > 0 for real x => true -- `x^4-x+1>0` -> `true` (expected `true`) 
- **N4** |x| < 1 => -1 < x < 1 (true) -- `assume(abs(x)<1) ; -1<x and x<1` -> `true` (expected `true`) 
- **N7** x > y > 0, k, n > 0 => k x^n > k y^n -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; assume(n>0) ; k*x^n>k*y^n` -> `true` (expected `true`) 
- **N8** x > 1, y >= x - 1 => y > 0 -- `assume(x>1) ; assume(y>=x-1) ; y>0` -> `true` (expected `true`) 
- **N10** |x - 1| > 2 => x < -1 or x > 3 -- `solve(abs(x-1)>2,x)` -> `list[x<-1,x>3]` (expected `x<-1 or x>3`) 
- **N11** (x-1)...(x-5) < 0 expanded => x < 1 or 2 < x < 3 or 4 < x < 5 -- `solve(expand((x-1)*(x-2)*(x-3)*(x-4)*(x-5))<0,x)` -> `list[x<1,((x>2) and (x<3)),((x>4) and (x<5))]` (expected `x<1 or (2<x and x<3) or (4<x and x<5)`) 
- **N12** 6/(x - 3) <= 3 => x < 3 or x >= 5 -- `solve(6/(x-3)<=3,x)` -> `list[x<3,x>=5]` (expected `x<3 or x>=5`) 
- **N13** sqrt(x) < 2 => 0 <= x < 4 -- `solve(sqrt(x)<2,x)` -> `list[((x>=0) and (x<4))]` (expected `0<=x and x<4`) 
- **O1** norm([1 + i, -2, 3 i]) => sqrt(15) -- `norm([1+i,-2,3*i])` -> `sqrt(15)` (expected `sqrt(15)`) 
- **O2** (2, 2, -3) x (1, 3, 1) => (11, -5, 4) -- `cross([2,2,-3],[1,3,1])` -> `[11,-5,4]` (expected `[11,-5,4]`) 
- **O4** curl(x y z, x^2 y^2 z^2, y^2 z^3) -- `curl([x*y*z,x^2*y^2*z^2,y^2*z^3],[x,y,z])` -> `[2*y*z^3-x^2*y^2*2*z,x*y,2*x*y^2*z^2-x*z]` (expected `[2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z]`) 
- **O8** scalar potential of (2xy, x^2 + 1, 6z^2) => x^2 y + y + 2z^3 -- `potential([2*x*y,x^2+1,6*z^2],[x,y,z])` -> `x^2*y+y+2*z^3` (expected `x^2*y+y+2*z^3`) 
- **O9** vector potential of the O4 curl -- `vpotential([2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z],[x,y,z])` -> `[0,x^2*y^2*z^2-1/2*x^2*z,y^2*z^3-1/2*x^2*y]` (expected `[x*y*z,x^2*y^2*z^2,y^2*z^3]`) curl of the result equals the field (checked by hand)
- **O10** Gram-Schmidt of [0,1,2,1], [0,1,3,1], [1,1,1,0], [1,3,6,2] -- `gramschmidt([[0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]])` -> `[[0,1/sqrt(6),2/sqrt(6),1/sqrt(6)],[0,-1/3/(sqrt(3)/3),1/3/(sqrt(3)/3),-1/3/(sqrt(3)/3)],[1/(sqrt(6)/2),1/2/(sqrt(6)/2),0,-1/2/(sqrt(6)/2)],[0,0,0,0]]` (expected `[[0,1,2,1],[0,-1,1,-1],[2,1,0,-1]]`) dependent 4th vector returned as the zero vector (Orthogonalize convention); the others are an orthonormal basis of the span
- **P1** superdiagonal of [[1,2,3],[4,5,6],[7,8,9]] => [2, 6] -- `diag([[1,2,3],[4,5,6],[7,8,9]],1)` -> `[2,6]` (expected `[2,6]`) 
- **P2** (2,3)-minor matrix => [[1, 2], [7, 8]] -- `delcols(delrows([[1,2,3],[4,5,6],[7,8,9]],2-1),3-1)` -> `[[1,2],[7,8]]` (expected `[[1,2],[7,8]]`) 
- **P5** [[7, 11], [3, 8]] mod 2 => [[1, 1], [1, 0]] -- `irem([[7,11],[3,8]],2)` -> `[[1,1],[1,0]]` (expected `[[1,1],[1,0]]`) 
- **P6** d^2/dt^2 [[cos t, sin t], [-sin t, cos t]] -- `diff([[cos(t),sin(t)],[-sin(t),cos(t)]],t,2)` -> `[[-cos(t),-sin(t)],[sin(t),-cos(t)]]` (expected `[[-cos(t),-sin(t)],[sin(t),-cos(t)]]`) 
- **P7** [[x, y]] (a [[1,3,5],[2,4,6]] + [[7,-9,11],[-8,10,-12]]) -- `[[x,y]]*(a*[[1,3,5],[2,4,6]]+[[7,-9,11],[-8,10,-12]])` -> `[[x*(a+7)+y*(2*a-8),x*(3*a-9)+y*(4*a+10),x*(5*a+11)+y*(6*a-12)]]` (expected `[[(a+7)*x+(2*a-8)*y,(3*a-9)*x+(4*a+10)*y,(5*a+11)*x+(6*a-12)*y]]`) 
- **P8** infinity norm of [[1, -2i], [-3i, 4]] => 7 -- `matrix_norm([[1,-2*i],[-3*i,4]],inf)` -> `7` (expected `7`) 
- **P9** Frobenius norm => (a^2 + b^2 + c^2)/(|a| |b| |c|) -- `factor(norm([[a/(b*c),1/c,1/b],[1/c,b/(a*c),1/a],[1/b,1/a,c/(a*b)]]))` -> `(a^2+b^2+c^2)/(abs(c)*abs(b)*abs(a))` (expected `(a^2+b^2+c^2)/(abs(a)*abs(b)*abs(c))`) 
- **P10** Hermitian transpose of [[1, 2+3i], [f(4-5i), 6]] -- `conj(transpose([[1,2+3*i],[f(4-5*i),6]]))` -> `matrix[[1,conj(f(4-5*i))],[2-3*i,6]]` (expected `[[1,f(4+5*i)],[2-3*i,6]]`) conj(f(4-5i)) is the exact Hermitian-transpose entry for an unknown f; Wester's f(4+5i) assumes conj(f(z)) = f(conj(z))
- **P11** inverse of [[a, b], [1, a b]] => 1/(a^2 - 1) [[a, -1], [-1/b, a/b]] -- `inv([[a,b],[1,a*b]])` -> `[[a/(a^2-1),-1/(a^2-1)],[-1/(a^2*b-b),a/(a^2*b-b)]]` (expected `[[a/(a^2-1),-1/(a^2-1)],[-1/(b*(a^2-1)),a/(b*(a^2-1))]]`) 
- **P13** LU decomposition of a symbolic matrix (Wood) -- `[lu([[1,x-2,x-3],[x-1,x^2-3*x+6,x^2-3*x-2],[x-2,x^2-8,2*x^2-12*x+14]])[1],lu([[1,x-2,x-3],[x-1,x^2-3*x+6,x^2-3*x-2],[x-2,x^2-8,2*x^2-12*x+14]])[2],permu2mat(lu([[1,x-2,x-3],[x-1,x^2-3*x+6,x^2-3*x-2],[x-2,x^2-8,2*x^2-12*x+14]])[0])]` -> `[[[1,0,0],[x-2,1,0],[x-1,(x^2-3*x+6+(-x+1)*(x-2))/(x^2-8+(-x+2)*(x-2)),1]],[[1,x-2,x-3],[0,x^2-8+(-x+2)*(x-2),2*x^2-12*x+14+(-x+2)*(x-3)],[0,0,x^2-3*x-2+(-x+1)*(x-3)-(x^2-3*x+6+(-x+1)*(x-2))/(x^2-8+(-` (expected `[[[1,0,0],[x-1,1,0],[x-2,x-3,1]],[[1,x-2,x-3],[0,4,x-5],[0,0,x-7]],[[1,0,0],[0,1,0],[0,0,1]]]`) valid P A = L U with a row exchange (checked with SymPy); entries unsimplified
- **P14** reduced row echelon form (Cullen) -- `rref([[1,2,3,1,3],[3,2,1,1,7],[0,2,4,1,1],[1,1,1,1,4]])` -> `[[1,0,-1,0,2],[0,1,2,0,-1],[0,0,0,1,3],[0,0,0,0,0]]` (expected `[[1,0,-1,0,2],[0,1,2,0,-1],[0,0,0,1,3],[0,0,0,0,0]]`) 
- **P15** rank => 2 -- `rank([[-1,3,7,-5],[4,-2,1,3],[2,4,15,-7]])` -> `2` (expected `2`) 
- **P16** rank of [[2 sqrt 2, 8], [6 sqrt 6, 24 sqrt 3]] => 1 -- `rank([[2*sqrt(2),8],[6*sqrt(6),24*sqrt(3)]])` -> `1` (expected `1`) 
- **P17** rank of a trigonometric matrix => 1 -- `rank([[sin(2*t),cos(2*t)],[2*(1-cos(t)^2)*cos(t),(1-2*sin(t)^2)*sin(t)]])` -> `1` (expected `1`) 
- **P18** null space => span{[2,4,1,0], [0,-3,0,1]} -- `ker([[1,0,-2,0],[-2,1,0,3],[-1,2,-6,6]])` -> `[[-2,-4,-1,0],[0,3,0,-1]]` (expected `[[2,4,1,0],[0,-3,0,1]]`) 
- **P19** factor the 4x4 Vandermonde determinant -- `factor(det([[1,1,1,1],[w,x,y,z],[w^2,x^2,y^2,z^2],[w^3,x^3,y^3,z^3]]))` -> `(y-z)*(x-z)*(x-y)*(w-z)*(w-y)*(w-x)` (expected `(w-x)*(w-y)*(w-z)*(x-y)*(x-z)*(y-z)`) 
- **P21** characteristic polynomial => x^3 - 2x^2 - 5x + 6 -- `charpoly([[5,-3,-7],[-2,1,2],[2,-3,-4]],x)` -> `x^3-2*x^2-5*x+6` (expected `x^3-2*x^2-5*x+6`) 
- **P21b** eigenvalues from it => {1, -2, 3} -- `solve(charpoly([[5,-3,-7],[-2,1,2],[2,-3,-4]],x)=0,x)` -> `list[-2,1,3]` (expected `[1,-2,3]`) 
- **P22** eigenvalues of (2 - a) I_100 => 2 - a -- `eigenvals((2-a)*identity(100))` -> `2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,2-a,` (expected `[2-a]`) 
- **P23** eigenvalues of the 5x5 tridiagonal [1 2 1] matrix -- `eigenvals([[2,1,0,0,0],[1,2,1,0,0],[0,1,2,1,0],[0,0,1,2,1],[0,0,0,1,2]])` -> `3,2,1,sqrt(3)+2,-sqrt(3)+2` (expected `[2-sqrt(3),1,2,3,2+sqrt(3)]`) 
- **P24** exact eigenvalues of the Rosser matrix -- `eigenvals([[611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99]])` -> `0,1020,1000,1000,10*sqrt(10405),-10*sqrt(10405),100*sqrt(26)+510,-100*sqrt(26)+510` (expected `[-10*sqrt(10405),0,510-100*sqrt(26),1000,510+100*sqrt(26),1020,10*sqrt(10405)]`) 
- **P25** numeric eigenvalues of the Rosser matrix -- `eigenvals(evalf([[611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99]]))` -> `-1020.04901843,1.09576555612e-13,0.0980486407217,1000.0,1000.0,1019.90195136,1020.0,1020.04901843` (expected `[-1020.0490184299969,0,0.09804864072151699,1000,1019.9019513592784,1020,1020.0490184299969]`) 
- **P27** eigenvectors of a 5x5 matrix with eigenvalues {a, a, a, 1 - i, 1 + i} -- `seq([jordan([[a,0,0,0,0],[0,0,0,0,1],[0,0,a,0,0],[0,0,0,a,0],[0,-2,0,0,2]])[1][k,k],[col(jordan([[a,0,0,0,0],[0,0,0,0,1],[0,0,a,0,0],[0,0,0,a,0],[0,-2,0,0,2]])[0],k)]],k,0,rowdim([[a,0,0,0,0],[0,0,0,0,1],[0,0,a,0,0],[0,0,0,a,0],[0,-2,0,0,2]])-1)` -> `[[a,[[1,0,0,0,0]]],[1-i,[[0,-1,0,0,-1+i]]],[a,[[0,0,1,0,0]]],[a,[[0,0,0,1,0]]],[1+i,[[0,i,0,0,-1+i]]]]` (expected ``) 
- **P30** Jordan form => diag([[1,1],[0,1]], [[1,1],[0,1]], -1) -- `jordan([[1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2]])[1]` -> `[[1,1,0,0,0],[0,1,0,0,0],[0,0,1,1,0],[0,0,0,1,0],[0,0,0,0,-1]]` (expected `[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]`) Jordan form: blocks [[1,1],[0,1]] twice and -1
- **P32** matrix exponential of [[1, -2], [2, 1]] => e [[cos 2, -sin 2], [sin 2, cos 2]] -- `exp([[1,-2],[2,1]])` -> `[[((-i)*exp(1-2*i)*i+i*exp(1+2*i)*(-i))/2,((-i)*exp(1-2*i)+i*exp(1+2*i))/2],[(exp(1-2*i)*i+exp(1+2*i)*(-i))/2,(exp(1-2*i)+exp(1+2*i))/2]]` (expected `[[e*cos(2),-e*sin(2)],[e*sin(2),e*cos(2)]]`) 
- **P33** matrix exponential exp(M t) (Niles) -- `exp([[0,1,0,0],[0,0,0,2*w],[0,0,0,1],[0,-2*w,3*w^2,0]]*t)` -> `[[1,-3*t*w/w-2*exp(i*t*w)*i/w-2*exp((-i)*t*w)*(-i)/w,(3*t*w*4-2*exp(i*t*w)*(-3*i)-2*exp((-i)*t*w)*3*i)/2,3*t*w*2/(3*t*w^2)-2*exp(i*t*w)/(2*w)-2*exp((-i)*t*w)/(2*w)],[0,-3*w/w+(-2*i)*w*exp(i*t*w)*i/w+2` (expected `[[1,4*sin(w*t)/w-3*t,6*(w*t-sin(w*t)),2/w*(1-cos(w*t))],[0,4*cos(w*t)-3,6*w*(1-cos(w*t)),2*sin(w*t)],[0,-2/w*(1-cos(w*t)),4-3*cos(w*t),sin(w*t)/w],[0,-2*sin(w*t),3*w*sin(w*t),cos(w*t)]]`) 
- **P34** sine of a Jordan matrix -- `sin([[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]])` -> `[[sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-1/2*sin(c)],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)]]` (expected `[[sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-sin(c)/2],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)]]`) 
- **P35** sin(pi/2 [[2,1,1],[2,3,2],[1,1,2]]) => identity -- `sin(pi/2*[[2,1,1],[2,3,2],[1,1,2]])` -> `[[1,0,0],[0,1,0],[0,0,1]]` (expected `[[1,0,0],[0,1,0],[0,0,1]]`) 
- **P36** matrix square root of [[10, 7], [7, 17]] -- `sqrt([[10,7],[7,17]])` -> `[[(7*sqrt(5)-7)*(7/2+2*sqrt(5)/4)*sqrt(5)/70-(-7*sqrt(5)-7)*(7/2-2*sqrt(5)/4)*sqrt(5)/70,(7*sqrt(5)-7)*(7/2+2*sqrt(5)/4)*(sqrt(5)+5)/140+(-7*sqrt(5)-7)*(7/2-2*sqrt(5)/4)*(-sqrt(5)+5)/140],[14*(7/2+2*s` (expected `[[10,7],[7,17]]`) 
- **P37** square root of [[1,1,0],[0,1,0],[0,0,1]] => [[1,1/2,0],[0,1,0],[0,0,1]] -- `sqrt([[1,1,0],[0,1,0],[0,0,1]])` -> `[[1,1/2,0],[0,1,0],[0,0,1]]` (expected `[[1,1,0],[0,1,0],[0,0,1]]`) 
- **P41** Hessian of r^2 sin t -- `hessian(r^2*sin(t),[r,t])` -> `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P43** user-defined Jacobian -- `g(f):=grad(f,[r,t]) ; map([r*cos(t),r*sin(t)],g)` -> `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) 
- **P44** user-defined Hessian -- `g(f):=grad(f,[r,t]) ; map(grad(r^2*sin(t),[r,t]),g)` -> `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P45** user-defined Wronskian -- `g(f):=[f,diff(f,t)] ; transpose(map([cos(t),sin(t)],g))` -> `matrix[[cos(t),sin(t)],[-sin(t),cos(t)]]` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) 
- **R7** sum(k^3, k = 1..n) => n^2 (n+1)^2/4 -- `sum(k^3,k,1,n)` -> `((n+1)^4-2*(n+1)^3+(n+1)^2)/4` (expected `n^2*(n+1)^2/4`) 
- **R8** sum(k^2 binomial(n, k), k = 1..n) => n (n+1) 2^(n-2) -- `sum(k^2*binomial(n,k),k,1,n)` -> `n^2*2^(n-2)-n*2^(n-2)+n*2^(n-1)` (expected `n*(n+1)*2^(n-2)`) 
- **R9** sum(binomial(n, k)/(k + 1), k = 0..n) => (2^(n+1) - 1)/(n + 1) -- `sum(binomial(n,k)/(k+1),k,0,n)` -> `2^(n+1)/(n+1)-1/(n+1)` (expected `(2^(n+1)-1)/(n+1)`) 
- **R13** sum(sin(k x), k = 1..n) => cot(x/2)/2 - cos((2n+1) x/2)/(2 sin(x/2)) -- `sum(sin(k*x),k,1,n)` -> `(-2*cos(1/2*(n+1)*x)*sin(1/2*(n+1)*x)*sin(1/2*x)+2*cos(1/2*x)*sin(1/2*(n+1)*x)^2-cos(1/2*x))/2/sin(1/2*x)-(-2*cos(1/2*x)*sin(1/2*x)*sin(1/2*x)+2*cos(1/2*x)*sin(1/2*x)^2-cos(1/2*x))/2/sin(1/2*x)` (expected `cot(x/2)/2-cos((2*n+1)*x/2)/(2*sin(x/2))`) 
- **R14** sum(sin((2k - 1) x), k = 1..n) => sin(n x)^2/sin x -- `sum(sin((2*k-1)*x),k,1,n)` -> `(64*cos(1/2*(n+1)*x)^4*cos(1/2*x)^4*sin(1/2*x)-64*cos(1/2*(n+1)*x)^4*cos(1/2*x)^2*sin(1/2*x)+8*cos(1/2*(n+1)*x)^4*sin(1/2*x)-64*cos(1/2*(n+1)*x)^3*cos(1/2*x)^5*sin(1/2*(n+1)*x)+96*cos(1/2*(n+1)*x)^3*c` (expected `sin(n*x)^2/sin(x)`) 
- **R16** sum(1/k^2 + 1/k^3, k = 1..inf) => pi^2/6 + zeta(3) -- `sum(1/k^2+1/k^3,k,1,inf)` -> `pi^2/6+1/2*2*Zeta(3)` (expected `pi^2/6+zeta(3)`) 
- **R17** numeric value of R16 => 2.84699 -- `evalf(sum(1/k^2+1/k^3,k,1,inf),15)` -> `2.84699097000782` (expected `2.8469909700078206`) 
- **S2** product(k, k = 1..n) => n! -- `product(k,k,1,n)` -> `n!` (expected `factorial(n)`) 
- **S4** product(1 + 1/k, k = 1..n-1) => n -- `product(1+1/k,k,1,n-1)` -> `n` (expected `n`) 
- **S5** product((2k - 1)/(2k), k = 1..n) => binomial(2n, n)/2^(2n) -- `product((2*k-1)/(2*k),k,1,n)` -> `sqrt(pi)*((2*n-1)/2)!/(pi*n!)` (expected `binomial(2*n,n)/4^n`) 
- **T1** limit((1 + 1/n)^n, n, inf) => e -- `limit((1+1/n)^n,n,inf)` -> `exp(1)` (expected `e`) 
- **T2** limit((1 - cos x)/x^2, x, 0) => 1/2 -- `limit((1-cos(x))/x^2,x,0)` -> `1/2` (expected `1/2`) 
- **T3** limit((3^x + 5^x)^(1/x), x, inf) => 5 (Gruntz) -- `limit((3^x+5^x)^(1/x),x,inf)` -> `5` (expected `5`) 
- **T4** limit(log x/(log x + sin x), x, inf) => 1 -- `limit(ln(x)/(ln(x)+sin(x)),x,inf)` -> `1` (expected `1`) 
- **T5** Gruntz: limit((exp(x e^-x/(e^-x + e^(-2x^2/(x+1)))) - e^x)/x, x, inf) => -e^2 -- `limit((exp(x*exp(-x)/(exp(-x)+exp(-2*x^2/(x+1))))-exp(x))/x,x,inf)` -> `-exp(2)` (expected `-exp(2)`) 
- **T6** Gruntz: x log x log(x e^x - x^2)^2/log(log(x^2 + 2 e^(e^(3 x^3 log x)))) => 1/3 -- `limit(x*ln(x)*ln(x*exp(x)-x^2)^2/ln(ln(x^2+2*exp(exp(3*x^3*ln(x))))),x,inf)` -> `1/3` (expected `1/3`) 
- **T7** limit(n!^(1/n)/n, n, inf) => 1/e -- `limit(n!^(1/n)/n,n,inf)` -> `1/exp(1)` (expected `exp(-1)`) 
- **T8** limit(gamma(n + 1)^(1/n)/n, n, inf) => 1/e -- `limit(Gamma(n+1)^(1/n)/n,n,inf)` -> `1/exp(1)` (expected `exp(-1)`) 
- **T9** limit(gamma(z + a)/gamma(z) exp(-a log z), z, inf) => 1 (a > 0) -- `assume(a>0) ; limit(Gamma(z+a)/Gamma(z)*exp(-a*ln(z)),z,inf)` -> `1` (expected `1`) 
- **T10** same for a < 0 => 1 -- `assume(a<0) ; limit(Gamma(z+a)/Gamma(z)*exp(-a*ln(z)),z,inf)` -> `1` (expected `1`) 
- **T14** limit(x int(exp(-t^2), t = 0..x)/(1 - exp(-x^2)), x, 0) => 1 -- `limit(x*integrate(exp(-t^2),t,0,x)/(1-exp(-x^2)),x,0)` -> `1` (expected `1`) 
- **T15** one-sided limits of x/|x| at 0 => [-1, 1] -- `[limit(x/abs(x),x,0,-1),limit(x/abs(x),x,0,1)]` -> `[-1,1]` (expected `[-1,1]`) 
- **T16** limit(atan(-log x), x, 0, +) => pi/2 -- `limit(atan(-ln(x)),x,0,1)` -> `pi/2` (expected `pi/2`) 
- **U1** d/dx |x| => sign(x) -- `diff(abs(x),x)` -> `abs(x)/x` (expected `sign(x)`) 
- **U2** derivative of the piecewise function (-x for x < 0, x otherwise) -- `diff(piecewise(x<0,-x,x),x)` -> `piecewise(0>x,-1,1)` (expected `piecewise(x<0,-1,1)`) 
- **U2b** same with unit steps: d/dx(-x H(-x) + x H(x)) => sign(x) -- `simplify(diff(-x*Heaviside(-x)+x*Heaviside(x),x))` -> `x*Dirac(-x)+x*Dirac(x)-Heaviside(-x)+Heaviside(x)` (expected `sign(x)`) 
- **U3** f(x) = x^2 - 1 at x = 1, x^3 otherwise: f(1) => 0 -- `f(x):=piecewise(x=1,x^2-1,x^3) ; f(1)` -> `0` (expected `0`) 
- **U3b** same f: f'(1) => 3 -- `f(x):=piecewise(x=1,x^2-1,x^3) ; subst(diff(f(x),x),x,1)` -> `3` (expected `3`) 
- **U5** chain rule: d^2/dt^2 f(g(t)) => f''(g) g'^2 + f'(g) g'' -- `diff(f(g(t)),t,2)` -> `(diff(g(t),t))^2*(f')'(g(t))+diff(g(t),t,2)*f'(g(t))` (expected ``) chain rule: g'^2 f''(g) + g'' f'(g)
- **U6** d/dx int(f(y), y = g(x)..h(x)) => f(h(x)) h'(x) - f(g(x)) g'(x) -- `diff(integrate(f(y),y,g(x),h(x)),x)` -> `diff(h(x),x)*f(h(x))-diff(g(x),x)*f(g(x))` (expected ``) Leibniz rule: h' f(h) - g' f(g)
- **U8** implicit differentiation of y = cos(x y) + x => (1 - y sin(x y))/(1 + x sin(x y)) -- `implicitdiff(y=cos(x*y)+x,y,x)` -> `(-y*sin(x*y)+1)/(x*sin(x*y)+1)` (expected `(1-y*sin(x*y))/(1+x*sin(x*y))`) 
- **U9** f_x + f_y with f(x, y) = g(x^2 + y^2) => 2 (x + y) g'(x^2 + y^2) -- `factor(diff(g(x^2+y^2),x)+diff(g(x^2+y^2),y))` -> `2*g'(x^2+y^2)*(x+y)` (expected ``) 2 (x+y) g'(x^2+y^2)
- **U10** residue((z^3 + 5)/((z^4 - 1)(z + 1)), z, -1) => -9/4 -- `residue((z^3+5)/((z^4-1)*(z+1)),z,-1)` -> `-9/4` (expected `-9/4`) 
- **U13** minimum of x^4 - x + 1 => 1 - 3/8 2^(1/3) -- `minimize(x^4-x+1,x)` -> `[]
(-3*(1/4)^(1/3)+4)/4` (expected `1-3*2^(1/3)/8`) 
- **V1** int |x| dx => x |x|/2 -- `integrate(abs(x),x)` -> `1/2*x^2*sign(x)` (expected `abs(x)`) 
- **V3** int of the piecewise function (-x for x < 0, x otherwise) -- `integrate(piecewise(x<0,-x,x),x)` -> `piecewise(0>x,-1/2*x^2,x^2/2)` (expected `piecewise(x<0,-x,x)`) 
- **V4** int -x H(-x) + x H(x) dx -- `integrate(-x*Heaviside(-x)+x*Heaviside(x),x)` -> `-(piecewise((-x)>0,x^2/2,0))+piecewise(x>0,x^2/2,0)` (expected `-x*heaviside(-x)+x*heaviside(x)`) 
- **V5** int 1/(x^3 + 2) dx -- `integrate(1/(x^3+2),x)` -> `-2^(1/3)/12*ln(x^2-2^(1/3)*x+2^(1/3)*2^(1/3))-2^(1/3)*sqrt(3)/6*atan(-(x-1/2*2^(1/3))/(sqrt(3)/2*2^(1/3)))+2^(1/3)/6*ln(abs(x+2^(1/3)))` (expected `1/(x^3+2)`) 
- **V7** int (3x - 5)^2/(2x - 1)^(7/2) dx -- `integrate((3*x-5)^2/(2*x-1)^(7/2),x)` -> `(-45*(2*x-1)^2+70*(2*x-1)-49)/20/sqrt(2*x-1)/(2*x-1)^2` (expected `(3*x-5)^2/(2*x-1)^(7/2)`) 
- **V8** int 1/(2 e^(m x) - 5 e^(-m x)) dx -- `integrate(1/(2*exp(m*x)-5*exp(-m*x)),x)` -> `1/m/2/sqrt(10)*ln(abs(4*exp(m*x)-2*sqrt(10))/(4*exp(m*x)+2*sqrt(10)))` (expected `1/(2*exp(m*x)-5*exp(-m*x))`) 
- **V9** int sinh^4 x/cosh^2 x dx -- `integrate(sinh(x)^4/cosh(x)^2,x)` -> `exp(x)^2/8+(3*exp(x)^4-14*exp(x)^2-1)/8/(exp(x)^4+exp(x)^2)-3/2*x` (expected `sinh(x)^4/cosh(x)^2`) 
- **V10** int 1/(a + b cos x) dx (a^2 < b^2) -- `integrate(1/(a+b*cos(x)),x)` -> `-2*2/(2*sqrt(a^2-b^2))*(atan((-a*tan(x/2)+b*tan(x/2))/sqrt(a^2-b^2))+pi*sign(2*b-2*a)*floor(x/2/pi+1/2))` (expected `1/(a+b*cos(x))`) derivative checked by central differences
- **V11** int 1/(3 + 3 cos x + 4 sin x) dx => log(3 + 4 tan(x/2))/4 -- `integrate(1/(3+3*cos(x)+4*sin(x)),x)` -> `2/8*ln(abs(4*tan(x/2)+3))` (expected `1/(3+3*cos(x)+4*sin(x))`) 
- **V12** int 1/(4 + 3 cos x + 4 sin x) dx -- `integrate(1/(4+3*cos(x)+4*sin(x)),x)` -> `2*(1/6*ln(abs(tan(x/2)+1))-1/6*ln(abs(tan(x/2)+7)))` (expected `1/(4+3*cos(x)+4*sin(x))`) 
- **V13** int 1/(5 + 3 cos x + 4 sin x) dx => -1/(2 + tan(x/2)) -- `integrate(1/(5+3*cos(x)+4*sin(x)),x)` -> `-2/(2*(tan(x/2)+2))` (expected `1/(5+3*cos(x)+4*sin(x))`) 
- **V14** int 1/(6 + 3 cos x + 4 sin x) dx -- `integrate(1/(6+3*cos(x)+4*sin(x)),x)` -> `2*2/(2*sqrt(11))*(atan((-sqrt(11)*sin(x)+4*cos(x)+3*sin(x)+4)/(sqrt(11)*cos(x)+sqrt(11)-3*cos(x)+4*sin(x)+3))+x/2)` (expected `1/(6+3*cos(x)+4*sin(x))`) 
- **V15** int log|x^2 - a^2| dx -- `integrate(ln(abs(x^2-a^2)),x)` -> `2*(1/2*a*ln(abs(a+x))-1/2*a*ln(abs(-a+x))-x)+x*ln(sign(-a^2+x^2)*(-a^2+x^2))` (expected `ln(abs(x^2-a^2))`) derivative checked by central differences
- **V16** int x acot(x/a) dx -- `integrate(x*acot(x/a),x)` -> `-1/a*(-a*a^2/2/(a/x)^2*atan(a/x)+a^3*(-1/2/a*x-1/2*atan(a/x)))` (expected `x*acot(x/a)`) 
- **V17** int cos(5x) Ci(2x) dx => sin(5x) Ci(2x)/5 - (Si(7x) + Si(3x))/10 -- `integrate(cos(5*x)*Ci(2*x),x)` -> `-1/10*Si(3*x)-1/10*Si(7*x)+sin(5*x)/5*Ci(2*x)` (expected `cos(5*x)*Ci(2*x)`) 
- **W1** int 1/(x - a), x = a-1..a+1 => 0 (principal value) -- `integrate(1/(x-a),x,a-1,a+1)` -> `0` (expected `0`) 
- **W3** int sqrt(x + 1/x - 2), x = 0..1 => 4/3 -- `integrate(sqrt(x+1/x-2),x,0,1)` -> `4/3` (expected `4/3`) 
- **W4** int sqrt(x + 1/x - 2), x = 1..2 => (4 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,1,2)` -> `-2/3*sqrt(2)+4/3` (expected `(4-sqrt(8))/3`) 
- **W5** int sqrt(x + 1/x - 2), x = 0..2 => (8 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,0,2)` -> `1/3*(-2*sqrt(2)+4)+4/3` (expected `(8-sqrt(8))/3`) 
- **W6** int sqrt(2 - 2 cos 2x)/2, x = -3 pi/4..-pi/4 => sqrt 2 (Kahan) -- `integrate(sqrt(2-2*cos(2*x))/2,x,-3*pi/4,-pi/4)` -> `sqrt(2)` (expected `sqrt(2)`) 
- **W7** int cos x/(x^2 + a^2), x = -inf..inf => pi e^(-a)/a (a > 0) -- `assume(a>0) ; integrate(cos(x)/(x^2+a^2),x,-inf,inf)` -> `pi*exp(-a)/a` (expected `pi*exp(-a)/a`) 
- **W10** int 1/(1 + x + x^2 + x^4), -inf..inf -- `integrate(1/(1+x+x^2+x^4),x,-inf,inf)` -> `1.98667745715` (expected `1.98667745715246`) 
- **W11** int sqrt(1 - x^2)/(1 + x^2), x = -1..1 => pi (sqrt 2 - 1) -- `integrate(sqrt(1-x^2)/(1+x^2),x,-1,1)` -> `-1/2*(-sqrt(2)*pi+pi)+1/2*(sqrt(2)*pi-pi)` (expected `pi*(sqrt(2)-1)`) 
- **W12** int x exp(-p x^2 + 2 q x), x = -inf..inf => q/p sqrt(pi/p) e^(q^2/p) -- `assume(p>0) ; integrate(x*exp(-p*x^2+2*q*x),x,-inf,inf)` -> `sqrt(pi/p)*q/p*exp(q^2/p)` (expected `q/p*sqrt(pi/p)*exp(q^2/p)`) 
- **W13** int 1/log t + 1/(1 - t) - log(log(1/t)), t = 0..1 => 2 gamma -- `integrate(1/ln(t)+1/(1-t)-ln(ln(1/t)),t,0,1)` -> `2*euler_gamma` (expected `2*euler_gamma`) 
- **W14** int sin t/t exp(2 i t), t = -inf..inf => 0 -- `integrate(sin(t)/t*exp(2*i*t),t,-inf,inf)` -> `0` (expected `0`) 
- **W16** int (1 + x)^3 P1(x) P2(x), x = -1..1 => 36/35 -- `integrate((1+x)^3*legendre(1,x)*legendre(2,x),x,-1,1)` -> `36/35` (expected `36/35`) 
- **W16b** same with the Legendre polynomials written out -- `integrate((1+x)^3*x*(3*x^2-1)/2,x,-1,1)` -> `36/35` (expected `36/35`) 
- **W24** int(int(x/(x^2 + y^2), y = -inf..inf), x = a..b), a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `b*pi-a*pi` (expected `pi*(b-a)`) 
- **W25** other order, a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,-inf,inf)` -> `-1/2*a*pi+1/2*b*pi-1/2*a*pi+1/2*b*pi` (expected `pi*(b-a)`) 
- **W26** y first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `b*pi+a*pi` (expected `pi*(b+a)`) 
- **W27** x first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,-inf,inf)` -> `1/2*a*pi+1/2*b*pi+1/2*a*pi+1/2*b*pi` (expected `pi*(b+a)`) 
- **W28** y first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `-b*pi+a*pi` (expected `pi*(a-b)`) 
- **W29** x first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,-inf,inf)` -> `1/2*a*pi-1/2*b*pi+1/2*a*pi-1/2*b*pi` (expected `pi*(a-b)`) 
- **W30** int(int(sqrt(x^2 + y^2), x = 0..1), y = 0..1) => (log(sqrt 2 + 1) + sqrt 2)/3 -- `integrate(integrate(sqrt(x^2+y^2),x,0,1),y,0,1)` -> `1/3*(sqrt(2)-ln(sqrt(2)-1))` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) 
- **W32** int(int(|y - x^2|, y = 0..2), x = -1..1) => 46/15 -- `integrate(integrate(abs(y-x^2),y,0,2),x,-1,1)` -> `46/15` (expected `46/15`) 
- **W33** volume of a tetrahedron => a b c/6 -- `integrate(integrate(integrate(1,z,0,c*(1-x/a-y/b)),y,0,b*(1-x/a)),x,0,a)` -> `a*b*c/6` (expected `a*b*c/6`) 
- **X1** Taylor 1/sqrt(1 - (v/c)^2) to O(v^8) -- `series(1/sqrt(1-(v/c)^2),v,0,(8)-1)` -> `1+1/(2*c^2)*v^2+3/(8*c^4)*v^4+5/(16*c^6)*v^6+v^8*order_size(v)` (expected `1+v^2/(2*c^2)+3*v^4/(8*c^4)+5*v^6/(16*c^6)`) 
- **X2** series of 1/s1^2 => 1 - v^2/c^2 -- `s1:=series(1/sqrt(1-(v/c)^2),v,0,(8)-1) ; series(1/s1^2,v,0,(8)-1)` -> `1-1/c^2*v^2+v^8*order_size(v)` (expected `1-v^2/c^2`) 
- **X3** series(sin)/series(cos) => tan series -- `series(series(sin(x),x,0,(8)-1)/series(cos(x),x,0,(8)-1),x,0,(8)-1)` -> `x+1/3*x^3+2/15*x^5+17/315*x^7+x^8*order_size(x)` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X4** tan(x) series to O(x^8) -- `series(tan(x),x,0,(8)-1)` -> `x+1/3*x^3+2/15*x^5+17/315*x^7+x^8*order_size(x)` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X5** log(sin x/x) => -x^2/6 - x^4/180 - x^6/2835 -- `series(ln(sin(x)/x),x,0,(8)-1)` -> `-1/6*x^2-1/180*x^4-1/2835*x^6+x^8*order_size(x)` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X6** log of the series of sin x/x -- `series(ln(series(sin(x)/x,x,0,(8)-1)),x,0,(8)-1)` -> `-1/6*x^2-1/180*x^4-1/2835*x^6+x^8*order_size(x)` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X9** Laurent series of 1/(x (e^x - 1)) -- `series(1/(x*(exp(x)-1)),x,0,(7)-1)` -> `1/x^2-1/2/x+1/12-1/720*x^2+1/30240*x^4-1/1209600*x^6+x^7*order_size(x)` (expected `1/x^2-1/(2*x)+1/12-x^2/720+x^4/30240-x^6/1209600`) 
- **X10** Puiseux series of sqrt(sec x) at 3 pi/2 -- `series(sqrt(sec(x)),x,3*pi/2,5)` -> `(sqrt(x-3*pi/2))^-1+1/12*sqrt(x-3*pi/2)*(x-3*pi/2)+1/160*sqrt(x-3*pi/2)*(x-3*pi/2)^3+sqrt(x-3*pi/2)*(x-3*pi/2)^4*order_size(x-3*pi/2)` (expected `1/sqrt(x-3*pi/2)+(x-3*pi/2)^(3/2)/12+(x-3*pi/2)^(7/2)/160`)  [translation uncertain: for a Puiseux series Giac's order counts the integer powers that multiply sqrt(x-3pi/2); order 5 is needed to keep the (x-3pi/2)^(7/2) term that our order 4 keeps]
- **X11** generalized Taylor series of x^x -- `series(x^x,x,0,(4)-1)` -> `1+ln(x)*x+ln(x)^2/2*x^2+ln(x)^3/6*x^3+x^4*order_size(x)` (expected `1+x*ln(x)+x^2*ln(x)^2/2+x^3*ln(x)^3/6`) 
- **X12** log(sinh z) + log(cosh(z + w)) => log z + log cosh w + tanh(w) z -- `series(ln(sinh(z))+ln(cosh(z+w)),z,0,(2)-1)` -> `ln(cosh(w))+ln(z)+sinh(w)/cosh(w)*z+z^2*order_size(z)` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X13** log(sinh z cosh(z + w)) => same -- `series(ln(sinh(z)*cosh(z+w)),z,0,(2)-1)` -> `ln(z)+ln(cosh(w))+sinh(w)/cosh(w)*z+z^2*order_size(z)` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X15** asymptotic sqrt(2 x^2 + 1) => sqrt(2) x + O(1/x) -- `series(sqrt(2*x^2+1),x,inf,1)` -> `sqrt(2)*(1/x)^-1+sqrt(2)/4/x+1/x*order_size(1/x)` (expected `sqrt(2)*x`) 
- **X18** multivariate Taylor cos(x + y) => 1 - (x + y)^2/2 -- `series(cos(x+y),[x,y],[0,0],(4)-1)` -> `1+(-x^2-2*x*y-y^2)/2` (expected `1-(x+y)^2/2`) 
- **X22** Pade approximation of e^-x => (2 - x)/(2 + x) -- `pade(exp(-x),x,(1)+(1),(1)+1)` -> `(-x+2)/(x+2)` (expected `(2-x)/(2+x)`) 
- **X23** Fourier series of x on [-p, p]: b_n => -2 p (-1)^n/(n pi) -- `assume(p>0) ; assume(n,integer) ; fourier_bn(x,x,2*p,n,-p)` -> `-2*p*(-1)^n/(n*pi)` (expected `-2*p*(-1)^n/(n*pi)`) 
- **X23b** Fourier series of x on [-p, p]: a_n => 0 -- `assume(p>0) ; assume(n,integer) ; fourier_an(x,x,2*p,n,-p)` -> `0` (expected `0`) 
- **X24** Fourier series of |x| on [-p, p]: a_n => 2 p ((-1)^n - 1)/(pi^2 n^2) -- `assume(p>0) ; assume(n,integer) ; fourier_an(abs(x),x,2*p,n,-p)` -> `(2*p*(-1)^n-2*p)/(n^2*pi^2)` (expected `2*p*((-1)^n-1)/(pi^2*n^2)`) 
- **X24b** Fourier series of |x|: constant term => p/2 -- `assume(p>0) ; fourier_an(abs(x),x,2*p,0,-p)` -> `1/2*p` (expected `p/2`) 
- **Y1** laplace(cos((w - 1) t)) => s/(s^2 + (w - 1)^2) -- `laplace(cos((w-1)*t),t,s)` -> `s/(s^2+w^2-2*w+1)` (expected `s/(s^2+(w-1)^2)`) 
- **Y2** inverse Laplace back => cos((w - 1) t) -- `ilaplace(s/(s^2+(w-1)^2),s,t)` -> `cos(abs(w-1)*t)` (expected `cos((w-1)*t)`) 
- **Y3** laplace(sinh(w t) cosh(w t)) => w/(s^2 - 4 w^2) -- `laplace(sinh(w*t)*cosh(w*t),t,s)` -> `w/(s^2-4*w^2)` (expected `w/(s^2-4*w^2)`) 
- **Y5** y'' + y = 4 (H(t - 1) - H(t - 2)), y(0) = 1, y'(0) = 0 -- `desolve([y''+y=4*(Heaviside(x-1)-Heaviside(x-2)),y(0)=1,y'(0)=0],x,y)` -> `cos(x)+(i*exp((-i)*x)*piecewise((x-1)>0,piecewise((x-2)>0,0,(-4*i)*exp(i*x)),piecewise((x-2)>0,4*i*exp(i*x),0))+(-i)*exp(i*x)*piecewise((x-1)>0,piecewise((x-2)>0,0,4*i*exp((-i)*x)),piecewise((x-2)>0,(` (expected `Y2+Y-4*(sp.Heaviside(x-1)-sp.Heaviside(x-2))`) 
- **Y6** inverse Laplace of the transformed solution of Y5 -- `ilaplace(s/(s^2+1)+4*(1/s-s/(s^2+1))*(exp(-s)-exp(-2*s)),s,t)` -> `cos(t)+4*((1-cos(t-1))*Heaviside(t-1)+(-1+cos(t-2))*Heaviside(t-2))` (expected `cos(t)+4*((1-cos(t-1))*heaviside(t-1)-(1-cos(t-2))*heaviside(t-2))`) 
- **Y8** Fourier transform of 1 => sqrt(2 pi) delta(z) -- `fourier(1,x,z)` -> `pi*2*Dirac(z)` (expected `sqrt(2*pi)*dirac(z)`) 2 pi delta(z): correct for Giac's convention (no 1/sqrt(2 pi) factor)
- **Y9** Fourier transform of exp(-9 x^2) -- `fourier(exp(-9*x^2),x,z)` -> `sqrt(pi)/3*exp(-z^2/36)` (expected `exp(-z^2/36)/(3*sqrt(2))`) sqrt(pi)/3 exp(-z^2/36): correct for Giac's convention (no 1/sqrt(2 pi) factor)
- **Y10** Fourier transform of |x| exp(-3 |x|) -- `fourier(abs(x)*exp(-3*abs(x)),x,z)` -> `-2*(z-3)*(z+3)/(z^2+9)^2` (expected `sqrt(2/pi)*(9-z^2)/(9+z^2)^2`) 2 (9-z^2)/(9+z^2)^2: correct for Giac's convention (no 1/sqrt(2 pi) factor)
- **Y13** Z transform of H(t - 3) -- `ztrans(Heaviside(t-3),t,z)` -> `1/(z^3-z^2)` (expected `0`) z^-2/(z-1) = sum over n >= 3 of z^-n
- **Z1** r(n+2) - 2 r(n+1) + r(n) = 2, r(0) = 1, r(1) = m => n^2 + (m - 2) n + 1 -- `rsolve(f(n+2)-2*f(n+1)+f(n)=2,f(n),[f(0)=1,f(1)=m])` -> `[m*n+n^2-2*n+1]` (expected `n^2+(m-2)*n+1`) 
- **Z2** r(n) = 5 r(n-1) - 6 r(n-2), r(0) = 0, r(1) = 1 => 3^n - 2^n -- `rsolve(f(n)=5*f(n-1)-6*f(n-2),f(n),[f(0)=0,f(1)=1])` -> `[-2^n+3^n]` (expected `3^n-2^n`) 
- **Z3** Fibonacci recurrence, r(1) = 1, r(2) = 2 => fib(n + 1) -- `rsolve(f(n)=f(n-1)+f(n-2),f(n),[f(1)=1,f(2)=2])` -> `[(-sqrt(5)+5)/10*((-sqrt(5)+1)/2)^n+(sqrt(5)+5)/10*((sqrt(5)+1)/2)^n]` (expected `fibonacci(n+1)`) 
- **Z5** f'' + 4 f = sin 2t, f(0) = f'(0) = 0 => sin(2t)/8 - t cos(2t)/4 -- `desolve([y''+4*y=sin(2*x),y(0)=0,y'(0)=0],x,y)` -> `1/16*sin(2*x)+(-4*x*cos(2*x)+sin(2*x))/16` (expected `Y2+4*Y-sp.sin(2*x)`) 
- **Z6** same via the Laplace transform: ilaplace(2/(s^2 + 4)^2) -- `ilaplace(2/(s^2+4)^2,s,t)` -> `2*(-t/8*cos(2*t)+1/16*sin(2*t))` (expected `sin(2*t)/8-t*cos(2*t)/4`) 
- **Z7** separable y' = x^2/(y (1 + x)^3) -- `desolve(y'=x^2/(y*(1+x)^3),x,y)` -> `[(x+1)*sqrt(2*ln(x+1)-2*c_0*x^2-4*c_0*x+4*x*ln(x+1)+2*x^2*ln(x+1)+4*x-2*c_0+3)/(x^2+2*x+1),-(x+1)*sqrt(2*ln(x+1)-2*c_0*x^2-4*c_0*x+4*x*ln(x+1)+2*x^2*ln(x+1)+4*x-2*c_0+3)/(x^2+2*x+1)]` (expected `Y1-x**2/(Y*(1+x)**3)`) 
- **Z8** homogeneous y' = y/x + x/y => y^2 = 2 x^2 log|A x| -- `desolve(y'=y/x+x/y,x,y)` -> `[x*sqrt(2)*sqrt(ln(x/c_0)),-x*sqrt(2)*sqrt(ln(x/c_0))]` (expected `Y1-(Y/x+x/Y)`) 
- **Z9** linear x^2 y' + 3 x y = sin(x)/x => y = (A - cos x)/x^3 -- `desolve(x^2*y'+3*x*y=sin(x)/x,x,y)` -> `(c_0-cos(x))/x^3` (expected `x**2*Y1+3*x*Y-sp.sin(x)/x`) 
- **Z13** y' = a y (parametric) => A e^(a x) -- `desolve(y'=a*y,x,y)` -> `c_0*exp(a*x)` (expected `Y1-a*Y`) 
- **Z18** integro-differential equation via Laplace => 5/13 (-8 e^-4t + e^-t (8 cos 2t + sin 2t)) -- `ilaplace(10*s/((s+4)*(s^2+2*s+5)),s,t)` -> `10*(-4/13*exp(-4*t)+4/13*cos(2*t)*exp(-t)+1/26*sin(2*t)*exp(-t))` (expected `5/13*(-8*exp(-4*t)+exp(-t)*(8*cos(2*t)+sin(2*t)))`) 
- **Z19** system u' = u - v, v' = u + v -- `Y:=desolve(y'=[[1,-1],[1,1]]*y,t,y)[0] ; [u=Y[0],v=Y[1]]` -> `[u=(c_0*cos(t)*exp(t)-c_1*exp(t)*sin(t)),v=(c_0*exp(t)*sin(t)+c_1*cos(t)*exp(t))]` (expected `[U.diff(t)-(U-V), V.diff(t)-(U+V)]`) 
- **Z21** 3x3 system with roots 2, 1, 3 -- `Y:=desolve(y'=[[2,0,0],[-2,1,-2],[1,0,3]]*y,t,y)[0] ; [u=Y[0],v=Y[1],w=Y[2]]` -> `[u=(c_0*exp(2*t)),v=(-c_0*exp(3*t)+c_0*exp(t)+c_1*exp(t)-c_2*exp(3*t)+c_2*exp(t)),w=(-c_0*exp(2*t)+c_0*exp(3*t)+c_2*exp(3*t))]` (expected `[U.diff(t)-2*U, V.diff(t)-(-2*U+V-2*W), W.diff(t)-(U+3*W)]`) 
- **Z22** 3x3 system with roots 0, -1 +- sqrt(2) i -- `Y:=desolve(y'=[[0,1,0],[0,0,1],[0,-3,-2]]*y,t,y)[0] ; [u=Y[0],v=Y[1],w=Y[2]]` -> `[u=((3*c_0*sqrt(2)-2*c_1*sqrt(2)*cos(sqrt(2)*t)*exp(-t)+2*c_1*sqrt(2)+c_1*exp(-t)*sin(sqrt(2)*t)-c_2*sqrt(2)*cos(sqrt(2)*t)*exp(-t)+c_2*sqrt(2)-c_2*exp(-t)*sin(sqrt(2)*t))*1/3/sqrt(2)),v=((c_1*sqrt(2)` (expected `[U.diff(t)-V, V.diff(t)-W, W.diff(t)-(-3*V-2*W)]`) 
- **Z23** 3x3 system with the triple root 2 -- `Y:=desolve(y'=[[3,0,-1],[0,2,0],[1,0,1]]*y,t,y)[0] ; [u=Y[0],v=Y[1],w=Y[2]]` -> `[u=(c_0*t*exp(2*t)+c_0*exp(2*t)-c_2*t*exp(2*t)),v=(c_1*exp(2*t)),w=(c_0*t*exp(2*t)-c_2*t*exp(2*t)+c_2*exp(2*t))]` (expected `[U.diff(t)-(3*U-W), V.diff(t)-2*V, W.diff(t)-(U+W)]`) 

## WRONG (31)

- **A4** xor(xor(x, y), y) => x -- `((((x) xor (y))) xor (y))` -> `1` (expected `x`) 
- **D2** exp(-1000000.0) => 3.29683e-434295 (underflow) -- `exp(-1000000.0)` -> `0.0` (expected `3.29683147808856e-434295`) relative error 1
- **G6** x^2 + x + 4 = 0 mod 8 => x = {3, 4} mod 8 -- `solve(x^2+x+4=0 mod 8,x)` -> `[]` (expected `[3,4]`) missing 2 solution(s)
- **G13** Diophantine x^2 + 4 = y^3 => (11, 5) -- `isolve(x^2+4=y^3)` -> `list[]` (expected ``) claims no integer solution; (2,2) and (11,5) exist
- **H12** gcd(2 x^(n+4) - x^(n+2), 4 x^(n+1) + 3 x^n) => x^n -- `gcd(2*x^(n+4)-x^(n+2),4*x^(n+1)+3*x^n)` -> `1` (expected `x^n`) 
- **I12** derivative of the 0/0 expression => undefined -- `diff((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1),x)` -> `((2*(1+tan(x)^2)*tan(x)-2*sin(x)/cos(x)^2/cos(x))*(sin(x)^2+cos(x)^2-1)-(tan(x)^2+1-(1/cos(x))^2)*(2*cos(x)*sin(x)-2*sin(x)*cos(x)))/(sin(x)^2+cos(x)^2-1)^2` (expected ``) expected an error/divergence
- **K6** sqrt(x y |z|^2)/(sqrt(x) |z|) => sqrt(x y)/sqrt(x) (no further in general) -- `simplify(sqrt(x*y*abs(z)^2)/(sqrt(x)*abs(z)))` -> `sqrt(y)` (expected `sqrt(x*y)/sqrt(x)`) 
- **K8** sqrt(1/z) - 1/sqrt(z) does not vanish (z real negative) -- `simplify(sqrt(1/z)-1/sqrt(z))` -> `0` (expected `sqrt(1/z)-1/sqrt(z)`) 
- **K10** sqrt(1/z) + 1/sqrt(z) => 0 for z < 0 -- `assume(z<0) ; simplify(sqrt(1/z)+1/sqrt(z))` -> `2/sqrt(z)` (expected `0`) 
- **K13** sqrt(e^(6 i)) => -e^(3 i) -- `sqrt(exp(6*i))` -> `exp(6*i*1/2)` (expected `-exp(3*i)`) 
- **K23** atan(tan(11 + 30 i)) => 11 - 4 pi + 30 i -- `atan(tan(11+30*i))` -> `11+30*i-(4+9*i)*pi` (expected `11-4*pi+30*i`) 
- **M10** x^x = x => {-1, 1} -- `solve(x^x=x,x)` -> `list[1]` (expected `[-1,1]`) missing 1 solution(s)
- **N14** sin x < 2 => all real x -- `solve(sin(x)<2,x)` -> `list[((x>=0) and (x<(2*pi)))]` (expected `true`) solution sets differ at 142 sample points
- **N15** sin x < 1 => x != pi/2 + 2 n pi -- `solve(sin(x)<1,x)` -> `list[((x>=0) and (x<(pi/2))),((x>(pi/2)) and (x<(2*pi)))]` (expected `sin(x)<1`) solution sets differ at 142 sample points
- **S7** product((k^3 - 1)/(k^3 + 1), k = 2..inf) => 2/3 -- `product((k^3-1)/(k^3+1),k,2,inf)` -> `undef` (expected `2/3`) 
- **S8** product(1 - 1/(2k)^2, k = 1..inf) => 2/pi -- `product(1-1/(2*k)^2,k,1,inf)` -> `undef` (expected `2/pi`) 
- **S10** product((k (k+1) + 1 + i)/(k (k+1) + 1 - i), k = 0..inf) => -1 -- `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` -> `undef` (expected `-1`) 
- **U4** d^n/dx^n x^n => n! (integer n) -- `assume(n,integer) ; diff(x^n,x,n)` -> `n*x^(n-1)*ln(x)+x^(n-1)` (expected `factorial(n)`) 
- **V18** int (f' g - f g')/(f^2 - g^2) dx => log((f - g)/(f + g))/2 -- `integrate((diff(f(x),x)*g(x)-f(x)*diff(g(x),x))/(f(x)^2-g(x)^2),x)` -> `infinity` (expected ``) returns infinity
- **W2** int 1/(x - a)^2, x = a-1..a+1 => divergent -- `integrate(1/(x-a)^2,x,a-1,a+1)` -> `-2` (expected ``) expected an error/divergence
- **W17** int exp(-a x) J0(b x), x = 0..inf => 1/sqrt(a^2 + b^2) -- `assume(a>0) ; integrate(exp(-a*x)*BesselJ(0,b*x),x,0,inf)` -> `undef` (expected `1/sqrt(a^2+b^2)`) 
- **W18** int (J1(x)/x)^2, x = 0..inf => 4/(3 pi) -- `integrate((BesselJ(1,x)/x)^2,x,0,inf)` -> `undef` (expected `4/(3*pi)`) 
- **W19** int Ci(x) J0(2 sqrt(7 x)), x = 0..inf => (cos 7 - 1)/7 -- `integrate(Ci(x)*BesselJ(0,2*sqrt(7*x)),x,0,inf)` -> `undef` (expected `(cos(7)-1)/7`) 
- **W22** int s(t) cos t, t = 0..u for the step s = 1 on [1, 2] -- `integrate(piecewise(t>=1 and t<=2,1,0)*cos(t),t,0,u)` -> `undef` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) 
- **X7** series of f'(a x) a + g(b x) + int(h(c y), y = 0..x) at x = d -- `series(diff(f(a*x),x)+g(b*x)+integrate(h(c*y),y,0,x),x,d,(2)-1)` -> `integrate(h(c*y),y,0,d)+g(b*d)+(b*(function_diff(g))(b*d)+h(c*d))*(x-d)+(x-d)^2*order_size(x-d)` (expected ``) the a f'(a x) term of the input is missing from the series
- **X14** log(x)^a exp(-b x) at x = 1 => (x-1)^a e^-b (1 - (a + 2b)(x - 1)/2) -- `series(ln(x)^a*exp(-b*x),x,1,(2)-1)` -> `exp(-b)*exp(a*ln(x))+(-1/2*a*exp(-b)*exp(a*ln(x))-b*exp(-b)*exp(a*ln(x)))*(x-1)+(x-1)^2*order_size(x-1)` (expected `(x-1)^a*exp(-b)*(1-(a+2*b)*(x-1)/2)`) 
- **Y12** Mellin transform of J3(x)/x^3 => 2^(s-4) gamma(s/2)/gamma(4 - s/2) -- `assume(s>0) ; integrate(BesselJ(3,x)/x^3*x^(s-1),x,0,inf)` -> `undef` (expected `2^(s-4)*gamma(s/2)/gamma(4-s/2)`) 
- **Z10** exact y' = -(1 + 2 x sin y)/(1 + x^2 cos y) => x + x^2 sin y + y = A -- `desolve(y'=-(1+2*x*sin(y))/(1+x^2*cos(y)),x,y)` -> `[]` (expected `Y1+(1+2*x*sp.sin(Y))/(1+x**2*sp.cos(Y))`) no solution returned
- **Z11** nonlinear y'' + y y'^3 = 0 => y^3/6 + A y = x + B -- `desolve(y''+y*y'^3=0,x,y)` -> `[]` (expected `Y2+Y*Y1**3`) no solution returned
- **Z12** same with y(0) = 0, y'(0) = 2 => (3x + sqrt(1 + 9x^2))^(1/3) - 1/(3x + sqrt(1 + 9x^2))^(1/3) -- `desolve([y''+y*y'^3=0,y(0)=0,y'(0)=2],x,y)` -> `[]` (expected `Y2+Y*Y1**3`) no solution returned
- **Z17** discontinuous ODE y'' + sign(t - T) y = 0 -- `desolve(y''+sign(x-q)*y=0,x,y)` -> `c_0*cosh(sqrt(sign(q-x))*x)+c_1/sqrt(sign(q-x))*sinh(sqrt(sign(q-x))*x)` (expected ``) sign(x-q) treated as a constant: the two branches do not match at x = q

## UNEVAL (67)

- **A2** x or (not x) => true -- `x or not(x)` -> `(x or (not(x)))` (expected `true`) no truth value
- **A3** x or y or (x and y) => x or y -- `x or y or (x and y)` -> `(x or y or ((x and y)))` (expected `x or y`) equivalent but not simplified
- **A12** simplify the QEPCAD output => v > 0 and w > abs(ww) -- `simplify(v>0 and 4*w*v>0 and 4*w*(4*w^2*v^2+3*ww^2+w^2)>0 and 64*w^2*v^2*(w^2-ww^2)*(w^2*v^2+ww^2)>0 and 64*w^2*v^2*(w^2-ww^2)^3*(w^2*v^2+ww^2)>0)` -> `((v>0) and ((4*v*w)>0) and ((16*v^2*w^3+4*w^3+12*w*ww^2)>0) and ((64*v^4*w^6-64*v^4*w^4*ww^2+64*v^2*w^4*ww^2-64*v^2*w^2*ww^4)>0) and ((64*v^4*w^10-192*v^4*w^8*ww^2+192*v^4*w^6*ww^4-64*v^4*w^4*ww^6+64*` (expected `v>0 and w>abs(ww)`) conjunction only expanded, not simplified
- **C13** 10/7 (1 + 29/1000)^(1/3) => 3^(1/3) automatically -- `10/7*(1+29/1000)^(1/3)` -> `10/7*(1029/1000)^(1/3)` (expected `3^(1/3)`) equivalent but not in the requested form (SymPy's evaluation of the output did the simplification)
- **C16** Jeffrey-Rich denesting => sqrt(2) + sqrt(3) + sqrt(5) -- `simplify(sqrt(10+2*sqrt(6)+2*sqrt(10)+2*sqrt(15)))` -> `sqrt((2*sqrt(10)*sqrt(6)+4*sqrt(10)+4*sqrt(6)+20)/2)` (expected `sqrt(2)+sqrt(3)+sqrt(5)`) equivalent but not simplified (ops 18 > 8)
- **C22** nasty Axiom example => (log(sqrt(2) + 1) + sqrt(2))/3 -- `simplify(((6-4*sqrt(2))*ln(3-2*sqrt(2))+(3-2*sqrt(2))*ln(17-12*sqrt(2))+32-24*sqrt(2))/(48*sqrt(2)-72))` -> `(8*sqrt(2)-ln(-12*sqrt(2)+17)-2*ln(-2*sqrt(2)+3))/24` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) equivalent but not simplified (ops 17 > 8)
- **E2** median => 7 -- `median([3,7,11,5,19])` -> `7.0` (expected `7`) numeric instead of exact
- **F4** 2^n n! product(2k-1, k=1..n) => (2n)! -- `simplify(2^n*n!*product(2*k-1,k,1,n))` -> `2^(2*n+1)*((2*n-1)/2)!*n!/(2*sqrt(pi))` (expected `factorial(2*n)`) equivalent but not simplified (ops 14 > 2)
- **F5** gamma(n+1/2)/(sqrt(pi) n!) => (2n)!/(2^(2n) (n!)^2) -- `simplify(Gamma(n+1/2)/(sqrt(pi)*n!))` -> `((2*n-1)/2)!/(sqrt(pi)*n!)` (expected `factorial(2*n)/(4^n*factorial(n)^2)`) equivalent but not simplified (ops 9 > 7)
- **G7** x^3 + 2x^2 + 5x + 6 = 0 mod 11 => x = 3 mod 11 -- `solve(x^3+2*x^2+5*x+6=0 mod 11,x)` -> `list[-(rootof([[-1,0],[1,2,5,6]]))]` (expected `[3]`) unevaluated rootof
- **H2** 4 2^n => 2^(n+2) -- `simplify(4*2^n)` -> `4*2^n` (expected `2^(n+2)`) returned unchanged (output = argument of simplify)
- **H3** (-1)^(n (n+1)) => 1 for integer n -- `assume(n,integer) ; (-1)^(n*(n+1))` -> `(-1)^(n*(n+1))` (expected `1`) returned unevaluated (output = input)
- **H17** factor x^3 + x^2 - 7 via its roots, multiply them back together -- `R:=csolve(x^3+x^2-7=0,x) ; u(r):=x-r ; expand(product(map(R,u)))` -> `x^3+1/374*x^2*rootof([[3,0,-5,187,126],[1,0,-2,0,1,0,1295]])+1/374*x^2*rootof([[3,0,-5,-187,126],[1,0,-2,0,1,0,1295]])+1/187*x^2*rootof([[-3,0,5,0,61],[1,0,-2,0,1,0,1295]])+1/139876*x*rootof([[3,0,-5,` (expected `x^3+x^2-7`) unevaluated rootof
- **H22** (cbrt2 + sqrt3)^4 with cbrt2^3 = 2, sqrt3^2 = 3 -- `expand((2^(1/3)+sqrt(3))^4)` -> `18*(2^(1/3))^2+12*2^(1/3)*sqrt(3)+8*sqrt(3)+2*2^(1/3)+9` (expected `2*2^(1/3)+8*sqrt(3)+18*2^(2/3)+12*2^(1/3)*sqrt(3)+9`) equivalent but not simplified (ops 20 > 19)
- **I3** cos(n pi) + sin((4n - 1) pi/2) => (-1)^n - 1 for integer n -- `assume(n,integer) ; cos(n*pi)+sin((4*n-1)/2*pi)` -> `(-1)^n+sin((4*n-1)/2*pi)` (expected `(-1)^n-1`) equivalent but not simplified (ops 8 > 3)
- **I4** cos(cos(n pi) pi) + sin(cos(n pi) pi/2) => (-1)^n - 1 -- `assume(n,integer) ; cos(cos(n*pi)*pi)+sin(cos(n*pi)*pi/2)` -> `cos((-1)^n*pi)+sin((-1)^n*pi/2)` (expected `(-1)^n-1`) equivalent but not simplified (ops 10 > 3)
- **I5** sin((n^5/5 + n^4/2 + n^3/3 - n/30) pi) => 0 for integer n -- `assume(n,integer) ; sin((n^5/5+n^4/2+n^3/3-n/30)*pi)` -> `sin((n^5/5+n^4/2+n^3/3-n/30)*pi)` (expected `0`) returned unevaluated (output = input)
- **J6** J_2(1 + i) => 0.04158 + 0.24740 i -- `evalf(BesselJ(2,1+i))` -> `BesselJ(2,1.0+i)` (expected `0.04157988694396212+0.24739764151330632*i`) BesselJ is not evaluated at a complex argument, even by evalf
- **J9** d/dz J_0(z) => -J_1(z) -- `diff(BesselJ(0,z),z)` -> `diff(BesselJ(0,z),z)` (expected `-besselj(1,z)`) unevaluated diff
- **J16** zeta'(0) => -log(2 pi)/2 -- `subst(diff(Zeta(x),x),x,0)` -> `Zeta(0,1)` (expected `-ln(2*pi)/2`) Zeta(0,1) is Giac's notation for zeta'(0), left unevaluated
- **K2** |3 - sqrt(7) + i sqrt(6 sqrt(7) - 15)| => 1 (Kahan) -- `abs(3-sqrt(7)+i*sqrt(6*sqrt(7)-15))` -> `sqrt(6*sqrt(7)-15+(3-sqrt(7))^2)` (expected `1`) equivalent but not simplified (ops 11 > 0)
- **K16** log(e^(10 i)) => (10 - 4 pi) i -- `ln(exp(10*i))` -> `ln(exp(10*i))` (expected `(10-4*pi)*i`) returned unevaluated (output = input)
- **K24** Kahan: w + sqrt(w+1) sqrt(w-1), w = (z + 1/z)/2 => z (z > 1) -- `simplify((z+1/z)/2+sqrt((z+1/z)/2+1)*sqrt((z+1/z)/2-1))` -> `(z^2+z*abs(z-1)+abs(z-1)+1)/(2*z)` (expected `z`) equivalent but not simplified (ops 11 > 0)
- **L1** sqrt(997) - (997^3)^(1/6) => 0 -- `sqrt(997)-(997^3)^(1/6)` -> `sqrt(997)-991026973^(1/6)` (expected `0`) equivalent but not in the requested form (SymPy's evaluation of the output did the simplification)
- **L2** sqrt(999983) - (999983^3)^(1/6) => 0 -- `sqrt(999983)-(999983^3)^(1/6)` -> `sqrt(999983)-999949000866995087^(1/6)` (expected `0`) equivalent but not in the requested form (SymPy's evaluation of the output did the simplification)
- **L5** log(tan(x/2 + pi/4)) - asinh(tan x) => 0 for -pi/2 < x < pi/2 -- `simplify(ln(tan(x/2+pi/4))-asinh(tan(x)))` -> `ln((4*sqrt(tan(x)^2+1)*cos(1/2*x)^3*sin(1/2*x)-2*sqrt(tan(x)^2+1)*cos(1/2*x)^2-2*sqrt(tan(x)^2+1)*cos(1/2*x)*sin(1/2*x)+sqrt(tan(x)^2+1)+4*cos(1/2*x)^4-4*cos(1/2*x)^2+2*cos(1/2*x)*sin(1/2*x))/(4*cos(1` (expected `0`) equivalent but not in the requested form
- **L6** d/dx of the L5 expression => 0 -- `simplify(diff(ln(tan(x/2+pi/4))-asinh(tan(x)),x))` -> `(-2*sqrt(tan(x)^2+1)*cos(1/2*x)^2+sqrt(tan(x)^2+1)+1)/(2*cos(1/2*x)^2-1)` (expected `0`) equivalent but not in the requested form
- **L9** 2^(1-z) gamma(z) zeta(z) cos(z pi/2) - pi^z zeta(1-z) => 0 -- `simplify(2^(1-z)*Gamma(z)*Zeta(z)*cos(z*pi/2)-pi^z*Zeta(1-z))` -> `-Zeta(-z+1)*pi^z+Zeta(z)*2^(-z+1)*cos(pi*z/2)*(z-1)!` (expected `0`) equivalent but not in the requested form
- **M2** 3x^3 - 18x^2 + 33x - 19 = 0 (three real roots, exact) -- `solve(3*x^3-18*x^2+33*x-19=0,x)` -> `list[-1/486*rootof([[81,0,-405,243,-648],[1,0,-6,0,9,0,-1]]),-1/729*rootof([[-243,0,1215,0,-2430],[1,0,-6,0,9,0,-1]]),-1/486*rootof([[81,0,-405,-243,-648],[1,0,-6,0,9,0,-1]])]` (expected `[2+2/sqrt(3)*cos(pi/18),2+2/sqrt(3)*cos(pi/18-2*pi/3),2+2/sqrt(3)*cos(pi/18+2*pi/3)]`) unevaluated rootof
- **M4** x^6 - 9x^4 - 4x^3 + 27x^2 - 36x - 23 = 0 (Rayes) -- `csolve(x^6-9*x^4-4*x^3+27*x^2-36*x-23=0,x)` -> `list[-0.47212975767400412876023573422764401637,2.9919718574637504582946569487841007175,-2.3620113325163138759110516451449865422+1.0911236359717214035600726141898088813*i,-2.362011332516313875911051645` (expected `[2^(1/3)+sqrt(3),2^(1/3)-sqrt(3),sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3)]`) numeric instead of exact
- **M9** e^x = x => x = -W(-1) -- `csolve(exp(x)=x,x)` -> `list[0.318131505205+1.33723570143*i]` (expected `[-LambertW(-1)]`) one numeric root (the conjugate of -W(-1)); no Lambert W
- **M16** asin x = atan x => 0 -- `solve(asin(x)=atan(x),x)` -> `list[1.31434242222e-07]` (expected `[0]`) numeric root finder: 1.3e-7 instead of the exact root 0
- **M17** acos x = atan x => sqrt((sqrt(5) - 1)/2) -- `solve(acos(x)=atan(x),x)` -> `list[0.786151377757]` (expected `[sqrt((sqrt(5)-1)/2)]`) numeric instead of exact
- **M30** 1 - |x| = max(-x - 2, x - 2) => {-3/2, 3/2} -- `solve(1-abs(x)=max(-x-2,x-2),x)` -> `list[-1.5,1.5]` (expected `[-3/2,3/2]`) numeric instead of exact
- **M31** max(2 - x^2, x) = max(-x, x^3/9) => {-1, 3} -- `solve(max(2-x^2,x)=max(-x,x^3/9),x)` -> `list[-1.0,3.0]` (expected `[-1,3]`) numeric instead of exact
- **M32** max(2 - x^2, x) = x^3/9 => {-3, 3, -1.554894} -- `solve(max(2-x^2,x)=x^3/9,x)` -> `list[-3.0,-1.55489408847,3.0]` (expected `[-3,3,-3*(1+sqrt(3)*sin((atan(sqrt(5)/2)-pi)/3)+cos((atan(sqrt(5)/2)-pi)/3))]`) numeric instead of exact
- **N3** x^4 - x + 1 > 1 for all real x => false -- `x^4-x+1>1` -> `(x^4-x+1)>1` (expected `false`) no truth value
- **N5** x > y > 0 => 2 x^2 > 2 y^2 -- `assume(x>y) ; assume(y>0) ; 2*x^2>2*y^2` -> `(2*x^2)>(2*y^2)` (expected `true`) no truth value
- **N6** x > y > 0, k > 0 => k x^2 > k y^2 -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; k*x^2>k*y^2` -> `(k*x^2)>(k*y^2)` (expected `true`) no truth value
- **N9** x >= y >= z >= x => x = y -- `assume(x>=y) ; assume(y>=z) ; assume(z>=x) ; x=y` -> `x=y` (expected `true`) returned unevaluated (output = input)
- **N18** x + y > 0 and x - y < 0 => |x| < y -- `solve([x+y>0,x-y<0],[x,y])` -> `[[0,1],"Certificate of existence, more solutions may exist"]` (expected `abs(x)<y`) returns one feasible point [0,1] with a "certificate of existence", not the solution set (auto: ERROR [[0,1],"Certificate of existence, more solutions may exist"])
- **P20** minimal polynomial => (lambda - 1)^2 (lambda + 1) -- `pmin([[17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]],x)` -> `x^3-x^2-x+1` (expected `(x-1)^2*(x+1)`) factor count 1, expected 3
- **P31** Smith normal form of [[x^2, x - 1], [x + 1, x^2]] => [[1, 0], [0, x^4 - x^2 + 1]] -- `smith([[x^2,x-1],[x+1,x^2]])[1]` -> `[[1,0],[0,(-x*x+1)*x*x-1]]` (expected `[[1,0],[0,x^4-x^2+1]]`) invariant factor -(x^4-x^2+1) not normalized to monic
- **P39** singular value decomposition of [[1,1],[2,2],[3,3]] -- `svd([[1,1],[2,2],[3,3]])` -> `[[-0.267261241912,0.963624111659,-2.11041391348e-16],[-0.534522483825,-0.148249863332,-0.832050294338],[-0.801783725737,-0.222374794998,0.554700196225]],[5.29150262213,3.77456465917e-16],[[-0.70710678` (expected `[2*sqrt(7),0]`) numeric SVD only: singular values 5.2915 (= 2 sqrt 7) and ~4e-16
- **R3** sum((-1)^k binomial(2n, k)^2, k = 0..2n) => (-1)^n binomial(2n, n) -- `assume(n,integer) ; sum((-1)^k*binomial(2*n,k)^2,k,0,2*n)` -> `sum((-1)^k*((2*n)!/(k!*(2*n-k)!))^2,k,0,2*n)` (expected `(-1)^n*binomial(2*n,n)`) unevaluated sum
- **R5** Dixon's identity => (a+b+c)!/(a! b! c!) -- `sum((-1)^k*binomial(a+b,a+k)*binomial(b+c,b+k)*binomial(c+a,c+k),k,-a,a)` -> `sum((-1)^k*(a+b)!/((a+k)!*(a+b-a-k)!)*(b+c)!/((b+k)!*(b+c-b-k)!)*(c+a)!/((c+k)!*(c+a-c-k)!),k,-a,a)` (expected `factorial(a+b+c)/(factorial(a)*factorial(b)*factorial(c))`) unevaluated sum
- **R6** telescoping sum(g(k+1) - g(k), k = 0..n) => g(n+1) - g(0) -- `sum(g(k+1)-g(k),k,0,n)` -> `sum(g(k+1)-(g(k)),k,0,n)` (expected `g(n+1)-g(0)`) unevaluated sum
- **R10** Vandermonde: sum(binomial(n, k) binomial(m, r - k), k = 0..r) => binomial(n + m, r) -- `sum(binomial(n,k)*binomial(m,r-k),k,0,r)` -> `sum(n!/(k!*(n-k)!)*m!/((r-k)!*(-r+k+m)!),k,0,r)` (expected `binomial(n+m,r)`) unevaluated sum
- **R15** sum(binomial(n - k, k), k = 0..floor(n/2)) => fib(n + 1) -- `sum(binomial(n-k,k),k,0,floor(n/2))` -> `sum((n-k)!/(k!*(n-k-k)!),k,0,floor(n/2))` (expected `fibonacci(n+1)`) unevaluated sum
- **R18** sum(1/(2^k k^2), k = 1..inf) => pi^2/12 - log(2)^2/2 -- `sum(1/(2^k*k^2),k,1,inf)` -> `sum(1/(2^k*k^2),k,1,+infinity)` (expected `pi^2/12-ln(2)^2/2`) unevaluated sum
- **R20** sum(binomial(n, 4k), k = 0..inf) => (2^(n-1) + 2^(n/2) cos(n pi/4))/2 -- `sum(binomial(n,4*k),k,0,inf)` -> `sum(n!/((4*k)!*(n-4*k)!),k,0,+infinity)` (expected `(2^(n-1)+2^(n/2)*cos(n*pi/4))/2`) unevaluated sum
- **R21** sum(1/(sqrt(k (k+1)) (sqrt k + sqrt(k+1))), k = 1..inf) => 1 -- `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` -> `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,+infinity)` (expected `1`) unevaluated sum
- **R22** double sum (capping) => 1/sqrt((1 - x y)^2 - 4 x^2) -- `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` -> `sum(sum(n!/(k!*(n-k)!)*(n-k)!/((n-2*k)!*(n-k-n+2*k)!)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,+infinity)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R24** sum(product(k/(2k - 1), k = 1..m), m = 2..inf) => pi/2 -- `sum(product(k/(2*k-1),k,1,m),m,2,inf)` -> `sum(sqrt(pi)*m!/(2^m*((2*m-1)/2)!),m,2,+infinity)` (expected `pi/2`) unevaluated sum
- **S1** product(gamma(k/3), k = 1..8) => 640 pi^3/(2187 sqrt 3) -- `product(Gamma(k/3),k,1,8)` -> `Gamma(1/3)*2*pi/(Gamma(1/3)*sqrt(3))/3*Gamma(1/3)*2/3*Gamma(2/3)*4/9*Gamma(1/3)*10/9*Gamma(2/3)` (expected `640*pi^3/(2187*sqrt(3))`) equivalent but not simplified (ops 16 > 6)
- **S3** product(x^k, k = 1..n) => x^(n (n+1)/2) -- `product(x^k,k,1,n)` -> `product(x^k,k,1,n)` (expected `x^(n*(n+1)/2)`) unevaluated product
- **S6** product(x^2 - 2x cos(k pi/n) + 1, k = 1..n-1) => (x^(2n) - 1)/(x^2 - 1) -- `product(x^2-2*x*cos(k*pi/n)+1,k,1,n-1)` -> `product(x^2-2*x*cos(k*pi/n)+1,k,1,n-1)` (expected `(x^(2*n)-1)/(x^2-1)`) unevaluated product
- **S9** product(1 + (-1)^(k+1)/(2k - 1), k = 1..inf) => sqrt(2) -- `product(1+(-1)^(k+1)/(2*k-1),k,1,inf)` -> `product(1-(-1)^k/(2*k-1),k,1,+infinity)` (expected `sqrt(2)`) unevaluated product
- **T12** limit(zeta(x) - 1/(x - 1), x, 1) => Euler's gamma -- `limit(Zeta(x)-1/(x-1),x,1)` -> `(2*Eta(1,1)+ln(2)^2)*1/2/ln(2)` (expected `euler_gamma`) correct value expressed through the unevaluated Eta'(1)
- **V6** int 2^x/sqrt(1 + 4^x) dx => asinh(2^x)/log 2 -- `integrate(2^x/sqrt(1+4^x),x)` -> `integrate(2^x/sqrt(4^x+1),x)` (expected `2^x/sqrt(1+4^x)`) unevaluated integrate
- **W8** int t^(a-1)/(1 + t), t = 0..inf => pi/sin(pi a) (0 < a < 1) -- `assume(a>0) ; integrate(t^(a-1)/(1+t),t,0,inf)` -> `integrate(t^(a-1)/(1+t),t,0,+infinity)` (expected `pi/sin(pi*a)`) unevaluated integrate
- **W9** PV int 5x^3/(1 + x + x^2 + x^3 + x^4), -inf..inf => -2 pi (sin(pi/5) + sin(2 pi/5)) -- `integrate(5*x^3/(1+x+x^2+x^3+x^4),x,-inf,inf)` -> `undef` (expected `-2*pi*(sin(pi/5)+sin(2*pi/5))`) improper integral reported as undefined; no principal value (same classification as our CAS)
- **X17** e^x int(e^-t/t, t = x..inf) => 1/x - 1/x^2 + 2/x^3 - 6/x^4 -- `series(exp(x)*integrate(exp(-t)/t,t,x,inf),x,inf,5)` -> `1/x-(1/x)^2+2*(1/x)^3+(1/x)^4*order_size(1/x)` (expected `1/x-1/x^2+2/x^3-6/x^4`) correct terms, but the expansion stops at O(1/x^4) whatever order is requested
- **Y4** laplace(erf(3/sqrt t)) => 1/s - e^(-6 sqrt s)/s -- `laplace(erf(3/sqrt(t)),t,s)` -> `(s*integrate(-6*sqrt(pi)/pi/s*exp(-9/t)*exp(-s*t)/t/2*(sqrt(t))^-1,t,0,+infinity)+1)/s` (expected `1/s-exp(-6*sqrt(s))/s`) unevaluated integrate
- **Y11** Mellin transform of 1/(1 - x) => pi cot(pi s) (PV) -- `assume(s>0) ; integrate(x^(s-1)/(1-x),x,0,inf)` -> `integrate(x^(s-1)/(1-x),x,0,+infinity)` (expected `pi*cot(pi*s)`) unevaluated integrate
- **Z14** BVP y'' + k^2 y = 0, y(0) = 0, y'(1) = 0 => A sin(k x) with cos k = 0 -- `desolve([y''+k^2*y=0,y(0)=0,y'(1)=0],x,y)` -> `0` (expected ``) only the trivial solution; no eigenvalue condition cos(k) = 0
- **Z15** y'' + y'/x + (1 - v^2/x) y/(4x) = 0 => Z_v(sqrt x) -- `desolve(y''+y'/x+(1-v^2/x)*y/(4*x)=0,x,y)` -> `["Unable to solve differential equation"]` (expected `Y2+Y1/x+(1-v**2/x)*Y/(4*x)`) ["Unable to solve differential equation"]

## UNSUPPORTED (64)

- **A5** (w and x) implies (y and z) => not(w and x) or (y and z) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical implication operator)
- **A6** x iff y => (x and y) or not(x or y) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical equivalence operator)
- **B4** Cartesian product {a,b} x {c,d} -- `cartprod([a,b],[c,d])` -> `` (expected `[[a,c],[a,d],[b,c],[b,d]]`) no Giac function cartprod
- **C3** double factorials [10!!, 9!!] => [3840, 945] -- `[10!!,9!!]` -> `Done` (expected `[3840,945]`) no double factorial: 10!! does not parse as one (Giac prints Done) (auto: WRONG )
- **D8** Horner form of sum(a[i] x^i, i=1..5) -- `horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x)` -> `"horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x) 
 Error: Bad Argument Value"` (expected `x*(a1+x*(a2+x*(a3+x*(a4+a5*x))))`) horner(P,x) evaluates P at a point (error for symbolic x); no Horner-form rewrite (auto: ERROR horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x)   Error: Bad Argument Value)
- **D12** interval arithmetic ([-4,2] x + [1,3])^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no interval arithmetic)
- **E4** mode([3, 7, 11, 7, 3, 5, 7]) => 7 -- `mode([3,7,11,7,3,5,7])` -> `` (expected `7`) no Giac function mode
- **E13** multiple linear regression => -16278.7 + 960.925 x1 + 2975.66 x2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no multiple regression in the CAS)
- **E14** L1 (least absolute deviations) regression -- `` -> `` (expected ``) not attempted: no input in problems.txt (no L1 regression)
- **E15** nonlinear least squares fit -- `` -> `` (expected ``) not attempted: no input in problems.txt (no nonlinear regression in the CAS)
- **F1** Pochhammer (a)_3 => a (a+1) (a+2) -- `pochhammer(a,3)` -> `` (expected `a*(a+1)*(a+2)`) no Giac function pochhammer
- **F3** 2^n n! (2n-1)!! => (2n)! -- `simplify(2^n*n!*factorial2(2*n-1))` -> `` (expected `factorial(2*n)`) no Giac function factorial2
- **F6** number of partitions of 4 => 5 -- `partition(4)` -> `` (expected `5`) no Giac function partition
- **F7** Stirling number of the first kind S1(5, 2) => -50 -- `stirling1(5,2)` -> `` (expected `-50`) no Giac function stirling1
- **G3** (a + b)^p mod p => a^p + b^p for p prime -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular polynomial arithmetic with symbolic modulus)
- **G9** linear system mod 7 => x = 1, y = 2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G10** 2x + 3y = 1 mod 5 => 5 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G11** 2x + 3y = 1 mod 6 => 6 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G14** Pythagorean system => (3,4,5,12,13), ... -- `` -> `` (expected ``) not attempted: no input in problems.txt (no Diophantine solver)
- **G15** rational approximation of sqrt(3) within 1/500 => 26/15 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no best rational approximation with tolerance)
- **G19** continued fraction of tanh(1/(2x)) => [0; 2x, 6x, 10x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **G20** continued fraction of sqrt(x^2+1) - x => [0; 2x, 2x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **I9** cos(3x)/cos(x) via user rewrite rules -- `` -> `` (expected ``) not attempted: no input in problems.txt (no user-defined rewrite rules)
- **J2** d/dk E(phi, k) => (E(phi, k) - F(phi, k))/k -- `diff(ellipticE(p,k^2),k)` -> `` (expected `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k`) no Giac function ellipticE
- **J3** d/du dn(u, k) => -k^2 sn(u, k) cn(u, k) -- `diff(jacobidn(u,k),u)` -> `` (expected `0`) no Giac function jacobidn
- **J10** associated Legendre P^mu_nu(0) -- `legendrep(n,m,0)` -> `` (expected `0`) no Giac function legendrep
- **J11** P^1_3(x) => -3/2 sqrt(1 - x^2) (5 x^2 - 1) -- `legendrep(3,1,x)` -> `` (expected `-3/2*sqrt(1-x^2)*(5*x^2-1)`) no Giac function legendrep
- **J14** 2F1(1/2, 1/2; 3/2; z^2) => asin(z)/z -- `hypergeom([1/2,1/2],[3/2],z^2)` -> `` (expected `asin(z)/z`) no Giac function hypergeom
- **J15** 2F1((n+2)/2, -(n-2)/2; 3/2; sin^2 z) => sin(n z)/(n sin z cos z) -- `hypergeom([(n+2)/2,-(n-2)/2],[3/2],sin(z)^2)` -> `` (expected `sin(n*z)/(n*sin(z)*cos(z))`) no Giac function hypergeom
- **J18** define an antisymmetric function -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symmetry declarations for functions)
- **K11** sqrt(e^z) - e^(z/2) for complex z (nonzero in general) -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K14** log(e^z) for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K17** (x y)^(1/n) - x^(1/n) y^(1/n) for complex x, y -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **O3** (a x b) . (c x d) for abstract vectors -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O5** div(f x g) = g . curl f - f . curl g for abstract fields -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O6** divergence in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **O7** dR/dt in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **P3** build a 7x6 matrix from submatrices of a 4x4 matrix -- `` -> `` (expected ``) not attempted: no input in problems.txt (no submatrix indexing / block assembly)
- **P4** block diagonal matrix from [[a,1],[0,a]], b, [[c,1,0],[0,c,1],[0,0,c]] -- `blockdiag([[a,1],[0,a]],b,[[c,1,0],[0,c,1],[0,0,c]])` -> `` (expected `[[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]`) no Giac function blockdiag
- **P12** inverse of a block triangular matrix with symbolic blocks -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symbolic (noncommutative) matrix blocks)
- **P28** generalized eigenvectors (3x3, lambda = 1 triple) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P29** generalized eigenvectors (6x6) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P40** Jacobian of (r cos t, r sin t) -- `jacobian([r*cos(t),r*sin(t)],[r,t])` -> `` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) no Giac function jacobian
- **P42** Wronskian matrix of (cos t, sin t) -- `wronskian([cos(t),sin(t)],t)` -> `` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) no Giac function wronskian
- **R1** sum((x[i] - xbar)^2) => sum(x[i]^2) - sum(x[i])^2/n -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R2** least squares fit of a line to (x[i], y[i]) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R4** Gosper: indefinite sum of binomial(n,k)/2^n - binomial(n+1,k)/2^(n+1) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indefinite summation (Gosper))
- **R11** sum(binomial(n, k) fib(k), k = 0..n) => fib(2n) -- `sum(binomial(n,k)*fibonacci(k),k,0,n)` -> `` (expected `fibonacci(2*n)`) no Giac function fibonacci
- **R12** sum(fib(k)^2, k = 1..n) => fib(n) fib(n+1) -- `sum(fibonacci(k)^2,k,1,n)` -> `` (expected `fibonacci(n)*fibonacci(n+1)`) no Giac function fibonacci
- **T11** limit(2F1(1, k; 1; z/k), k, inf) => e^z -- `limit(hypergeom([1,k],[1],z/k),k,inf)` -> `` (expected `exp(z)`) no Giac function hypergeom
- **U7** exact differential d V(P, T) => V_P dP + V_T dT -- `` -> `` (expected ``) not attempted: no input in problems.txt (no total differentials)
- **U14** [min, max] of 1/(x^2 + y^2 + 1) => [0, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U15** minimize a + b x + c y + d x y on [-1,1]^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U16** [min, max] of x^2 y^3 on [-1,1]^2 => [-1, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U17** linear programming (symbolic simplex) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no linear programming)
- **V2** int |z| dz for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **W20** int x^2 Li3(1/(x + 1)), x = 0..1 (Mertig) -- `integrate(x^2*polylog(3,1/(x+1)),x,0,1)` -> `` (expected `-(17/3+pi^2)/36+ln(2)/9*(35/3-pi^2/2-4*ln(2)+ln(2)^2)+zeta(3)/4`) no Giac function polylog
- **W21** numeric value of W20 => 0.210883 -- `romberg(x^2*polylog(3,1/(x+1)),x,0,1)` -> `` (expected `0.210882859565594`) no Giac function polylog
- **X19** power series of log(sin x/x), general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X20** power series of e^-x sin x, general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X21** series reversion of x = sin y + cos y -- `` -> `` (expected ``) not attempted: no input in problems.txt (no series reversion)
- **Z16** delay equation y'(t) + a y(t - 1) = 0 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no delay differential equations)
- **Z20** triangular system u' = u (1 + cos t/(2 + sin t)), v' = u - v -- `Y:=desolve(y'=[[1+cos(t)/(2+sin(t)),0],[1,-1]]*y,t,y)[0] ; [u=Y[0],v=Y[1]]` -> `"Non constant linear differential system Error: Bad Argument Value"` (expected `[U.diff(t)-U*(1+sp.cos(t)/(2+sp.sin(t))), V.diff(t)-(U-V)]`) desolve: "Non constant linear differential system" (only constant-coefficient systems) (auto: ERROR line 1: Non constant linear differential system Error: Bad Argument Value)
- **Z24** second order system (Niles) u'' = 2 q v', v'' = -2 q u' + 3 q^2 v -- `desolve([u''=2*q*v',v''=-2*q*u'+3*q^2*v],t,[u,v])` -> `"desolve([(u')'=(2*q*v'),(v')'=(-2*q*u'+3*q^2*v)],t,[u,v]) 
 Error: Bad Argument Value"` (expected `[U.diff(t,2)-2*q*V.diff(t), V.diff(t,2)-(-2*q*U.diff(t)+3*q**2*V)]`) desolve takes only first-order systems y' = A y (Bad Argument Value on the second-order system) (auto: ERROR desolve([(u')'=(2*q*v'),(v')'=(-2*q*u'+3*q^2*v)],t,[u,v])   Error: Bad Argument Value)

## ERROR (15)

- **J7** J_{-5/2}(pi/2) => 12/pi^2 -- `BesselJ(-5/2,pi/2)` -> `"BesselJ((-5)/2,pi/2) 
 Error: Bad Argument Value"` (expected `12/pi^2`) BesselJ((-5)/2,pi/2)   Error: Bad Argument Value
- **J8** J_{3/2}(z) => sqrt(2/(pi z)) (sin z/z - cos z) -- `BesselJ(3/2,z)` -> `"BesselJ(3/2,z) 
 Error: Bad Argument Value"` (expected `sqrt(2/(pi*z))*(sin(z)/z-cos(z))`) BesselJ(3/2,z)   Error: Bad Argument Value
- **J13** T_n(-1) => (-1)^n -- `assume(n,integer) ; tchebyshev1(n,-1)` -> `"tchebyshev1(n,-1) 
 Error: Bad Argument Value"` (expected `(-1)^n`) tchebyshev1(n,-1)   Error: Bad Argument Value
- **J17** Dirac delta: int f((x+2)/5) delta((x-2)/3) - g(x) delta'(x-1), 0..3 => 3 f(4/5) + g'(1) -- `integrate(f((x+2)/5)*Dirac((x-2)/3)-g(x)*diff(Dirac(x-1),x),x,0,3)` -> `"integrate(f((x+2)/5)*Dirac((x-2)/3)-g(x)*diff(Dirac(x-1),x),x,0,3) 
 Error: Bad Argument Value"` (expected `0`) integrate(f((x+2)/5)*Dirac((x-2)/3)-g(x)*diff(Dirac(x-1),x),x,0,3)   Error: Bad Argument Value
- **J17b** same with f = exp, g = sin => 3 exp(4/5) + cos(1) -- `integrate(exp((x+2)/5)*Dirac((x-2)/3)-sin(x)*diff(Dirac(x-1),x),x,0,3)` -> `"integrate(exp((x+2)/5)*Dirac((x-2)/3)-sin(x)*diff(Dirac(x-1),x),x,0,3) 
 Error: Bad Argument Value"` (expected `3*exp(4/5)+cos(1)`) integrate(exp((x+2)/5)*Dirac((x-2)/3)-sin(x)*diff(Dirac(x-1),x),x,0,3)   Error: Bad Argument Value
- **N16** |2 A (cos t - 1) + 1| <= 1 => 0 <= A <= 1/(1 - cos t) -- `solve(abs(2*a*(cos(t)-1)+1)<=1,a)` -> `"Unable to check sign: 0>(-1/(cos(t)-1))"` (expected `0<=a and a<=1/(1-cos(t))`) "Unable to check sign: 0>(-1/(cos(t)-1))"
- **N17** A^2 (cos t - 4)^2 sin^2 t < 9 => |A| < 3/|(cos t - 4) sin t| -- `solve(a^2*(cos(t)-4)^2*sin(t)^2<9,a)` -> `"Unable to check sign: (3/(cos(t)*sin(t)-4*sin(t)))>(-3/(cos(t)*sin(t)-4*sin(t)))"` (expected `abs(a)<3/abs((cos(t)-4)*sin(t))`) "Unable to check sign: (3/(cos(t)*sin(t)-4*sin(t)))>(-3/(cos(t)*sin(t)-4*sin(t)))"
- **P38** square root of the singular [[0,1,0],[0,0,0],[0,0,0]] => [[0,a,b],[0,0,0],[0,1/b,0]] -- `sqrt([[0,1,0],[0,0,0],[0,0,0]])` -> `"Unable to transpose Error: Bad Argument Value"` (expected `[[0,1,0],[0,0,0],[0,0,0]]`) Unable to transpose Error: Bad Argument Value
- **R23** equivalent double sum -- `sum(sum(n!/(k!^2*(n-2*k)!)*(x/y)^k*(x*y)^(n-k),n,2*k,inf),k,0,inf)` -> `"diff of sum with boundaries or mute variable depending on differentiation variable Error: Bad Argument Value"` (expected `1/sqrt((1-x*y)^2-4*x^2)`) diff of sum with boundaries or mute variable depending on differentiation variable Error: Bad Argument Value
- **T13** limit(n^x/(x product(1 + x/k, k = 1..n)), n, inf) => gamma(x) -- `limit(n^x/(x*product(1+x/k,k,1,n)),n,inf)` -> `"Limit: Max order reached or unable to make series expansion Error: Bad Argument Value"` (expected `gamma(x)`) Limit: Max order reached or unable to make series expansion Error: Bad Argument Value
- **W15** int log(gamma(x)) cos(6 pi x), x = 0..1 => 1/12 -- `integrate(ln(Gamma(x))*cos(6*pi*x),x,0,1)` -> `"Unable to handle singularities of sin(6*pi*x)*1/6/pi*ln(Gamma(x)) at [undef] Error: Bad Argument Value"` (expected `1/12`) Unable to handle singularities of sin(6*pi*x)*1/6/pi*ln(Gamma(x)) at [undef] Error: Bad Argument Value
- **W23** same with s(t) = H(t - 1) - H(t - 2) -- `integrate((Heaviside(t-1)-Heaviside(t-2))*cos(t),t,0,u)` -> `"Unable to check sign: 1>u"` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) "Unable to check sign: 1>u"
- **X16** Wallis: binomial(2n, n)/2^(2n) => 1/sqrt(pi n) -- `series(binomial(2*n,n)/4^n,n,inf,1)` -> `"Too many recursion levels Error: Bad Argument Value"` (expected `1/sqrt(pi*n)`) Too many recursion levels Error: Bad Argument Value
- **Y7** Laplace transform of a square wave (infinite sum of steps) -- `laplace(1+2*sum((-1)^n*Heaviside(t-n*a),n,1,inf),t,s)` -> `"Unable to check sign: (-a)>0"` (expected `0`) "Unable to check sign: (-a)>0"
- **Y14** Z transform of H(t - m T) => z/(z^m (z - 1)) -- `ztrans(Heaviside(t-m),t,z)` -> `"Unable to check sign: 0>m"` (expected `0`) "Unable to check sign: 0>m"

## TIMEOUT (4)

- **M37** 3x3 nonlinear system => 16 solutions -- `csolve([x^2*y+3*y*z-4=0,-3*x^2*z+2*y^2+1=0,2*y*z^2-z^2-1=0],[x,y,z])` -> `` (expected ``) wall-clock timeout
- **R19** sum(1/((3k+1)(3k+2)(3k+3)), k = 0..inf) => sqrt(3) pi/12 - log(3)/4 -- `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` -> `` (expected `sqrt(3)*pi/12-ln(3)/4`) wall-clock timeout
- **W31** int(int(sin a sin y/sqrt(1 - sin^2 a sin^2 x sin^2 y), x = 0..pi/2), y = 0..pi/2) => pi a/2 -- `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,pi/2),y,0,pi/2)` -> `` (expected `pi*a/2`) wall-clock timeout
- **Z4** Yu-Israel nonlinear-coefficient recurrence -- `rsolve(f(n)=(1+c-c^(n-1)-c^(n+1))/(1-c^n)*f(n-1)-c*(1-c^(n-2))/(1-c^(n-1))*f(n-2)+1,f(n),[f(1)=1,f(2)=(2+2*c+c^2)/(1+c)])` -> `` (expected `(c^(n+1)*(c^(n+1)-2*c-2)+(n+1)*c^2+2*c-n)/((c-1)^3*(c+1))`) wall-clock timeout

## CRASH (1)

- **P26** eigenvalues of a hypercompanion matrix => roots of x^2+x+1 (twice), RootOf(quintic) -- `eigenvals([[-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,1,0]])` -> `` (expected `[-1/2-sqrt(3)*i/2,-1/2+sqrt(3)*i/2]`) rc=-11, 2 of 3 outputs

