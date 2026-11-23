# Wester benchmark baseline: maxima

engine: `Maxima 5.49.0 (/opt/local/bin/maxima)`

Graded with `../run_wester.py` grade(); see `../COMPARISON.md`.

| section | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|
| A Boolean logic | 2 | 0 | 4 | 2 | 0 | 0 | 0 | 0 | 5 | 13 |
| B Set theory | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4 |
| C Numbers | 11 | 0 | 9 | 3 | 0 | 0 | 0 | 0 | 1 | 24 |
| D Numerical analysis | 5 | 1 | 0 | 2 | 0 | 0 | 0 | 0 | 6 | 14 |
| E Statistics | 9 | 1 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 15 |
| F Combinatorial theory | 5 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 8 |
| G Number theory | 6 | 0 | 0 | 14 | 0 | 0 | 0 | 0 | 0 | 20 |
| H Algebra | 24 | 0 | 5 | 3 | 0 | 0 | 0 | 0 | 2 | 34 |
| I Trigonometry | 3 | 2 | 8 | 1 | 0 | 0 | 0 | 0 | 0 | 14 |
| J Special functions | 12 | 0 | 6 | 1 | 0 | 0 | 0 | 0 | 0 | 19 |
| K Complex domain | 14 | 4 | 3 | 3 | 0 | 0 | 0 | 0 | 0 | 24 |
| L Zero equivalence | 3 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 9 |
| M Equations | 29 | 3 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 39 |
| N Inequalities | 14 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 18 |
| O Vector analysis | 6 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 10 |
| P Matrix theory | 32 | 3 | 1 | 8 | 1 | 1 | 0 | 0 | 0 | 46 |
| Q Tensor analysis | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 14 | 0 | 6 | 3 | 1 | 0 | 0 | 0 | 0 | 24 |
| S Products | 1 | 0 | 9 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| T Limits | 13 | 0 | 1 | 0 | 2 | 0 | 0 | 0 | 0 | 16 |
| U Calculus | 3 | 0 | 5 | 9 | 0 | 0 | 0 | 0 | 2 | 19 |
| V Indefinite integrals | 12 | 0 | 4 | 2 | 0 | 0 | 0 | 0 | 0 | 18 |
| W Definite integrals | 14 | 1 | 11 | 1 | 6 | 1 | 0 | 0 | 0 | 34 |
| X Series | 19 | 0 | 1 | 3 | 2 | 0 | 0 | 0 | 1 | 26 |
| Y Transforms | 2 | 0 | 5 | 5 | 2 | 0 | 0 | 0 | 0 | 14 |
| Z Difference and differential equations | 17 | 0 | 4 | 2 | 1 | 0 | 0 | 0 | 0 | 24 |
| **total** | **274** | **15** | **102** | **71** | **15** | **2** | **0** | **0** | **18** | **497** |

## PASS (274)

- **A1** true and false => false -- `true and false` -> `false` (expected `false`) 
- **A7** x and 1 > 2 => false -- `is(x and 1>2)` -> `false` (expected `false`) 
- **B1** union of [a,b,b,c,c,c], [d,c,b], [b,e,b] => {a,b,c,d,e} -- `union(setify([a,b,b,c,c,c]),setify([d,c,b]),setify([b,%e,b]))` -> `{%e,a,b,c,d}` (expected `[a,b,c,d,e]`) 
- **B2** intersection => {b} -- `intersection(setify([a,b,b,c,c,c]),setify([d,c,b]),setify([b,%e,b]))` -> `{b}` (expected `[b]`) 
- **B3** set difference {a,b,c,d,e} - {b} => {a,c,d,e} -- `setdifference(setify([a,b,c,d,%e]),setify([b]))` -> `{%e,a,c,d}` (expected `[a,c,d,e]`) 
- **B4** Cartesian product {a,b} x {c,d} -- `cartesian_product(setify([a,b]),setify([c,d]))` -> `{[a,c],[a,d],[b,c],[b,d]}` (expected `[[a,c],[a,d],[b,c],[b,d]]`) 
- **C1** 50! (bignum) -- `50!` -> `30414093201713378043612608166064768844377641568960512000000000000` (expected `30414093201713378043612608166064768844377641568960512000000000000`) 
- **C2** factor 50! -- `factor(50!)` -> `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47` (expected `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47`) 
- **C3** double factorials [10!!, 9!!] => [3840, 945] -- `[10!!,9!!]` -> `[3840,945]` (expected `[3840,945]`) 
- **C7** log base 8 of 32768 => 5 -- `(log(32768)/log(8))` -> `log(32768)/log(8)` (expected `5`) 
- **C8** 5^(-1) mod 7 => 3; 5^(-1) mod 6 => 5 -- `[inv_mod(5,7),inv_mod(5,6)]` -> `[3,5]` (expected `[3,5]`) 
- **C9** gcd(1776, 1554, 5698) => 74 -- `gcd(gcd(1776,1554),5698)` -> `74` (expected `74`) 
- **C10** 1/2 + 1/3 + ... + 1/10 => 4861/2520 -- `1/2+1/3+1/4+1/5+1/6+1/7+1/8+1/9+1/10` -> `4861/2520` (expected `4861/2520`) 
- **C11** complete decimal expansion of 1/7 => 0.(142857) -- `block([fpprec:30],bfloat(1/7))` -> `1.42857142857142857142857142857b-1` (expected `0.142857142857142857142857142857`) 
- **C12** (7/11)*(22/7) => 2 -- `7/11*(22/7)` -> `2` (expected `2`) 
- **C13** 10/7 (1 + 29/1000)^(1/3) => 3^(1/3) automatically -- `10/7*(1+29/1000)^(1/3)` -> `3^(1/3)` (expected `3^(1/3)`) 
- **C14** denest sqrt(2 sqrt(3) + 4) => 1 + sqrt(3) -- `wsimp(sqrt(2*sqrt(3)+4))` -> `sqrt(3)+1` (expected `1+sqrt(3)`) 
- **D1** 0.0/sqrt(2) => 0.0 -- `0.0/sqrt(2)` -> `0.0` (expected `0`) 
- **D2b** exp(-1000000) in arbitrary precision -- `block([fpprec:15],bfloat(exp(-1000000)))` -> `3.29683147808856b-434295` (expected `3.29683147808856e-434295`) 
- **D3** exp(pi sqrt(163)) to 50 digits (nearly an integer) -- `block([fpprec:50],bfloat(exp(%pi*sqrt(163))))` -> `2.6253741264076874399999999999925007259719818568888b17` (expected `262537412640768743.99999999999925007259719818568888`) 
- **D4** [floor(-5/3), ceil(-5/3)] => [-2, -1] -- `[floor(-5/3),ceiling(-5/3)]` -> `[-2,-1]` (expected `[-2,-1]`) 
- **D8** Horner form of sum(a[i] x^i, i=1..5) -- `horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x)` -> `x*(x*(x*(x*(a5*x+a4)+a3)+a2)+a1)` (expected `x*(a1+x*(a2+x*(a3+x*(a4+a5*x))))`) 
- **E1** mean([3, 7, 11, 5, 19]) => 9 -- `mean([3,7,11,5,19])` -> `9` (expected `9`) 
- **E2** median => 7 -- `median([3,7,11,5,19])` -> `7` (expected `7`) 
- **E5** unbiased sample standard deviation of [1..5] => sqrt(5/2) -- `std1([1,2,3,4,5])` -> `sqrt(5)/sqrt(2)` (expected `sqrt(5/2)`) 
- **E6** binomial pdf: 12 of 15 at p = 0.75 => 0.22520 -- `pdf_binomial(12,15,0.75)` -> `0.22519906517118216` (expected `0.225199`) 
- **E7** binomial cdf: up to 12 of 15 => 0.76391 -- `cdf_binomial(12,15,0.75)` -> `0.7639121888205389` (expected `0.763912`) 
- **E8** normal(4.35, 0.59): P(4 <= X <= 5) => 0.5867 -- `(cdf_normal(5,4.35,0.59)-cdf_normal(4,4.35,0.59))` -> `erf(1.101694915254238/sqrt(2))/2+erf(0.5932203389830503/sqrt(2))/2` (expected `0.588185984502579`) 
- **E9** Student t test of mean 5 => 0.057567 -- `L:[1,-2,3,-4,5,-6,7,-8,9,10] ; (cdf_student_t((mean(L)-5)/(std(L)*sqrt(10/9)/sqrt(10)),9)-0)` -> `beta_incomplete_regularized(9/2,1/2,145/194)/2` (expected `0.057567`) 
- **E10** same with the normal distribution => 0.040583 -- `L:[1,-2,3,-4,5,-6,7,-8,9,10] ; (cdf_normal((mean(L)-5)/(std(L)*sqrt(10/9)/sqrt(10)),0,1)-0)` -> `1/2-erf(21/(sqrt(2)*sqrt(145)))/2` (expected `0.040583`) 
- **E11** chi-square independence test (chi2 = 1153/252) => 0.469859 -- `(1-cdf_chi2(1153/252,5))` -> `gamma_incomplete_regularized(5/2,1153/504)` (expected `0.469859`) 
- **F1** Pochhammer (a)_3 => a (a+1) (a+2) -- `pochhammer(a,3)` -> `a*(a+1)*(a+2)` (expected `a*(a+1)*(a+2)`) 
- **F2** binomial(n, 3) => n (n-1) (n-2)/6 -- `binomial(n,3)` -> `((n-2)*(n-1)*n)/6` (expected `n*(n-1)*(n-2)/6`) 
- **F6** number of partitions of 4 => 5 -- `num_partitions(4)` -> `5` (expected `5`) 
- **F7** Stirling number of the first kind S1(5, 2) => -50 -- `stirling1(5,2)` -> `-50` (expected `-50`) 
- **F8** Euler totient(1776) => 576 -- `totient(1776)` -> `576` (expected `576`) 
- **G1** largest 6-digit and smallest 7-digit prime => [999983, 1000003] -- `[prev_prime(10^6),next_prime(10^6)]` -> `[999983,1000003]` (expected `[999983,1000003]`) 
- **G2** primitive root of 191 => 19 -- `zn_primroot(191)` -> `19` (expected `19`) 
- **G8** x = 7 mod 9, 13 mod 23, 1 mod 2 => x = 151 mod 414 -- `block([p:block([p:[7,9],q:[13,23]],[chinese([p[1],q[1]],[p[2],q[2]]),lcm(p[2],q[2])]),q:[1,2]],[chinese([p[1],q[1]],[p[2],q[2]]),lcm(p[2],q[2])])` -> `[151,414]` (expected `[151,414]`) 
- **G16** continued fraction of pi => [3; 7, 15, 1, 292, ...] -- `firstn(cf(bfloat(%pi)),5)` -> `[3,7,15,1,292]` (expected `[3,7,15,1,292]`) 
- **G17** continued fraction of sqrt(23) => [4; 1, 3, 1, 8, ...] -- `firstn(cf(bfloat(sqrt(23))),5)` -> `[4,1,3,1,8]` (expected `[4,1,3,1,8]`) 
- **G18** continued fraction of the golden ratio => [1; 1, 1, ...] -- `firstn(cf(bfloat((1+sqrt(5))/2)),5)` -> `[1,1,1,1,1]` (expected `[1,1,1,1,1]`) 
- **H1** 2 2^n => 2^(n+1) -- `wsimp(2*2^n)` -> `2^(n+1)` (expected `2^(n+1)`) 
- **H2** 4 2^n => 2^(n+2) -- `wsimp(4*2^n)` -> `2^(n+2)` (expected `2^(n+2)`) 
- **H4** factor(6 x - 10) => 2 (3 x - 5) -- `factor(6*x-10)` -> `2*(3*x-5)` (expected `2*(3*x-5)`) 
- **H5** univariate gcd(p1, p2) => 1 (Liska) -- `gcd(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81,72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)` -> `1` (expected `1`) 
- **H6** gcd(p1 q, p2 q) => q -- `gcd(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)))` -> `34*x^19-25*x^16+70*x^7+20*x^3-91*x-86` (expected `[34*x^19-25*x^16+70*x^7+20*x^3-91*x-86,-(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)]`) 
- **H7** resultant(p1 q, p2 q) => 0 -- `resultant(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),x)` -> `0` (expected `0`) 
- **H8** factor(expand(p1 p2)) => p1 p2 -- `factor(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)))` -> `-((16*x^60+21*x^47-64*x^34+126*x^8+46*x^5+81)*(72*x^60-83*x^52-22*x^39-25*x^25-19*x^23+54*x^10+81))` (expected `(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)`) 
- **H9** multivariate gcd(p1, p2) => 1 -- `gcd(24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5,34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)` -> `1` (expected `1`) 
- **H10** multivariate gcd(p1 q, p2 q) => q -- `gcd(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)),expand((34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)))` -> `11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8` (expected `[11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8,-(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)]`) 
- **H11** multivariate factor(expand(p1 p2)) => p1 p2 -- `factor(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)))` -> `2*y^7*z*(24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(17*x^5*y*z^12+10*x^7*z^6+6*x^9*y^9*z^3+40*y^7)` (expected `-2*z*y^7*(6*x^9*y^9*z^3+10*x^7*z^6+17*y*x^5*z^12+40*y^7)*(3*x^22+47*x^17*y^5*z^8-6*x^15*y^9*z^2-24*x*y^19*z^8-5)`) 
- **H12** gcd(2 x^(n+4) - x^(n+2), 4 x^(n+1) + 3 x^n) => x^n -- `gcd(2*x^(n+4)-x^(n+2),4*x^(n+1)+3*x^n)` -> `x^n` (expected `x^n`) 
- **H13** resultant with a common factor => 0 -- `resultant(3*x^4+3*x^3+x^2-x-2,x^3-3*x^2+x+5,x)` -> `0` (expected `0`) 
- **H14** (x^2 - 4)/(x^2 + 4x + 4) => (x - 2)/(x + 2) -- `ratsimp((x^2-4)/(x^2+4*x+4))` -> `(x-2)/(x+2)` (expected `(x-2)/(x+2)`) 
- **H16** expand (x+1)^20, differentiate, factor => 20 (x+1)^19 -- `factor(diff(expand((x+1)^20),x))` -> `20*(x+1)^19` (expected `20*(x+1)^19`) 
- **H18** factor(x^100 - 1) => 9 cyclotomic factors -- `factor(x^100-1)` -> `(x-1)*(x+1)*(x^2+1)*(x^4-x^3+x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^8-x^6+x^4-x^2+1)*(x^20-x^15+x^10-x^5+1)*(x^20+x^15+x^10+x^5+1)*(x^40-x^30+x^20-x^10+1)` (expected `(x-1)*(x+1)*(x^2+1)*(x^4-x^3+x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^8-x^6+x^4-x^2+1)*(x^20-x^15+x^10-x^5+1)*(x^20+x^15+x^10+x^5+1)*(x^40-x^30+x^20-x^10+1)`) 
- **H19** factor over Q(i): 4x^4+8x^3+77x^2+18x+153 => (2x+3i)(2x-3i)(x+1+4i)(x+1-4i) -- `gfactor(4*x^4+8*x^3+77*x^2+18*x+153)` -> `(x-4*%i+1)*(x+4*%i+1)*(2*x-3*%i)*(2*x+3*%i)` (expected `(2*x+3*i)*(2*x-3*i)*(x+1+4*i)*(x+1-4*i)`) 
- **H22** (cbrt2 + sqrt3)^4 with cbrt2^3 = 2, sqrt3^2 = 3 -- `expand((2^(1/3)+sqrt(3))^4)` -> `2^(7/3)*3^(3/2)+8*sqrt(3)+9*2^(5/3)+2^(4/3)+9` (expected `2*2^(1/3)+8*sqrt(3)+18*2^(2/3)+12*2^(1/3)*sqrt(3)+9`) 
- **H23** factor(x^4 - 3x^2 + 1) over Q => (x^2-x-1)(x^2+x-1) -- `factor(x^4-3*x^2+1)` -> `(x^2-x-1)*(x^2+x-1)` (expected `(x^2-x-1)*(x^2+x-1)`) 
- **H26** factor x^4 - 3x^2 + 1 over Q(phi) => 4 linear factors -- `subst((1+sqrt(5))/2,a,factor(x^4-3*x^2+1,a^2-a-1))` -> `(x-(sqrt(5)+1)/2)*(x-(sqrt(5)+1)/2+1)*(x+(sqrt(5)+1)/2-1)*(x+(sqrt(5)+1)/2)` (expected `(x-(1+sqrt(5))/2)*(x+(1+sqrt(5))/2)*(x-(1+sqrt(5))/2+1)*(x+(1+sqrt(5))/2-1)`) 
- **H27** factor(expand((x - 2y^2 + 3z^3)^20)) -- `factor(expand((x-2*y^2+3*z^3)^20))` -> `(3*z^3-2*y^2+x)^20` (expected `(x-2*y^2+3*z^3)^20`) 
- **H28** factor(expand((sin x - 2 cos^2 y + 3 tan^3 z)^20)) -- `factor(expand((sin(x)-2*cos(y)^2+3*tan(z)^3)^20))` -> `(3*tan(z)^3-2*cos(y)^2+sin(x))^20` (expected `(sin(x)-2*cos(y)^2+3*tan(z)^3)^20`) 
- **H29** (1-c^2)^5 (1-s^2)^5 (c^2+s^2)^10 with c^2 + s^2 = 1 => c^10 s^10 -- `factor(remainder(expand((1-c^2)^5*(1-s^2)^5*(c^2+s^2)^10),s^2+c^2-1,s))` -> `-((c-1)^5*c^10*(c+1)^5)` (expected `-c^10*(c-1)^5*(c+1)^5`) 
- **H31** factor x^3 + y^3 over Q(sqrt(-3)) -- `subst(%i*sqrt(3),a,factor(x^3+y^3,a^2+3))` -> `((y+x)*(2*y-sqrt(3)*%i*x-x)*(2*y+sqrt(3)*%i*x-x))/4` (expected `(x+y)*(x+y*(-1/2-sqrt(3)/2*i))*(x+y*(-1/2+sqrt(3)/2*i))`) 
- **H32** partial fractions of (x^2+2x+3)/(x^3+4x^2+5x+2) -- `partfrac((x^2+2*x+3)/(x^3+4*x^2+5*x+2),x)` -> `3/(x+2)-2/(x+1)+2/(x+1)^2` (expected `3/(x+2)-2/(x+1)+2/(x+1)^2`) 
- **I1** tan(7 pi/10) => -sqrt(1 + 2/sqrt(5)) -- `tan(7*%pi/10)` -> `tan((7*%pi)/10)` (expected `-sqrt(1+2/sqrt(5))`) 
- **I7** cos(3x)/cos(x) => cos(x)^2 - 3 sin(x)^2 or similar -- `ratsimp(trigexpand(cos(3*x)/cos(x)))` -> `cos(x)^2-3*sin(x)^2` (expected `cos(x)^2-3*sin(x)^2`) 
- **I11** limit of the 0/0 expression at 0 => undefined -- `limit((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1),x,0)` -> `expt: undefined: 0 to a negative exponent.` (expected ``) error expected: Maxima error: expt: undefined: 0 to a negative exponent.
- **J1** Bernoulli B_16 => -3617/510 -- `bern(16)` -> `-(3617/510)` (expected `-3617/510`) 
- **J2** d/dk E(phi, k) => (E(phi, k) - F(phi, k))/k -- `diff(elliptic_e(p,k^2),k)` -> `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k` (expected `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k`) 
- **J3** d/du dn(u, k) => -k^2 sn(u, k) cn(u, k) -- `diff(jacobi_dn(u,(k)^2),u)` -> `-(k^2*jacobi_cn(u,k^2)*jacobi_sn(u,k^2))` (expected `0`) -k^2 sn cn: Maxima's second argument is the parameter m = k^2 (the input was translated the same way)
- **J4** gamma(-1/2) => -2 sqrt(pi) -- `gamma(-1/2)` -> `-(2*sqrt(%pi))` (expected `-2*sqrt(pi)`) 
- **J5** psi(1/3) => -gamma - pi/(2 sqrt 3) - 3/2 log 3 -- `psi[0](1/3)` -> `-((3*log(3))/2)-%pi/(2*sqrt(3))-%gamma` (expected `-euler_gamma-pi/(2*sqrt(3))-3*ln(3)/2`) 
- **J6** J_2(1 + i) => 0.04158 + 0.24740 i -- `rectform(float(bessel_j(2,1+%i)))` -> `0.24739764151330637*%i+0.04157988694396212` (expected `0.04157988694396212+0.24739764151330632*i`) 
- **J7** J_{-5/2}(pi/2) => 12/pi^2 -- `bessel_j(-5/2,%pi/2)` -> `bessel_j(-(5/2),%pi/2)` (expected `12/pi^2`) 
- **J9** d/dz J_0(z) => -J_1(z) -- `diff(bessel_j(0,z),z)` -> `-bessel_j(1,z)` (expected `-besselj(1,z)`) 
- **J11** P^1_3(x) => -3/2 sqrt(1 - x^2) (5 x^2 - 1) -- `assoc_legendre_p(3,1,x)` -> `-(6*(-((5*(1-x))/2)+(5*(1-x)^2)/4+1)*sqrt(1-x^2))` (expected `-3/2*sqrt(1-x^2)*(5*x^2-1)`) 
- **J12** T_1008 - 2x T_1007 + T_1006 => 0 -- `ratsimp(chebyshev_t(1008,x)-2*x*chebyshev_t(1007,x)+chebyshev_t(1006,x))` -> `0` (expected `0`) 
- **J14** 2F1(1/2, 1/2; 3/2; z^2) => asin(z)/z -- `assume(z>0,z<1) ; hgfred([1/2,1/2],[3/2],z^2)` -> `asin(z)/z` (expected `asin(z)/z`) 
- **J15** 2F1((n+2)/2, -(n-2)/2; 3/2; sin^2 z) => sin(n z)/(n sin z cos z) -- `hgfred([(n+2)/2,-(n-2)/2],[3/2],sin(z)^2)` -> `sin(n*asin(sin(z)))/(n*sin(z)*sqrt(1-sin(z)^2))` (expected `sin(n*z)/(n*sin(z)*cos(z))`) 
- **K1** [Re(x + i y), Im(x + i y)] for complex x, y => [Re x - Im y, Im x + Re y] -- `[realpart(x+%i*y),imagpart(x+%i*y)]` -> `[x,y]` (expected `[re(x)-im(y),im(x)+re(y)]`) 
- **K3** |1/(a + i/a + i b)| for real a, b => 1/sqrt(a^2 + (1/a + b)^2) -- `abs(1/(a+%i/a+%i*b))` -> `1/abs(%i*b+a+%i/a)` (expected `1/sqrt(a^2+(1/a+b)^2)`) 
- **K4** log(3 + 4i) in rectangular form => log 5 + i atan(4/3) -- `rectform(log(3+4*%i))` -> `log(5)+%i*atan(4/3)` (expected `ln(5)+i*atan(4/3)`) 
- **K5** tan(x + i y) in rectangular form -- `rectform(tan(x+%i*y))` -> `(%i*sinh(2*y))/(cosh(2*y)+cos(2*x))+sin(2*x)/(cosh(2*y)+cos(2*x))` (expected `(sin(2*x)+i*sinh(2*y))/(cos(2*x)+cosh(2*y))`) 
- **K6** sqrt(x y |z|^2)/(sqrt(x) |z|) => sqrt(x y)/sqrt(x) (no further in general) -- `wsimp(sqrt(x*y*abs(z)^2)/(sqrt(x)*abs(z)))` -> `sqrt(x*y)/sqrt(x)` (expected `sqrt(x*y)/sqrt(x)`) 
- **K7** same for y >= 0 => sqrt(y) -- `assume(y>=0) ; wsimp(sqrt(x*y*abs(z)^2)/(sqrt(x)*abs(z)))` -> `sqrt(y)` (expected `sqrt(y)`) 
- **K9** sqrt(1/z) - 1/sqrt(z) => 0 for z > 0 -- `assume(z>0) ; wsimp(sqrt(1/z)-1/sqrt(z))` -> `0` (expected `0`) 
- **K12** sqrt(e^z) - e^(z/2) => 0 for real z -- `wsimp(sqrt(exp(z))-exp(z/2))` -> `0` (expected `0`) 
- **K15** log(e^z) => z for real z -- `log(exp(z))` -> `z` (expected `z`) 
- **K18** (x y)^(1/n) - x^(1/n) y^(1/n) => 0 for y > 0 -- `assume(y>0) ; wsimp((x*y)^(1/n)-x^(1/n)*y^(1/n))` -> `0` (expected `0`) 
- **K19** (x y)^n - x^n y^n => 0 for integer n -- `declare(n,integer) ; wsimp((x*y)^n-x^n*y^n)` -> `0` (expected `0`) 
- **K20** atan(tan(z)) does not simplify to z for real z -- `wsimp(atan(tan(z)))` -> `atan(tan(z))` (expected `atan(tan(z))`) 
- **K21** atan(tan(z)) => z for -pi/2 < z < pi/2 -- `assume(z>-%pi/2,z<%pi/2) ; wsimp(atan(tan(z)))` -> `z` (expected `z`) 
- **K22** atan(tan(10)) => 10 - 3 pi -- `atan(tan(10))` -> `10-3*%pi` (expected `10-3*pi`) 
- **L1** sqrt(997) - (997^3)^(1/6) => 0 -- `sqrt(997)-(997^3)^(1/6)` -> `0` (expected `0`) 
- **L2** sqrt(999983) - (999983^3)^(1/6) => 0 -- `sqrt(999983)-(999983^3)^(1/6)` -> `0` (expected `0`) 
- **L4** cos^3 x + cos x sin^2 x - cos x => 0 -- `trigsimp(cos(x)^3+cos(x)*sin(x)^2-cos(x))` -> `0` (expected `0`) 
- **M1** (x = 2)/2 + (1 = 1) => x/2 + 1 = 2 -- `(x=2)/2+(1=1)` -> `x/2+1 = 2` (expected `x/2+1=2`) 
- **M2** 3x^3 - 18x^2 + 33x - 19 = 0 (three real roots, exact) -- `wsolve(3*x^3-18*x^2+33*x-19=0,x)` -> `[((sqrt(3)*%i)/2+(-1)/2)/(3*(%i/(2*3^(3/2))+1/6)^(1/3))+(%i/(2*3^(3/2))+1/6)^(1/3)*(-1/2-(sqrt(3)*%i)/2)+2,(%i/(2*3^(3/2))+1/6)^(1/3)*((sqrt(3)*%i)/2+(-1)/2)+(-1/2-(sqrt(3)*%i)/2)/(3*(%i/(2*3^(3/2))+1` (expected `[2+2/sqrt(3)*cos(pi/18),2+2/sqrt(3)*cos(pi/18-2*pi/3),2+2/sqrt(3)*cos(pi/18+2*pi/3)]`) 
- **M3** x^4 + x^3 + x^2 + x + 1 = 0 (exact roots) -- `wsolve(x^4+x^3+x^2+x+1=0,x)` -> `[-(sqrt(sqrt(5)/2-5/2)/2)-sqrt(5)/4+(-1)/4,sqrt(sqrt(5)/2-5/2)/2-sqrt(5)/4+(-1)/4,-(sqrt(-(sqrt(5)/2)-5/2)/2)+sqrt(5)/4+(-1)/4,sqrt(-(sqrt(5)/2)-5/2)/2+sqrt(5)/4+(-1)/4]` (expected `[exp(2*pi*i/5),exp(4*pi*i/5),exp(6*pi*i/5),exp(8*pi*i/5)]`) 
- **M5** x^7 - 1 = 0 => 7th roots of unity -- `wsolve(x^7-1=0,x)` -> `[%e^((2*%i*%pi)/7),%e^((4*%i*%pi)/7),%e^((6*%i*%pi)/7),%e^-((6*%i*%pi)/7),%e^-((4*%i*%pi)/7),%e^-((2*%i*%pi)/7),1]` (expected `[1,exp(2*pi*i/7),exp(4*pi*i/7),exp(6*pi*i/7),exp(8*pi*i/7),exp(10*pi*i/7),exp(12*pi*i/7)]`) 
- **M6** Liska's degree-8 polynomial => 1 +- sqrt(+-sqrt(+-4 sqrt(3) - 3) - 3)/sqrt(2) -- `wsolve(x^8-8*x^7+34*x^6-92*x^5+175*x^4-236*x^3+226*x^2-140*x+46=0,x)` -> `[-((sqrt(-sqrt(-(4*sqrt(3))-3)-3)-sqrt(2))/sqrt(2)),(sqrt(-sqrt(-(4*sqrt(3))-3)-3)+sqrt(2))/sqrt(2),-((sqrt(sqrt(-(4*sqrt(3))-3)-3)-sqrt(2))/sqrt(2)),(sqrt(sqrt(-(4*sqrt(3))-3)-3)+sqrt(2))/sqrt(2),-((` (expected `[1+sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2)]`) 
- **M7** e^(2x) + 2 e^x + 1 = z => x = log(sqrt z - 1), log(sqrt z + 1) + i pi (+ 2 n pi i) -- `wsolve(exp(2*x)+2*exp(x)+1=z,x)` -> `[log(-sqrt(z)-1),log(sqrt(z)-1)]` (expected `[ln(sqrt(z)-1),ln(sqrt(z)+1)+i*pi]`) 
- **M8** exp(2 - x^2) = exp(-x) => real solutions {-1, 2} -- `wsolve(exp(2-x^2)=exp(-x),x)` -> `[2,-1]` (expected `[-1,2]`) 
- **M9** e^x = x => x = -W(-1) -- `wsolve(exp(x)=x,x)` -> `[-lambert_w(-1)]` (expected `[-LambertW(-1)]`) 
- **M11** (x + 1)(sin^2 x + 1)^2 cos^3(3x) = 0 => -1, pi/6 + n pi/3 (real) -- `wsolve((x+1)*(sin(x)^2+1)^2*cos(3*x)^3=0,x)` -> `[-1,-((5*%pi)/6),-(%pi/2),-(%pi/6),%pi/6,%pi/2,(5*%pi)/6,-(%i*log(sqrt(2)-1)),-(%i*(log(sqrt(2)-1)+%i*%pi)),-(%i*log(sqrt(2)+1)),-(%i*(log(sqrt(2)+1)+%i*%pi))]` (expected ``) particular solutions
- **M12** sin x = cos x => pi/4 (+ n pi) -- `wsolve(sin(x)=cos(x),x)` -> `[%pi/4]` (expected ``) particular solutions
- **M13** tan x = 1 => pi/4 (+ n pi) -- `wsolve(tan(x)=1,x)` -> `[%pi/4]` (expected ``) particular solutions
- **M14** sin x = 1/2 => pi/6, 5 pi/6 (+ 2 n pi) -- `wsolve(sin(x)=1/2,x)` -> `[%pi/6,(5*%pi)/6]` (expected ``) particular solutions
- **M15** sin x = tan x => 0 (+ n pi) -- `wsolve(sin(x)=tan(x),x)` -> `[0]` (expected ``) particular solutions
- **M18** (x - 2)/x^(1/3) = 0 => 2 -- `wsolve((x-2)/x^(1/3)=0,x)` -> `[2]` (expected `[2]`) 
- **M19** sqrt(x^2 + 1) = x - 2 => no solutions -- `wsolve(sqrt(x^2+1)=x-2,x)` -> `[]` (expected `[]`) 
- **M20** x + sqrt(x) = 2 => 1 -- `wsolve(x+sqrt(x)=2,x)` -> `[1]` (expected `[1]`) 
- **M21** 2 sqrt(x) + 3 x^(1/4) - 2 = 0 => 1/16 -- `wsolve(2*sqrt(x)+3*x^(1/4)-2=0,x)` -> `[1/16]` (expected `[1/16]`) 
- **M22** x = 1/sqrt(1 + x^2) => sqrt((sqrt5-1)/2), -i sqrt((sqrt5+1)/2) -- `wsolve(x=1/sqrt(1+x^2),x)` -> `[sqrt(sqrt(5)-1)/sqrt(2),-((sqrt(sqrt(5)+1)*%i)/sqrt(2))]` (expected `[sqrt((sqrt(5)-1)/2),-i*sqrt((sqrt(5)+1)/2)]`) 
- **M23** binomial(m, 2) 2^k = 1 => k = 1 - log2(m (m - 1)) -- `wsolve(binomial(m,2)*2^k=1,k)` -> `[-((log(m^2-m)-log(2))/log(2))]` (expected `[1-ln(m*(m-1))/ln(2)]`) 
- **M25** sqrt(log x) = log(sqrt x) => {1, e^4} -- `wsolve(sqrt(log(x))=log(sqrt(x)),x)` -> `[1,%e^4]` (expected `[1,exp(4)]`) 
- **M28** |x - 1| = 2 => {-1, 3} -- `wsolve(abs(x-1)=2,x)` -> `[-1,3]` (expected `[-1,3]`) 
- **M29** |2x + 5| = |x - 2| => {-1, -7} -- `wsolve(abs(2*x+5)=abs(x-2),x)` -> `[-7,-1]` (expected `[-1,-7]`) 
- **M30** 1 - |x| = max(-x - 2, x - 2) => {-3/2, 3/2} -- `wsolve(1-abs(x)=max(-x-2,x-2),x)` -> `[-(3/2),3/2]` (expected `[-3/2,3/2]`) 
- **M31** max(2 - x^2, x) = max(-x, x^3/9) => {-1, 3} -- `wsolve(max(2-x^2,x)=max(-x,x^3/9),x)` -> `[-1,3]` (expected `[-1,3]`) 
- **M33** (1 + i) z + (2 - i) conj(z) = -3 i => z = 2 + 3 i -- `(declare(z,complex),wsolve((1+%i)*z+(2-%i)*conjugate(z)=-3*%i,z))` -> `[3*%i+2]` (expected `[2+3*i]`) 
- **M33b** same with z = x + i y, x, y real => x = 2, y = 3 -- `solve([realpart((1+%i)*(x+%i*y)+(2-%i)*(x-%i*y)+3*%i)=0,imagpart((1+%i)*(x+%i*y)+(2-%i)*(x-%i*y)+3*%i)=0],[x,y])` -> `[[x = 2,y = 3]]` (expected `[[x=2,y=3]]`) 
- **M34** f(x)^2 + f(x) - 2 = 0 solved for f(x) => {1, -2} -- `wsolve(f(x)^2+f(x)-2=0,f(x))` -> `[1,-2]` (expected `[1,-2]`) 
- **M35** 3x3 linear system with a parametric solution => x = 4 - z, y = 2 -- `solve([x+y+z=6,2*x+y+2*z=10,x+3*y+z=10],[x,y,z])` -> `[[x = 4-%r1,y = 2,z = %r1]]` (expected ``) 
- **M36** Steinberg: 189 linear equations in 49 unknowns, 3 free -- `linsolve([-b*k8/a+c*k8/a=0,-b*k11/a+c*k11/a=0,-b*k10/a+c*k10/a+k2=0,-k3-b*k9/a+c*k9/a=0,-b*k14/a+c*k14/a=0,-b*k15/a+c*k15/a=0,-b*k18/a+c*k18/a-k2=0,-b*k17/a+c*k17/a=0,-b*k16/a+c*k16/a+k4=0,-b*k13/a+c*k13/a-b*k21/a+c*k21/a+b*k5/a-c*k5/a=0,b*k44/a-c*k44/a=0,-b*k45/a+c*k45/a=0,-b*k20/a+c*k20/a=0,-b*k44` -> `[k1 = 0,k2 = 0,k3 = 0,k4 = 0,k5 = 0,k6 = 0,k7 = 0,k8 = 0,k9 = 0,k10 = 0,k11 = 0,k12 = 0,k13 = 0,k14 = 0,k15 = 0,k16 = 0,k17 = 0,k18 = 0,k19 = 0,k20 = 0,k21 = 0,k22 = 0,k23 = %r2,k24 = 0,k25 = 0,k26 = ` (expected ``) 
- **N1** e^pi > pi^e => true -- `is(%e^%pi>%pi^%e)` -> `true` (expected `true`) 
- **N4** |x| < 1 => -1 < x < 1 (true) -- `assume(abs(x)<1) ; is(-1<x and x<1)` -> `true` (expected `true`) 
- **N5** x > y > 0 => 2 x^2 > 2 y^2 -- `assume(x>y) ; assume(y>0) ; is(2*x^2>2*y^2)` -> `true` (expected `true`) 
- **N6** x > y > 0, k > 0 => k x^2 > k y^2 -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; is(k*x^2>k*y^2)` -> `true` (expected `true`) 
- **N9** x >= y >= z >= x => x = y -- `assume(x>=y) ; assume(y>=z) ; assume(z>=x) ; is(equal(x,y))` -> `true` (expected `true`) 
- **N10** |x - 1| > 2 => x < -1 or x > 3 -- `fourier_elim([abs(x-1)>2],[x])` -> `[x < -1] or [3 < x]` (expected `x<-1 or x>3`) 
- **N11** (x-1)...(x-5) < 0 expanded => x < 1 or 2 < x < 3 or 4 < x < 5 -- `fourier_elim([expand((x-1)*(x-2)*(x-3)*(x-4)*(x-5))<0],[x])` -> `[4 < x,x < 5] or [2 < x,x < 3] or [x < 1]` (expected `x<1 or (2<x and x<3) or (4<x and x<5)`) 
- **N12** 6/(x - 3) <= 3 => x < 3 or x >= 5 -- `fourier_elim([6/(x-3)<=3],[x])` -> `[x = 5] or [5 < x] or [x < 3]` (expected `x<3 or x>=5`) 
- **N13** sqrt(x) < 2 => 0 <= x < 4 -- `fourier_elim([sqrt(x)<2],[x])` -> `[2-sqrt(x) > 0]` (expected `0<=x and x<4`) 
- **N14** sin x < 2 => all real x -- `fourier_elim([sin(x)<2],[x])` -> `universalset` (expected `true`) 
- **N15** sin x < 1 => x != pi/2 + 2 n pi -- `fourier_elim([sin(x)<1],[x])` -> `[1-sin(x) > 0]` (expected `sin(x)<1`) 
- **N16** |2 A (cos t - 1) + 1| <= 1 => 0 <= A <= 1/(1 - cos t) -- `fourier_elim([abs(2*a*(cos(t)-1)+1)<=1],[a])` -> `[2*a*cos(t)-2*a+1 = 0,cos(t)-1 = 0] or [a = 0,1 = 0] or [2*a*cos(t)-2*a+1 > 0,cos(t)-1 = 0] or [a = 0,1 > 0] or [-(2*a*cos(t)-2*a+1) > 0,a*cos(t)-a+1 = 0] or [0 < a,a*cos(t)-a+1 > 0,-(cos(t)-1) > 0]` (expected `0<=a and a<=1/(1-cos(t))`) 
- **N17** A^2 (cos t - 4)^2 sin^2 t < 9 => |A| < 3/|(cos t - 4) sin t| -- `fourier_elim([a^2*(cos(t)-4)^2*sin(t)^2<9],[a])` -> `[-(a*cos(t)*sin(t)-4*a*sin(t)-3) > 0,a*cos(t)*sin(t)-4*a*sin(t)+3 > 0] or [a*cos(t)*sin(t)-4*a*sin(t)-3 > 0,-(a*cos(t)*sin(t)-4*a*sin(t)+3) > 0]` (expected `abs(a)<3/abs((cos(t)-4)*sin(t))`) 
- **N18** x + y > 0 and x - y < 0 => |x| < y -- `fourier_elim([x+y>0,x-y<0],[x,y])` -> `[-y < x,x < y,0 < y]` (expected `abs(x)<y`) -y < x < y (so y > 0) is |x| < y
- **O1** norm([1 + i, -2, 3 i]) => sqrt(15) -- `mat_norm(matrix([1+%i,-2,3*%i]),frobenius)` -> `sqrt(15)` (expected `sqrt(15)`) 
- **O2** (2, 2, -3) x (1, 3, 1) => (11, -5, 4) -- `express([2,2,-3] ~ [1,3,1])` -> `[11,-5,4]` (expected `[11,-5,4]`) 
- **O4** curl(x y z, x^2 y^2 z^2, y^2 z^3) -- `(scalefactors([x,y,z]),ev(express(curl([x*y*z,x^2*y^2*z^2,y^2*z^3])),diff))` -> `[2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z]` (expected `[2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z]`) 
- **O8** scalar potential of (2xy, x^2 + 1, 6z^2) => x^2 y + y + 2z^3 -- `(scalefactors([x,y,z]),potential([2*x*y,x^2+1,6*z^2]))` -> `2*z^3+(x^2+1)*y` (expected `x^2*y+y+2*z^3`) 
- **O9** vector potential of the O4 curl -- `(scalefactors([x,y,z]),vectorpotential([2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z]))` -> `[x*y*z,-((y*z^4-2*x^2*y^2*z^2)/2),0]` (expected `[x*y*z,x^2*y^2*z^2,y^2*z^3]`) the curl of [x y z, x^2 y^2 z^2 - y z^4/2, 0] equals the field (checked with SymPy)
- **O10** Gram-Schmidt of [0,1,2,1], [0,1,3,1], [1,1,1,0], [1,3,6,2] -- `gramschmidt(matrix([0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]))` -> `[[0,1,2,1],[0,-(1/3),1/3,-(1/3)],[1,1/2,0,-(1/2)],[0,0,0,0]]` (expected `[[0,1,2,1],[0,-1,1,-1],[2,1,0,-1]]`) orthogonal (not normalized) basis of the span; the dependent 4th vector comes back as the zero vector (the convention accepted for Giac)
- **P2** (2,3)-minor matrix => [[1, 2], [7, 8]] -- `minor(matrix([1,2,3],[4,5,6],[7,8,9]),2,3)` -> `matrix([1,2],[7,8])` (expected `[[1,2],[7,8]]`) 
- **P4** block diagonal matrix from [[a,1],[0,a]], b, [[c,1,0],[0,c,1],[0,0,c]] -- `diag([matrix([a,1],[0,a]),matrix([b]),matrix([c,1,0],[0,c,1],[0,0,c])])` -> `matrix([a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c])` (expected `[[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]`) 
- **P5** [[7, 11], [3, 8]] mod 2 => [[1, 1], [1, 0]] -- `matrixmap(lambda([u],mod(u,2)),matrix([7,11],[3,8]))` -> `matrix([1,1],[1,0])` (expected `[[1,1],[1,0]]`) 
- **P6** d^2/dt^2 [[cos t, sin t], [-sin t, cos t]] -- `diff(matrix([cos(t),sin(t)],[-sin(t),cos(t)]),t,2)` -> `matrix([-cos(t),-sin(t)],[sin(t),-cos(t)])` (expected `[[-cos(t),-sin(t)],[sin(t),-cos(t)]]`) 
- **P7** [[x, y]] (a [[1,3,5],[2,4,6]] + [[7,-9,11],[-8,10,-12]]) -- `matrix([x,y]) . (a*matrix([1,3,5],[2,4,6])+matrix([7,-9,11],[-8,10,-12]))` -> `matrix([(2*a-8)*y+(a+7)*x,(4*a+10)*y+(3*a-9)*x,(6*a-12)*y+(5*a+11)*x])` (expected `[[(a+7)*x+(2*a-8)*y,(3*a-9)*x+(4*a+10)*y,(5*a+11)*x+(6*a-12)*y]]`) 
- **P8** infinity norm of [[1, -2i], [-3i, 4]] => 7 -- `mat_norm(matrix([1,-2*%i],[-3*%i,4]),inf)` -> `7` (expected `7`) 
- **P10** Hermitian transpose of [[1, 2+3i], [f(4-5i), 6]] -- `conjugate(transpose(matrix([1,2+3*%i],[f(4-5*%i),6])))` -> `matrix([1,conjugate(f(4-5*%i))],[2-3*%i,6])` (expected `[[1,f(4+5*i)],[2-3*i,6]]`) conj(f(4-5i)) is the exact Hermitian-transpose entry for an unknown f; Wester's f(4+5i) assumes conj(f(z)) = f(conj(z)) (as for Giac and SymPy)
- **P11** inverse of [[a, b], [1, a b]] => 1/(a^2 - 1) [[a, -1], [-1/b, a/b]] -- `invert(matrix([a,b],[1,a*b]))` -> `matrix([(a*b)/(a^2*b-b),-(b/(a^2*b-b))],[-(1/(a^2*b-b)),a/(a^2*b-b)])` (expected `[[a/(a^2-1),-1/(a^2-1)],[-1/(b*(a^2-1)),a/(b*(a^2-1))]]`) 
- **P13** LU decomposition of a symbolic matrix (Wood) -- `block([r:get_lu_factors(lu_factor(matrix([1,x-2,x-3],[x-1,x^2-3*x+6,x^2-3*x-2],[x-2,x^2-8,2*x^2-12*x+14])))],[r[2],r[3],transpose(r[1])])` -> `[matrix([1,0,0],[x-1,1,0],[x-2,(x^2-(x-2)^2-8)/(x^2-3*x-(x-2)*(x-1)+6),1]),matrix([1,x-2,x-3],[0,x^2-3*x-(x-2)*(x-1)+6,x^2-3*x-(x-3)*(x-1)-2],[0,0,-(((x^2-(x-2)^2-8)*(x^2-3*x-(x-3)*(x-1)-2))/(x^2-3*x-` (expected `[[[1,0,0],[x-1,1,0],[x-2,x-3,1]],[[1,x-2,x-3],[0,4,x-5],[0,0,x-7]],[[1,0,0],[0,1,0],[0,0,1]]]`) 
- **P15** rank => 2 -- `rank(matrix([-1,3,7,-5],[4,-2,1,3],[2,4,15,-7]))` -> `2` (expected `2`) 
- **P18** null space => span{[2,4,1,0], [0,-3,0,1]} -- `nullspace(matrix([1,0,-2,0],[-2,1,0,3],[-1,2,-6,6]))` -> `span(matrix([-2],[-4],[-1],[0]),matrix([0],[6],[0],[-2]))` (expected `[[2,4,1,0],[0,-3,0,1]]`) 
- **P19** factor the 4x4 Vandermonde determinant -- `factor(determinant(matrix([1,1,1,1],[w,x,y,z],[w^2,x^2,y^2,z^2],[w^3,x^3,y^3,z^3])))` -> `(x-w)*(y-w)*(y-x)*(z-w)*(z-x)*(z-y)` (expected `(w-x)*(w-y)*(w-z)*(x-y)*(x-z)*(y-z)`) 
- **P20** minimal polynomial => (lambda - 1)^2 (lambda + 1) -- `subst(x,x,minimalPoly(jordan(matrix([17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]))))` -> `(x-1)^2*(x+1)` (expected `(x-1)^2*(x+1)`) 
- **P21** characteristic polynomial => x^3 - 2x^2 - 5x + 6 -- `expand((-1)^length(matrix([5,-3,-7],[-2,1,2],[2,-3,-4]))*charpoly(matrix([5,-3,-7],[-2,1,2],[2,-3,-4]),x))` -> `x^3-2*x^2-5*x+6` (expected `x^3-2*x^2-5*x+6`) 
- **P21b** eigenvalues from it => {1, -2, 3} -- `wsolve(expand((-1)^length(matrix([5,-3,-7],[-2,1,2],[2,-3,-4]))*charpoly(matrix([5,-3,-7],[-2,1,2],[2,-3,-4]),x))=0,x)` -> `[3,-2,1]` (expected `[1,-2,3]`) 
- **P23** eigenvalues of the 5x5 tridiagonal [1 2 1] matrix -- `first(eigenvalues(matrix([2,1,0,0,0],[1,2,1,0,0],[0,1,2,1,0],[0,0,1,2,1],[0,0,0,1,2])))` -> `[2,3,2-sqrt(3),sqrt(3)+2,1]` (expected `[2-sqrt(3),1,2,3,2+sqrt(3)]`) 
- **P24** exact eigenvalues of the Rosser matrix -- `first(eigenvalues(matrix([611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99])))` -> `[510-100*sqrt(26),100*sqrt(26)+510,1020,-(10*sqrt(10405)),10*sqrt(10405),1000,0]` (expected `[-10*sqrt(10405),0,510-100*sqrt(26),1000,510+100*sqrt(26),1020,10*sqrt(10405)]`) 
- **P25** numeric eigenvalues of the Rosser matrix -- `first(eigenvalues(rectform(float(matrix([611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99])))))` -> `[510-100*sqrt(26),100*sqrt(26)+510,1020,-(10*sqrt(10405)),10*sqrt(10405),1000,0]` (expected `[-1020.0490184299969,0,0.09804864072151699,1000,1019.9019513592784,1020,1020.0490184299969]`) 
- **P27** eigenvectors of a 5x5 matrix with eigenvalues {a, a, a, 1 - i, 1 + i} -- `block([e:eigenvectors(matrix([a,0,0,0,0],[0,0,0,0,1],[0,0,a,0,0],[0,0,0,a,0],[0,-2,0,0,2]))],makelist([e[1][1][k],e[2][k]],k,1,length(e[1][1])))` -> `[[1-%i,[[0,1,0,0,1-%i]]],[%i+1,[[0,1,0,0,%i+1]]],[a,[[1,0,0,0,0],[0,0,1,0,0],[0,0,0,1,0]]]]` (expected ``) 
- **P30** Jordan form => diag([[1,1],[0,1]], [[1,1],[0,1]], -1) -- `dispJordan(jordan(matrix([1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2])))` -> `matrix([-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1])` (expected `[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]`) Jordan form: -1 and blocks [[1,1],[0,1]] twice
- **P32** matrix exponential of [[1, -2], [2, 1]] => e [[cos 2, -sin 2], [sin 2, cos 2]] -- `mat_function(exp,matrix([1,-2],[2,1]))` -> `matrix([%e^(2*%i+1)/2+%e^(1-2*%i)/2,(%e^(2*%i+1)*%i)/2-(%e^(1-2*%i)*%i)/2],[(%e^(1-2*%i)*%i)/2-(%e^(2*%i+1)*%i)/2,%e^(2*%i+1)/2+%e^(1-2*%i)/2])` (expected `[[e*cos(2),-e*sin(2)],[e*sin(2),e*cos(2)]]`) 
- **P33** matrix exponential exp(M t) (Niles) -- `mat_function(exp,matrix([0,1,0,0],[0,0,0,2*w],[0,0,0,1],[0,-2*w,3*w^2,0])*t)` -> `matrix([1,-((2*%e^(%i*t*w)*%i)/w)+(2*%e^-(%i*t*w)*%i)/w-3*t,6*t*w+3*%e^(%i*t*w)*%i-3*%e^-(%i*t*w)*%i,-(%e^(%i*t*w)/w)-%e^-(%i*t*w)/w+2/w],[0,2*%e^(%i*t*w)+2*%e^-(%i*t*w)-3,-(3*%e^(%i*t*w)*w)-3*%e^-(%i` (expected `[[1,4*sin(w*t)/w-3*t,6*(w*t-sin(w*t)),2/w*(1-cos(w*t))],[0,4*cos(w*t)-3,6*w*(1-cos(w*t)),2*sin(w*t)],[0,-2/w*(1-cos(w*t)),4-3*cos(w*t),sin(w*t)/w],[0,-2*sin(w*t),3*w*sin(w*t),cos(w*t)]]`) 
- **P34** sine of a Jordan matrix -- `mat_function(sin,matrix([a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]))` -> `matrix([sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-(sin(c)/2)],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)])` (expected `[[sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-sin(c)/2],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)]]`) 
- **P35** sin(pi/2 [[2,1,1],[2,3,2],[1,1,2]]) => identity -- `mat_function(sin,%pi/2*matrix([2,1,1],[2,3,2],[1,1,2]))` -> `matrix([1,0,0],[0,1,0],[0,0,1])` (expected `[[1,0,0],[0,1,0],[0,0,1]]`) 
- **P36** matrix square root of [[10, 7], [7, 17]] -- `mat_function(sqrt,matrix([10,7],[7,17]))` -> `matrix([((sqrt(5)-1)*sqrt(7*sqrt(5)+27))/(2^(3/2)*((sqrt(5)+1)/2+(sqrt(5)-1)/2))+(sqrt(27-7*sqrt(5))*(sqrt(5)+1))/(2^(3/2)*((sqrt(5)+1)/2+(sqrt(5)-1)/2)),sqrt(7*sqrt(5)+27)/(sqrt(2)*((sqrt(5)+1)/2+(sq` (expected `[[10,7],[7,17]]`) 
- **P37** square root of [[1,1,0],[0,1,0],[0,0,1]] => [[1,1/2,0],[0,1,0],[0,0,1]] -- `mat_function(sqrt,matrix([1,1,0],[0,1,0],[0,0,1]))` -> `matrix([1,1/2,0],[0,1,0],[0,0,1])` (expected `[[1,1,0],[0,1,0],[0,0,1]]`) 
- **P40** Jacobian of (r cos t, r sin t) -- `jacobian([r*cos(t),r*sin(t)],[r,t])` -> `matrix([cos(t),-(r*sin(t))],[sin(t),r*cos(t)])` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) 
- **P41** Hessian of r^2 sin t -- `hessian(r^2*sin(t),[r,t])` -> `matrix([2*sin(t),2*r*cos(t)],[2*r*cos(t),-(r^2*sin(t))])` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P42** Wronskian matrix of (cos t, sin t) -- `wronskian([cos(t),sin(t)],t)` -> `matrix([cos(t),sin(t)],[-sin(t),cos(t)])` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) 
- **P43** user-defined Jacobian -- `g(f):=makelist(diff(f,v_),v_,[r,t]) ; map(g,[r*cos(t),r*sin(t)])` -> `[[cos(t),-(r*sin(t))],[sin(t),r*cos(t)]]` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) 
- **P44** user-defined Hessian -- `g(f):=makelist(diff(f,v_),v_,[r,t]) ; map(g,makelist(diff(r^2*sin(t),v_),v_,[r,t]))` -> `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-(r^2*sin(t))]]` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P45** user-defined Wronskian -- `g(f):=[f,diff(f,t)] ; transpose(apply(matrix,map(g,[cos(t),sin(t)])))` -> `matrix([cos(t),sin(t)],[-sin(t),cos(t)])` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) 
- **R3** sum((-1)^k binomial(2n, k)^2, k = 0..2n) => (-1)^n binomial(2n, n) -- `declare(n,integer) ; simplify_sum(sum((-1)^k*binomial(2*n,k)^2,k,0,2*n))` -> `((-1)^n*2^(2*n)*((2*n-1)/2)!)/(sqrt(%pi)*n!)` (expected `(-1)^n*binomial(2*n,n)`) 
- **R6** telescoping sum(g(k+1) - g(k), k = 0..n) => g(n+1) - g(0) -- `simplify_sum(sum(g(k+1)-g(k),k,0,n))` -> `g(n+1)-g(0)` (expected `g(n+1)-g(0)`) 
- **R7** sum(k^3, k = 1..n) => n^2 (n+1)^2/4 -- `simplify_sum(sum(k^3,k,1,n))` -> `(n^4+2*n^3+n^2)/4` (expected `n^2*(n+1)^2/4`) 
- **R8** sum(k^2 binomial(n, k), k = 1..n) => n (n+1) 2^(n-2) -- `simplify_sum(sum(k^2*binomial(n,k),k,1,n))` -> `(2^n*n^2+2^n*n)/4` (expected `n*(n+1)*2^(n-2)`) 
- **R9** sum(binomial(n, k)/(k + 1), k = 0..n) => (2^(n+1) - 1)/(n + 1) -- `simplify_sum(sum(binomial(n,k)/(k+1),k,0,n))` -> `(2^(n+1)-1)/(n+1)` (expected `(2^(n+1)-1)/(n+1)`) 
- **R11** sum(binomial(n, k) fib(k), k = 0..n) => fib(2n) -- `simplify_sum(sum(binomial(n,k)*fib(k),k,0,n))` -> `((%phi+1)^n*(2*%phi-1)-2*(2-%phi)^n*%phi+(2-%phi)^n)/5` (expected `fibonacci(2*n)`) 
- **R12** sum(fib(k)^2, k = 1..n) => fib(n) fib(n+1) -- `simplify_sum(sum(fib(k)^2,k,1,n))` -> `(%phi^(2*n+1)-(2-%phi)^n*%phi+(2-%phi)^n-(-1)^n)/5` (expected `fibonacci(n)*fibonacci(n+1)`) 
- **R13** sum(sin(k x), k = 1..n) => cot(x/2)/2 - cos((2n+1) x/2)/(2 sin(x/2)) -- `simplify_sum(sum(sin(k*x),k,1,n))` -> `-(((cos(x)-1)*sin((n+1)*x)-sin(x)*cos((n+1)*x)+sin(x))/(2*cos(x)-2))` (expected `cot(x/2)/2-cos((2*n+1)*x/2)/(2*sin(x/2))`) 
- **R14** sum(sin((2k - 1) x), k = 1..n) => sin(n x)^2/sin x -- `simplify_sum(sum(sin((2*k-1)*x),k,1,n))` -> `(((%i*sin(x)*cos(2*x)-%i*sin(x))*sin(3*x)+(sin(x)-sin(x)*cos(2*x))*cos(3*x)+(cos(x)*sin(x)-%i*sin(x)^2)*cos(2*x)+%i*sin(x)^2-cos(x)*sin(x))*sin((2*n+2)*x)+((sin(x)*cos(2*x)-sin(x))*sin(3*x)+(%i*sin(x)` (expected `sin(n*x)^2/sin(x)`) 
- **R16** sum(1/k^2 + 1/k^3, k = 1..inf) => pi^2/6 + zeta(3) -- `simplify_sum(sum(1/k^2+1/k^3,k,1,inf))` -> `zeta(3)+%pi^2/6` (expected `pi^2/6+zeta(3)`) 
- **R17** numeric value of R16 => 2.84699 -- `block([fpprec:15],bfloat(simplify_sum(sum(1/k^2+1/k^3,k,1,inf))))` -> `2.84699097000782b0` (expected `2.8469909700078206`) 
- **R19** sum(1/((3k+1)(3k+2)(3k+3)), k = 0..inf) => sqrt(3) pi/12 - log(3)/4 -- `simplify_sum(sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf))` -> `(%pi/(2*sqrt(3))-(3*log(3))/2)/3-(-((3*log(3))/2)-%pi/(2*sqrt(3)))/6` (expected `sqrt(3)*pi/12-ln(3)/4`) 
- **R21** sum(1/(sqrt(k (k+1)) (sqrt k + sqrt(k+1))), k = 1..inf) => 1 -- `simplify_sum(sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf))` -> `1` (expected `1`) 
- **R23** equivalent double sum -- `simplify_sum(sum(simplify_sum(sum(n!/(k!^2*(n-2*k)!)*(x/y)^k*(x*y)^(n-k),n,2*k,inf)),k,0,inf))` -> `-(sqrt(x^2*y^2-2*x*y+1)/((x*y-1)*sqrt(x^2*y^2-2*x*y-4*x^2+1)))` (expected `1/sqrt((1-x*y)^2-4*x^2)`) 
- **S2** product(k, k = 1..n) => n! -- `ev(product(k,k,1,n),simpproduct)` -> `n!` (expected `factorial(n)`) 
- **T1** limit((1 + 1/n)^n, n, inf) => e -- `limit((1+1/n)^n,n,inf)` -> `%e` (expected `e`) 
- **T2** limit((1 - cos x)/x^2, x, 0) => 1/2 -- `limit((1-cos(x))/x^2,x,0)` -> `1/2` (expected `1/2`) 
- **T3** limit((3^x + 5^x)^(1/x), x, inf) => 5 (Gruntz) -- `limit((3^x+5^x)^(1/x),x,inf)` -> `5` (expected `5`) 
- **T4** limit(log x/(log x + sin x), x, inf) => 1 -- `limit(log(x)/(log(x)+sin(x)),x,inf)` -> `1` (expected `1`) 
- **T5** Gruntz: limit((exp(x e^-x/(e^-x + e^(-2x^2/(x+1)))) - e^x)/x, x, inf) => -e^2 -- `limit((exp(x*exp(-x)/(exp(-x)+exp(-2*x^2/(x+1))))-exp(x))/x,x,inf)` -> `-%e^2` (expected `-exp(2)`) 
- **T6** Gruntz: x log x log(x e^x - x^2)^2/log(log(x^2 + 2 e^(e^(3 x^3 log x)))) => 1/3 -- `limit(x*log(x)*log(x*exp(x)-x^2)^2/log(log(x^2+2*exp(exp(3*x^3*log(x))))),x,inf)` -> `1/3` (expected `1/3`) 
- **T7** limit(n!^(1/n)/n, n, inf) => 1/e -- `limit(n!^(1/n)/n,n,inf)` -> `%e^-1` (expected `exp(-1)`) 
- **T8** limit(gamma(n + 1)^(1/n)/n, n, inf) => 1/e -- `limit(gamma(n+1)^(1/n)/n,n,inf)` -> `%e^-1` (expected `exp(-1)`) 
- **T9** limit(gamma(z + a)/gamma(z) exp(-a log z), z, inf) => 1 (a > 0) -- `assume(a>0) ; limit(gamma(z+a)/gamma(z)*exp(-a*log(z)),z,inf)` -> `1` (expected `1`) 
- **T10** same for a < 0 => 1 -- `assume(a<0) ; limit(gamma(z+a)/gamma(z)*exp(-a*log(z)),z,inf)` -> `1` (expected `1`) 
- **T11** limit(2F1(1, k; 1; z/k), k, inf) => e^z -- `limit(hgfred([1,k],[1],z/k),k,inf)` -> `%e^z` (expected `exp(z)`) 
- **T15** one-sided limits of x/|x| at 0 => [-1, 1] -- `[limit(x/abs(x),x,0,minus),limit(x/abs(x),x,0,plus)]` -> `[-1,1]` (expected `[-1,1]`) 
- **T16** limit(atan(-log x), x, 0, +) => pi/2 -- `limit(atan(-log(x)),x,0,plus)` -> `%pi/2` (expected `pi/2`) 
- **U1** d/dx |x| => sign(x) -- `diff(abs(x),x)` -> `x/abs(x)` (expected `sign(x)`) 
- **U8** implicit differentiation of y = cos(x y) + x => (1 - y sin(x y))/(1 + x sin(x y)) -- `block([s],depends(y,x),s:solve(diff(y=cos(x*y)+x,x),diff(y,x)),rhs(first(s)))` -> `-((y*sin(x*y)-1)/(x*sin(x*y)+1))` (expected `(1-y*sin(x*y))/(1+x*sin(x*y))`) 
- **U10** residue((z^3 + 5)/((z^4 - 1)(z + 1)), z, -1) => -9/4 -- `residue((z^3+5)/((z^4-1)*(z+1)),z,-1)` -> `-(9/4)` (expected `-9/4`) 
- **V1** int |x| dx => x |x|/2 -- `integrate(abs(x),x)` -> `(x*abs(x))/2` (expected `abs(x)`) 
- **V5** int 1/(x^3 + 2) dx -- `integrate(1/(x^3+2),x)` -> `-(log(x^2-2^(1/3)*x+2^(2/3))/(3*2^(5/3)))+atan((2*x-2^(1/3))/(2^(1/3)*sqrt(3)))/(2^(2/3)*sqrt(3))+log(x+2^(1/3))/(3*2^(2/3))` (expected `1/(x^3+2)`) 
- **V7** int (3x - 5)^2/(2x - 1)^(7/2) dx -- `integrate((3*x-5)^2/(2*x-1)^(7/2),x)` -> `-((45*(2*x-1)^2-70*(2*x-1)+49)/(20*(2*x-1)^(5/2)))` (expected `(3*x-5)^2/(2*x-1)^(7/2)`) 
- **V8** int 1/(2 e^(m x) - 5 e^(-m x)) dx -- `integrate(1/(2*exp(m*x)-5*exp(-m*x)),x)` -> `log((10*%e^-(m*x)-2*sqrt(10))/(10*%e^-(m*x)+2*sqrt(10)))/(2*sqrt(10)*m)` (expected `1/(2*exp(m*x)-5*exp(-m*x))`) 
- **V9** int sinh^4 x/cosh^2 x dx -- `integrate(sinh(x)^4/cosh(x)^2,x)` -> `(-(6*x)+(17*%e^-(2*x)+1)/(2*%e^-(2*x)+2*%e^-(4*x))-%e^-(2*x)/2)/4` (expected `sinh(x)^4/cosh(x)^2`) 
- **V10** int 1/(a + b cos x) dx (a^2 < b^2) -- `assume(a>0,b>a) ; integrate(1/(a+b*cos(x)),x)` -> `-(log((((2*b-2*a)*sin(x))/(cos(x)+1)-2*sqrt(b^2-a^2))/(((2*b-2*a)*sin(x))/(cos(x)+1)+2*sqrt(b^2-a^2)))/sqrt(b^2-a^2))` (expected `1/(a+b*cos(x))`) 
- **V11** int 1/(3 + 3 cos x + 4 sin x) dx => log(3 + 4 tan(x/2))/4 -- `integrate(1/(3+3*cos(x)+4*sin(x)),x)` -> `log((4*sin(x))/(cos(x)+1)+3)/4` (expected `1/(3+3*cos(x)+4*sin(x))`) 
- **V12** int 1/(4 + 3 cos x + 4 sin x) dx -- `integrate(1/(4+3*cos(x)+4*sin(x)),x)` -> `2*(log(sin(x)/(cos(x)+1)+1)/6-log(sin(x)/(cos(x)+1)+7)/6)` (expected `1/(4+3*cos(x)+4*sin(x))`) 
- **V13** int 1/(5 + 3 cos x + 4 sin x) dx => -1/(2 + tan(x/2)) -- `integrate(1/(5+3*cos(x)+4*sin(x)),x)` -> `-(2/((2*sin(x))/(cos(x)+1)+4))` (expected `1/(5+3*cos(x)+4*sin(x))`) 
- **V14** int 1/(6 + 3 cos x + 4 sin x) dx -- `integrate(1/(6+3*cos(x)+4*sin(x)),x)` -> `(2*atan(((6*sin(x))/(cos(x)+1)+8)/(2*sqrt(11))))/sqrt(11)` (expected `1/(6+3*cos(x)+4*sin(x))`) 
- **V15** int log|x^2 - a^2| dx -- `integrate(log(abs(x^2-a^2)),x)` -> `x*log(abs(x^2-a^2))-2*(-((a*log(x+a))/2)+(a*log(x-a))/2+x)` (expected `ln(abs(x^2-a^2))`) 
- **V16** int x acot(x/a) dx -- `integrate(x*acot(x/a),x)` -> `(a^2*x-a^3*atan(x/a))/(2*a)+(x^2*acot(x/a))/2` (expected `x*acot(x/a)`) 
- **W1** int 1/(x - a), x = a-1..a+1 => 0 (principal value) -- `integrate(1/(x-a),x,a-1,a+1)` -> `0` (expected `0`) 
- **W2** int 1/(x - a)^2, x = a-1..a+1 => divergent -- `integrate(1/(x-a)^2,x,a-1,a+1)` -> `defint: integral is divergent.` (expected ``) error expected: Maxima error: defint: integral is divergent.
- **W3** int sqrt(x + 1/x - 2), x = 0..1 => 4/3 -- `integrate(sqrt(x+1/x-2),x,0,1)` -> `4/3` (expected `4/3`) 
- **W4** int sqrt(x + 1/x - 2), x = 1..2 => (4 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,1,2)` -> `4/3-2^(3/2)/3` (expected `(4-sqrt(8))/3`) 
- **W6** int sqrt(2 - 2 cos 2x)/2, x = -3 pi/4..-pi/4 => sqrt 2 (Kahan) -- `integrate(sqrt(2-2*cos(2*x))/2,x,-3*%pi/4,-%pi/4)` -> `((2^(3/2)+4)/2-(4-2^(3/2))/2)/2` (expected `sqrt(2)`) 
- **W7** int cos x/(x^2 + a^2), x = -inf..inf => pi e^(-a)/a (a > 0) -- `assume(a>0) ; integrate(cos(x)/(x^2+a^2),x,minf,inf)` -> `(%e^-a*%pi)/a` (expected `pi*exp(-a)/a`) 
- **W8** int t^(a-1)/(1 + t), t = 0..inf => pi/sin(pi a) (0 < a < 1) -- `assume(a>0,a<1) ; integrate(t^(a-1)/(1+t),t,0,inf)` -> `beta(1-a,a)` (expected `pi/sin(pi*a)`) 
- **W12** int x exp(-p x^2 + 2 q x), x = -inf..inf => q/p sqrt(pi/p) e^(q^2/p) -- `assume(p>0) ; integrate(x*exp(-p*x^2+2*q*x),x,minf,inf)` -> `(%e^(q^2/p)*sqrt(%pi)*q)/p^(3/2)` (expected `q/p*sqrt(pi/p)*exp(q^2/p)`) 
- **W13** int 1/log t + 1/(1 - t) - log(log(1/t)), t = 0..1 => 2 gamma -- `integrate(1/log(t)+1/(1-t)-log(log(1/t)),t,0,1)` -> `2*%gamma` (expected `2*euler_gamma`) 
- **W14** int sin t/t exp(2 i t), t = -inf..inf => 0 -- `integrate(sin(t)/t*exp(2*%i*t),t,minf,inf)` -> `0` (expected `0`) 
- **W16** int (1 + x)^3 P1(x) P2(x), x = -1..1 => 36/35 -- `integrate((1+x)^3*legendre_p(1,x)*legendre_p(2,x),x,-1,1)` -> `36/35` (expected `36/35`) 
- **W16b** same with the Legendre polynomials written out -- `integrate((1+x)^3*x*(3*x^2-1)/2,x,-1,1)` -> `36/35` (expected `36/35`) 
- **W21** numeric value of W20 => 0.210883 -- `first(quad_qags(x^2*li[3](1/(x+1)),x,0,1))` -> `0.21088285956566424` (expected `0.210882859565594`) 
- **W33** volume of a tetrahedron => a b c/6 -- `integrate(integrate(integrate(1,z,0,c*(1-x/a-y/b)),y,0,b*(1-x/a)),x,0,a)` -> `(a*b*c)/6` (expected `a*b*c/6`) 
- **X1** Taylor 1/sqrt(1 - (v/c)^2) to O(v^8) -- `ratdisrep(taylor(1/sqrt(1-(v/c)^2),v,0,(8)-1))` -> `(5*v^6)/(16*c^6)+(3*v^4)/(8*c^4)+v^2/(2*c^2)+1` (expected `1+v^2/(2*c^2)+3*v^4/(8*c^4)+5*v^6/(16*c^6)`) 
- **X2** series of 1/s1^2 => 1 - v^2/c^2 -- `s1:ratdisrep(taylor(1/sqrt(1-(v/c)^2),v,0,(8)-1)) ; ratdisrep(taylor(1/s1^2,v,0,(8)-1))` -> `1-v^2/c^2` (expected `1-v^2/c^2`) 
- **X3** series(sin)/series(cos) => tan series -- `ratdisrep(taylor(ratdisrep(taylor(sin(x),x,0,(8)-1))/ratdisrep(taylor(cos(x),x,0,(8)-1)),x,0,(8)-1))` -> `(17*x^7)/315+(2*x^5)/15+x^3/3+x` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X4** tan(x) series to O(x^8) -- `ratdisrep(taylor(tan(x),x,0,(8)-1))` -> `(17*x^7)/315+(2*x^5)/15+x^3/3+x` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X5** log(sin x/x) => -x^2/6 - x^4/180 - x^6/2835 -- `ratdisrep(taylor(log(sin(x)/x),x,0,(8)-1))` -> `-(x^6/2835)-x^4/180-x^2/6` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X6** log of the series of sin x/x -- `ratdisrep(taylor(log(ratdisrep(taylor(sin(x)/x,x,0,(8)-1))),x,0,(8)-1))` -> `-(x^6/2835)-x^4/180-x^2/6` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X9** Laurent series of 1/(x (e^x - 1)) -- `ratdisrep(taylor(1/(x*(exp(x)-1)),x,0,(7)-1))` -> `-(x^6/1209600)+x^4/30240-x^2/720-1/(2*x)+1/x^2+1/12` (expected `1/x^2-1/(2*x)+1/12-x^2/720+x^4/30240-x^6/1209600`) 
- **X10** Puiseux series of sqrt(sec x) at 3 pi/2 -- `ratdisrep(taylor(sqrt(sec(x)),x,3*%pi/2,4))` -> `(x-(3*%pi)/2)^(7/2)/160+(x-(3*%pi)/2)^(3/2)/12+1/sqrt(x-(3*%pi)/2)` (expected `1/sqrt(x-3*pi/2)+(x-3*pi/2)^(3/2)/12+(x-3*pi/2)^(7/2)/160`)  [translation uncertain: for a Puiseux series taylor's order counts the integer powers that multiply 1/sqrt(x-3pi/2); order 4 (not our order 4 - 1) is needed to keep the (x-3pi/2)^(7/2) term that our order 4 keeps (as for Giac)]
- **X11** generalized Taylor series of x^x -- `ratdisrep(taylor(x^x,x,0,(4)-1))` -> `(x^3*log(x)^3)/6+(x^2*log(x)^2)/2+x*log(x)+1` (expected `1+x*ln(x)+x^2*ln(x)^2/2+x^3*ln(x)^3/6`) 
- **X12** log(sinh z) + log(cosh(z + w)) => log z + log cosh w + tanh(w) z -- `ratdisrep(taylor(log(sinh(z))+log(cosh(z+w)),z,0,(2)-1))` -> `log(z)+(sinh(w)*z)/cosh(w)+log(cosh(w))` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X13** log(sinh z cosh(z + w)) => same -- `ratdisrep(taylor(log(sinh(z)*cosh(z+w)),z,0,(2)-1))` -> `log(z)+(sinh(w)*z)/cosh(w)+log(cosh(w))` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X14** log(x)^a exp(-b x) at x = 1 => (x-1)^a e^-b (1 - (a + 2b)(x - 1)/2) -- `ratdisrep(taylor(log(x)^a*exp(-b*x),x,1,(2)-1))` -> `%e^-b*(x-1)^a-(%e^-b*(2*b*(x-1)^a+a*(x-1)^a)*(x-1))/2` (expected `(x-1)^a*exp(-b)*(1-(a+2*b)*(x-1)/2)`) 
- **X15** asymptotic sqrt(2 x^2 + 1) => sqrt(2) x + O(1/x) -- `ratdisrep(taylor(sqrt(2*x^2+1),x,inf,1))` -> `sqrt(2)*x+1/(2^(3/2)*x)` (expected `sqrt(2)*x`) 
- **X18** multivariate Taylor cos(x + y) => 1 - (x + y)^2/2 -- `ratdisrep(taylor(cos(x+y),[x,y],[0,0],(4)-1))` -> `1-(y^2+2*x*y+x^2)/2` (expected `1-(x+y)^2/2`) 
- **X22** Pade approximation of e^-x => (2 - x)/(2 + x) -- `first(pade(taylor(exp(-x),x,0,(1)+(1)),1,1))` -> `-((x-2)/(x+2))` (expected `(2-x)/(2+x)`) 
- **X23** Fourier series of x on [-p, p]: b_n => -2 p (-1)^n/(n pi) -- `assume(p>0) ; declare(n,integer) ; ev(rhs(apply(ev,[fourier(x,x,(2*p)/2)[3]])),n=n)` -> `-((2*(-1)^n*p)/(%pi*n))` (expected `-2*p*(-1)^n/(n*pi)`) 
- **X23b** Fourier series of x on [-p, p]: a_n => 0 -- `assume(p>0) ; declare(n,integer) ; ev(rhs(apply(ev,[fourier(x,x,(2*p)/2)[2]])),n=n)` -> `0` (expected `0`) 
- **X24** Fourier series of |x| on [-p, p]: a_n => 2 p ((-1)^n - 1)/(pi^2 n^2) -- `assume(p>0) ; declare(n,integer) ; ev(rhs(apply(ev,[fourier(abs(x),x,(2*p)/2)[2]])),n=n)` -> `(2*(((-1)^n*p^2)/(%pi^2*n^2)-p^2/(%pi^2*n^2)))/p` (expected `2*p*((-1)^n-1)/(pi^2*n^2)`) 
- **X24b** Fourier series of |x|: constant term => p/2 -- `assume(p>0) ; rhs(apply(ev,[first(fourier(abs(x),x,(2*p)/2))]))` -> `p/2` (expected `p/2`) 
- **Y1** laplace(cos((w - 1) t)) => s/(s^2 + (w - 1)^2) -- `laplace(cos((w-1)*t),t,s)` -> `s/((w-1)^2+s^2)` (expected `s/(s^2+(w-1)^2)`) 
- **Y3** laplace(sinh(w t) cosh(w t)) => w/(s^2 - 4 w^2) -- `laplace(sinh(w*t)*cosh(w*t),t,s)` -> `-(w/(4*w^2-s^2))` (expected `w/(s^2-4*w^2)`) 
- **Z1** r(n+2) - 2 r(n+1) + r(n) = 2, r(0) = 1, r(1) = m => n^2 + (m - 2) n + 1 -- `solve_rec(f[n+2]-2*f[n+1]+f[n]=2,f[n],f[0]=1,f[1]=m)` -> `f[n] = n^2+(m-2)*n+1` (expected `n^2+(m-2)*n+1`) 
- **Z2** r(n) = 5 r(n-1) - 6 r(n-2), r(0) = 0, r(1) = 1 => 3^n - 2^n -- `solve_rec(f[n]=5*f[n-1]-6*f[n-2],f[n],f[0]=0,f[1]=1)` -> `f[n] = 3^n-2^n` (expected `3^n-2^n`) 
- **Z3** Fibonacci recurrence, r(1) = 1, r(2) = 2 => fib(n + 1) -- `solve_rec(f[n]=f[n-1]+f[n-2],f[n],f[1]=1,f[2]=2)` -> `f[n] = (2^(-n-1)*(sqrt(5)+1)^n*(sqrt(5)+5))/5-((-1)^n*2^(-n-1)*(sqrt(5)-5)*(sqrt(5)-1)^n)/5` (expected `fibonacci(n+1)`) 
- **Z5** f'' + 4 f = sin 2t, f(0) = f'(0) = 0 => sin(2t)/8 - t cos(2t)/4 -- `block([s_:ode2('diff(y,x,2)+4*y=sin(2*x),y,x)],if s_=false then false else ic2(s_,x=0,y=0,'diff(y,x)=0))` -> `y = sin(2*x)/8-(x*cos(2*x))/4` (expected `Y2+4*Y-sp.sin(2*x)`) 
- **Z6** same via the Laplace transform: ilaplace(2/(s^2 + 4)^2) -- `ilt(2/(s^2+4)^2,s,t)` -> `sin(2*t)/8-(t*cos(2*t))/4` (expected `sin(2*t)/8-t*cos(2*t)/4`) 
- **Z7** separable y' = x^2/(y (1 + x)^3) -- `ode2('diff(y,x)=x^2/(y*(1+x)^3),y,x)` -> `y^2/2 = ((2*x^2+4*x+2)*log(x+1)+4*x+3)/(2*x^2+4*x+2)+%c` (expected `Y1-x**2/(Y*(1+x)**3)`) implicit solution
- **Z8** homogeneous y' = y/x + x/y => y^2 = 2 x^2 log|A x| -- `ode2('diff(y,x)=y/x+x/y,y,x)` -> `(y^2-2*x^2*log(x))/(2*x^2) = %c` (expected `Y1-(Y/x+x/Y)`) implicit solution
- **Z9** linear x^2 y' + 3 x y = sin(x)/x => y = (A - cos x)/x^3 -- `ode2(x^2*'diff(y,x)+3*x*y=sin(x)/x,y,x)` -> `y = (%c-cos(x))/x^3` (expected `x**2*Y1+3*x*Y-sp.sin(x)/x`) 
- **Z10** exact y' = -(1 + 2 x sin y)/(1 + x^2 cos y) => x + x^2 sin y + y = A -- `ode2('diff(y,x)=-(1+2*x*sin(y))/(1+x^2*cos(y)),y,x)` -> `x^2*sin(y)+y+x = %c` (expected `Y1+(1+2*x*sp.sin(Y))/(1+x**2*sp.cos(Y))`) implicit solution
- **Z11** nonlinear y'' + y y'^3 = 0 => y^3/6 + A y = x + B -- `ode2('diff(y,x,2)+y*'diff(y,x)^3=0,y,x)` -> `(y^3+6*%k1*y)/6 = x+%k2` (expected `Y2+Y*Y1**3`) implicit solution
- **Z12** same with y(0) = 0, y'(0) = 2 => (3x + sqrt(1 + 9x^2))^(1/3) - 1/(3x + sqrt(1 + 9x^2))^(1/3) -- `block([s_:ode2('diff(y,x,2)+y*'diff(y,x)^3=0,y,x)],if s_=false then false else ic2(s_,x=0,y=0,'diff(y,x)=2))` -> `(y^3+3*y)/6 = x` (expected `Y2+Y*Y1**3`) implicit solution of the IVP: y(0) = 0 and (3y^2+3) y' = 6 gives y'(0) = 2; Cardano on y^3 + 3y = 6x gives Wester's explicit answer (checked with SymPy)
- **Z13** y' = a y (parametric) => A e^(a x) -- `ode2('diff(y,x)=a*y,y,x)` -> `y = %e^(a*x)*%c` (expected `Y1-a*Y`) 
- **Z18** integro-differential equation via Laplace => 5/13 (-8 e^-4t + e^-t (8 cos 2t + sin 2t)) -- `ilt(10*s/((s+4)*(s^2+2*s+5)),s,t)` -> `%e^-t*((5*sin(2*t))/13+(40*cos(2*t))/13)-(40*%e^-(4*t))/13` (expected `5/13*(-8*exp(-4*t)+exp(-t)*(8*cos(2*t)+sin(2*t)))`) 
- **Z19** system u' = u - v, v' = u + v -- `desolve([diff(u(t),t)=u(t)-v(t),diff(v(t),t)=u(t)+v(t)],[u(t),v(t)])` -> `[u(t) = %e^t*(((2*(-v(0)-u(0))+2*u(0))*sin(t))/2+u(0)*cos(t)),v(t) = %e^t*(((2*v(0)+2*(u(0)-v(0)))*sin(t))/2+v(0)*cos(t))]` (expected `[U.diff(t)-(U-V), V.diff(t)-(U+V)]`) 
- **Z21** 3x3 system with roots 2, 1, 3 -- `desolve([diff(u(t),t)=2*u(t),diff(v(t),t)=-2*u(t)+v(t)-2*w(t),diff(w(t),t)=u(t)+3*w(t)],[u(t),v(t),w(t)])` -> `[u(t) = %e^(2*t)*u(0),v(t) = %e^t*(w(0)+v(0)+u(0))+%e^(3*t)*(-w(0)-u(0)),w(t) = %e^(3*t)*(w(0)+u(0))-%e^(2*t)*u(0)]` (expected `[U.diff(t)-2*U, V.diff(t)-(-2*U+V-2*W), W.diff(t)-(U+3*W)]`) 
- **Z22** 3x3 system with roots 0, -1 +- sqrt(2) i -- `desolve([diff(u(t),t)=v(t),diff(v(t),t)=w(t),diff(w(t),t)=-3*v(t)-2*w(t)],[u(t),v(t),w(t)])` -> `[u(t) = %e^-t*((((2*(w(0)+2*v(0)))/3-(2*(2*w(0)+v(0)))/3)*sin(sqrt(2)*t))/2^(3/2)-((w(0)+2*v(0))*cos(sqrt(2)*t))/3)+(w(0)+2*v(0)+3*u(0))/3,v(t) = %e^-t*(((2*(w(0)+2*v(0))-2*v(0))*sin(sqrt(2)*t))/2^(3/` (expected `[U.diff(t)-V, V.diff(t)-W, W.diff(t)-(-3*V-2*W)]`) 
- **Z23** 3x3 system with the triple root 2 -- `desolve([diff(u(t),t)=3*u(t)-w(t),diff(v(t),t)=2*v(t),diff(w(t),t)=u(t)+w(t)],[u(t),v(t),w(t)])` -> `[u(t) = -(%e^(2*t)*w(0)*t)+%e^(2*t)*u(0)*t+%e^(2*t)*u(0),v(t) = %e^(2*t)*v(0),w(t) = -(%e^(2*t)*w(0)*t)+%e^(2*t)*u(0)*t+%e^(2*t)*w(0)]` (expected `[U.diff(t)-(3*U-W), V.diff(t)-2*V, W.diff(t)-(U+W)]`) 

## WRONG (15)

- **D2** exp(-1000000.0) => 3.29683e-434295 (underflow) -- `exp(-1000000.0)` -> `0.0` (expected `3.29683147808856e-434295`) relative error 1
- **E3** first quartile of [1..8] => 2 or 5/2 -- `quantile([1,2,3,4,5,6,7,8],1/4)` -> `11/4` (expected `[2,5/2]`) 
- **I10** trigsimp((tan^2 x + 1 - sec^2 x)/(sin^2 x + cos^2 x - 1)) => undefined (0/0) -- `trigsimp((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1))` -> `1/cos(x)^2` (expected ``) expected an error/divergence
- **I12** derivative of the 0/0 expression => undefined -- `diff((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1),x)` -> `0` (expected ``) expected an error/divergence
- **K8** sqrt(1/z) - 1/sqrt(z) does not vanish (z real negative) -- `wsimp(sqrt(1/z)-1/sqrt(z))` -> `0` (expected `sqrt(1/z)-1/sqrt(z)`) 
- **K10** sqrt(1/z) + 1/sqrt(z) => 0 for z < 0 -- `assume(z<0) ; wsimp(sqrt(1/z)+1/sqrt(z))` -> `2/sqrt(z)` (expected `0`) 
- **K13** sqrt(e^(6 i)) => -e^(3 i) -- `sqrt(exp(6*%i))` -> `%e^(3*%i)` (expected `-exp(3*i)`) 
- **K16** log(e^(10 i)) => (10 - 4 pi) i -- `log(exp(10*%i))` -> `10*%i` (expected `(10-4*pi)*i`) 
- **M32** max(2 - x^2, x) = x^3/9 => {-3, 3, -1.554894} -- `wsolve(max(2-x^2,x)=x^3/9,x)` -> `[-3,3]` (expected `[-3,3,-3*(1+sqrt(3)*sin((atan(sqrt(5)/2)-pi)/3)+cos((atan(sqrt(5)/2)-pi)/3))]`) missing 1 solution(s)
- **M37** 3x3 nonlinear system => 16 solutions -- `solve([x^2*y+3*y*z-4=0,-3*x^2*z+2*y^2+1=0,2*y*z^2-z^2-1=0],[x,y,z])` -> `[[x = 1,y = 1,z = 1],[x = -1,y = 1,z = 1],[x = sqrt(sqrt(2)*%i-1),y = -(sqrt(2)*%i),z = (sqrt(2)*%i+1)/3],[x = -sqrt(sqrt(2)*%i-1),y = -(sqrt(2)*%i),z = (sqrt(2)*%i+1)/3],[x = sqrt(-(sqrt(2)*%i)-1),y ` (expected ``) solution does not satisfy the system
- **M37b** same, real solutions => (+-1, 1, 1) and (+-0.5332, 0.6172, 2.0655) -- `solve([x^2*y+3*y*z-4=0,-3*x^2*z+2*y^2+1=0,2*y*z^2-z^2-1=0],[x,y,z])` -> `[[x = 1,y = 1,z = 1],[x = -1,y = 1,z = 1],[x = sqrt(sqrt(2)*%i-1),y = -(sqrt(2)*%i),z = (sqrt(2)*%i+1)/3],[x = -sqrt(sqrt(2)*%i-1),y = -(sqrt(2)*%i),z = (sqrt(2)*%i+1)/3],[x = sqrt(-(sqrt(2)*%i)-1),y ` (expected ``) solution does not satisfy the system
- **P16** rank of [[2 sqrt 2, 8], [6 sqrt 6, 24 sqrt 3]] => 1 -- `rank(matrix([2*sqrt(2),8],[6*sqrt(6),24*sqrt(3)]))` -> `2` (expected `1`) 
- **P17** rank of a trigonometric matrix => 1 -- `rank(matrix([sin(2*t),cos(2*t)],[2*(1-cos(t)^2)*cos(t),(1-2*sin(t)^2)*sin(t)]))` -> `2` (expected `1`) 
- **P26** eigenvalues of a hypercompanion matrix => roots of x^2+x+1 (twice), RootOf(quintic) -- `first(eigenvalues(matrix([-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,1,0])))` -> `[-((sqrt(3)*%i+1)/2),(sqrt(3)*%i-1)/2]` (expected `[-1/2-sqrt(3)*i/2,-1/2+sqrt(3)*i/2]`) returns only the 2 roots of x^2+x+1 and drops the 5 roots of the quintic factor; Maxima prints a warning ("solve is unable to find some of the roots") but returns the short list as the eigenvalues (auto: UNGRADED manual review)
- **W27** x first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,minf,inf)` -> `%pi*b+3*%pi*a` (expected `pi*(b+a)`) 

## UNEVAL (102)

- **A2** x or (not x) => true -- `x or not(x)` -> `x or not x` (expected `true`) no truth value
- **A3** x or y or (x and y) => x or y -- `x or y or (x and y)` -> `x or y or x and y` (expected `x or y`) equivalent but not simplified
- **A4** xor(xor(x, y), y) => x -- `((((((x) and not (y)) or (not (x) and (y)))) and not (y)) or (not ((((x) and not (y)) or (not (x) and (y)))) and (y)))` -> `(x and not y or not x and y) and not y or (not x or y) and (x or not y) and y` (expected `x`) equivalent but not simplified
- **A12** simplify the QEPCAD output => v > 0 and w > abs(ww) -- `wsimp(v>0 and 4*w*v>0 and 4*w*(4*w^2*v^2+3*ww^2+w^2)>0 and 64*w^2*v^2*(w^2-ww^2)*(w^2*v^2+ww^2)>0 and 64*w^2*v^2*(w^2-ww^2)^3*(w^2*v^2+ww^2)>0)` -> `v > 0 and 4*v*w > 0 and 4*w*(3*ww^2+4*v^2*w^2+w^2) > 0 and 64*v^2*w^2*(w^2-ww^2)*(ww^2+v^2*w^2) > 0 and 64*v^2*w^2*(w^2-ww^2)^3*(ww^2+v^2*w^2) > 0` (expected `v>0 and w>abs(ww)`) conjunction returned unsimplified (only factored)
- **C15** Putnam denesting => 3 + sqrt(2) -- `wsimp(sqrt(14+3*sqrt(3+2*sqrt(5-12*sqrt(3-2*sqrt(2))))))` -> `sqrt(3*sqrt(2*sqrt(5-12*(sqrt(2)-1))+3)+14)` (expected `3+sqrt(2)`) equivalent but not simplified (ops 15 > 3)
- **C16** Jeffrey-Rich denesting => sqrt(2) + sqrt(3) + sqrt(5) -- `wsimp(sqrt(10+2*sqrt(6)+2*sqrt(10)+2*sqrt(15)))` -> `sqrt(2*sqrt(15)+2*sqrt(10)+2*sqrt(6)+10)` (expected `sqrt(2)+sqrt(3)+sqrt(5)`) returned unchanged (output = argument of simplify)
- **C17** rationalize (sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)) => 5 + 2 sqrt(6) -- `wsimp((sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)))` -> `(sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2))` (expected `5+2*sqrt(6)`) returned unchanged (output = argument of simplify)
- **C18** sqrt(-2 + sqrt(-5)) sqrt(-2 - sqrt(-5)) => 3 -- `wsimp(sqrt(-2+sqrt(-5))*sqrt(-2-sqrt(-5)))` -> `sqrt(-(sqrt(5)*%i)-2)*sqrt(sqrt(5)*%i-2)` (expected `3`) equivalent but not simplified (ops 14 > 0)
- **C19** (90 + 34 sqrt(7))^(1/3) => 3 + sqrt(7) -- `wsimp((90+34*sqrt(7))^(1/3))` -> `(34*sqrt(7)+90)^(1/3)` (expected `3+sqrt(7)`) returned unchanged (output = argument of simplify)
- **C20** nontrivial way of writing 12 -- `wsimp(((135+78*sqrt(3))^(2/3)+3)*sqrt(3)/(135+78*sqrt(3))^(1/3))` -> `(sqrt(3)*((26*3^(3/2)+135)^(2/3)+3))/(26*3^(3/2)+135)^(1/3)` (expected `12`) equivalent but not simplified (ops 17 > 0)
- **C21** (41 + 29 sqrt(2))^(1/5) => 1 + sqrt(2) -- `wsimp((41+29*sqrt(2))^(1/5))` -> `(29*sqrt(2)+41)^(1/5)` (expected `1+sqrt(2)`) returned unchanged (output = argument of simplify)
- **C22** nasty Axiom example => (log(sqrt(2) + 1) + sqrt(2))/3 -- `wsimp(((6-4*sqrt(2))*log(3-2*sqrt(2))+(3-2*sqrt(2))*log(17-12*sqrt(2))+32-24*sqrt(2))/(48*sqrt(2)-72))` -> `((3-2^(3/2))*log(17-3*2^(5/2))+(6-2^(5/2))*log(3-2^(3/2))-3*2^(7/2)+32)/(3*2^(9/2)-72)` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) equivalent but not simplified (ops 28 > 8)
- **C23** cardinal arithmetic 2 inf - 3 => inf -- `2*inf-3` -> `2*inf-3` (expected `inf`) returned unevaluated (output = input)
- **F3** 2^n n! (2n-1)!! => (2n)! -- `wsimp(2^n*n!*(2*n-1)!!)` -> `2^n*n!*genfact(2*n-1,(2*n-1)/2,2)` (expected `factorial(2*n)`) returned unchanged (output = argument of simplify)
- **F4** 2^n n! product(2k-1, k=1..n) => (2n)! -- `wsimp(2^n*n!*ev(product(2*k-1,k,1,n),simpproduct))` -> `2^n*('product(2*k-1,k,1,n))*n!` (expected `factorial(2*n)`) unevaluated product
- **F5** gamma(n+1/2)/(sqrt(pi) n!) => (2n)!/(2^(2n) (n!)^2) -- `wsimp(gamma(n+1/2)/(sqrt(%pi)*n!))` -> `gamma(n+1/2)/(sqrt(%pi)*n!)` (expected `factorial(2*n)/(4^n*factorial(n)^2)`) returned unchanged (output = argument of simplify)
- **H3** (-1)^(n (n+1)) => 1 for integer n -- `declare(n,integer) ; (-1)^(n*(n+1))` -> `(-1)^(n*(n+1))` (expected `1`) returned unevaluated (output = input)
- **H15** (e^x - 1)/(e^(x/2) + 1) => e^(x/2) - 1 -- `wsimp((exp(x)-1)/(exp(x/2)+1))` -> `(%e^x-1)/(%e^(x/2)+1)` (expected `exp(x/2)-1`) equivalent but not simplified (ops 6 > 3)
- **H17** factor x^3 + x^2 - 7 via its roots, multiply them back together -- `R:map(rhs,solve(x^3+x^2-7=0,x)) ; u(r):=x-r ; expand(apply("*",map(u,R)))` -> `x^3+x^2-1/(2*3^(9/2)*sqrt(1295)+10098)-1/(2*3^(7/2)*sqrt(1295)+3366)-sqrt(1295)/(8*sqrt(3))-sqrt(1295)/(8*3^(3/2))-191/54` (expected `x^3+x^2-7`) equivalent but not simplified (ops 37 > 4)
- **H20** 1/(sqrt2 - 1) with sqrt2^2 = 2 => sqrt2 + 1 -- `wsimp(1/(sqrt(2)-1))` -> `1/(sqrt(2)-1)` (expected `sqrt(2)+1`) returned unchanged (output = argument of simplify)
- **H21** (x^3 + (sqrt2-2)x^2 - (2sqrt2+3)x - 3sqrt2)/(x^2-2) => (x^2-2x-3)/(x-sqrt2) -- `wsimp((x^3+(sqrt(2)-2)*x^2-(2*sqrt(2)+3)*x-3*sqrt(2))/(x^2-2))` -> `(x^3+(sqrt(2)-2)*x^2+(-2^(3/2)-3)*x-3*sqrt(2))/(x^2-2)` (expected `(x^2-2*x-3)/(x-sqrt(2))`) equivalent but not simplified (ops 20 > 8)
- **I1b** simplify(tan(7 pi/10)) -- `wsimp(tan(7*%pi/10))` -> `tan((7*%pi)/10)` (expected `-sqrt(1+2/sqrt(5))`) returned unchanged (output = argument of simplify)
- **I2** sqrt((1 + cos 6)/2) => -cos 3 -- `wsimp(sqrt((1+cos(6))/2))` -> `sqrt(cos(6)+1)/sqrt(2)` (expected `-cos(3)`) equivalent but not simplified (ops 7 > 2)
- **I3** cos(n pi) + sin((4n - 1) pi/2) => (-1)^n - 1 for integer n -- `declare(n,integer) ; cos(n*%pi)+sin((4*n-1)/2*%pi)` -> `sin((%pi*(4*n-1))/2)+(-1)^n` (expected `(-1)^n-1`) equivalent but not simplified (ops 8 > 3)
- **I4** cos(cos(n pi) pi) + sin(cos(n pi) pi/2) => (-1)^n - 1 -- `declare(n,integer) ; cos(cos(n*%pi)*%pi)+sin(cos(n*%pi)*%pi/2)` -> `cos((-1)^n*%pi)+sin(((-1)^n*%pi)/2)` (expected `(-1)^n-1`) equivalent but not simplified (ops 10 > 3)
- **I5** sin((n^5/5 + n^4/2 + n^3/3 - n/30) pi) => 0 for integer n -- `declare(n,integer) ; sin((n^5/5+n^4/2+n^3/3-n/30)*%pi)` -> `sin(%pi*(n^5/5+n^4/2+n^3/3-n/30))` (expected `0`) equivalent but not simplified (ops 12 > 0)
- **I6** |cos x|, |sin x| for -3 pi < x < -5/2 pi => -cos x, -sin x -- `assume(x>-3*%pi,x<-5*%pi/2) ; [abs(cos(x)),abs(sin(x))]` -> `[abs(cos(x)),abs(sin(x))]` (expected `[-cos(x),-sin(x)]`) returned unevaluated (output = input)
- **I8** cos(3x)/cos(x) => 2 cos(2x) - 1 -- `trigreduce(ratsimp(trigexpand(cos(3*x)/cos(x))))` -> `(cos(2*x)+1)/2-3*(1/2-cos(2*x)/2)` (expected `2*cos(2*x)-1`) equivalent but not simplified (ops 11 > 4)
- **I8b** simplify(cos(3x)/cos(x)) => 2 cos(2x) - 1 or similar -- `wsimp(cos(3*x)/cos(x))` -> `cos(3*x)/cos(x)` (expected `2*cos(2*x)-1`) returned unchanged (output = argument of simplify)
- **J8** J_{3/2}(z) => sqrt(2/(pi z)) (sin z/z - cos z) -- `bessel_j(3/2,z)` -> `bessel_j(3/2,z)` (expected `sqrt(2/(pi*z))*(sin(z)/z-cos(z))`) returned unevaluated (output = input)
- **J10** associated Legendre P^mu_nu(0) -- `assoc_legendre_p(n,m,0)` -> `assoc_legendre_p(n,m,0)` (expected `0`) returned unevaluated
- **J13** T_n(-1) => (-1)^n -- `declare(n,integer) ; chebyshev_t(n,-1)` -> `'sum((pochhammer(-n,i1)*pochhammer(n,i1))/(pochhammer(1/2,i1)*i1!),i1,0,n)` (expected `(-1)^n`) unevaluated sum
- **J16** zeta'(0) => -log(2 pi)/2 -- `at(diff(zeta(x),x),x=0)` -> `?%at('diff(zeta(x),x,1),x = 0)` (expected `-ln(2*pi)/2`) unevaluated diff
- **J17** Dirac delta: int f((x+2)/5) delta((x-2)/3) - g(x) delta'(x-1), 0..3 => 3 f(4/5) + g'(1) -- `integrate(f((x+2)/5)*delta((x-2)/3)-g(x)*diff(delta(x-1),x),x,0,3)` -> `'integrate(delta((x-2)/3)*f((x+2)/5)-'diff(delta(x-1),x,1)*g(x),x,0,3)` (expected `0`) unevaluated diff,integrate
- **J17b** same with f = exp, g = sin => 3 exp(4/5) + cos(1) -- `integrate(exp((x+2)/5)*delta((x-2)/3)-sin(x)*diff(delta(x-1),x),x,0,3)` -> `'integrate(%e^((x+2)/5)*delta((x-2)/3)-'diff(delta(x-1),x,1)*sin(x),x,0,3)` (expected `3*exp(4/5)+cos(1)`) unevaluated diff,integrate
- **K2** |3 - sqrt(7) + i sqrt(6 sqrt(7) - 15)| => 1 (Kahan) -- `abs(3-sqrt(7)+%i*sqrt(6*sqrt(7)-15))` -> `sqrt((3-sqrt(7))^2+6*sqrt(7)-15)` (expected `1`) equivalent but not simplified (ops 11 > 0)
- **K23** atan(tan(11 + 30 i)) => 11 - 4 pi + 30 i -- `atan(tan(11+30*%i))` -> `atan(tan(30*%i+11))` (expected `11-4*pi+30*i`) equivalent but not simplified (ops 4 > 4)
- **K24** Kahan: w + sqrt(w+1) sqrt(w-1), w = (z + 1/z)/2 => z (z > 1) -- `wsimp((z+1/z)/2+sqrt((z+1/z)/2+1)*sqrt((z+1/z)/2-1))` -> `sqrt((z+1/z)/2-1)*sqrt((z+1/z)/2+1)+(z+1/z)/2` (expected `z`) returned unchanged (output = argument of simplify)
- **L3** (2^(1/3) + 4^(1/3))^3 - 6 (2^(1/3) + 4^(1/3)) - 6 => 0 -- `wsimp((2^(1/3)+4^(1/3))^3-6*(2^(1/3)+4^(1/3))-6)` -> `(4^(1/3)+2^(1/3))^3-6*(4^(1/3)+2^(1/3))-6` (expected `0`) returned unchanged (output = argument of simplify)
- **L5** log(tan(x/2 + pi/4)) - asinh(tan x) => 0 for -pi/2 < x < pi/2 -- `wsimp(log(tan(x/2+%pi/4))-asinh(tan(x)))` -> `log(tan(x/2+%pi/4))-asinh(tan(x))` (expected `0`) returned unchanged (output = argument of simplify)
- **L6** d/dx of the L5 expression => 0 -- `wsimp(diff(log(tan(x/2+%pi/4))-asinh(tan(x)),x))` -> `sec(x/2+%pi/4)^2/(2*tan(x/2+%pi/4))-sec(x)^2/sqrt(tan(x)^2+1)` (expected `0`) equivalent but not in the requested form
- **L7** log((2 sqrt(r) + 1)/sqrt(4r + 4 sqrt(r) + 1)) => 0 -- `wsimp(log((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1)))` -> `log((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1))` (expected `0`) returned unchanged (output = argument of simplify)
- **L8** (4r + 4 sqrt r + 1)^(sqrt r/(2 sqrt r + 1)) (2 sqrt r + 1)^(1/(2 sqrt r + 1)) - 2 sqrt r - 1 => 0 -- `wsimp((4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))*(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))-2*sqrt(r)-1)` -> `(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))*(4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))-2*sqrt(r)-1` (expected `0`) returned unchanged (output = argument of simplify)
- **L9** 2^(1-z) gamma(z) zeta(z) cos(z pi/2) - pi^z zeta(1-z) => 0 -- `wsimp(2^(1-z)*gamma(z)*zeta(z)*cos(z*%pi/2)-%pi^z*zeta(1-z))` -> `2^(1-z)*gamma(z)*zeta(z)*cos((%pi*z)/2)-%pi^z*zeta(1-z)` (expected `0`) returned unchanged (output = argument of simplify)
- **M4** x^6 - 9x^4 - 4x^3 + 27x^2 - 36x - 23 = 0 (Rayes) -- `wsolve(x^6-9*x^4-4*x^3+27*x^2-36*x-23=0,x)` -> `[-0.4721297836938436,2.9919719016557953,1.1020902826214405-1.0911236359717227*%i,-(1.0911236359717216*%i)-2.362011332516314,1.0911236359717216*%i-2.362011332516314,1.0911236359717227*%i+1.102090282621` (expected `[2^(1/3)+sqrt(3),2^(1/3)-sqrt(3),sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3)]`) numeric instead of exact
- **M10** x^x = x => {-1, 1} -- `wsolve(x^x=x,x)` -> `unsolved([x = x^x])` (expected `[-1,1]`) solve and to_poly_solve leave it unsolved: unsolved([x = x^x])
- **M16** asin x = atan x => 0 -- `wsolve(asin(x)=atan(x),x)` -> `unsolved([atan(x) = asin(x)])` (expected `[0]`) solve and to_poly_solve leave it unsolved: unsolved([atan(x) = asin(x)])
- **M17** acos x = atan x => sqrt((sqrt(5) - 1)/2) -- `wsolve(acos(x)=atan(x),x)` -> `unsolved([atan(x) = acos(x)])` (expected `[sqrt((sqrt(5)-1)/2)]`) solve and to_poly_solve leave it unsolved: unsolved([atan(x) = acos(x)])
- **M24** a b^x = c d^x => x = log(c/a)/log(b/d) -- `wsolve(a*b^x=c*d^x,x)` -> `unsolved([d^x = (a*b^x)/c])` (expected `[ln(c/a)/ln(b/d)]`) solve and to_poly_solve leave it unsolved: unsolved([d^x = (a*b^x)/c])
- **M26** log(acos(asin(x^(2/3) - b)) - 1) + 2 = 0 => x = (b + sin(cos(e^-2) + 1))^(3/2) -- `wsolve(log(acos(asin(x^(2/3)-b))-1)+2=0,x)` -> `%union(%if((-(%pi/3) < parg(%e^(-((%e^(%e^-2*%i+%i)*%i)/2)-(%e^(-(%e^-2*%i)-%i)*%i)/2)*(2*%e^((%e^(%e^-2*%i+%i)*%i)/2+(%e^(-(%e^-2*%i)-%i)*%i)/2)*b+(1-%e^(%e^(%e^-2*%i+%i)*%i+%e^(-(%e^-2*%i)-%i)*%i))*` (expected `[[(b+sin(cos(exp(-2)+1)))^(3/2)],[-(b+sin(cos(exp(-2)+1)))^(3/2),(b+sin(cos(exp(-2)+1)))^(3/2)],[(b+sin(cos(exp(-2)+1)))^(3/2),-(b+sin(cos(exp(-2)+1)))^(3/2)]]`) unevaluated union
- **M27** 5x + exp((x - 5)/2) = 8x^3 => {-0.784966, -0.016291, 0.802557, 29.454528} -- `wsolve(5*x+exp((x-5)/2)=8*x^3,x)` -> `unsolved([x = (5*((sqrt(3)*%i)/2+(-1)/2))/(24*(sqrt(54*%e^(x-5)-125)/(8*6^(3/2))+%e^((x-5)/2)/16)^(1/3))+(sqrt(54*%e^(x-5)-125)/(8*6^(3/2))+%e^((x-5)/2)/16)^(1/3)*(-1/2-(sqrt(3)*%i)/2),x = (sqrt(54*%e` (expected `[-0.78496614648634752,-0.016290737729935251,0.8025567019143254,29.454527572105314]`) solve and to_poly_solve leave it unsolved: unsolved([x = (5*((sqrt(3)*%i)/2+(-1)/2))/(24*(sqrt(54*%e^(x-5)-125)/(8*6^(3/2))+%e^((x-5)/2)/16)^(1/3))+(sqrt(54*%e^(x-5)-125)/(8*6^(3/2))+%e^((x-5)/2)/16)^(1/3)*(-1/2-(sqrt(3)*%i)/2),x = (sqrt(54*%e
- **N2** x^4 - x + 1 > 0 for real x => true -- `is(x^4-x+1>0)` -> `unknown` (expected `true`) no truth value
- **N3** x^4 - x + 1 > 1 for all real x => false -- `is(x^4-x+1>1)` -> `unknown` (expected `false`) no truth value
- **N7** x > y > 0, k, n > 0 => k x^n > k y^n -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; assume(n>0) ; is(k*x^n>k*y^n)` -> `unknown` (expected `true`) no truth value
- **N8** x > 1, y >= x - 1 => y > 0 -- `assume(x>1) ; assume(y>=x-1) ; is(y>0)` -> `unknown` (expected `true`) no truth value
- **P9** Frobenius norm => (a^2 + b^2 + c^2)/(|a| |b| |c|) -- `factor(mat_norm(matrix([a/(b*c),1/c,1/b],[1/c,b/(a*c),1/a],[1/b,1/a,c/(a*b)]),frobenius))` -> `sqrt(c^4+(2*b^2+2*a^2)*c^2+b^4+2*a^2*b^2+a^4)/(abs(a)*abs(b)*abs(c))` (expected `(a^2+b^2+c^2)/(abs(a)*abs(b)*abs(c))`) equivalent but not simplified (ops 26 > 11)
- **R5** Dixon's identity => (a+b+c)!/(a! b! c!) -- `simplify_sum(sum((-1)^k*binomial(a+b,a+k)*binomial(b+c,b+k)*binomial(c+a,c+k),k,-a,a))` -> `'sum((-1)^k*(binomial(b+a,k+a)*binomial(c+a,k+c)*binomial(c+b,k+b)+binomial(b+a,a-k)*binomial(c+a,c-k)*binomial(c+b,b-k)),k,0,a)` (expected `factorial(a+b+c)/(factorial(a)*factorial(b)*factorial(c))`) unevaluated sum
- **R10** Vandermonde: sum(binomial(n, k) binomial(m, r - k), k = 0..r) => binomial(n + m, r) -- `simplify_sum(sum(binomial(n,k)*binomial(m,r-k),k,0,r))` -> `'sum(binomial(m,r-k)*binomial(n,k),k,0,r)` (expected `binomial(n+m,r)`) unevaluated sum
- **R18** sum(1/(2^k k^2), k = 1..inf) => pi^2/12 - log(2)^2/2 -- `simplify_sum(sum(1/(2^k*k^2),k,1,inf))` -> `%f[3,2]([1,1,1],[2,2],1/2)/2` (expected `pi^2/12-ln(2)^2/2`) unevaluated hypergeom
- **R20** sum(binomial(n, 4k), k = 0..inf) => (2^(n-1) + 2^(n/2) cos(n pi/4))/2 -- `simplify_sum(sum(binomial(n,4*k),k,0,inf))` -> `%f[4,3]([-((n-3)/4),-((n-2)/4),-((n-1)/4),-(n/4)],[3/4,1/4,1/2],1)` (expected `(2^(n-1)+2^(n/2)*cos(n*pi/4))/2`) unevaluated hypergeom
- **R22** double sum (capping) => 1/sqrt((1 - x y)^2 - 4 x^2) -- `simplify_sum(sum(simplify_sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2))),n,0,inf))` -> `'sum('sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R24** sum(product(k/(2k - 1), k = 1..m), m = 2..inf) => pi/2 -- `simplify_sum(sum(ev(product(k/(2*k-1),k,1,m),simpproduct),m,2,inf))` -> `'sum('product(k/(2*k-1),k,1,m),m,2,inf)` (expected `pi/2`) unevaluated product,sum
- **S1** product(gamma(k/3), k = 1..8) => 640 pi^3/(2187 sqrt 3) -- `ev(product(gamma(k/3),k,1,8),simpproduct)` -> `(80*gamma(1/3)^3*gamma(2/3)^3)/729` (expected `640*pi^3/(2187*sqrt(3))`) equivalent but not simplified (ops 9 > 6)
- **S3** product(x^k, k = 1..n) => x^(n (n+1)/2) -- `ev(product(x^k,k,1,n),simpproduct)` -> `'product(x^k,k,1,n)` (expected `x^(n*(n+1)/2)`) unevaluated product
- **S4** product(1 + 1/k, k = 1..n-1) => n -- `ev(product(1+1/k,k,1,n-1),simpproduct)` -> `'product(1/k+1,k,1,n-1)` (expected `n`) unevaluated product
- **S5** product((2k - 1)/(2k), k = 1..n) => binomial(2n, n)/2^(2n) -- `ev(product((2*k-1)/(2*k),k,1,n),simpproduct)` -> `'product((2*k-1)/(2*k),k,1,n)` (expected `binomial(2*n,n)/4^n`) unevaluated product
- **S6** product(x^2 - 2x cos(k pi/n) + 1, k = 1..n-1) => (x^(2n) - 1)/(x^2 - 1) -- `ev(product(x^2-2*x*cos(k*%pi/n)+1,k,1,n-1),simpproduct)` -> `'product(x^2-2*cos((%pi*k)/n)*x+1,k,1,n-1)` (expected `(x^(2*n)-1)/(x^2-1)`) unevaluated product
- **S7** product((k^3 - 1)/(k^3 + 1), k = 2..inf) => 2/3 -- `ev(product((k^3-1)/(k^3+1),k,2,inf),simpproduct)` -> `'product((k^3-1)/(k^3+1),k,2,inf)` (expected `2/3`) unevaluated product
- **S8** product(1 - 1/(2k)^2, k = 1..inf) => 2/pi -- `ev(product(1-1/(2*k)^2,k,1,inf),simpproduct)` -> `'product(1-1/(4*k^2),k,1,inf)` (expected `2/pi`) unevaluated product
- **S9** product(1 + (-1)^(k+1)/(2k - 1), k = 1..inf) => sqrt(2) -- `ev(product(1+(-1)^(k+1)/(2*k-1),k,1,inf),simpproduct)` -> `'product((-1)^(k+1)/(2*k-1)+1,k,1,inf)` (expected `sqrt(2)`) unevaluated product
- **S10** product((k (k+1) + 1 + i)/(k (k+1) + 1 - i), k = 0..inf) => -1 -- `ev(product((k*(k+1)+1+%i)/(k*(k+1)+1-%i),k,0,inf),simpproduct)` -> `'product((k*(k+1)+%i+1)/(k*(k+1)-%i+1),k,0,inf)` (expected `-1`) unevaluated product
- **T12** limit(zeta(x) - 1/(x - 1), x, 1) => Euler's gamma -- `limit(zeta(x)-1/(x-1),x,1)` -> `'limit(zeta(x)-1/(x-1),x,1)` (expected `euler_gamma`) unevaluated limit
- **U2b** same with unit steps: d/dx(-x H(-x) + x H(x)) => sign(x) -- `wsimp(diff(-x*unit_step(-x)+x*unit_step(x),x))` -> `x*'diff(unit_step(x),x,1)+unit_step(x)-'diff(unit_step(-x),x,1)*x-unit_step(-x)` (expected `sign(x)`) unevaluated diff
- **U4** d^n/dx^n x^n => n! (integer n) -- `declare(n,integer) ; diff(x^n,x,n)` -> `'diff(x^n,x,n)` (expected `factorial(n)`) unevaluated diff
- **U5** chain rule: d^2/dt^2 f(g(t)) => f''(g) g'^2 + f'(g) g'' -- `diff(f(g(t)),t,2)` -> `'diff(f(g(t)),t,2)` (expected ``) unevaluated diff
- **U6** d/dx int(f(y), y = g(x)..h(x)) => f(h(x)) h'(x) - f(g(x)) g'(x) -- `diff(integrate(f(y),y,g(x),h(x)),x)` -> `f(h(x))*'diff(h(x),x,1)-f(g(x))*'diff(g(x),x,1)` (expected ``) unevaluated diff
- **U9** f_x + f_y with f(x, y) = g(x^2 + y^2) => 2 (x + y) g'(x^2 + y^2) -- `factor(diff(g(x^2+y^2),x)+diff(g(x^2+y^2),y))` -> `'diff(g(y^2+x^2),y,1)+'diff(g(y^2+x^2),x,1)` (expected ``) the derivatives of g(x^2+y^2) stay noun forms (no chain rule), so nothing is factored
- **V4** int -x H(-x) + x H(x) dx -- `integrate(-x*unit_step(-x)+x*unit_step(x),x)` -> `'integrate(x*unit_step(x)-unit_step(-x)*x,x)` (expected `-x*heaviside(-x)+x*heaviside(x)`) unevaluated integrate
- **V6** int 2^x/sqrt(1 + 4^x) dx => asinh(2^x)/log 2 -- `integrate(2^x/sqrt(1+4^x),x)` -> `'integrate(2^x/sqrt(4^x+1),x)` (expected `2^x/sqrt(1+4^x)`) unevaluated integrate
- **V17** int cos(5x) Ci(2x) dx => sin(5x) Ci(2x)/5 - (Si(7x) + Si(3x))/10 -- `integrate(cos(5*x)*expintegral_ci(2*x),x)` -> `'integrate(expintegral_ci(2*x)*cos(5*x),x)` (expected `cos(5*x)*Ci(2*x)`) unevaluated integrate
- **V18** int (f' g - f g')/(f^2 - g^2) dx => log((f - g)/(f + g))/2 -- `integrate((diff(f(x),x)*g(x)-f(x)*diff(g(x),x))/(f(x)^2-g(x)^2),x)` -> `'integrate((g(x)*'diff(f(x),x,1)-f(x)*'diff(g(x),x,1))/(f(x)^2-g(x)^2),x)` (expected ``) unevaluated integrate
- **W5** int sqrt(x + 1/x - 2), x = 0..2 => (8 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,0,2)` -> `'integrate(sqrt(x+1/x-2),x,0,2)` (expected `(8-sqrt(8))/3`) unevaluated integrate
- **W9** PV int 5x^3/(1 + x + x^2 + x^3 + x^4), -inf..inf => -2 pi (sin(pi/5) + sin(2 pi/5)) -- `integrate(5*x^3/(1+x+x^2+x^3+x^4),x,minf,inf)` -> `5*'integrate(x^3/(x^4+x^3+x^2+x+1),x,minf,inf)` (expected `-2*pi*(sin(pi/5)+sin(2*pi/5))`) unevaluated integrate
- **W11** int sqrt(1 - x^2)/(1 + x^2), x = -1..1 => pi (sqrt 2 - 1) -- `integrate(sqrt(1-x^2)/(1+x^2),x,-1,1)` -> `'integrate(sqrt(1-x^2)/(x^2+1),x,-1,1)` (expected `pi*(sqrt(2)-1)`) unevaluated integrate
- **W15** int log(gamma(x)) cos(6 pi x), x = 0..1 => 1/12 -- `integrate(log(gamma(x))*cos(6*%pi*x),x,0,1)` -> `'realpart(psi[0](6*%pi))/(36*%pi^2)-'limit((cos(6*%pi*x)*'realpart(psi[0](6*%pi*x)))/(36*%pi^2)+(sin(6*%pi*x)*log(gamma(x)))/(6*%pi),x,0,plus)` (expected `1/12`) unevaluated limit
- **W17** int exp(-a x) J0(b x), x = 0..inf => 1/sqrt(a^2 + b^2) -- `assume(a>0) ; integrate(exp(-a*x)*bessel_j(0,b*x),x,0,inf)` -> `'integrate(%e^-(a*x)*bessel_j(0,b*x),x,0,inf)` (expected `1/sqrt(a^2+b^2)`) unevaluated integrate
- **W18** int (J1(x)/x)^2, x = 0..inf => 4/(3 pi) -- `integrate((bessel_j(1,x)/x)^2,x,0,inf)` -> `'integrate(bessel_j(1,x)^2/x^2,x,0,inf)` (expected `4/(3*pi)`) unevaluated integrate
- **W19** int Ci(x) J0(2 sqrt(7 x)), x = 0..inf => (cos 7 - 1)/7 -- `integrate(expintegral_ci(x)*bessel_j(0,2*sqrt(7*x)),x,0,inf)` -> `'integrate(bessel_j(0,2*sqrt(7)*sqrt(x))*expintegral_ci(x),x,0,inf)` (expected `(cos(7)-1)/7`) unevaluated integrate
- **W20** int x^2 Li3(1/(x + 1)), x = 0..1 (Mertig) -- `integrate(x^2*li[3](1/(x+1)),x,0,1)` -> `'integrate(x^2*li[3](1/(x+1)),x,0,1)` (expected `-(17/3+pi^2)/36+ln(2)/9*(35/3-pi^2/2-4*ln(2)+ln(2)^2)+zeta(3)/4`) unevaluated integrate
- **W23** same with s(t) = H(t - 1) - H(t - 2) -- `assume(u>0,u<3) ; integrate((unit_step(t-1)-unit_step(t-2))*cos(t),t,0,u)` -> `'integrate((unit_step(t-1)-unit_step(t-2))*cos(t),t,0,u)` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) unevaluated integrate
- **W31** int(int(sin a sin y/sqrt(1 - sin^2 a sin^2 x sin^2 y), x = 0..pi/2), y = 0..pi/2) => pi a/2 -- `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,%pi/2),y,0,%pi/2)` -> `sin(a)*'integrate(sin(y)*'integrate(1/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,%pi/2),y,0,%pi/2)` (expected `pi*a/2`) unevaluated integrate
- **W32** int(int(|y - x^2|, y = 0..2), x = -1..1) => 46/15 -- `integrate(integrate(abs(y-x^2),y,0,2),x,-1,1)` -> `'integrate(x^4/2-((x^2-2)*abs(x^2-2))/2,x,-1,1)` (expected `46/15`) unevaluated integrate
- **X7** series of f'(a x) a + g(b x) + int(h(c y), y = 0..x) at x = d -- `ratdisrep(taylor(diff(f(a*x),x)+g(b*x)+integrate(h(c*y),y,0,x),x,d,(2)-1))` -> `'integrate(h(c*y),y,0,d)+(x-d)*('at('diff(g(b*x),x,1),x = d)+'at('diff(f(a*x),x,2),x = d)+h(c*d))+'at('diff(f(a*x),x,1),x = d)+g(b*d)` (expected ``) unevaluated diff,integrate
- **Y4** laplace(erf(3/sqrt t)) => 1/s - e^(-6 sqrt s)/s -- `laplace(erf(3/sqrt(t)),t,s)` -> `'laplace(erf(3/sqrt(t)),t,s)` (expected `1/s-exp(-6*sqrt(s))/s`) unevaluated laplace
- **Y5** y'' + y = 4 (H(t - 1) - H(t - 2)), y(0) = 1, y'(0) = 0 -- `block([s_:ode2('diff(y,x,2)+y=4*(unit_step(x-1)-unit_step(x-2)),y,x)],if s_=false then false else ic2(s_,x=0,y=1,'diff(y,x)=0))` -> `y = cos(x)*'integrate((4*unit_step(x-2)-4*unit_step(x-1))*sin(x),x)-sin(x)*'integrate((4*unit_step(x-2)-4*unit_step(x-1))*cos(x),x)+cos(x)` (expected `Y2+Y-4*(sp.Heaviside(x-1)-sp.Heaviside(x-2))`) unevaluated integrate
- **Y6** inverse Laplace of the transformed solution of Y5 -- `ilt(s/(s^2+1)+4*(1/s-s/(s^2+1))*(exp(-s)-exp(-2*s)),s,t)` -> `cos(t)+'ilt(4*(%e^-s-%e^-(2*s))*(1/s-s/(s^2+1)),s,t)` (expected `cos(t)+4*((1-cos(t-1))*heaviside(t-1)-(1-cos(t-2))*heaviside(t-2))`) unevaluated ilaplace
- **Y11** Mellin transform of 1/(1 - x) => pi cot(pi s) (PV) -- `assume(s>0,s<1) ; integrate(x^(s-1)/(1-x),x,0,inf)` -> `'integrate(x^(s-1)/(1-x),x,0,inf)` (expected `pi*cot(pi*s)`) unevaluated integrate
- **Y12** Mellin transform of J3(x)/x^3 => 2^(s-4) gamma(s/2)/gamma(4 - s/2) -- `assume(s>0,s<1) ; integrate(bessel_j(3,x)/x^3*x^(s-1),x,0,inf)` -> `'integrate(bessel_j(3,x)*x^(s-4),x,0,inf)` (expected `2^(s-4)*gamma(s/2)/gamma(4-s/2)`) unevaluated integrate
- **Z4** Yu-Israel nonlinear-coefficient recurrence -- `solve_rec(f[n]=(1+c-c^(n-1)-c^(n+1))/(1-c^n)*f[n-1]-c*(1-c^(n-2))/(1-c^(n-1))*f[n-2]+1,f[n],f[1]=1,f[2]=(2+2*c+c^2)/(1+c))` -> `false` (expected `(c^(n+1)*(c^(n+1)-2*c-2)+(n+1)*c^2+2*c-n)/((c-1)^3*(c+1))`) solve_rec finds no solution (returns false)
- **Z15** y'' + y'/x + (1 - v^2/x) y/(4x) = 0 => Z_v(sqrt x) -- `ode2('diff(y,x,2)+'diff(y,x)/x+(1-v^2/x)*y/(4*x)=0,y,x)` -> `false` (expected `Y2+Y1/x+(1-v**2/x)*Y/(4*x)`) ode2 finds no solution (returns false)
- **Z17** discontinuous ODE y'' + sign(t - T) y = 0 -- `ode2('diff(y,x,2)+signum(x-q)*y=0,y,x)` -> `false` (expected ``) ode2 finds no solution (returns false)
- **Z20** triangular system u' = u (1 + cos t/(2 + sin t)), v' = u - v -- `desolve([diff(u(t),t)=u(t)*(1+cos(t)/(2+sin(t))),diff(v(t),t)=u(t)-v(t)],[u(t),v(t)])` -> `[u(t) = 'ilt(('laplace((u(t)*cos(t))/(sin(t)+2),t,g6971)+u(0))/(g6971-1),g6971,t),v(t) = 'ilt(('laplace((u(t)*cos(t))/(sin(t)+2),t,g6971)+v(0)*g6971-v(0)+u(0))/(g6971^2-1),g6971,t)]` (expected `[U.diff(t)-U*(1+sp.cos(t)/(2+sp.sin(t))), V.diff(t)-(U-V)]`) unevaluated ilaplace,laplace

## UNSUPPORTED (71)

- **A5** (w and x) implies (y and z) => not(w and x) or (y and z) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical implication operator)
- **A6** x iff y => (x and y) or not(x or y) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical equivalence operator)
- **C4** ABC base 16 => 2748 -- `0xABC` -> `` (expected `2748`) no Maxima counterpart for hex/octal literals
- **C5** 123 base 10 => 234 base 7 -- `digits(123,7)` -> `` (expected `[2,3,4]`) no Maxima counterpart for digits
- **C6** 677 base 8 => 1BF base 16 -- `hex(0o677)` -> `` (expected `"0x1BF"`) no Maxima counterpart for hex, hex/octal literals
- **D5** natural cubic spline through (1,1),(2,4),(4,2),(5,3) at 3 => 27/8 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no spline interpolation)
- **D12** interval arithmetic ([-4,2] x + [1,3])^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no interval arithmetic)
- **E4** mode([3, 7, 11, 7, 3, 5, 7]) => 7 -- `mode([3,7,11,7,3,5,7])` -> `` (expected `7`) no Maxima counterpart for mode
- **E12** linear regression => y = 0.7365 x + 6.964 -- `linreg([3.33,3.25,3.92,3.50,4.33,4.92,6.08,7.42,8.33,8.00,9.25,10.75],[8.61,9.40,9.86,9.91,10.53,10.61,10.59,13.28,12.76,13.44,14.27,14.13])` -> `` (expected `0.7365*x+6.964`) no Maxima counterpart for linreg
- **E13** multiple linear regression => -16278.7 + 960.925 x1 + 2975.66 x2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no multiple regression in the CAS)
- **E14** L1 (least absolute deviations) regression -- `` -> `` (expected ``) not attempted: no input in problems.txt (no L1 regression)
- **E15** nonlinear least squares fit -- `` -> `` (expected ``) not attempted: no input in problems.txt (no nonlinear regression in the CAS)
- **G3** (a + b)^p mod p => a^p + b^p for p prime -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular polynomial arithmetic with symbolic modulus)
- **G4** 9 x = 15 mod 21 => x = 4 mod 7 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G5** 7 x = 22 mod 39 => x = 31 mod 39 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G6** x^2 + x + 4 = 0 mod 8 => x = {3, 4} mod 8 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G7** x^3 + 2x^2 + 5x + 6 = 0 mod 11 => x = 3 mod 11 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G9** linear system mod 7 => x = 1, y = 2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G10** 2x + 3y = 1 mod 5 => 5 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G11** 2x + 3y = 1 mod 6 => 6 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G12** Diophantine x^4 + 9 = y^2 => (2, 5) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no Diophantine solver)
- **G13** Diophantine x^2 + 4 = y^3 => (11, 5) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no Diophantine solver)
- **G14** Pythagorean system => (3,4,5,12,13), ... -- `` -> `` (expected ``) not attempted: no input in problems.txt (no Diophantine solver)
- **G15** rational approximation of sqrt(3) within 1/500 => 26/15 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no best rational approximation with tolerance)
- **G19** continued fraction of tanh(1/(2x)) => [0; 2x, 6x, 10x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **G20** continued fraction of sqrt(x^2+1) - x => [0; 2x, 2x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **H24** factor x^4 - 3x^2 + 1 mod 5 => (x-2)^2 (x+2)^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no factorization over finite fields)
- **H25** factor x^11 + x + 1 mod 65537 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no factorization over finite fields)
- **H30** factor 4x^2 - 21xy + 20y^2 mod 3 => (x + y)(x - y) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no factorization over finite fields)
- **I9** cos(3x)/cos(x) via user rewrite rules -- `` -> `` (expected ``) not attempted: no input in problems.txt (no user-defined rewrite rules)
- **J18** define an antisymmetric function -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symmetry declarations for functions)
- **K11** sqrt(e^z) - e^(z/2) for complex z (nonzero in general) -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K14** log(e^z) for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K17** (x y)^(1/n) - x^(1/n) y^(1/n) for complex x, y -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **O3** (a x b) . (c x d) for abstract vectors -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O5** div(f x g) = g . curl f - f . curl g for abstract fields -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O6** divergence in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **O7** dR/dt in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **P1** superdiagonal of [[1,2,3],[4,5,6],[7,8,9]] => [2, 6] -- `diag(matrix([1,2,3],[4,5,6],[7,8,9]),1)` -> `` (expected `[2,6]`) no Maxima counterpart for diag
- **P3** build a 7x6 matrix from submatrices of a 4x4 matrix -- `` -> `` (expected ``) not attempted: no input in problems.txt (no submatrix indexing / block assembly)
- **P12** inverse of a block triangular matrix with symbolic blocks -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symbolic (noncommutative) matrix blocks)
- **P14** reduced row echelon form (Cullen) -- `rref(matrix([1,2,3,1,3],[3,2,1,1,7],[0,2,4,1,1],[1,1,1,1,4]))` -> `` (expected `[[1,0,-1,0,2],[0,1,2,0,-1],[0,0,0,1,3],[0,0,0,0,0]]`) no Maxima counterpart for rref
- **P28** generalized eigenvectors (3x3, lambda = 1 triple) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P29** generalized eigenvectors (6x6) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P31** Smith normal form of [[x^2, x - 1], [x + 1, x^2]] => [[1, 0], [0, x^4 - x^2 + 1]] -- `smith(matrix([x^2,x-1],[x+1,x^2]))` -> `` (expected `[[1,0],[0,x^4-x^2+1]]`) no Maxima counterpart for smith
- **P39** singular value decomposition of [[1,1],[2,2],[3,3]] -- `svd(matrix([1,1],[2,2],[3,3]))` -> `` (expected `[2*sqrt(7),0]`) no Maxima counterpart for svd
- **R1** sum((x[i] - xbar)^2) => sum(x[i]^2) - sum(x[i])^2/n -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R2** least squares fit of a line to (x[i], y[i]) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R4** Gosper: indefinite sum of binomial(n,k)/2^n - binomial(n+1,k)/2^(n+1) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indefinite summation (Gosper))
- **U2** derivative of the piecewise function (-x for x < 0, x otherwise) -- `diff(piecewise(x<0,-x,x),x)` -> `` (expected `piecewise(x<0,-1,1)`) no Maxima counterpart for piecewise (Maxima has no piecewise expressions)
- **U3** f(x) = x^2 - 1 at x = 1, x^3 otherwise: f(1) => 0 -- `f(x):=piecewise(x=1,x^2-1,x^3) ; f(1)` -> `` (expected `0`) no Maxima counterpart for piecewise (Maxima has no piecewise expressions)
- **U3b** same f: f'(1) => 3 -- `f(x):=piecewise(x=1,x^2-1,x^3) ; subst(1,x,diff(f(x),x))` -> `` (expected `3`) no Maxima counterpart for piecewise (Maxima has no piecewise expressions)
- **U7** exact differential d V(P, T) => V_P dP + V_T dT -- `` -> `` (expected ``) not attempted: no input in problems.txt (no total differentials)
- **U13** minimum of x^4 - x + 1 => 1 - 3/8 2^(1/3) -- `minimize(x^4-x+1,x)` -> `` (expected `1-3*2^(1/3)/8`) no Maxima counterpart for minimize
- **U14** [min, max] of 1/(x^2 + y^2 + 1) => [0, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U15** minimize a + b x + c y + d x y on [-1,1]^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U16** [min, max] of x^2 y^3 on [-1,1]^2 => [-1, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U17** linear programming (symbolic simplex) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no linear programming)
- **V2** int |z| dz for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **V3** int of the piecewise function (-x for x < 0, x otherwise) -- `integrate(piecewise(x<0,-x,x),x)` -> `` (expected `piecewise(x<0,-x,x)`) no Maxima counterpart for piecewise (Maxima has no piecewise expressions)
- **W22** int s(t) cos t, t = 0..u for the step s = 1 on [1, 2] -- `integrate(piecewise(t>=1 and t<=2,1,0)*cos(t),t,0,u)` -> `` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) no Maxima counterpart for piecewise (Maxima has no piecewise expressions)
- **X19** power series of log(sin x/x), general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X20** power series of e^-x sin x, general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X21** series reversion of x = sin y + cos y -- `` -> `` (expected ``) not attempted: no input in problems.txt (no series reversion)
- **Y8** Fourier transform of 1 => sqrt(2 pi) delta(z) -- `fourier(1,x,z)` -> `` (expected `sqrt(2*pi)*dirac(z)`) no Maxima counterpart for fourier
- **Y9** Fourier transform of exp(-9 x^2) -- `fourier(exp(-9*x^2),x,z)` -> `` (expected `exp(-z^2/36)/(3*sqrt(2))`) no Maxima counterpart for fourier
- **Y10** Fourier transform of |x| exp(-3 |x|) -- `fourier(abs(x)*exp(-3*abs(x)),x,z)` -> `` (expected `sqrt(2/pi)*(9-z^2)/(9+z^2)^2`) no Maxima counterpart for fourier
- **Y13** Z transform of H(t - 3) -- `ztrans(unit_step(t-3),t,z)` -> `` (expected `0`) no Maxima counterpart for ztrans
- **Y14** Z transform of H(t - m T) => z/(z^m (z - 1)) -- `ztrans(unit_step(t-m),t,z)` -> `` (expected `0`) no Maxima counterpart for ztrans
- **Z14** BVP y'' + k^2 y = 0, y(0) = 0, y'(1) = 0 => A sin(k x) with cos k = 0 -- `dsolve([y''+k^2*y=0,y(0)=0,y'(1)=0],y,x)` -> `` (expected ``) no translation: boundary conditions on derivatives at two points
- **Z16** delay equation y'(t) + a y(t - 1) = 0 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no delay differential equations)

## ERROR (15)

- **P38** square root of the singular [[0,1,0],[0,0,0],[0,0,0]] => [[0,a,b],[0,0,0],[0,1/b,0]] -- `mat_function(sqrt,matrix([0,1,0],[0,0,0],[0,0,0]))` -> `expt: undefined: 0 to a negative exponent.` (expected `[[0,1,0],[0,0,0],[0,0,0]]`) Maxima error: expt: undefined: 0 to a negative exponent.
- **R15** sum(binomial(n - k, k), k = 0..floor(n/2)) => fib(n + 1) -- `simplify_sum(sum(binomial(n-k,k),k,0,floor(n/2)))` -> `Is n-2*floor(n/2) positive, negative or zero?` (expected `fibonacci(n+1)`) Maxima asks: Is n-2*floor(n/2) positive, negative or zero? (no answer in batch mode)
- **T13** limit(n^x/(x product(1 + x/k, k = 1..n)), n, inf) => gamma(x) -- `limit(n^x/(x*ev(product(1+x/k,k,1,n),simpproduct)),n,inf)` -> `Is x positive, negative or zero?` (expected `gamma(x)`) Maxima asks: Is x positive, negative or zero? (no answer in batch mode)
- **T14** limit(x int(exp(-t^2), t = 0..x)/(1 - exp(-x^2)), x, 0) => 1 -- `limit(x*integrate(exp(-t^2),t,0,x)/(1-exp(-x^2)),x,0)` -> `Is x positive, negative or zero?` (expected `1`) Maxima asks: Is x positive, negative or zero? (no answer in batch mode)
- **W24** int(int(x/(x^2 + y^2), y = -inf..inf), x = a..b), a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),y,minf,inf),x,a,b)` -> `Is x zero or nonzero?` (expected `pi*(b-a)`) Maxima asks: Is x zero or nonzero? (no answer in batch mode)
- **W25** other order, a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,minf,inf)` -> `Is b-a positive, negative or zero?` (expected `pi*(b-a)`) Maxima asks: Is b-a positive, negative or zero? (no answer in batch mode)
- **W26** y first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x^2+y^2),y,minf,inf),x,a,b)` -> `Is x zero or nonzero?` (expected `pi*(b+a)`) Maxima asks: Is x zero or nonzero? (no answer in batch mode)
- **W28** y first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x^2+y^2),y,minf,inf),x,a,b)` -> `Is x zero or nonzero?` (expected `pi*(a-b)`) Maxima asks: Is x zero or nonzero? (no answer in batch mode)
- **W29** x first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x^2+y^2),x,a,b),y,minf,inf)` -> `Is b-a positive, negative or zero?` (expected `pi*(a-b)`) Maxima asks: Is b-a positive, negative or zero? (no answer in batch mode)
- **W30** int(int(sqrt(x^2 + y^2), x = 0..1), y = 0..1) => (log(sqrt 2 + 1) + sqrt 2)/3 -- `integrate(integrate(sqrt(x^2+y^2),x,0,1),y,0,1)` -> `Is y positive or negative?` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) Maxima asks: Is y positive or negative? (no answer in batch mode)
- **X16** Wallis: binomial(2n, n)/2^(2n) => 1/sqrt(pi n) -- `ratdisrep(taylor(binomial(2*n,n)/4^n,n,inf,1))` -> `taylor: encountered an unfamiliar singularity in: binomial(2*n,n)` (expected `1/sqrt(pi*n)`) Maxima error: taylor: encountered an unfamiliar singularity in: binomial(2*n,n)
- **X17** e^x int(e^-t/t, t = x..inf) => 1/x - 1/x^2 + 2/x^3 - 6/x^4 -- `ratdisrep(taylor(exp(x)*integrate(exp(-t)/t,t,x,inf),x,inf,5))` -> `Is x positive, negative or zero?` (expected `1/x-1/x^2+2/x^3-6/x^4`) Maxima asks: Is x positive, negative or zero? (no answer in batch mode)
- **Y2** inverse Laplace back => cos((w - 1) t) -- `ilt(s/(s^2+(w-1)^2),s,t)` -> `Is w-1 zero or nonzero?` (expected `cos((w-1)*t)`) Maxima asks: Is w-1 zero or nonzero? (no answer in batch mode)
- **Y7** Laplace transform of a square wave (infinite sum of steps) -- `laplace(1+2*simplify_sum(sum((-1)^n*unit_step(t-n*a),n,1,inf)),t,s)` -> `-(unit_step(t-a*n-a)/unit_step(t-a*n)) non-rational term ratio to nusum Is a*n positive, negative or zero?` (expected `0`) Maxima asks: Is a*n positive, negative or zero? (no answer in batch mode)
- **Z24** second order system (Niles) u'' = 2 q v', v'' = -2 q u' + 3 q^2 v -- `desolve([diff(u(t),t,2)=2*q*diff(v(t),t),diff(v(t),t,2)=-2*q*diff(u(t),t)+3*q^2*v(t)],[u(t),v(t)])` -> `Is q zero or nonzero?` (expected `[U.diff(t,2)-2*q*V.diff(t), V.diff(t,2)-(-2*q*U.diff(t)+3*q**2*V)]`) Maxima asks: Is q zero or nonzero? (no answer in batch mode)

## TIMEOUT (2)

- **P22** eigenvalues of (2 - a) I_100 => 2 - a -- `first(eigenvalues((2-a)*ident(100)))` -> `` (expected `[2-a]`) wall-clock timeout
- **W10** int 1/(1 + x + x^2 + x^4), -inf..inf -- `integrate(1/(1+x+x^2+x^4),x,minf,inf)` -> `` (expected `1.98667745715246`) wall-clock timeout

