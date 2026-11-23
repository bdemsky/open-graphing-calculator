# Wester benchmark baseline: sympy

engine: `SymPy 1.14.0 (Python 3.13.16)`

Graded with `../run_wester.py` grade(); see `../COMPARISON.md`.

| section | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|
| A Boolean logic | 3 | 0 | 3 | 2 | 0 | 0 | 0 | 0 | 5 | 13 |
| B Set theory | 3 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 4 |
| C Numbers | 14 | 0 | 9 | 0 | 0 | 0 | 0 | 0 | 1 | 24 |
| D Numerical analysis | 6 | 0 | 0 | 2 | 0 | 0 | 0 | 0 | 6 | 14 |
| E Statistics | 9 | 0 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 15 |
| F Combinatorial theory | 4 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 8 |
| G Number theory | 10 | 0 | 2 | 8 | 0 | 0 | 0 | 0 | 0 | 20 |
| H Algebra | 27 | 1 | 4 | 0 | 0 | 0 | 0 | 0 | 2 | 34 |
| I Trigonometry | 6 | 1 | 5 | 1 | 0 | 1 | 0 | 0 | 0 | 14 |
| J Special functions | 14 | 0 | 3 | 2 | 0 | 0 | 0 | 0 | 0 | 19 |
| K Complex domain | 17 | 0 | 4 | 3 | 0 | 0 | 0 | 0 | 0 | 24 |
| L Zero equivalence | 4 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 9 |
| M Equations | 28 | 3 | 7 | 1 | 0 | 0 | 0 | 0 | 0 | 39 |
| N Inequalities | 9 | 2 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 18 |
| O Vector analysis | 4 | 0 | 0 | 5 | 1 | 0 | 0 | 0 | 0 | 10 |
| P Matrix theory | 36 | 0 | 1 | 6 | 2 | 1 | 0 | 0 | 0 | 46 |
| Q Tensor analysis | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 8 | 1 | 12 | 3 | 0 | 0 | 0 | 0 | 0 | 24 |
| S Products | 4 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| T Limits | 14 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 16 |
| U Calculus | 10 | 2 | 0 | 5 | 0 | 0 | 0 | 0 | 2 | 19 |
| V Indefinite integrals | 14 | 0 | 3 | 1 | 0 | 0 | 0 | 0 | 0 | 18 |
| W Definite integrals | 18 | 2 | 13 | 0 | 0 | 1 | 0 | 0 | 0 | 34 |
| X Series | 16 | 0 | 3 | 5 | 1 | 0 | 0 | 0 | 1 | 26 |
| Y Transforms | 9 | 0 | 3 | 2 | 0 | 0 | 0 | 0 | 0 | 14 |
| Z Difference and differential equations | 17 | 1 | 3 | 1 | 1 | 1 | 0 | 0 | 0 | 24 |
| **total** | **304** | **13** | **100** | **53** | **5** | **4** | **0** | **0** | **18** | **497** |

## PASS (304)

- **A1** true and false => false -- `And(true,false)` -> `false` (expected `false`) 
- **A4** xor(xor(x, y), y) => x -- `xor(xor(x,y),y)` -> `x` (expected `x`) 
- **A7** x and 1 > 2 => false -- `And(x,1>2)` -> `false` (expected `false`) 
- **B1** union of [a,b,b,c,c,c], [d,c,b], [b,e,b] => {a,b,c,d,e} -- `union([a,b,b,c,c,c],[d,c,b],[b,e,b])` -> `[e,a,b,c,d]` (expected `[a,b,c,d,e]`) 
- **B3** set difference {a,b,c,d,e} - {b} => {a,c,d,e} -- `setdiff([a,b,c,d,e],[b])` -> `[e,a,c,d]` (expected `[a,c,d,e]`) 
- **B4** Cartesian product {a,b} x {c,d} -- `cartprod([a,b],[c,d])` -> `[[a,c],[a,d],[b,c],[b,d]]` (expected `[[a,c],[a,d],[b,c],[b,d]]`) 
- **C1** 50! (bignum) -- `50!` -> `30414093201713378043612608166064768844377641568960512000000000000` (expected `30414093201713378043612608166064768844377641568960512000000000000`) 
- **C2** factor 50! -- `ifactor(50!)` -> `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47` (expected `2^47*3^22*5^12*7^8*11^4*13^3*17^2*19^2*23^2*29*31*37*41*43*47`) 
- **C3** double factorials [10!!, 9!!] => [3840, 945] -- `[10!!,9!!]` -> `[3840,945]` (expected `[3840,945]`) 
- **C4** ABC base 16 => 2748 -- `0xABC` -> `2748` (expected `2748`) 
- **C5** 123 base 10 => 234 base 7 -- `digits(123,7)` -> `[2,3,4]` (expected `[2,3,4]`) 
- **C6** 677 base 8 => 1BF base 16 -- `hex(0o677)` -> `"0x1BF"` (expected `"0x1BF"`) 
- **C7** log base 8 of 32768 => 5 -- `log(32768,8)` -> `5` (expected `5`) 
- **C8** 5^(-1) mod 7 => 3; 5^(-1) mod 6 => 5 -- `[invmod(5,7),invmod(5,6)]` -> `[3,5]` (expected `[3,5]`) 
- **C9** gcd(1776, 1554, 5698) => 74 -- `gcd(1776,1554,5698)` -> `74` (expected `74`) 
- **C10** 1/2 + 1/3 + ... + 1/10 => 4861/2520 -- `1/2+1/3+1/4+1/5+1/6+1/7+1/8+1/9+1/10` -> `4861/2520` (expected `4861/2520`) 
- **C11** complete decimal expansion of 1/7 => 0.(142857) -- `approx(1/7,30)` -> `0.142857142857142857142857142857` (expected `0.142857142857142857142857142857`) 
- **C12** (7/11)*(22/7) => 2 -- `7/11*(22/7)` -> `2` (expected `2`) 
- **C13** 10/7 (1 + 29/1000)^(1/3) => 3^(1/3) automatically -- `10/7*(1+29/1000)**(1/3)` -> `3^(1/3)` (expected `3^(1/3)`) 
- **C23** cardinal arithmetic 2 inf - 3 => inf -- `2*inf-3` -> `inf` (expected `inf`) 
- **D1** 0.0/sqrt(2) => 0.0 -- `0.0/sqrt(2)` -> `0` (expected `0`) 
- **D2** exp(-1000000.0) => 3.29683e-434295 (underflow) -- `exp(-1000000.0)` -> `3.29683147808856e-434295` (expected `3.29683147808856e-434295`) 
- **D2b** exp(-1000000) in arbitrary precision -- `approx(exp(-1000000),15)` -> `3.29683147808856e-434295` (expected `3.29683147808856e-434295`) 
- **D3** exp(pi sqrt(163)) to 50 digits (nearly an integer) -- `approx(exp(pi*sqrt(163)),50)` -> `262537412640768743.99999999999925007259719818568888` (expected `262537412640768743.99999999999925007259719818568888`) 
- **D4** [floor(-5/3), ceil(-5/3)] => [-2, -1] -- `[floor(-5/3),ceil(-5/3)]` -> `[-2,-1]` (expected `[-2,-1]`) 
- **D8** Horner form of sum(a[i] x^i, i=1..5) -- `horner(a1*x+a2*x**2+a3*x**3+a4*x**4+a5*x**5,x)` -> `x*(a1 + x*(a2 + x*(a3 + x*(a4 + a5*x))))` (expected `x*(a1+x*(a2+x*(a3+x*(a4+a5*x))))`) 
- **E1** mean([3, 7, 11, 5, 19]) => 9 -- `mean([3,7,11,5,19])` -> `9` (expected `9`) 
- **E2** median => 7 -- `median([3,7,11,5,19])` -> `7` (expected `7`) 
- **E3** first quartile of [1..8] => 2 or 5/2 -- `quartile1([1,2,3,4,5,6,7,8])` -> `2` (expected `[2,5/2]`) 
- **E6** binomial pdf: 12 of 15 at p = 0.75 => 0.22520 -- `binompdf(15,0.75,12)` -> `0.225199065171182` (expected `0.225199`) 
- **E7** binomial cdf: up to 12 of 15 => 0.76391 -- `binomcdf(15,0.75,12)` -> `0.763912188820541` (expected `0.763912`) 
- **E8** normal(4.35, 0.59): P(4 <= X <= 5) => 0.5867 -- `normalcdf(4,5,4.35,0.59)` -> `erf(0.296610169491525*sqrt(2))/2 + erf(0.550847457627119*sqrt(2))/2` (expected `0.588185984502579`) 
- **E9** Student t test of mean 5 => 0.057567 -- `L := [1,-2,3,-4,5,-6,7,-8,9,10] ; tcdf(-inf,(mean(L)-5)/(stddev(L)*sqrt(10/9)/sqrt(10)),9)` -> `-128*sqrt(145)*hypergeom([1/2,5],[3/2],-49/145)/(725*pi) + 1/2` (expected `0.057567`) 
- **E10** same with the normal distribution => 0.040583 -- `L := [1,-2,3,-4,5,-6,7,-8,9,10] ; normalcdf(-inf,(mean(L)-5)/(stddev(L)*sqrt(10/9)/sqrt(10)))` -> `1/2 - erf(21*sqrt(290)/290)/2` (expected `0.040583`) 
- **E11** chi-square independence test (chi2 = 1153/252) => 0.469859 -- `chi2cdf(1153/252,inf,5)` -> `-erf(sqrt(16142)/84) + 1909*sqrt(16142)*exp(-1153/504)/(31752*sqrt(pi)) + 1` (expected `0.469859`) 
- **F1** Pochhammer (a)_3 => a (a+1) (a+2) -- `pochhammer(a,3)` -> `a*(a + 1)*(a + 2)` (expected `a*(a+1)*(a+2)`) 
- **F6** number of partitions of 4 => 5 -- `partition(4)` -> `5` (expected `5`) 
- **F7** Stirling number of the first kind S1(5, 2) => -50 -- `stirling1(5,2)` -> `-50` (expected `-50`) 
- **F8** Euler totient(1776) => 576 -- `euler(1776)` -> `576` (expected `576`) 
- **G1** largest 6-digit and smallest 7-digit prime => [999983, 1000003] -- `[prevprime(10**6),nextprime(10**6)]` -> `[999983,1000003]` (expected `[999983,1000003]`) 
- **G2** primitive root of 191 => 19 -- `primroot(191)` -> `19` (expected `19`) 
- **G4** 9 x = 15 mod 21 => x = 4 mod 7 -- `polynomial_congruence(9*x-15,21)` -> `[4,11,18]` (expected `[4,11,18]`) 
- **G5** 7 x = 22 mod 39 => x = 31 mod 39 -- `polynomial_congruence(7*x-22,39)` -> `[31]` (expected `[31]`) 
- **G6** x^2 + x + 4 = 0 mod 8 => x = {3, 4} mod 8 -- `polynomial_congruence(x**2+x+4,8)` -> `[3,4]` (expected `[3,4]`) 
- **G7** x^3 + 2x^2 + 5x + 6 = 0 mod 11 => x = 3 mod 11 -- `polynomial_congruence(x**3+2*x**2+5*x+6,11)` -> `[3]` (expected `[3]`) 
- **G8** x = 7 mod 9, 13 mod 23, 1 mod 2 => x = 151 mod 414 -- `ichinrem(ichinrem([7,9],[13,23]),[1,2])` -> `[151,414]` (expected `[151,414]`) 
- **G16** continued fraction of pi => [3; 7, 15, 1, 292, ...] -- `contfrac(pi,5)` -> `[3,7,15,1,292]` (expected `[3,7,15,1,292]`) 
- **G17** continued fraction of sqrt(23) => [4; 1, 3, 1, 8, ...] -- `contfrac(sqrt(23),5)` -> `[4,1,3,1,8]` (expected `[4,1,3,1,8]`) 
- **G18** continued fraction of the golden ratio => [1; 1, 1, ...] -- `contfrac((1+sqrt(5))/2,5)` -> `[1,1,1,1,1]` (expected `[1,1,1,1,1]`) 
- **H1** 2 2^n => 2^(n+1) -- `simplify(2*2**n)` -> `2^(n + 1)` (expected `2^(n+1)`) 
- **H2** 4 2^n => 2^(n+2) -- `simplify(4*2**n)` -> `2^(n + 2)` (expected `2^(n+2)`) 
- **H3** (-1)^(n (n+1)) => 1 for integer n -- `assume(n,integer) ; (-1)**(n*(n+1))` -> `1` (expected `1`) 
- **H4** factor(6 x - 10) => 2 (3 x - 5) -- `factor(6*x-10)` -> `2*(3*x - 5)` (expected `2*(3*x-5)`) 
- **H5** univariate gcd(p1, p2) => 1 (Liska) -- `gcd(64*x**34-21*x**47-126*x**8-46*x**5-16*x**60-81,72*x**60-25*x**25-19*x**23-22*x**39-83*x**52+54*x**10+81)` -> `1` (expected `1`) 
- **H6** gcd(p1 q, p2 q) => q -- `gcd(expand((64*x**34-21*x**47-126*x**8-46*x**5-16*x**60-81)*(34*x**19-25*x**16+70*x**7+20*x**3-91*x-86)),expand((72*x**60-25*x**25-19*x**23-22*x**39-83*x**52+54*x**10+81)*(34*x**19-25*x**16+70*x**7+20*x**3-91*x-86)))` -> `34*x^19 - 25*x^16 + 70*x^7 + 20*x^3 - 91*x - 86` (expected `[34*x^19-25*x^16+70*x^7+20*x^3-91*x-86,-(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)]`) 
- **H7** resultant(p1 q, p2 q) => 0 -- `resultant(expand((64*x**34-21*x**47-126*x**8-46*x**5-16*x**60-81)*(34*x**19-25*x**16+70*x**7+20*x**3-91*x-86)),expand((72*x**60-25*x**25-19*x**23-22*x**39-83*x**52+54*x**10+81)*(34*x**19-25*x**16+70*x**7+20*x**3-91*x-86)),x)` -> `0` (expected `0`) 
- **H8** factor(expand(p1 p2)) => p1 p2 -- `factor(expand((64*x**34-21*x**47-126*x**8-46*x**5-16*x**60-81)*(72*x**60-25*x**25-19*x**23-22*x**39-83*x**52+54*x**10+81)))` -> `-(16*x^60 + 21*x^47 - 64*x^34 + 126*x^8 + 46*x^5 + 81)*(72*x^60 - 83*x^52 - 22*x^39 - 25*x^25 - 19*x^23 + 54*x^10 + 81)` (expected `(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)`) 
- **H9** multivariate gcd(p1, p2) => 1 -- `gcd(24*x*y**19*z**8-47*x**17*y**5*z**8+6*x**15*y**9*z**2-3*x**22+5,34*x**5*y**8*z**13+20*x**7*y**7*z**7+12*x**9*y**16*z**4+80*y**14*z)` -> `1` (expected `1`) 
- **H10** multivariate gcd(p1 q, p2 q) => q -- `gcd(expand((24*x*y**19*z**8-47*x**17*y**5*z**8+6*x**15*y**9*z**2-3*x**22+5)*(11*x**12*y**7*z**13-23*x**2*y**8*z**10+47*x**17*y**5*z**8)),expand((34*x**5*y**8*z**13+20*x**7*y**7*z**7+12*x**9*y**16*z**4+80*y**14*z)*(11*x**12*y**7*z**13-23*x**2*y**8*z**10+47*x**17*y**5*z**8)))` -> `47*x^17*y^5*z^8 + 11*x^12*y^7*z^13 - 23*x^2*y^8*z^10` (expected `[11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8,-(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)]`) 
- **H11** multivariate factor(expand(p1 p2)) => p1 p2 -- `factor(expand((24*x*y**19*z**8-47*x**17*y**5*z**8+6*x**15*y**9*z**2-3*x**22+5)*(34*x**5*y**8*z**13+20*x**7*y**7*z**7+12*x**9*y**16*z**4+80*y**14*z)))` -> `-2*y^7*z*(6*x^9*y^9*z^3 + 10*x^7*z^6 + 17*x^5*y*z^12 + 40*y^7)*(3*x^22 + 47*x^17*y^5*z^8 - 6*x^15*y^9*z^2 - 24*x*y^19*z^8 - 5)` (expected `-2*z*y^7*(6*x^9*y^9*z^3+10*x^7*z^6+17*y*x^5*z^12+40*y^7)*(3*x^22+47*x^17*y^5*z^8-6*x^15*y^9*z^2-24*x*y^19*z^8-5)`) 
- **H13** resultant with a common factor => 0 -- `resultant(3*x**4+3*x**3+x**2-x-2,x**3-3*x**2+x+5,x)` -> `0` (expected `0`) 
- **H14** (x^2 - 4)/(x^2 + 4x + 4) => (x - 2)/(x + 2) -- `normal((x**2-4)/(x**2+4*x+4))` -> `(x - 2)/(x + 2)` (expected `(x-2)/(x+2)`) 
- **H16** expand (x+1)^20, differentiate, factor => 20 (x+1)^19 -- `factor(diff(expand((x+1)**20),x))` -> `20*(x + 1)^19` (expected `20*(x+1)^19`) 
- **H18** factor(x^100 - 1) => 9 cyclotomic factors -- `factor(x**100-1)` -> `(x - 1)*(x + 1)*(x^2 + 1)*(x^4 - x^3 + x^2 - x + 1)*(x^4 + x^3 + x^2 + x + 1)*(x^8 - x^6 + x^4 - x^2 + 1)*(x^20 - x^15 + x^10 - x^5 + 1)*(x^20 + x^15 + x^10 + x^5 + 1)*(x^40 - x^30 + x^20 - x^10 + 1)` (expected `(x-1)*(x+1)*(x^2+1)*(x^4-x^3+x^2-x+1)*(x^4+x^3+x^2+x+1)*(x^8-x^6+x^4-x^2+1)*(x^20-x^15+x^10-x^5+1)*(x^20+x^15+x^10+x^5+1)*(x^40-x^30+x^20-x^10+1)`) 
- **H19** factor over Q(i): 4x^4+8x^3+77x^2+18x+153 => (2x+3i)(2x-3i)(x+1+4i)(x+1-4i) -- `cfactor(4*x**4+8*x**3+77*x**2+18*x+153)` -> `4*(x - 3*i/2)*(x + 3*i/2)*(x + 1 - 4*i)*(x + 1 + 4*i)` (expected `(2*x+3*i)*(2*x-3*i)*(x+1+4*i)*(x+1-4*i)`) 
- **H20** 1/(sqrt2 - 1) with sqrt2^2 = 2 => sqrt2 + 1 -- `simplify(1/(sqrt(2)-1))` -> `1 + sqrt(2)` (expected `sqrt(2)+1`) 
- **H22** (cbrt2 + sqrt3)^4 with cbrt2^3 = 2, sqrt3^2 = 3 -- `expand((2**(1/3)+sqrt(3))**4)` -> `2*2^(1/3) + 9 + 8*sqrt(3) + 12*2^(1/3)*sqrt(3) + 18*2^(2/3)` (expected `2*2^(1/3)+8*sqrt(3)+18*2^(2/3)+12*2^(1/3)*sqrt(3)+9`) 
- **H23** factor(x^4 - 3x^2 + 1) over Q => (x^2-x-1)(x^2+x-1) -- `factor(x**4-3*x**2+1)` -> `(x^2 - x - 1)*(x^2 + x - 1)` (expected `(x^2-x-1)*(x^2+x-1)`) 
- **H24** factor x^4 - 3x^2 + 1 mod 5 => (x-2)^2 (x+2)^2 -- `factor(x**4-3*x**2+1,modulus=5)` -> `(x - 2)^2*(x + 2)^2` (expected `(x-2)^2*(x+2)^2`) 
- **H25** factor x^11 + x + 1 mod 65537 -- `factor(x**11+x+1,modulus=65537)` -> `(x^2 + x + 1)*(x^9 - x^8 + x^6 - x^5 + x^3 - x^2 + 1)` (expected `(x^2+x+1)*(x^9-x^8+x^6-x^5+x^3-x^2+1)`) 
- **H26** factor x^4 - 3x^2 + 1 over Q(phi) => 4 linear factors -- `factor(x**4-3*x**2+1,extension=sqrt(5))` -> `(x - 1/2 + sqrt(5)/2)*(x + 1/2 + sqrt(5)/2)*(x - sqrt(5)/2 - 1/2)*(x - sqrt(5)/2 + 1/2)` (expected `(x-(1+sqrt(5))/2)*(x+(1+sqrt(5))/2)*(x-(1+sqrt(5))/2+1)*(x+(1+sqrt(5))/2-1)`) 
- **H27** factor(expand((x - 2y^2 + 3z^3)^20)) -- `factor(expand((x-2*y**2+3*z**3)**20))` -> `(x - 2*y^2 + 3*z^3)^20` (expected `(x-2*y^2+3*z^3)^20`) 
- **H28** factor(expand((sin x - 2 cos^2 y + 3 tan^3 z)^20)) -- `factor(expand((sin(x)-2*cos(y)**2+3*tan(z)**3)**20))` -> `(-sin(x) + 2*cos(y)^2 - 3*tan(z)^3)^20` (expected `(sin(x)-2*cos(y)^2+3*tan(z)^3)^20`) 
- **H29** (1-c^2)^5 (1-s^2)^5 (c^2+s^2)^10 with c^2 + s^2 = 1 => c^10 s^10 -- `factor(rem(expand((1-c**2)**5*(1-s**2)**5*(c**2+s**2)**10),s**2+c**2-1,s))` -> `-c^10*(c - 1)^5*(c + 1)^5` (expected `-c^10*(c-1)^5*(c+1)^5`) 
- **H31** factor x^3 + y^3 over Q(sqrt(-3)) -- `factor(x**3+y**3,extension=sqrt(3)*i)` -> `(x + y)*(x + y*(-1/2 - sqrt(3)*i/2))*(x + y*(-1/2 + sqrt(3)*i/2))` (expected `(x+y)*(x+y*(-1/2-sqrt(3)/2*i))*(x+y*(-1/2+sqrt(3)/2*i))`) 
- **H32** partial fractions of (x^2+2x+3)/(x^3+4x^2+5x+2) -- `partfrac((x**2+2*x+3)/(x**3+4*x**2+5*x+2),x)` -> `3/(x + 2) - 2/(x + 1) + 2/(x + 1)^2` (expected `3/(x+2)-2/(x+1)+2/(x+1)^2`) 
- **I1** tan(7 pi/10) => -sqrt(1 + 2/sqrt(5)) -- `tan(7*pi/10)` -> `-sqrt(2*sqrt(5)/5 + 1)` (expected `-sqrt(1+2/sqrt(5))`) 
- **I1b** simplify(tan(7 pi/10)) -- `simplify(tan(7*pi/10))` -> `-sqrt(10*sqrt(5) + 25)/5` (expected `-sqrt(1+2/sqrt(5))`) 
- **I3** cos(n pi) + sin((4n - 1) pi/2) => (-1)^n - 1 for integer n -- `assume(n,integer) ; cos(n*pi)+sin((4*n-1)/2*pi)` -> `(-1)^n - 1` (expected `(-1)^n-1`) 
- **I7** cos(3x)/cos(x) => cos(x)^2 - 3 sin(x)^2 or similar -- `normal(texpand(cos(3*x)/cos(x)))` -> `4*cos(x)^2 - 3` (expected `cos(x)^2-3*sin(x)^2`) 
- **I8** cos(3x)/cos(x) => 2 cos(2x) - 1 -- `tcollect(normal(texpand(cos(3*x)/cos(x))))` -> `2*cos(2*x) - 1` (expected `2*cos(2*x)-1`) 
- **I10** trigsimp((tan^2 x + 1 - sec^2 x)/(sin^2 x + cos^2 x - 1)) => undefined (0/0) -- `trigsimp((tan(x)**2+1-sec(x)**2)/(sin(x)**2+cos(x)**2-1))` -> `undef` (expected ``) divergence reported as undef
- **J1** Bernoulli B_16 => -3617/510 -- `bernoulli(16)` -> `-3617/510` (expected `-3617/510`) 
- **J2** d/dk E(phi, k) => (E(phi, k) - F(phi, k))/k -- `diff(ellipticE(p,k**2),k)` -> `(elliptic_e(p,k^2) - elliptic_f(p,k^2))/k` (expected `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k`) 
- **J4** gamma(-1/2) => -2 sqrt(pi) -- `gamma(-1/2)` -> `-2*sqrt(pi)` (expected `-2*sqrt(pi)`) 
- **J5** psi(1/3) => -gamma - pi/(2 sqrt 3) - 3/2 log 3 -- `digamma(1/3)` -> `-ln(3) - sqrt(3)*pi/6 - euler_gamma - ln(sqrt(3))` (expected `-euler_gamma-pi/(2*sqrt(3))-3*ln(3)/2`) 
- **J6** J_2(1 + i) => 0.04158 + 0.24740 i -- `approx(besselj(2,1+i))` -> `0.0415798869439621 + 0.247397641513306*i` (expected `0.04157988694396212+0.24739764151330632*i`) 
- **J9** d/dz J_0(z) => -J_1(z) -- `diff(besselj(0,z),z)` -> `-besselj(1,z)` (expected `-besselj(1,z)`) 
- **J10** associated Legendre P^mu_nu(0) -- `legendrep(n,m,0)` -> `2^m*sqrt(pi)/(gamma(-m/2 - n/2 + 1/2)*gamma(-m/2 + n/2 + 1))` (expected `0`) closed form 2^m sqrt(pi)/(gamma((1-m-n)/2) gamma((n-m)/2+1))
- **J11** P^1_3(x) => -3/2 sqrt(1 - x^2) (5 x^2 - 1) -- `legendrep(3,1,x)` -> `-sqrt(1 - x^2)*(15*x^2/2 - 3/2)` (expected `-3/2*sqrt(1-x^2)*(5*x^2-1)`) 
- **J12** T_1008 - 2x T_1007 + T_1006 => 0 -- `normal(chebyshevt(1008,x)-2*x*chebyshevt(1007,x)+chebyshevt(1006,x))` -> `0` (expected `0`) 
- **J13** T_n(-1) => (-1)^n -- `assume(n,integer) ; chebyshevt(n,-1)` -> `(-1)^n` (expected `(-1)^n`) 
- **J14** 2F1(1/2, 1/2; 3/2; z^2) => asin(z)/z -- `hypergeom([1/2,1/2],[3/2],z**2)` -> `asin(z)/z` (expected `asin(z)/z`) 
- **J15** 2F1((n+2)/2, -(n-2)/2; 3/2; sin^2 z) => sin(n z)/(n sin z cos z) -- `hypergeom([(n+2)/2,-(n-2)/2],[3/2],sin(z)**2)` -> `2*cos((2 - n)*asin(abs(sin(z))))/n + (2*sin(z)^2 - 1)*sin((2 - n)*asin(abs(sin(z))))*abs(sin(z))/(n*sqrt(1 - sin(z)^2)*sin(z)^2)` (expected `sin(n*z)/(n*sin(z)*cos(z))`) 
- **J17** Dirac delta: int f((x+2)/5) delta((x-2)/3) - g(x) delta'(x-1), 0..3 => 3 f(4/5) + g'(1) -- `integrate(f((x+2)/5)*dirac((x-2)/3)-g(x)*diff(dirac(x-1),x),x,0,3)` -> `3*f(4/5) + subst(diff(g(x),x),x,1)` (expected `0`) 3 f(4/5) + g'(1)
- **J17b** same with f = exp, g = sin => 3 exp(4/5) + cos(1) -- `integrate(exp((x+2)/5)*dirac((x-2)/3)-sin(x)*diff(dirac(x-1),x),x,0,3)` -> `cos(1) + 3*exp(4/5)` (expected `3*exp(4/5)+cos(1)`) 
- **K1** [Re(x + i y), Im(x + i y)] for complex x, y => [Re x - Im y, Im x + Re y] -- `assume(x,complex) ; assume(y,complex) ; [re(x+i*y),im(x+i*y)]` -> `[re(x) - im(y),re(y) + im(x)]` (expected `[re(x)-im(y),im(x)+re(y)]`) 
- **K2** |3 - sqrt(7) + i sqrt(6 sqrt(7) - 15)| => 1 (Kahan) -- `abs(3-sqrt(7)+i*sqrt(6*sqrt(7)-15))` -> `1` (expected `1`) 
- **K4** log(3 + 4i) in rectangular form => log 5 + i atan(4/3) -- `expand_complex(ln(3+4*i))` -> `ln(5) + i*atan(4/3)` (expected `ln(5)+i*atan(4/3)`) 
- **K5** tan(x + i y) in rectangular form -- `expand_complex(tan(x+i*y))` -> `sin(2*x)/(cos(2*x) + cosh(2*y)) + i*sinh(2*y)/(cos(2*x) + cosh(2*y))` (expected `(sin(2*x)+i*sinh(2*y))/(cos(2*x)+cosh(2*y))`) 
- **K6** sqrt(x y |z|^2)/(sqrt(x) |z|) => sqrt(x y)/sqrt(x) (no further in general) -- `simplify(sqrt(x*y*abs(z)**2)/(sqrt(x)*abs(z)))` -> `sqrt(x*y)/sqrt(x)` (expected `sqrt(x*y)/sqrt(x)`) 
- **K7** same for y >= 0 => sqrt(y) -- `assume(y>=0) ; simplify(sqrt(x*y*abs(z)**2)/(sqrt(x)*abs(z)))` -> `sqrt(y)` (expected `sqrt(y)`) 
- **K8** sqrt(1/z) - 1/sqrt(z) does not vanish (z real negative) -- `simplify(sqrt(1/z)-1/sqrt(z))` -> `sqrt(1/z) - 1/sqrt(z)` (expected `sqrt(1/z)-1/sqrt(z)`) 
- **K9** sqrt(1/z) - 1/sqrt(z) => 0 for z > 0 -- `assume(z>0) ; simplify(sqrt(1/z)-1/sqrt(z))` -> `0` (expected `0`) 
- **K10** sqrt(1/z) + 1/sqrt(z) => 0 for z < 0 -- `assume(z<0) ; simplify(sqrt(1/z)+1/sqrt(z))` -> `0` (expected `0`) 
- **K12** sqrt(e^z) - e^(z/2) => 0 for real z -- `simplify(sqrt(exp(z))-exp(z/2))` -> `0` (expected `0`) 
- **K13** sqrt(e^(6 i)) => -e^(3 i) -- `sqrt(exp(6*i))` -> `-exp(3*i)` (expected `-exp(3*i)`) 
- **K15** log(e^z) => z for real z -- `ln(exp(z))` -> `z` (expected `z`) 
- **K16** log(e^(10 i)) => (10 - 4 pi) i -- `ln(exp(10*i))` -> `-4*i*pi + 10*i` (expected `(10-4*pi)*i`) 
- **K18** (x y)^(1/n) - x^(1/n) y^(1/n) => 0 for y > 0 -- `assume(y>0) ; simplify((x*y)**(1/n)-x**(1/n)*y**(1/n))` -> `0` (expected `0`) 
- **K19** (x y)^n - x^n y^n => 0 for integer n -- `assume(n,integer) ; simplify((x*y)**n-x**n*y**n)` -> `0` (expected `0`) 
- **K20** atan(tan(z)) does not simplify to z for real z -- `simplify(atan(tan(z)))` -> `atan(tan(z))` (expected `atan(tan(z))`) 
- **K22** atan(tan(10)) => 10 - 3 pi -- `atan(tan(10))` -> `10 - 3*pi` (expected `10-3*pi`) 
- **L1** sqrt(997) - (997^3)^(1/6) => 0 -- `sqrt(997)-(997**3)**(1/6)` -> `0` (expected `0`) 
- **L2** sqrt(999983) - (999983^3)^(1/6) => 0 -- `sqrt(999983)-(999983**3)**(1/6)` -> `0` (expected `0`) 
- **L3** (2^(1/3) + 4^(1/3))^3 - 6 (2^(1/3) + 4^(1/3)) - 6 => 0 -- `simplify((2**(1/3)+4**(1/3))**3-6*(2**(1/3)+4**(1/3))-6)` -> `0` (expected `0`) 
- **L4** cos^3 x + cos x sin^2 x - cos x => 0 -- `trigsimp(cos(x)**3+cos(x)*sin(x)**2-cos(x))` -> `0` (expected `0`) 
- **M2** 3x^3 - 18x^2 + 33x - 19 = 0 (three real roots, exact) -- `solve(Eq(3*x**3-18*x**2+33*x-19,0),x)` -> `[2 + (-1/2 - sqrt(3)*i/2)*(1/6 + sqrt(3)*i/18)^(1/3) + 1/(3*(-1/2 - sqrt(3)*i/2)*(1/6 + sqrt(3)*i/18)^(1/3)),2 + 1/(3*(-1/2 + sqrt(3)*i/2)*(1/6 + sqrt(3)*i/18)^(1/3)) + (-1/2 + sqrt(3)*i/2)*(1/6 + sqr` (expected `[2+2/sqrt(3)*cos(pi/18),2+2/sqrt(3)*cos(pi/18-2*pi/3),2+2/sqrt(3)*cos(pi/18+2*pi/3)]`) 
- **M3** x^4 + x^3 + x^2 + x + 1 = 0 (exact roots) -- `csolve(Eq(x**4+x**3+x**2+x+1,0),x)` -> `[-1/4 + sqrt(5)/4 - i*sqrt(sqrt(5)/8 + 5/8),-1/4 + sqrt(5)/4 + i*sqrt(sqrt(5)/8 + 5/8),-sqrt(5)/4 - 1/4 - i*sqrt(5/8 - sqrt(5)/8),-sqrt(5)/4 - 1/4 + i*sqrt(5/8 - sqrt(5)/8)]` (expected `[exp(2*pi*i/5),exp(4*pi*i/5),exp(6*pi*i/5),exp(8*pi*i/5)]`) 
- **M5** x^7 - 1 = 0 => 7th roots of unity -- `csolve(Eq(x**7-1,0),x)` -> `[1,-cos(pi/7) - i*sin(pi/7),-cos(pi/7) + i*sin(pi/7),cos(2*pi/7) - i*sin(2*pi/7),cos(2*pi/7) + i*sin(2*pi/7),-cos(3*pi/7) - i*sin(3*pi/7),-cos(3*pi/7) + i*sin(3*pi/7)]` (expected `[1,exp(2*pi*i/7),exp(4*pi*i/7),exp(6*pi*i/7),exp(8*pi*i/7),exp(10*pi*i/7),exp(12*pi*i/7)]`) 
- **M6** Liska's degree-8 polynomial => 1 +- sqrt(+-sqrt(+-4 sqrt(3) - 3) - 3)/sqrt(2) -- `csolve(Eq(x**8-8*x**7+34*x**6-92*x**5+175*x**4-236*x**3+226*x**2-140*x+46,0),x)` -> `[1 - sqrt(2)*sqrt(-3 - i*sqrt(3 + 4*sqrt(3)))/2,1 + sqrt(2)*sqrt(-3 - i*sqrt(3 + 4*sqrt(3)))/2,1 - sqrt(2)*sqrt(-3 + i*sqrt(3 + 4*sqrt(3)))/2,1 + sqrt(2)*sqrt(-3 + i*sqrt(3 + 4*sqrt(3)))/2,1 - sqrt(2)` (expected `[1+sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2)]`) 
- **M7** e^(2x) + 2 e^x + 1 = z => x = log(sqrt z - 1), log(sqrt z + 1) + i pi (+ 2 n pi i) -- `csolve(Eq(exp(2*x)+2*exp(x)+1,z),x)` -> `[ln(-sqrt(z) - 1),ln(sqrt(z) - 1)]` (expected `[ln(sqrt(z)-1),ln(sqrt(z)+1)+i*pi]`) 
- **M8** exp(2 - x^2) = exp(-x) => real solutions {-1, 2} -- `solve(Eq(exp(2-x**2),exp(-x)),x)` -> `[-1,2]` (expected `[-1,2]`) 
- **M9** e^x = x => x = -W(-1) -- `csolve(Eq(exp(x),x),x)` -> `[-LambertW(-1)]` (expected `[-LambertW(-1)]`) 
- **M10** x^x = x => {-1, 1} -- `solve(Eq(x**x,x),x)` -> `[-1,1]` (expected `[-1,1]`) 
- **M11** (x + 1)(sin^2 x + 1)^2 cos^3(3x) = 0 => -1, pi/6 + n pi/3 (real) -- `solve(Eq((x+1)*(sin(x)**2+1)**2*cos(3*x)**3,0),x)` -> `[-1,pi/6,pi/2]` (expected ``) particular solutions
- **M12** sin x = cos x => pi/4 (+ n pi) -- `solve(Eq(sin(x),cos(x)),x)` -> `[pi/4]` (expected ``) particular solutions
- **M13** tan x = 1 => pi/4 (+ n pi) -- `solve(Eq(tan(x),1),x)` -> `[pi/4]` (expected ``) particular solutions
- **M14** sin x = 1/2 => pi/6, 5 pi/6 (+ 2 n pi) -- `solve(Eq(sin(x),1/2),x)` -> `[pi/6,5*pi/6]` (expected ``) particular solutions
- **M15** sin x = tan x => 0 (+ n pi) -- `solve(Eq(sin(x),tan(x)),x)` -> `[0,-pi,pi,2*pi]` (expected ``) particular solutions
- **M18** (x - 2)/x^(1/3) = 0 => 2 -- `solve(Eq((x-2)/x**(1/3),0),x)` -> `[2]` (expected `[2]`) 
- **M19** sqrt(x^2 + 1) = x - 2 => no solutions -- `solve(Eq(sqrt(x**2+1),x-2),x)` -> `[]` (expected `[]`) 
- **M20** x + sqrt(x) = 2 => 1 -- `solve(Eq(x+sqrt(x),2),x)` -> `[1]` (expected `[1]`) 
- **M21** 2 sqrt(x) + 3 x^(1/4) - 2 = 0 => 1/16 -- `solve(Eq(2*sqrt(x)+3*x**(1/4)-2,0),x)` -> `[1/16]` (expected `[1/16]`) 
- **M22** x = 1/sqrt(1 + x^2) => sqrt((sqrt5-1)/2), -i sqrt((sqrt5+1)/2) -- `csolve(Eq(x,1/sqrt(1+x**2)),x)` -> `[-i*sqrt(1/2 + sqrt(5)/2),sqrt(-1/2 + sqrt(5)/2)]` (expected `[sqrt((sqrt(5)-1)/2),-i*sqrt((sqrt(5)+1)/2)]`) 
- **M23** binomial(m, 2) 2^k = 1 => k = 1 - log2(m (m - 1)) -- `solve(Eq(binomial(m,2)*2**k,1),k)` -> `[ln(2/(m*(m - 1)))/ln(2)]` (expected `[1-ln(m*(m-1))/ln(2)]`) 
- **M24** a b^x = c d^x => x = log(c/a)/log(b/d) -- `solve(Eq(a*b**x,c*d**x),x)` -> `[ln(c/a)/(ln(b) - ln(d))]` (expected `[ln(c/a)/ln(b/d)]`) 
- **M25** sqrt(log x) = log(sqrt x) => {1, e^4} -- `solve(Eq(sqrt(ln(x)),ln(sqrt(x))),x)` -> `[1,exp(4)]` (expected `[1,exp(4)]`) 
- **M26** log(acos(asin(x^(2/3) - b)) - 1) + 2 = 0 => x = (b + sin(cos(e^-2) + 1))^(3/2) -- `solve(Eq(ln(acos(asin(x**(2/3)-b))-1)+2,0),x)` -> `[(b + sin(cos(exp(-2) + 1)))^(3/2)]` (expected `[[(b+sin(cos(exp(-2)+1)))^(3/2)],[-(b+sin(cos(exp(-2)+1)))^(3/2),(b+sin(cos(exp(-2)+1)))^(3/2)],[(b+sin(cos(exp(-2)+1)))^(3/2),-(b+sin(cos(exp(-2)+1)))^(3/2)]]`) 
- **M28** |x - 1| = 2 => {-1, 3} -- `solve(Eq(abs(x-1),2),x)` -> `[-1,3]` (expected `[-1,3]`) 
- **M29** |2x + 5| = |x - 2| => {-1, -7} -- `solve(Eq(abs(2*x+5),abs(x-2)),x)` -> `[-7,-1]` (expected `[-1,-7]`) 
- **M33b** same with z = x + i y, x, y real => x = 2, y = 3 -- `solve([Eq(re((1+i)*(x+i*y)+(2-i)*(x-i*y)+3*i),0),Eq(im((1+i)*(x+i*y)+(2-i)*(x-i*y)+3*i),0)],[x,y])` -> `[[x=2,y=3]]` (expected `[[x=2,y=3]]`) 
- **M34** f(x)^2 + f(x) - 2 = 0 solved for f(x) => {1, -2} -- `solve(Eq(f(x)**2+f(x)-2,0),f(x))` -> `[-2,1]` (expected `[1,-2]`) 
- **M35** 3x3 linear system with a parametric solution => x = 4 - z, y = 2 -- `solve([Eq(x+y+z,6),Eq(2*x+y+2*z,10),Eq(x+3*y+z,10)],[x,y,z])` -> `[[x=4 - z,y=2]]` (expected ``) 
- **M36** Steinberg: 189 linear equations in 49 unknowns, 3 free -- `linsolve([Eq(-b*k8/a+c*k8/a,0),Eq(-b*k11/a+c*k11/a,0),Eq(-b*k10/a+c*k10/a+k2,0),Eq(-k3-b*k9/a+c*k9/a,0),Eq(-b*k14/a+c*k14/a,0),Eq(-b*k15/a+c*k15/a,0),Eq(-b*k18/a+c*k18/a-k2,0),Eq(-b*k17/a+c*k17/a,0),Eq(-b*k16/a+c*k16/a+k4,0),Eq(-b*k13/a+c*k13/a-b*k21/a+c*k21/a+b*k5/a-c*k5/a,0),Eq(b*k44/a-c*k44/a,0),` -> `[[k1=0,k10=0,k11=0,k12=0,k13=0,k14=0,k15=0,k16=0,k17=0,k18=0,k19=0,k2=0,k20=0,k21=0,k22=0,k23=k39,k24=0,k25=0,k26=a*k42/c,k27=0,k28=0,k29=0,k3=0,k30=0,k31=k39,k32=0,k33=0,k34=b*k42/c,k35=0,k36=0,k37=0` (expected ``) 
- **N1** e^pi > pi^e => true -- `e**pi>pi**e` -> `true` (expected `true`) 
- **N2** x^4 - x + 1 > 0 for real x => true -- `x**4-x+1>0` -> `true` (expected `true`) 
- **N8** x > 1, y >= x - 1 => y > 0 -- `assume(x>1) ; assume(y>=x-1) ; y>0` -> `true` (expected `true`) 
- **N9** x >= y >= z >= x => x = y -- `assume(x>=y) ; assume(y>=z) ; assume(z>=x) ; Eq(x,y)` -> `true` (expected `true`) 
- **N10** |x - 1| > 2 => x < -1 or x > 3 -- `solve(abs(x-1)>2,x)` -> `Or(3 < x,x < -1)` (expected `x<-1 or x>3`) 
- **N11** (x-1)...(x-5) < 0 expanded => x < 1 or 2 < x < 3 or 4 < x < 5 -- `solve(expand((x-1)*(x-2)*(x-3)*(x-4)*(x-5))<0,x)` -> `Or(x < 1,And(2 < x,x < 3),And(4 < x,x < 5))` (expected `x<1 or (2<x and x<3) or (4<x and x<5)`) 
- **N12** 6/(x - 3) <= 3 => x < 3 or x >= 5 -- `solve(6/(x-3)<=3,x)` -> `Or(5 <= x,x < 3)` (expected `x<3 or x>=5`) 
- **N13** sqrt(x) < 2 => 0 <= x < 4 -- `solve(sqrt(x)<2,x)` -> `And(0 <= x,x < 4)` (expected `0<=x and x<4`) 
- **N14** sin x < 2 => all real x -- `solve(sin(x)<2,x)` -> `true` (expected `true`) 
- **O1** norm([1 + i, -2, 3 i]) => sqrt(15) -- `norm([1+i,-2,3*i])` -> `sqrt(15)` (expected `sqrt(15)`) 
- **O2** (2, 2, -3) x (1, 3, 1) => (11, -5, 4) -- `cross([2,2,-3],[1,3,1])` -> `[11,-5,4]` (expected `[11,-5,4]`) 
- **O4** curl(x y z, x^2 y^2 z^2, y^2 z^3) -- `curl([x*y*z,x**2*y**2*z**2,y**2*z**3],[x,y,z])` -> `[-2*x^2*y^2*z + 2*y*z^3,x*y,2*x*y^2*z^2 - x*z]` (expected `[2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z]`) 
- **O8** scalar potential of (2xy, x^2 + 1, 6z^2) => x^2 y + y + 2z^3 -- `potential([2*x*y,x**2+1,6*z**2],[x,y,z])` -> `x^2*y + y + 2*z^3` (expected `x^2*y+y+2*z^3`) 
- **P1** superdiagonal of [[1,2,3],[4,5,6],[7,8,9]] => [2, 6] -- `diag(Matrix([[1,2,3],[4,5,6],[7,8,9]]),1)` -> `[2,6]` (expected `[2,6]`) 
- **P2** (2,3)-minor matrix => [[1, 2], [7, 8]] -- `minor(Matrix([[1,2,3],[4,5,6],[7,8,9]]),2,3)` -> `[[1,2],[7,8]]` (expected `[[1,2],[7,8]]`) 
- **P4** block diagonal matrix from [[a,1],[0,a]], b, [[c,1,0],[0,c,1],[0,0,c]] -- `blockdiag(Matrix([[a,1],[0,a]]),b,Matrix([[c,1,0],[0,c,1],[0,0,c]]))` -> `[[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]` (expected `[[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]`) 
- **P5** [[7, 11], [3, 8]] mod 2 => [[1, 1], [1, 0]] -- `mod(Matrix([[7,11],[3,8]]),2)` -> `[[1,1],[1,0]]` (expected `[[1,1],[1,0]]`) 
- **P6** d^2/dt^2 [[cos t, sin t], [-sin t, cos t]] -- `diff(Matrix([[cos(t),sin(t)],[-sin(t),cos(t)]]),t,2)` -> `[[-cos(t),-sin(t)],[sin(t),-cos(t)]]` (expected `[[-cos(t),-sin(t)],[sin(t),-cos(t)]]`) 
- **P7** [[x, y]] (a [[1,3,5],[2,4,6]] + [[7,-9,11],[-8,10,-12]]) -- `Matrix([[x,y]])*(a*Matrix([[1,3,5],[2,4,6]])+Matrix([[7,-9,11],[-8,10,-12]]))` -> `[[x*(a + 7) + y*(2*a - 8),x*(3*a - 9) + y*(4*a + 10),x*(5*a + 11) + y*(6*a - 12)]]` (expected `[[(a+7)*x+(2*a-8)*y,(3*a-9)*x+(4*a+10)*y,(5*a+11)*x+(6*a-12)*y]]`) 
- **P8** infinity norm of [[1, -2i], [-3i, 4]] => 7 -- `norm(Matrix([[1,-2*i],[-3*i,4]]),inf)` -> `7` (expected `7`) 
- **P10** Hermitian transpose of [[1, 2+3i], [f(4-5i), 6]] -- `conj(transpose(Matrix([[1,2+3*i],[f(4-5*i),6]])))` -> `[[1,conj(f(4 - 5*i))],[2 - 3*i,6]]` (expected `[[1,f(4+5*i)],[2-3*i,6]]`) conj(f(4-5i)) is the exact Hermitian-transpose entry for an unknown f; Wester's f(4+5i) assumes conj(f(z)) = f(conj(z))
- **P11** inverse of [[a, b], [1, a b]] => 1/(a^2 - 1) [[a, -1], [-1/b, a/b]] -- `inv(Matrix([[a,b],[1,a*b]]))` -> `[[a/(a^2 - 1),-1/(a^2 - 1)],[-1/(a^2*b - b),a/(a^2*b - b)]]` (expected `[[a/(a^2-1),-1/(a^2-1)],[-1/(b*(a^2-1)),a/(b*(a^2-1))]]`) 
- **P13** LU decomposition of a symbolic matrix (Wood) -- `lu(Matrix([[1,x-2,x-3],[x-1,x**2-3*x+6,x**2-3*x-2],[x-2,x**2-8,2*x**2-12*x+14]]))` -> `[[[1,0,0],[x - 1,1,0],[x - 2,(x^2 - (x - 2)^2 - 8)/(x^2 - 3*x - (x - 2)*(x - 1) + 6),1]],[[1,x - 2,x - 3],[0,x^2 - 3*x - (x - 2)*(x - 1) + 6,x^2 - 3*x - (x - 3)*(x - 1) - 2],[0,0,2*x^2 - 12*x - (x - 3` (expected `[[[1,0,0],[x-1,1,0],[x-2,x-3,1]],[[1,x-2,x-3],[0,4,x-5],[0,0,x-7]],[[1,0,0],[0,1,0],[0,0,1]]]`) 
- **P14** reduced row echelon form (Cullen) -- `rref(Matrix([[1,2,3,1,3],[3,2,1,1,7],[0,2,4,1,1],[1,1,1,1,4]]))` -> `[[1,0,-1,0,2],[0,1,2,0,-1],[0,0,0,1,3],[0,0,0,0,0]]` (expected `[[1,0,-1,0,2],[0,1,2,0,-1],[0,0,0,1,3],[0,0,0,0,0]]`) 
- **P15** rank => 2 -- `rank(Matrix([[-1,3,7,-5],[4,-2,1,3],[2,4,15,-7]]))` -> `2` (expected `2`) 
- **P16** rank of [[2 sqrt 2, 8], [6 sqrt 6, 24 sqrt 3]] => 1 -- `rank(Matrix([[2*sqrt(2),8],[6*sqrt(6),24*sqrt(3)]]))` -> `1` (expected `1`) 
- **P17** rank of a trigonometric matrix => 1 -- `rank(Matrix([[sin(2*t),cos(2*t)],[2*(1-cos(t)**2)*cos(t),(1-2*sin(t)**2)*sin(t)]]))` -> `1` (expected `1`) 
- **P18** null space => span{[2,4,1,0], [0,-3,0,1]} -- `ker(Matrix([[1,0,-2,0],[-2,1,0,3],[-1,2,-6,6]]))` -> `[[2,4,1,0],[0,-3,0,1]]` (expected `[[2,4,1,0],[0,-3,0,1]]`) 
- **P19** factor the 4x4 Vandermonde determinant -- `factor(det(Matrix([[1,1,1,1],[w,x,y,z],[w**2,x**2,y**2,z**2],[w**3,x**3,y**3,z**3]])))` -> `-(-w + x)*(-w + y)*(-w + z)*(x - y)*(x - z)*(y - z)` (expected `(w-x)*(w-y)*(w-z)*(x-y)*(x-z)*(y-z)`) 
- **P21** characteristic polynomial => x^3 - 2x^2 - 5x + 6 -- `charpoly(Matrix([[5,-3,-7],[-2,1,2],[2,-3,-4]]),x)` -> `x^3 - 2*x^2 - 5*x + 6` (expected `x^3-2*x^2-5*x+6`) 
- **P21b** eigenvalues from it => {1, -2, 3} -- `solve(Eq(charpoly(Matrix([[5,-3,-7],[-2,1,2],[2,-3,-4]]),x),0),x)` -> `[-2,1,3]` (expected `[1,-2,3]`) 
- **P22** eigenvalues of (2 - a) I_100 => 2 - a -- `eigenvals((2-a)*identity(100))` -> `[2 - a]` (expected `[2-a]`) 
- **P23** eigenvalues of the 5x5 tridiagonal [1 2 1] matrix -- `eigenvals(Matrix([[2,1,0,0,0],[1,2,1,0,0],[0,1,2,1,0],[0,0,1,2,1],[0,0,0,1,2]]))` -> `[3,2,1,2 - sqrt(3),sqrt(3) + 2]` (expected `[2-sqrt(3),1,2,3,2+sqrt(3)]`) 
- **P24** exact eigenvalues of the Rosser matrix -- `eigenvals(Matrix([[611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99]]))` -> `[1020,1000,510 - 100*sqrt(26),100*sqrt(26) + 510,-10*sqrt(10405),10*sqrt(10405),0]` (expected `[-10*sqrt(10405),0,510-100*sqrt(26),1000,510+100*sqrt(26),1020,10*sqrt(10405)]`) 
- **P25** numeric eigenvalues of the Rosser matrix -- `eigenvals(approx(Matrix([[611,196,-192,407,-8,-52,-49,29],[196,899,113,-192,-71,-43,-8,-44],[-192,113,899,196,61,49,8,52],[407,-192,196,611,8,44,59,-23],[-8,-71,61,8,411,-599,208,208],[-52,-43,49,44,-599,411,208,208],[-49,-8,8,59,208,208,99,-911],[29,-44,52,-23,208,208,-911,99]])))` -> `[-1020.04901843,0.098048640721517,-1.56506781950751e-61,1000.0,1020.04901843,1020.0,1019.90195135928]` (expected `[-1020.0490184299969,0,0.09804864072151699,1000,1019.9019513592784,1020,1020.0490184299969]`) 
- **P27** eigenvectors of a 5x5 matrix with eigenvalues {a, a, a, 1 - i, 1 + i} -- `eigenvects(Matrix([[a,0,0,0,0],[0,0,0,0,1],[0,0,a,0,0],[0,0,0,a,0],[0,-2,0,0,2]]))` -> `[[a,[[1,0,0,0,0],[0,0,1,0,0],[0,0,0,1,0]]],[1 - i,[[0,1/2 + i/2,0,0,1]]],[1 + i,[[0,1/2 - i/2,0,0,1]]]]` (expected ``) 
- **P30** Jordan form => diag([[1,1],[0,1]], [[1,1],[0,1]], -1) -- `jordan(Matrix([[1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2]]))` -> `[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]` (expected `[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]`) Jordan form: -1 and blocks [[1,1],[0,1]] twice
- **P31** Smith normal form of [[x^2, x - 1], [x + 1, x^2]] => [[1, 0], [0, x^4 - x^2 + 1]] -- `smith(Matrix([[x**2,x-1],[x+1,x**2]]))` -> `[[1,0],[0,x^4 - x^2 + 1]]` (expected `[[1,0],[0,x^4-x^2+1]]`) 
- **P32** matrix exponential of [[1, -2], [2, 1]] => e [[cos 2, -sin 2], [sin 2, cos 2]] -- `expm(Matrix([[1,-2],[2,1]]))` -> `[[e*cos(2),-e*sin(2)],[e*sin(2),e*cos(2)]]` (expected `[[e*cos(2),-e*sin(2)],[e*sin(2),e*cos(2)]]`) 
- **P33** matrix exponential exp(M t) (Niles) -- `expm(Matrix([[0,1,0,0],[0,0,0,2*w],[0,0,0,1],[0,-2*w,3*w**2,0]])*t)` -> `[[1,-3*t + 4*sin(t*w)/w,6*t*w - 6*sin(t*w),-2*cos(t*w)/w + 2/w],[0,4*cos(t*w) - 3,-6*w*cos(t*w) + 6*w,2*sin(t*w)],[0,2*cos(t*w)/w - 2/w,4 - 3*cos(t*w),sin(t*w)/w],[0,-2*sin(t*w),3*w*sin(t*w),cos(t*w)]` (expected `[[1,4*sin(w*t)/w-3*t,6*(w*t-sin(w*t)),2/w*(1-cos(w*t))],[0,4*cos(w*t)-3,6*w*(1-cos(w*t)),2*sin(w*t)],[0,-2/w*(1-cos(w*t)),4-3*cos(w*t),sin(w*t)/w],[0,-2*sin(w*t),3*w*sin(w*t),cos(w*t)]]`) 
- **P35** sin(pi/2 [[2,1,1],[2,3,2],[1,1,2]]) => identity -- `sinm(pi/2*Matrix([[2,1,1],[2,3,2],[1,1,2]]))` -> `[[1,0,0],[0,1,0],[0,0,1]]` (expected `[[1,0,0],[0,1,0],[0,0,1]]`) 
- **P36** matrix square root of [[10, 7], [7, 17]] -- `Matrix([[10,7],[7,17]])**(1/2)` -> `[[7*sqrt(7*sqrt(5)/2 + 27/2)/((-7*sqrt(5)/2 - 7/2)*(-7/(-7/2 + 7*sqrt(5)/2) + 7/(-7*sqrt(5)/2 - 7/2))) - 7*(-3 + sqrt(5))*sqrt(27/2 - 7*sqrt(5)/2)/((-5 + 3*sqrt(5))*(-7/2 + 7*sqrt(5)/2)),-7*(-3 + sqrt` (expected `[[10,7],[7,17]]`) 
- **P37** square root of [[1,1,0],[0,1,0],[0,0,1]] => [[1,1/2,0],[0,1,0],[0,0,1]] -- `Matrix([[1,1,0],[0,1,0],[0,0,1]])**(1/2)` -> `[[1,1/2,0],[0,1,0],[0,0,1]]` (expected `[[1,1,0],[0,1,0],[0,0,1]]`) 
- **P39** singular value decomposition of [[1,1],[2,2],[3,3]] -- `svd(Matrix([[1,1],[2,2],[3,3]]))` -> `[[[sqrt(14)/14],[sqrt(14)/7],[3*sqrt(14)/14]],[[2*sqrt(7)]],[[sqrt(2)/2],[sqrt(2)/2]]]` (expected `[2*sqrt(7),0]`) exact (reduced) SVD: singular value 2 sqrt(7); the zero one is omitted
- **P40** Jacobian of (r cos t, r sin t) -- `jacobian([r*cos(t),r*sin(t)],[r,t])` -> `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) 
- **P41** Hessian of r^2 sin t -- `hessian(r**2*sin(t),[r,t])` -> `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P43** user-defined Jacobian -- `g(f) := grad(f,[r,t]) ; map([r*cos(t),r*sin(t)],g)` -> `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]` (expected `[[cos(t),-r*sin(t)],[sin(t),r*cos(t)]]`) 
- **P44** user-defined Hessian -- `g(f) := grad(f,[r,t]) ; map(grad(r**2*sin(t),[r,t]),g)` -> `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]` (expected `[[2*sin(t),2*r*cos(t)],[2*r*cos(t),-r^2*sin(t)]]`) 
- **P45** user-defined Wronskian -- `g(f) := [f,diff(f,t)] ; transpose(map([cos(t),sin(t)],g))` -> `[[cos(t),sin(t)],[-sin(t),cos(t)]]` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) 
- **R6** telescoping sum(g(k+1) - g(k), k = 0..n) => g(n+1) - g(0) -- `sum(g(k+1)-g(k),k,0,n)` -> `-g(0) + g(n + 1)` (expected `g(n+1)-g(0)`) 
- **R7** sum(k^3, k = 1..n) => n^2 (n+1)^2/4 -- `sum(k**3,k,1,n)` -> `n^4/4 + n^3/2 + n^2/4` (expected `n^2*(n+1)^2/4`) 
- **R9** sum(binomial(n, k)/(k + 1), k = 0..n) => (2^(n+1) - 1)/(n + 1) -- `sum(binomial(n,k)/(k+1),k,0,n)` -> `piecewise(Or(n > -1,And(n <= -1,n > -2)),2^(n + 1)/(n + 1) - 1/(n + 1),sum(binomial(n,k)/(k + 1),k,0,n))` (expected `(2^(n+1)-1)/(n+1)`) (conditional answer: the branch that holds on the problem domain)
- **R10** Vandermonde: sum(binomial(n, k) binomial(m, r - k), k = 0..r) => binomial(n + m, r) -- `sum(binomial(n,k)*binomial(m,r-k),k,0,r)` -> `piecewise(m + n > -1,gamma(m + n + 1)/(gamma(r + 1)*gamma(m + n - r + 1)),sum(binomial(m,-k + r)*binomial(n,k),k,0,r))` (expected `binomial(n+m,r)`) (conditional answer: the branch that holds on the problem domain)
- **R16** sum(1/k^2 + 1/k^3, k = 1..inf) => pi^2/6 + zeta(3) -- `sum(1/k**2+1/k**3,k,1,inf)` -> `zeta(3) + pi^2/6` (expected `pi^2/6+zeta(3)`) 
- **R17** numeric value of R16 => 2.84699 -- `approx(sum(1/k**2+1/k**3,k,1,inf),15)` -> `2.84699097000782` (expected `2.8469909700078206`) 
- **R18** sum(1/(2^k k^2), k = 1..inf) => pi^2/12 - log(2)^2/2 -- `sum(1/(2**k*k**2),k,1,inf)` -> `-ln(2)^2/2 + pi^2/12` (expected `pi^2/12-ln(2)^2/2`) 
- **R24** sum(product(k/(2k - 1), k = 1..m), m = 2..inf) => pi/2 -- `sum(product(k/(2*k-1),k,1,m),m,2,inf)` -> `pi/2` (expected `pi/2`) 
- **S2** product(k, k = 1..n) => n! -- `product(k,k,1,n)` -> `factorial(n)` (expected `factorial(n)`) 
- **S3** product(x^k, k = 1..n) => x^(n (n+1)/2) -- `product(x**k,k,1,n)` -> `x^(n^2/2 + n/2)` (expected `x^(n*(n+1)/2)`) 
- **S4** product(1 + 1/k, k = 1..n-1) => n -- `product(1+1/k,k,1,n-1)` -> `rf(2,n - 1)/factorial(n - 1)` (expected `n`) 
- **S5** product((2k - 1)/(2k), k = 1..n) => binomial(2n, n)/2^(2n) -- `product((2*k-1)/(2*k),k,1,n)` -> `rf(1/2,n)/factorial(n)` (expected `binomial(2*n,n)/4^n`) 
- **T1** limit((1 + 1/n)^n, n, inf) => e -- `limit((1+1/n)**n,n,inf)` -> `e` (expected `e`) 
- **T2** limit((1 - cos x)/x^2, x, 0) => 1/2 -- `limit((1-cos(x))/x**2,x,0)` -> `1/2` (expected `1/2`) 
- **T3** limit((3^x + 5^x)^(1/x), x, inf) => 5 (Gruntz) -- `limit((3**x+5**x)**(1/x),x,inf)` -> `5` (expected `5`) 
- **T4** limit(log x/(log x + sin x), x, inf) => 1 -- `limit(ln(x)/(ln(x)+sin(x)),x,inf)` -> `1` (expected `1`) 
- **T5** Gruntz: limit((exp(x e^-x/(e^-x + e^(-2x^2/(x+1)))) - e^x)/x, x, inf) => -e^2 -- `limit((exp(x*exp(-x)/(exp(-x)+exp(-2*x**2/(x+1))))-exp(x))/x,x,inf)` -> `-exp(2)` (expected `-exp(2)`) 
- **T6** Gruntz: x log x log(x e^x - x^2)^2/log(log(x^2 + 2 e^(e^(3 x^3 log x)))) => 1/3 -- `limit(x*ln(x)*ln(x*exp(x)-x**2)**2/ln(ln(x**2+2*exp(exp(3*x**3*ln(x))))),x,inf)` -> `1/3` (expected `1/3`) 
- **T7** limit(n!^(1/n)/n, n, inf) => 1/e -- `limit(n!**(1/n)/n,n,inf)` -> `exp(-1)` (expected `exp(-1)`) 
- **T8** limit(gamma(n + 1)^(1/n)/n, n, inf) => 1/e -- `limit(gamma(n+1)**(1/n)/n,n,inf)` -> `exp(-1)` (expected `exp(-1)`) 
- **T9** limit(gamma(z + a)/gamma(z) exp(-a log z), z, inf) => 1 (a > 0) -- `assume(a>0) ; limit(gamma(z+a)/gamma(z)*exp(-a*ln(z)),z,inf)` -> `1` (expected `1`) 
- **T10** same for a < 0 => 1 -- `assume(a<0) ; limit(gamma(z+a)/gamma(z)*exp(-a*ln(z)),z,inf)` -> `1` (expected `1`) 
- **T11** limit(2F1(1, k; 1; z/k), k, inf) => e^z -- `limit(hypergeom([1,k],[1],z/k),k,inf)` -> `exp(z)` (expected `exp(z)`) 
- **T14** limit(x int(exp(-t^2), t = 0..x)/(1 - exp(-x^2)), x, 0) => 1 -- `limit(x*integrate(exp(-t**2),t,0,x)/(1-exp(-x**2)),x,0)` -> `1` (expected `1`) 
- **T15** one-sided limits of x/|x| at 0 => [-1, 1] -- `[limit(x/abs(x),x,0,-1),limit(x/abs(x),x,0,1)]` -> `[-1,1]` (expected `[-1,1]`) 
- **T16** limit(atan(-log x), x, 0, +) => pi/2 -- `limit(atan(-ln(x)),x,0,1)` -> `pi/2` (expected `pi/2`) 
- **U1** d/dx |x| => sign(x) -- `diff(abs(x),x)` -> `sign(x)` (expected `sign(x)`) 
- **U2** derivative of the piecewise function (-x for x < 0, x otherwise) -- `diff(piecewise(x<0,-x,x),x)` -> `piecewise(x < 0,-1,1)` (expected `piecewise(x<0,-1,1)`) 
- **U2b** same with unit steps: d/dx(-x H(-x) + x H(x)) => sign(x) -- `simplify(diff(-x*heaviside(-x)+x*heaviside(x),x))` -> `2*x*dirac(x) - heaviside(-x) + heaviside(x)` (expected `sign(x)`) 
- **U3** f(x) = x^2 - 1 at x = 1, x^3 otherwise: f(1) => 0 -- `f(x) := piecewise(Eq(x,1),x**2-1,x**3) ; f(1)` -> `0` (expected `0`) 
- **U5** chain rule: d^2/dt^2 f(g(t)) => f''(g) g'^2 + f'(g) g'' -- `diff(f(g(t)),t,2)` -> `diff(f(g(t)),g(t))*diff(g(t),t,2) + diff(f(g(t)),g(t),2)*diff(g(t),t)^2` (expected ``) chain rule: g'' f'(g) + g'^2 f''(g)
- **U6** d/dx int(f(y), y = g(x)..h(x)) => f(h(x)) h'(x) - f(g(x)) g'(x) -- `diff(integrate(f(y),y,g(x),h(x)),x)` -> `-f(g(x))*diff(g(x),x) + f(h(x))*diff(h(x),x)` (expected ``) Leibniz rule: h' f(h) - g' f(g)
- **U8** implicit differentiation of y = cos(x y) + x => (1 - y sin(x y))/(1 + x sin(x y)) -- `implicitdiff(Eq(y,cos(x*y)+x),y,x)` -> `(-y*sin(x*y) + 1)/(x*sin(x*y) + 1)` (expected `(1-y*sin(x*y))/(1+x*sin(x*y))`) 
- **U9** f_x + f_y with f(x, y) = g(x^2 + y^2) => 2 (x + y) g'(x^2 + y^2) -- `factor(diff(g(x**2+y**2),x)+diff(g(x**2+y**2),y))` -> `2*(x + y)*subst(diff(g(_xi_1),_xi_1),_xi_1,x^2 + y^2)` (expected ``) 2 (x+y) g'(x^2+y^2) (as a Subs object)
- **U10** residue((z^3 + 5)/((z^4 - 1)(z + 1)), z, -1) => -9/4 -- `residue((z**3+5)/((z**4-1)*(z+1)),z,-1)` -> `-9/4` (expected `-9/4`) 
- **U13** minimum of x^4 - x + 1 => 1 - 3/8 2^(1/3) -- `minimize(x**4-x+1,x)` -> `1 - 3*2^(1/3)/8` (expected `1-3*2^(1/3)/8`) 
- **V1** int |x| dx => x |x|/2 -- `integrate(abs(x),x)` -> `piecewise(x <= 0,-x^2/2,x^2/2)` (expected `abs(x)`) 
- **V3** int of the piecewise function (-x for x < 0, x otherwise) -- `integrate(piecewise(x<0,-x,x),x)` -> `piecewise(x < 0,-x^2/2,x^2/2)` (expected `piecewise(x<0,-x,x)`) 
- **V4** int -x H(-x) + x H(x) dx -- `integrate(-x*heaviside(-x)+x*heaviside(x),x)` -> `-x^2*heaviside(-x)/2 + x^2*heaviside(x)/2` (expected `-x*heaviside(-x)+x*heaviside(x)`) 
- **V5** int 1/(x^3 + 2) dx -- `integrate(1/(x**3+2),x)` -> `2^(1/3)*ln(x + 2^(1/3))/6 - 2^(1/3)*ln(x^2 - 2^(1/3)*x + 2^(2/3))/12 + 2^(1/3)*sqrt(3)*atan(2^(2/3)*sqrt(3)*x/3 - sqrt(3)/3)/6` (expected `1/(x^3+2)`) 
- **V6** int 2^x/sqrt(1 + 4^x) dx => asinh(2^x)/log 2 -- `integrate(2**x/sqrt(1+4**x),x)` -> `asinh(2^x)/ln(2)` (expected `2^x/sqrt(1+4^x)`) 
- **V7** int (3x - 5)^2/(2x - 1)^(7/2) dx -- `integrate((3*x-5)**2/(2*x-1)**(7/2),x)` -> `-45*x^2/(20*x^2*sqrt(2*x - 1) - 20*x*sqrt(2*x - 1) + 5*sqrt(2*x - 1)) + 80*x/(20*x^2*sqrt(2*x - 1) - 20*x*sqrt(2*x - 1) + 5*sqrt(2*x - 1)) - 41/(20*x^2*sqrt(2*x - 1) - 20*x*sqrt(2*x - 1) + 5*sqrt(2*x ` (expected `(3*x-5)^2/(2*x-1)^(7/2)`) 
- **V8** int 1/(2 e^(m x) - 5 e^(-m x)) dx -- `integrate(1/(2*exp(m*x)-5*exp(-m*x)),x)` -> `(sqrt(10)*ln(-sqrt(10)/5 + exp(-m*x))/20 - sqrt(10)*ln(sqrt(10)/5 + exp(-m*x))/20)/m` (expected `1/(2*exp(m*x)-5*exp(-m*x))`) 
- **V9** int sinh^4 x/cosh^2 x dx -- `integrate(sinh(x)**4/cosh(x)**2,x)` -> `3*x*sinh(x)^2/2 - 3*x*cosh(x)^2/2 - sinh(x)^3/cosh(x) + 3*sinh(x)*cosh(x)/2` (expected `sinh(x)^4/cosh(x)^2`) 
- **V10** int 1/(a + b cos x) dx (a^2 < b^2) -- `integrate(1/(a+b*cos(x)),x)` -> `piecewise(And(Eq(a,0),Eq(b,0)),undef*(-ln(tan(x/2) - 1) + ln(tan(x/2) + 1)),Eq(a,b),tan(x/2)/b,Eq(a,-b),1/(b*tan(x/2)),ln(-sqrt(-a/(a - b) - b/(a - b)) + tan(x/2))/(a*sqrt(-a/(a - b) - b/(a - b)) - b*` (expected `1/(a+b*cos(x))`) (conditional answer: the branch that holds on the problem domain)
- **V11** int 1/(3 + 3 cos x + 4 sin x) dx => log(3 + 4 tan(x/2))/4 -- `integrate(1/(3+3*cos(x)+4*sin(x)),x)` -> `ln(4*tan(x/2) + 3)/4` (expected `1/(3+3*cos(x)+4*sin(x))`) 
- **V12** int 1/(4 + 3 cos x + 4 sin x) dx -- `integrate(1/(4+3*cos(x)+4*sin(x)),x)` -> `ln(tan(x/2) + 1)/3 - ln(tan(x/2) + 7)/3` (expected `1/(4+3*cos(x)+4*sin(x))`) 
- **V13** int 1/(5 + 3 cos x + 4 sin x) dx => -1/(2 + tan(x/2)) -- `integrate(1/(5+3*cos(x)+4*sin(x)),x)` -> `-1/(tan(x/2) + 2)` (expected `1/(5+3*cos(x)+4*sin(x))`) 
- **V14** int 1/(6 + 3 cos x + 4 sin x) dx -- `integrate(1/(6+3*cos(x)+4*sin(x)),x)` -> `2*sqrt(11)*(atan(3*sqrt(11)*tan(x/2)/11 + 4*sqrt(11)/11) + pi*floor((x/2 - pi/2)/pi))/11` (expected `1/(6+3*cos(x)+4*sin(x))`) derivative checked by central differences
- **V16** int x acot(x/a) dx -- `integrate(x*acot(x/a),x)` -> `a^2*acot(x/a)/2 + a*x/2 + x^2*acot(x/a)/2` (expected `x*acot(x/a)`) 
- **W7** int cos x/(x^2 + a^2), x = -inf..inf => pi e^(-a)/a (a > 0) -- `assume(a>0) ; integrate(cos(x)/(x**2+a**2),x,-inf,inf)` -> `sqrt(pi)*(-sqrt(pi)*sinh(a) + sqrt(pi)*cosh(a))/a` (expected `pi*exp(-a)/a`) 
- **W8** int t^(a-1)/(1 + t), t = 0..inf => pi/sin(pi a) (0 < a < 1) -- `assume(a>0) ; integrate(t**(a-1)/(1+t),t,0,inf)` -> `piecewise(a < 1,pi/sin(pi*a),integrate(t^(a - 1)/(t + 1),t,0,inf))` (expected `pi/sin(pi*a)`) (conditional answer: the branch that holds on the problem domain)
- **W12** int x exp(-p x^2 + 2 q x), x = -inf..inf => q/p sqrt(pi/p) e^(q^2/p) -- `assume(p>0) ; integrate(x*exp(-p*x**2+2*q*x),x,-inf,inf)` -> `-q*(-2*sqrt(p)*exp(-q^2/p)/q - 2*sqrt(pi)*(2 - erfc(q/sqrt(p))))*exp(q^2/p)/(4*p^(3/2)) - q*(2*sqrt(p)*exp(-q^2/p)/q - 2*sqrt(pi)*erfc(q/sqrt(p)))*exp(q^2/p)/(4*p^(3/2))` (expected `q/p*sqrt(pi/p)*exp(q^2/p)`) 
- **W14** int sin t/t exp(2 i t), t = -inf..inf => 0 -- `integrate(sin(t)/t*exp(2*i*t),t,-inf,inf)` -> `0` (expected `0`) 
- **W16** int (1 + x)^3 P1(x) P2(x), x = -1..1 => 36/35 -- `integrate((1+x)**3*legendre(1,x)*legendre(2,x),x,-1,1)` -> `36/35` (expected `36/35`) 
- **W16b** same with the Legendre polynomials written out -- `integrate((1+x)**3*x*(3*x**2-1)/2,x,-1,1)` -> `36/35` (expected `36/35`) 
- **W17** int exp(-a x) J0(b x), x = 0..inf => 1/sqrt(a^2 + b^2) -- `assume(a>0) ; integrate(exp(-a*x)*besselj(0,b*x),x,0,inf)` -> `piecewise(Eq(abs(arg(b)),0),1/(b*sqrt(a^2/b^2 + 1)),integrate(exp(-a*x)*besselj(0,b*x),x,0,inf))` (expected `1/sqrt(a^2+b^2)`) (conditional answer: the branch that holds on the problem domain)
- **W18** int (J1(x)/x)^2, x = 0..inf => 4/(3 pi) -- `integrate((besselj(1,x)/x)**2,x,0,inf)` -> `4/(3*pi)` (expected `4/(3*pi)`) 
- **W21** numeric value of W20 => 0.210883 -- `nint(x**2*polylog(3,1/(x+1)),x,0,1)` -> `0.210882859565594` (expected `0.210882859565594`) 
- **W22** int s(t) cos t, t = 0..u for the step s = 1 on [1, 2] -- `integrate(piecewise(And(t>=1,t<=2),1,0)*cos(t),t,0,u)` -> `piecewise(u < 0,0,-sin(Min(1,u)) + sin(Min(2,u)))` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) 
- **W23** same with s(t) = H(t - 1) - H(t - 2) -- `integrate((heaviside(t-1)-heaviside(t-2))*cos(t),t,0,u)` -> `(sin(u) - sin(1))*heaviside(u - 1) - (sin(u) - sin(2))*heaviside(u - 2)` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) 
- **W24** int(int(x/(x^2 + y^2), y = -inf..inf), x = a..b), a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x**2+y**2),y,-inf,inf),x,a,b)` -> `-pi*a + pi*b` (expected `pi*(b-a)`) 
- **W25** other order, a, b > 0 => (b - a) pi -- `assume(a>0) ; assume(b>0) ; integrate(integrate(x/(x**2+y**2),x,a,b),y,-inf,inf)` -> `(-pi*a^3 + pi*a^2*b + pi*a*b^2 - pi*b^3)/(2*a^2 - 2*b^2) - (pi*a^3 - pi*a^2*b - pi*a*b^2 + pi*b^3)/(2*a^2 - 2*b^2)` (expected `pi*(b-a)`) 
- **W27** x first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x**2+y**2),x,a,b),y,-inf,inf)` -> `-(-pi*a^3 - pi*a^2*b + pi*a*b^2 + pi*b^3)/(2*a^2 - 2*b^2) + (pi*a^3 + pi*a^2*b - pi*a*b^2 - pi*b^3)/(2*a^2 - 2*b^2)` (expected `pi*(b+a)`) 
- **W28** y first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x**2+y**2),y,-inf,inf),x,a,b)` -> `pi*a - pi*b` (expected `pi*(a-b)`) 
- **W29** x first, a, b < 0 => (|b| - |a|) pi -- `assume(a<0) ; assume(b<0) ; integrate(integrate(x/(x**2+y**2),x,a,b),y,-inf,inf)` -> `-(-pi*a^3 + pi*a^2*b + pi*a*b^2 - pi*b^3)/(2*a^2 - 2*b^2) + (pi*a^3 - pi*a^2*b - pi*a*b^2 + pi*b^3)/(2*a^2 - 2*b^2)` (expected `pi*(a-b)`) 
- **W32** int(int(|y - x^2|, y = 0..2), x = -1..1) => 46/15 -- `integrate(integrate(abs(y-x**2),y,0,2),x,-1,1)` -> `46/15` (expected `46/15`) 
- **W33** volume of a tetrahedron => a b c/6 -- `integrate(integrate(integrate(1,z,0,c*(1-x/a-y/b)),y,0,b*(1-x/a)),x,0,a)` -> `a*b*c/6` (expected `a*b*c/6`) 
- **X1** Taylor 1/sqrt(1 - (v/c)^2) to O(v^8) -- `series(1/sqrt(1-(v/c)**2),v,0,8)` -> `1 + v^2/(2*c^2) + 3*v^4/(8*c^4) + 5*v^6/(16*c^6)` (expected `1+v^2/(2*c^2)+3*v^4/(8*c^4)+5*v^6/(16*c^6)`) 
- **X2** series of 1/s1^2 => 1 - v^2/c^2 -- `s1 := series(1/sqrt(1-(v/c)**2),v,0,8) ; series(1/s1**2,v,0,8)` -> `1 - v^2/c^2` (expected `1-v^2/c^2`) 
- **X3** series(sin)/series(cos) => tan series -- `series(series(sin(x),x,0,8)/series(cos(x),x,0,8),x,0,8)` -> `17*x^7/315 + 2*x^5/15 + x^3/3 + x` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X4** tan(x) series to O(x^8) -- `series(tan(x),x,0,8)` -> `17*x^7/315 + 2*x^5/15 + x^3/3 + x` (expected `x+x^3/3+2*x^5/15+17*x^7/315`) 
- **X5** log(sin x/x) => -x^2/6 - x^4/180 - x^6/2835 -- `series(ln(sin(x)/x),x,0,8)` -> `-x^6/2835 - x^4/180 - x^2/6` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X6** log of the series of sin x/x -- `series(ln(series(sin(x)/x,x,0,8)),x,0,8)` -> `-x^6/2835 - x^4/180 - x^2/6` (expected `-x^2/6-x^4/180-x^6/2835`) 
- **X9** Laurent series of 1/(x (e^x - 1)) -- `series(1/(x*(exp(x)-1)),x,0,7)` -> `-x^6/1209600 + x^4/30240 - x^2/720 + 1/12 - 1/(2*x) + x^(-2)` (expected `1/x^2-1/(2*x)+1/12-x^2/720+x^4/30240-x^6/1209600`) 
- **X10** Puiseux series of sqrt(sec x) at 3 pi/2 -- `series(sqrt(sec(x)),x,3*pi/2,4)` -> `(x - 3*pi/2)^(7/2)/160 + (x - 3*pi/2)^(3/2)/12 + 1/sqrt(x - 3*pi/2)` (expected `1/sqrt(x-3*pi/2)+(x-3*pi/2)^(3/2)/12+(x-3*pi/2)^(7/2)/160`) 
- **X11** generalized Taylor series of x^x -- `series(x**x,x,0,4)` -> `x^3*ln(x)^3/6 + x^2*ln(x)^2/2 + x*ln(x) + 1` (expected `1+x*ln(x)+x^2*ln(x)^2/2+x^3*ln(x)^3/6`) 
- **X12** log(sinh z) + log(cosh(z + w)) => log z + log cosh w + tanh(w) z -- `series(ln(sinh(z))+ln(cosh(z+w)),z,0,2)` -> `z*sinh(w)/cosh(w) + ln(z) + ln(cosh(w))` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X13** log(sinh z cosh(z + w)) => same -- `series(ln(sinh(z)*cosh(z+w)),z,0,2)` -> `z*sinh(w)/cosh(w) + ln(z) + ln(cosh(w))` (expected `ln(z)+ln(cosh(w))+tanh(w)*z`) 
- **X15** asymptotic sqrt(2 x^2 + 1) => sqrt(2) x + O(1/x) -- `series(sqrt(2*x**2+1),x,inf,1)` -> `sqrt(2)*x` (expected `sqrt(2)*x`) 
- **X23** Fourier series of x on [-p, p]: b_n => -2 p (-1)^n/(n pi) -- `assume(p>0) ; assume(n,integer) ; fourier_bn(x,x,2*p,n,-p)` -> `piecewise(Or(n > 0,n < 0),-2*(-1)^n*p/(pi*n),n >= 0,0,undef)` (expected `-2*p*(-1)^n/(n*pi)`) (conditional answer: the branch that holds on the problem domain)
- **X23b** Fourier series of x on [-p, p]: a_n => 0 -- `assume(p>0) ; assume(n,integer) ; fourier_an(x,x,2*p,n,-p)` -> `0` (expected `0`) 
- **X24** Fourier series of |x| on [-p, p]: a_n => 2 p ((-1)^n - 1)/(pi^2 n^2) -- `assume(p>0) ; assume(n,integer) ; fourier_an(abs(x),x,2*p,n,-p)` -> `piecewise(Or(n > 0,n < 0),2*p*((-1)^n - 1)/(pi^2*n^2),n >= 0,p,undef)` (expected `2*p*((-1)^n-1)/(pi^2*n^2)`) (conditional answer: the branch that holds on the problem domain)
- **X24b** Fourier series of |x|: constant term => p/2 -- `assume(p>0) ; fourier_an(abs(x),x,2*p,0,-p)` -> `p/2` (expected `p/2`) 
- **Y1** laplace(cos((w - 1) t)) => s/(s^2 + (w - 1)^2) -- `laplace(cos((w-1)*t),t,s)` -> `s/(s^2 + (w - 1)^2)` (expected `s/(s^2+(w-1)^2)`) 
- **Y2** inverse Laplace back => cos((w - 1) t) -- `ilaplace(s/(s**2+(w-1)**2),s,t)` -> `cos(t*(1 - w))*heaviside(t)` (expected `cos((w-1)*t)`) 
- **Y3** laplace(sinh(w t) cosh(w t)) => w/(s^2 - 4 w^2) -- `laplace(sinh(w*t)*cosh(w*t),t,s)` -> `w/(s^2 - 4*w^2)` (expected `w/(s^2-4*w^2)`) 
- **Y4** laplace(erf(3/sqrt t)) => 1/s - e^(-6 sqrt s)/s -- `laplace(erf(3/sqrt(t)),t,s)` -> `9*(sqrt(pi)*(-cosh(6*sqrt(s))/(9*s) + 1/(9*s)) + sqrt(pi)*sinh(6*sqrt(s))/(9*s))/sqrt(pi)` (expected `1/s-exp(-6*sqrt(s))/s`) 
- **Y5** y'' + y = 4 (H(t - 1) - H(t - 2)), y(0) = 1, y'(0) = 0 -- `dsolve_([Eq(Derivative(y(x),x,2)+y(x),4*(heaviside(x-1)-heaviside(x-2)))],{y(x).subs(x,0):1,y(x).diff(x,1).subs(x,0):0},[y(x)],x)` -> `y=cos(x) + 4*cos(x - 2)*heaviside(x - 2) - 4*cos(x - 1)*heaviside(x - 1) - 4*heaviside(x - 2) + 4*heaviside(x - 1)` (expected `Y2+Y-4*(sp.Heaviside(x-1)-sp.Heaviside(x-2))`) 
- **Y6** inverse Laplace of the transformed solution of Y5 -- `ilaplace(s/(s**2+1)+4*(1/s-s/(s**2+1))*(exp(-s)-exp(-2*s)),s,t)` -> `cos(t)*heaviside(t) + 4*cos(t - 2)*heaviside(t - 2) - 4*cos(t - 1)*heaviside(t - 1) - 4*heaviside(t - 2) + 4*heaviside(t - 1)` (expected `cos(t)+4*((1-cos(t-1))*heaviside(t-1)-(1-cos(t-2))*heaviside(t-2))`) 
- **Y9** Fourier transform of exp(-9 x^2) -- `fourier(exp(-9*x**2),x,z)` -> `sqrt(pi)*exp(-pi^2*z^2/9)/3` (expected `exp(-z^2/36)/(3*sqrt(2))`) sqrt(pi)/3 exp(-pi^2 z^2/9): correct for SymPy's convention (exp(-2 pi i x z))
- **Y10** Fourier transform of |x| exp(-3 |x|) -- `fourier(abs(x)*exp(-3*abs(x)),x,z)` -> `2*(-4*pi^2*z^2 + 9)/(16*pi^4*z^4 + 72*pi^2*z^2 + 81)` (expected `sqrt(2/pi)*(9-z^2)/(9+z^2)^2`) 2 (9-4 pi^2 z^2)/(9+4 pi^2 z^2)^2: correct for SymPy's convention (exp(-2 pi i x z))
- **Y12** Mellin transform of J3(x)/x^3 => 2^(s-4) gamma(s/2)/gamma(4 - s/2) -- `assume(s>0) ; integrate(besselj(3,x)/x**3*x**(s-1),x,0,inf)` -> `piecewise(s < 9/2,-2*4^(s/2 - 5/2)*gamma(s/2 - 3)/gamma(1 - s/2),integrate(x^(s - 1)*besselj(3,x)/x^3,x,0,inf))` (expected `2^(s-4)*gamma(s/2)/gamma(4-s/2)`) (conditional answer: the branch that holds on the problem domain)
- **Z1** r(n+2) - 2 r(n+1) + r(n) = 2, r(0) = 1, r(1) = m => n^2 + (m - 2) n + 1 -- `rsolve(Eq(f(n+2)-2*f(n+1)+f(n),2),f(n),[Eq(f(0),1),Eq(f(1),m)])` -> `n^2 + n*(m - 2) + 1` (expected `n^2+(m-2)*n+1`) 
- **Z2** r(n) = 5 r(n-1) - 6 r(n-2), r(0) = 0, r(1) = 1 => 3^n - 2^n -- `rsolve(Eq(f(n),5*f(n-1)-6*f(n-2)),f(n),[Eq(f(0),0),Eq(f(1),1)])` -> `-2^n + 3^n` (expected `3^n-2^n`) 
- **Z3** Fibonacci recurrence, r(1) = 1, r(2) = 2 => fib(n + 1) -- `rsolve(Eq(f(n),f(n-1)+f(n-2)),f(n),[Eq(f(1),1),Eq(f(2),2)])` -> `(1/2 - sqrt(5)/2)^n*(1/2 - sqrt(5)/10) + (1/2 + sqrt(5)/2)^n*(sqrt(5)/10 + 1/2)` (expected `fibonacci(n+1)`) 
- **Z5** f'' + 4 f = sin 2t, f(0) = f'(0) = 0 => sin(2t)/8 - t cos(2t)/4 -- `dsolve_([Eq(Derivative(y(x),x,2)+4*y(x),sin(2*x))],{y(x).subs(x,0):0,y(x).diff(x,1).subs(x,0):0},[y(x)],x)` -> `y=-x*cos(2*x)/4 + sin(2*x)/8` (expected `Y2+4*Y-sp.sin(2*x)`) 
- **Z6** same via the Laplace transform: ilaplace(2/(s^2 + 4)^2) -- `ilaplace(2/(s**2+4)**2,s,t)` -> `2*(-t*cos(2*t)/8 + sin(2*t)/16)*heaviside(t)` (expected `sin(2*t)/8-t*cos(2*t)/4`) 
- **Z7** separable y' = x^2/(y (1 + x)^3) -- `dsolve_([Eq(Derivative(y(x),x),x**2/(y(x)*(1+x)**3))],{},[y(x)],x)` -> `[y=-sqrt(2*c1*x^2 + 4*c1*x + 2*c1 + 2*x^2*ln(x + 1) + 4*x*ln(x + 1) + 4*x + 2*ln(x + 1) + 3)/(x + 1),y=sqrt(2*c1*x^2 + 4*c1*x + 2*c1 + 2*x^2*ln(x + 1) + 4*x*ln(x + 1) + 4*x + 2*ln(x + 1) + 3)/(x + 1)]` (expected `Y1-x**2/(Y*(1+x)**3)`) 
- **Z8** homogeneous y' = y/x + x/y => y^2 = 2 x^2 log|A x| -- `dsolve_([Eq(Derivative(y(x),x),y(x)/x+x/y(x))],{},[y(x)],x)` -> `[y=-x*sqrt(c1 + 2*ln(x)),y=x*sqrt(c1 + 2*ln(x))]` (expected `Y1-(Y/x+x/Y)`) 
- **Z9** linear x^2 y' + 3 x y = sin(x)/x => y = (A - cos x)/x^3 -- `dsolve_([Eq(x**2*Derivative(y(x),x)+3*x*y(x),sin(x)/x)],{},[y(x)],x)` -> `y=(c1 - cos(x))/x^3` (expected `x**2*Y1+3*x*Y-sp.sin(x)/x`) 
- **Z13** y' = a y (parametric) => A e^(a x) -- `dsolve_([Eq(Derivative(y(x),x),a*y(x))],{},[y(x)],x)` -> `y=c1*exp(a*x)` (expected `Y1-a*Y`) 
- **Z15** y'' + y'/x + (1 - v^2/x) y/(4x) = 0 => Z_v(sqrt x) -- `dsolve_([Eq(Derivative(y(x),x,2)+Derivative(y(x),x)/x+(1-v**2/x)*y(x)/(4*x),0)],{},[y(x)],x)` -> `y=c1*besselj(abs(v),sqrt(x)) + c2*bessely(abs(v),sqrt(x))` (expected `Y2+Y1/x+(1-v**2/x)*Y/(4*x)`) 
- **Z18** integro-differential equation via Laplace => 5/13 (-8 e^-4t + e^-t (8 cos 2t + sin 2t)) -- `ilaplace(10*s/((s+4)*(s**2+2*s+5)),s,t)` -> `10*(exp(-t)*sin(2*t)/26 + 4*exp(-t)*cos(2*t)/13)*heaviside(t) - 40*exp(-4*t)*heaviside(t)/13` (expected `5/13*(-8*exp(-4*t)+exp(-t)*(8*cos(2*t)+sin(2*t)))`) 
- **Z19** system u' = u - v, v' = u + v -- `dsolve_([Eq(Derivative(u(t),t),u(t)-v(t)),Eq(Derivative(v(t),t),u(t)+v(t))],{},[u(t),v(t)],t)` -> `[u=-c1*exp(t)*sin(t) - c2*exp(t)*cos(t),v=c1*exp(t)*cos(t) - c2*exp(t)*sin(t)]` (expected `[U.diff(t)-(U-V), V.diff(t)-(U+V)]`) 
- **Z20** triangular system u' = u (1 + cos t/(2 + sin t)), v' = u - v -- `dsolve_([Eq(Derivative(u(t),t),u(t)*(1+cos(t)/(2+sin(t)))),Eq(Derivative(v(t),t),u(t)-v(t))],{},[u(t),v(t)],t)` -> `[u=c1*exp(t)*sin(t) + 2*c1*exp(t),v=2*c1*exp(t)*sin(t)/5 - c1*exp(t)*cos(t)/5 + c1*exp(t) + c2*exp(-t)]` (expected `[U.diff(t)-U*(1+sp.cos(t)/(2+sp.sin(t))), V.diff(t)-(U-V)]`) 
- **Z21** 3x3 system with roots 2, 1, 3 -- `dsolve_([Eq(Derivative(u(t),t),2*u(t)),Eq(Derivative(v(t),t),-2*u(t)+v(t)-2*w(t)),Eq(Derivative(w(t),t),u(t)+3*w(t))],{},[u(t),v(t),w(t)],t)` -> `[u=-c1*exp(2*t),v=c2*exp(t) - c3*exp(3*t),w=c1*exp(2*t) + c3*exp(3*t)]` (expected `[U.diff(t)-2*U, V.diff(t)-(-2*U+V-2*W), W.diff(t)-(U+3*W)]`) 
- **Z22** 3x3 system with roots 0, -1 +- sqrt(2) i -- `dsolve_([Eq(Derivative(u(t),t),v(t)),Eq(Derivative(v(t),t),w(t)),Eq(Derivative(w(t),t),-3*v(t)-2*w(t))],{},[u(t),v(t),w(t)],t)` -> `[u=c1 - (c2/9 + 2*sqrt(2)*c3/9)*exp(-t)*cos(sqrt(2)*t) - (2*sqrt(2)*c2/9 - c3/9)*exp(-t)*sin(sqrt(2)*t),v=-(c2/3 - sqrt(2)*c3/3)*exp(-t)*cos(sqrt(2)*t) + (sqrt(2)*c2/3 + c3/3)*exp(-t)*sin(sqrt(2)*t),w` (expected `[U.diff(t)-V, V.diff(t)-W, W.diff(t)-(-3*V-2*W)]`) 
- **Z23** 3x3 system with the triple root 2 -- `dsolve_([Eq(Derivative(u(t),t),3*u(t)-w(t)),Eq(Derivative(v(t),t),2*v(t)),Eq(Derivative(w(t),t),u(t)+w(t))],{},[u(t),v(t),w(t)],t)` -> `[u=c2*t*exp(2*t) + (c1 + c2)*exp(2*t),v=c3*exp(2*t),w=c1*exp(2*t) + c2*t*exp(2*t)]` (expected `[U.diff(t)-(3*U-W), V.diff(t)-2*V, W.diff(t)-(U+W)]`) 
- **Z24** second order system (Niles) u'' = 2 q v', v'' = -2 q u' + 3 q^2 v -- `dsolve_([Eq(Derivative(u(t),t,2),2*q*Derivative(v(t),t)),Eq(Derivative(v(t),t,2),-2*q*Derivative(u(t),t)+3*q**2*v(t))],{},[u(t),v(t)],t)` -> `[u=2*c1*sin(q*t)/q - 2*c2*cos(q*t)/q + 3*c3*q/2 + 3*c4*q*t/2,v=c1*cos(q*t)/q + c2*sin(q*t)/q + c4]` (expected `[U.diff(t,2)-2*q*V.diff(t), V.diff(t,2)-(-2*q*U.diff(t)+3*q**2*V)]`) 

## WRONG (13)

- **H12** gcd(2 x^(n+4) - x^(n+2), 4 x^(n+1) + 3 x^n) => x^n -- `gcd(2*x**(n+4)-x**(n+2),4*x**(n+1)+3*x**n)` -> `1` (expected `x^n`) 
- **I12** derivative of the 0/0 expression => undefined -- `diff((tan(x)**2+1-sec(x)**2)/(sin(x)**2+cos(x)**2-1),x)` -> `((2*tan(x)^2 + 2)*tan(x) - 2*tan(x)*sec(x)^2)/(sin(x)^2 + cos(x)^2 - 1)` (expected ``) expected an error/divergence
- **M33** (1 + i) z + (2 - i) conj(z) = -3 i => z = 2 + 3 i -- `csolve(Eq((1+i)*z+(2-i)*conj(z),-3*i),z)` -> `[]` (expected `[2+3*i]`) missing 1 solution(s)
- **M37** 3x3 nonlinear system => 16 solutions -- `csolve([Eq(x**2*y+3*y*z-4,0),Eq(-3*x**2*z+2*y**2+1,0),Eq(2*y*z**2-z**2-1,0)],[x,y,z])` -> `[[x=-1,y=1,z=1],[x=1,y=1,z=1],[x=-sqrt(-1 - sqrt(2)*i),y=sqrt(2)*i,z=1/3 - sqrt(2)*i/3],[x=sqrt(-1 - sqrt(2)*i),y=sqrt(2)*i,z=1/3 - sqrt(2)*i/3],[x=-sqrt(-1 + sqrt(2)*i),y=-sqrt(2)*i,z=1/3 + sqrt(2)*i` (expected ``) 6 distinct solutions, expected 16
- **M37b** same, real solutions => (+-1, 1, 1) and (+-0.5332, 0.6172, 2.0655) -- `solve([Eq(x**2*y+3*y*z-4,0),Eq(-3*x**2*z+2*y**2+1,0),Eq(2*y*z**2-z**2-1,0)],[x,y,z])` -> `[[x=-1,y=1,z=1],[x=1,y=1,z=1]]` (expected ``) 2 distinct solutions, expected 4
- **N3** x^4 - x + 1 > 1 for all real x => false -- `x**4-x+1>1` -> `true` (expected `false`) 
- **N15** sin x < 1 => x != pi/2 + 2 n pi -- `solve(sin(x)<1,x)` -> `And(x >= 0,x < 2*pi,Ne(x,pi/2))` (expected `sin(x)<1`) solution sets differ at 142 sample points
- **R19** sum(1/((3k+1)(3k+2)(3k+3)), k = 0..inf) => sqrt(3) pi/12 - log(3)/4 -- `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` -> `exp(-2*i*pi/3)*ln(1 - exp(4*i*pi/3))/3 - exp(2*i*pi/3)*ln(1 - exp(4*i*pi/3))/6 - exp(-2*i*pi/3)*ln(1 - exp(2*i*pi/3))/6 + exp(2*i*pi/3)*ln(1 - exp(2*i*pi/3))/3` (expected `sqrt(3)*pi/12-ln(3)/4`) 
- **U3b** same f: f'(1) => 3 -- `f(x) := piecewise(Eq(x,1),x**2-1,x**3) ; subst(diff(f(x),x),x,1)` -> `2` (expected `3`) 
- **U4** d^n/dx^n x^n => n! (integer n) -- `assume(n,integer) ; diff(x**n,x,n)` -> `x^n*(n*ln(x) + 1)/x` (expected `factorial(n)`) 
- **W1** int 1/(x - a), x = a-1..a+1 => 0 (principal value) -- `integrate(1/(x-a),x,a-1,a+1)` -> `-i*pi` (expected `0`) 
- **W2** int 1/(x - a)^2, x = a-1..a+1 => divergent -- `integrate(1/(x-a)**2,x,a-1,a+1)` -> `-2` (expected ``) expected an error/divergence
- **Z17** discontinuous ODE y'' + sign(t - T) y = 0 -- `dsolve_([Eq(Derivative(y(x),x,2)+sign(x-q)*y(x),0)],{},[y(x)],x)` -> `y=x^2*piecewise(Eq(q,x),0,c1*q/(2*abs(q - x)) - c1*x/(2*abs(q - x))) + x^3*piecewise(Eq(q,x),0,c2*q/(6*abs(q - x)) - c2*x/(6*abs(q - x))) + x^4*piecewise(Eq(q,x),0,q*piecewise(Eq(q,x),0,c1*q/(2*abs(q ` (expected ``) truncated power series in x with |q-x| terms; not a solution

## UNEVAL (100)

- **A2** x or (not x) => true -- `Or(x,Not(x))` -> `Or(x,Not(x))` (expected `true`) no truth value
- **A3** x or y or (x and y) => x or y -- `Or(x,y,(And(x,y)))` -> `Or(x,y,And(x,y))` (expected `x or y`) equivalent but not simplified
- **A12** simplify the QEPCAD output => v > 0 and w > abs(ww) -- `simplify(And(v>0,4*w*v>0,4*w*(4*w**2*v**2+3*ww**2+w**2)>0,64*w**2*v**2*(w**2-ww**2)*(w**2*v**2+ww**2)>0,64*w**2*v**2*(w**2-ww**2)**3*(w**2*v**2+ww**2)>0))` -> `And(v > 0,4*v*w > 0,4*w*(4*v^2*w^2 + w^2 + 3*ww^2) > 0,64*v^2*w^2*(w^2 - ww^2)*(v^2*w^2 + ww^2) > 0,64*v^2*w^2*(w^2 - ww^2)^3*(v^2*w^2 + ww^2) > 0)` (expected `v>0 and w>abs(ww)`) conjunction returned unsimplified
- **B2** intersection => {b} -- `intersect([a,b,b,c,c,c],[d,c,b],[b,e,b])` -> `intersect([e,b],[a,b,c],[b,c,d])` (expected `[b]`) unevaluated intersect
- **C14** denest sqrt(2 sqrt(3) + 4) => 1 + sqrt(3) -- `simplify(sqrt(2*sqrt(3)+4))` -> `sqrt(2*sqrt(3) + 4)` (expected `1+sqrt(3)`) returned unchanged (output = argument of simplify)
- **C15** Putnam denesting => 3 + sqrt(2) -- `simplify(sqrt(14+3*sqrt(3+2*sqrt(5-12*sqrt(3-2*sqrt(2))))))` -> `sqrt(3*sqrt(2*sqrt(5 - 12*sqrt(3 - 2*sqrt(2))) + 3) + 14)` (expected `3+sqrt(2)`) returned unchanged (output = argument of simplify)
- **C16** Jeffrey-Rich denesting => sqrt(2) + sqrt(3) + sqrt(5) -- `simplify(sqrt(10+2*sqrt(6)+2*sqrt(10)+2*sqrt(15)))` -> `sqrt(2)*sqrt(sqrt(6) + sqrt(10) + sqrt(15) + 5)` (expected `sqrt(2)+sqrt(3)+sqrt(5)`) equivalent but not simplified (ops 14 > 8)
- **C17** rationalize (sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)) => 5 + 2 sqrt(6) -- `simplify((sqrt(3)+sqrt(2))/(sqrt(3)-sqrt(2)))` -> `(-sqrt(3) - sqrt(2))/(-sqrt(3) + sqrt(2))` (expected `5+2*sqrt(6)`) equivalent but not simplified (ops 12 > 4)
- **C18** sqrt(-2 + sqrt(-5)) sqrt(-2 - sqrt(-5)) => 3 -- `simplify(sqrt(-2+sqrt(-5))*sqrt(-2-sqrt(-5)))` -> `sqrt(-2 - sqrt(5)*i)*sqrt(-2 + sqrt(5)*i)` (expected `3`) equivalent but not simplified (ops 14 > 0)
- **C19** (90 + 34 sqrt(7))^(1/3) => 3 + sqrt(7) -- `simplify((90+34*sqrt(7))**(1/3))` -> `(34*sqrt(7) + 90)^(1/3)` (expected `3+sqrt(7)`) returned unchanged (output = argument of simplify)
- **C20** nontrivial way of writing 12 -- `simplify(((135+78*sqrt(3))**(2/3)+3)*sqrt(3)/(135+78*sqrt(3))**(1/3))` -> `3^(1/6)*(3 + (135 + 78*sqrt(3))^(2/3))/(45 + 26*sqrt(3))^(1/3)` (expected `12`) equivalent but not simplified (ops 17 > 0)
- **C21** (41 + 29 sqrt(2))^(1/5) => 1 + sqrt(2) -- `simplify((41+29*sqrt(2))**(1/5))` -> `(41 + 29*sqrt(2))^(1/5)` (expected `1+sqrt(2)`) returned unchanged (output = argument of simplify)
- **C22** nasty Axiom example => (log(sqrt(2) + 1) + sqrt(2))/3 -- `simplify(((6-4*sqrt(2))*ln(3-2*sqrt(2))+(3-2*sqrt(2))*ln(17-12*sqrt(2))+32-24*sqrt(2))/(48*sqrt(2)-72))` -> `-ln(3 - 2*sqrt(2))/12 - ln(17 - 12*sqrt(2))/24 + sqrt(2)/3` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) equivalent but not simplified (ops 18 > 8)
- **F2** binomial(n, 3) => n (n-1) (n-2)/6 -- `binomial(n,3)` -> `binomial(n,3)` (expected `n*(n-1)*(n-2)/6`) returned unevaluated (output = input)
- **F3** 2^n n! (2n-1)!! => (2n)! -- `simplify(2**n*n!*factorial2(2*n-1))` -> `2^n*factorial(n)*factorial2(2*n - 1)` (expected `factorial(2*n)`) returned unchanged (output = argument of simplify)
- **F4** 2^n n! product(2k-1, k=1..n) => (2n)! -- `simplify(2**n*n!*product(2*k-1,k,1,n))` -> `gamma(2*n + 1)` (expected `factorial(2*n)`) equivalent but not simplified (ops 3 > 2)
- **F5** gamma(n+1/2)/(sqrt(pi) n!) => (2n)!/(2^(2n) (n!)^2) -- `simplify(gamma(n+1/2)/(sqrt(pi)*n!))` -> `gamma(n + 1/2)/(sqrt(pi)*gamma(n + 1))` (expected `factorial(2*n)/(4^n*factorial(n)^2)`) equivalent but not simplified (ops 9 > 7)
- **G12** Diophantine x^4 + 9 = y^2 => (2, 5) -- `diophantine(x**4+9-y**2)` -> `NotImplementedError: 
This equation is not yet recognized or else has not been simplified
sufficiently to put it in a form recognized by diop_classify().` (expected ``) NotImplementedError: 
This equation is not yet recognized or else has not been simplified
sufficiently to put it in a form recognized by diop_classify().
- **G13** Diophantine x^2 + 4 = y^3 => (11, 5) -- `diophantine(x**2+4-y**3)` -> `NotImplementedError: No solver has been written for cubic_thue.` (expected ``) NotImplementedError: No solver has been written for cubic_thue.
- **H15** (e^x - 1)/(e^(x/2) + 1) => e^(x/2) - 1 -- `simplify((exp(x)-1)/(exp(x/2)+1))` -> `(exp(x) - 1)/(exp(x/2) + 1)` (expected `exp(x/2)-1`) returned unchanged (output = argument of simplify)
- **H17** factor x^3 + x^2 - 7 via its roots, multiply them back together -- `R := csolve(Eq(x**3+x**2-7,0),x) ; u(r) := x-r ; expand(product(map(R,u)))` -> `x^3 - x^2/(9*(sqrt(3885)/18 + 187/54)^(1/3)) + x^2 - x^2/(-9*(sqrt(3885)/18 + 187/54)^(1/3)/2 - 9*sqrt(3)*i*(sqrt(3885)/18 + 187/54)^(1/3)/2) - x^2/(-9*(sqrt(3885)/18 + 187/54)^(1/3)/2 + 9*sqrt(3)*i*(` (expected `x^3+x^2-7`) equivalent but not simplified (ops 785 > 4)
- **H21** (x^3 + (sqrt2-2)x^2 - (2sqrt2+3)x - 3sqrt2)/(x^2-2) => (x^2-2x-3)/(x-sqrt2) -- `simplify((x**3+(sqrt(2)-2)*x**2-(2*sqrt(2)+3)*x-3*sqrt(2))/(x**2-2))` -> `(x^3 + x^2*(-2 + sqrt(2)) - x*(2*sqrt(2) + 3) - 3*sqrt(2))/(x^2 - 2)` (expected `(x^2-2*x-3)/(x-sqrt(2))`) equivalent but not simplified (ops 20 > 8)
- **H30** factor 4x^2 - 21xy + 20y^2 mod 3 => (x + y)(x - y) -- `factor(4*x**2-21*x*y+20*y**2,modulus=3)` -> `NotImplementedError: multivariate polynomials over finite fields` (expected `(x+y)*(x-y)`) NotImplementedError: multivariate polynomials over finite fields
- **I2** sqrt((1 + cos 6)/2) => -cos 3 -- `simplify(sqrt((1+cos(6))/2))` -> `sqrt(2*cos(6) + 2)/2` (expected `-cos(3)`) equivalent but not simplified (ops 6 > 2)
- **I4** cos(cos(n pi) pi) + sin(cos(n pi) pi/2) => (-1)^n - 1 -- `assume(n,integer) ; cos(cos(n*pi)*pi)+sin(cos(n*pi)*pi/2)` -> `(-1)^((-1)^n/2 - 1/2) - 1` (expected `(-1)^n-1`) equivalent but not simplified (ops 8 > 3)
- **I5** sin((n^5/5 + n^4/2 + n^3/3 - n/30) pi) => 0 for integer n -- `assume(n,integer) ; sin((n**5/5+n**4/2+n**3/3-n/30)*pi)` -> `sin(pi*(n^5/5 + n^4/2 + n^3/3 - n/30))` (expected `0`) equivalent but not simplified (ops 12 > 0)
- **I6** |cos x|, |sin x| for -3 pi < x < -5/2 pi => -cos x, -sin x -- `assume(And(x>-3*pi,x<-5*pi/2)) ; [abs(cos(x)),abs(sin(x))]` -> `[abs(cos(x)),abs(sin(x))]` (expected `[-cos(x),-sin(x)]`) returned unevaluated (output = input)
- **I8b** simplify(cos(3x)/cos(x)) => 2 cos(2x) - 1 or similar -- `simplify(cos(3*x)/cos(x))` -> `cos(3*x)/cos(x)` (expected `2*cos(2*x)-1`) returned unchanged (output = argument of simplify)
- **J7** J_{-5/2}(pi/2) => 12/pi^2 -- `besselj(-5/2,pi/2)` -> `besselj(-5/2,pi/2)` (expected `12/pi^2`) returned unevaluated (output = input)
- **J8** J_{3/2}(z) => sqrt(2/(pi z)) (sin z/z - cos z) -- `besselj(3/2,z)` -> `besselj(3/2,z)` (expected `sqrt(2/(pi*z))*(sin(z)/z-cos(z))`) returned unevaluated (output = input)
- **J16** zeta'(0) => -log(2 pi)/2 -- `subst(diff(zeta(x),x),x,0)` -> `subst(diff(zeta(x),x),x,0)` (expected `-ln(2*pi)/2`) unevaluated diff,subst
- **K3** |1/(a + i/a + i b)| for real a, b => 1/sqrt(a^2 + (1/a + b)^2) -- `abs(1/(a+i/a+i*b))` -> `sqrt(-1/(-a^2 - b^2 - 2*b/a - 1/a^2))` (expected `1/sqrt(a^2+(1/a+b)^2)`) equivalent but not simplified (ops 14 > 8)
- **K21** atan(tan(z)) => z for -pi/2 < z < pi/2 -- `assume(And(z>-pi/2,z<pi/2)) ; simplify(atan(tan(z)))` -> `atan(tan(z))` (expected `z`) returned unchanged (output = argument of simplify)
- **K23** atan(tan(11 + 30 i)) => 11 - 4 pi + 30 i -- `atan(tan(11+30*i))` -> `atan(tan(11 + 30*i))` (expected `11-4*pi+30*i`) returned unevaluated (output = input)
- **K24** Kahan: w + sqrt(w+1) sqrt(w-1), w = (z + 1/z)/2 => z (z > 1) -- `simplify((z+1/z)/2+sqrt((z+1/z)/2+1)*sqrt((z+1/z)/2-1))` -> `(z^2 + z*sqrt(z - 2 + 1/z)*sqrt(z + 2 + 1/z) + 1)/(2*z)` (expected `z`) equivalent but not simplified (ops 17 > 0)
- **L5** log(tan(x/2 + pi/4)) - asinh(tan x) => 0 for -pi/2 < x < pi/2 -- `simplify(ln(tan(x/2+pi/4))-asinh(tan(x)))` -> `ln(tan(x/2 + pi/4)) - asinh(tan(x))` (expected `0`) returned unchanged (output = argument of simplify)
- **L6** d/dx of the L5 expression => 0 -- `simplify(diff(ln(tan(x/2+pi/4))-asinh(tan(x)),x))` -> `-1/abs(cos(x)) + 1/(2*sin(x/2 + pi/4)*cos(x/2 + pi/4))` (expected `0`) equivalent but not in the requested form
- **L7** log((2 sqrt(r) + 1)/sqrt(4r + 4 sqrt(r) + 1)) => 0 -- `simplify(ln((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1)))` -> `ln((2*sqrt(r) + 1)/sqrt(4*sqrt(r) + 4*r + 1))` (expected `0`) returned unchanged (output = argument of simplify)
- **L8** (4r + 4 sqrt r + 1)^(sqrt r/(2 sqrt r + 1)) (2 sqrt r + 1)^(1/(2 sqrt r + 1)) - 2 sqrt r - 1 => 0 -- `simplify((4*r+4*sqrt(r)+1)**(sqrt(r)/(2*sqrt(r)+1))*(2*sqrt(r)+1)**(1/(2*sqrt(r)+1))-2*sqrt(r)-1)` -> `-2*sqrt(r) + (2*sqrt(r) + 1)^(1/(2*sqrt(r) + 1))*(4*sqrt(r) + 4*r + 1)^(sqrt(r)/(2*sqrt(r) + 1)) - 1` (expected `0`) equivalent but not in the requested form
- **L9** 2^(1-z) gamma(z) zeta(z) cos(z pi/2) - pi^z zeta(1-z) => 0 -- `simplify(2**(1-z)*gamma(z)*zeta(z)*cos(z*pi/2)-pi**z*zeta(1-z))` -> `(-z*(2*pi)^z*zeta(1 - z) + 2*cos(pi*z/2)*gamma(z + 1)*zeta(z))/(2^z*z)` (expected `0`) equivalent but not in the requested form
- **M4** x^6 - 9x^4 - 4x^3 + 27x^2 - 36x - 23 = 0 (Rayes) -- `csolve(Eq(x**6-9*x**4-4*x**3+27*x**2-36*x-23,0),x)` -> `[rootof(x^6 - 9*x^4 - 4*x^3 + 27*x^2 - 36*x - 23,0),rootof(x^6 - 9*x^4 - 4*x^3 + 27*x^2 - 36*x - 23,1),rootof(x^6 - 9*x^4 - 4*x^3 + 27*x^2 - 36*x - 23,2),rootof(x^6 - 9*x^4 - 4*x^3 + 27*x^2 - 36*x - 2` (expected `[2^(1/3)+sqrt(3),2^(1/3)-sqrt(3),sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3)]`) unevaluated rootof
- **M16** asin x = atan x => 0 -- `solve(Eq(asin(x),atan(x)),x)` -> `NotImplementedError: multiple generators [asin(x), atan(x)]
No algorithms are implemented to solve equation asin(x) - atan(x)` (expected `[0]`) NotImplementedError: multiple generators [asin(x), atan(x)]
No algorithms are implemented to solve equation asin(x) - atan(x)
- **M17** acos x = atan x => sqrt((sqrt(5) - 1)/2) -- `solve(Eq(acos(x),atan(x)),x)` -> `NotImplementedError: multiple generators [acos(x), atan(x)]
No algorithms are implemented to solve equation acos(x) - atan(x)` (expected `[sqrt((sqrt(5)-1)/2)]`) NotImplementedError: multiple generators [acos(x), atan(x)]
No algorithms are implemented to solve equation acos(x) - atan(x)
- **M27** 5x + exp((x - 5)/2) = 8x^3 => {-0.784966, -0.016291, 0.802557, 29.454528} -- `solve(Eq(5*x+exp((x-5)/2),8*x**3),x)` -> `NotImplementedError: multiple generators [x, exp(x/2)]
No algorithms are implemented to solve equation -8*x**3 + 5*x + exp(x/2 - 5/2)` (expected `[-0.78496614648634752,-0.016290737729935251,0.8025567019143254,29.454527572105314]`) NotImplementedError: multiple generators [x, exp(x/2)]
No algorithms are implemented to solve equation -8*x**3 + 5*x + exp(x/2 - 5/2)
- **M30** 1 - |x| = max(-x - 2, x - 2) => {-3/2, 3/2} -- `solve(Eq(1-abs(x),max(-x-2,x-2)),x)` -> `NotImplementedError: multiple generators [x, Max(-x - 2, x - 2)]
No algorithms are implemented to solve equation x - Max(-x - 2, x - 2) + 1` (expected `[-3/2,3/2]`) NotImplementedError: multiple generators [x, Max(-x - 2, x - 2)]
No algorithms are implemented to solve equation x - Max(-x - 2, x - 2) + 1
- **M31** max(2 - x^2, x) = max(-x, x^3/9) => {-1, 3} -- `solve(Eq(max(2-x**2,x),max(-x,x**3/9)),x)` -> `NotImplementedError: multiple generators [Max(-x, x**3/9), Max(x, 2 - x**2)]
No algorithms are implemented to solve equation -Max(-x, x**3/9) + Max(x, 2 - x**2)` (expected `[-1,3]`) NotImplementedError: multiple generators [Max(-x, x**3/9), Max(x, 2 - x**2)]
No algorithms are implemented to solve equation -Max(-x, x**3/9) + Max(x, 2 - x**2)
- **M32** max(2 - x^2, x) = x^3/9 => {-3, 3, -1.554894} -- `solve(Eq(max(2-x**2,x),x**3/9),x)` -> `NotImplementedError: multiple generators [x, Max(x, 2 - x**2)]
No algorithms are implemented to solve equation -x**3/9 + Max(x, 2 - x**2)` (expected `[-3,3,-3*(1+sqrt(3)*sin((atan(sqrt(5)/2)-pi)/3)+cos((atan(sqrt(5)/2)-pi)/3))]`) NotImplementedError: multiple generators [x, Max(x, 2 - x**2)]
No algorithms are implemented to solve equation -x**3/9 + Max(x, 2 - x**2)
- **N4** |x| < 1 => -1 < x < 1 (true) -- `assume(abs(x)<1) ; And(-1<x,x<1)` -> `And(-1 < x,x < 1)` (expected `true`) no truth value
- **N5** x > y > 0 => 2 x^2 > 2 y^2 -- `assume(x>y) ; assume(y>0) ; 2*x**2>2*y**2` -> `2*x^2 > 2*y^2` (expected `true`) returned unevaluated (output = input)
- **N6** x > y > 0, k > 0 => k x^2 > k y^2 -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; k*x**2>k*y**2` -> `k*x^2 > k*y^2` (expected `true`) returned unevaluated (output = input)
- **N7** x > y > 0, k, n > 0 => k x^n > k y^n -- `assume(x>y) ; assume(y>0) ; assume(k>0) ; assume(n>0) ; k*x**n>k*y**n` -> `k*x^n > k*y^n` (expected `true`) returned unevaluated (output = input)
- **N16** |2 A (cos t - 1) + 1| <= 1 => 0 <= A <= 1/(1 - cos t) -- `solve(abs(2*a*(cos(t)-1)+1)<=1,a)` -> `NotImplementedError: 
The inequality, Abs(2*x*(cos(t) - 1) + 1) <= 1, cannot be solved using
solve_univariate_inequality.` (expected `0<=a and a<=1/(1-cos(t))`) NotImplementedError: 
The inequality, Abs(2*x*(cos(t) - 1) + 1) <= 1, cannot be solved using
solve_univariate_inequality.
- **N17** A^2 (cos t - 4)^2 sin^2 t < 9 => |A| < 3/|(cos t - 4) sin t| -- `solve(a**2*(cos(t)-4)**2*sin(t)**2<9,a)` -> `NotImplementedError: 
The inequality, x**2*(cos(t) - 4)**2*sin(t)**2 < 9, cannot be solved
using solve_univariate_inequality.` (expected `abs(a)<3/abs((cos(t)-4)*sin(t))`) NotImplementedError: 
The inequality, x**2*(cos(t) - 4)**2*sin(t)**2 < 9, cannot be solved
using solve_univariate_inequality.
- **N18** x + y > 0 and x - y < 0 => |x| < y -- `solve([x+y>0,x-y<0],[x,y])` -> `NotImplementedError: 
inequality has more than one symbol of interest.` (expected `abs(x)<y`) NotImplementedError: 
inequality has more than one symbol of interest.
- **P9** Frobenius norm => (a^2 + b^2 + c^2)/(|a| |b| |c|) -- `factor(norm(Matrix([[a/(b*c),1/c,1/b],[1/c,b/(a*c),1/a],[1/b,1/a,c/(a*b)]])))` -> `sqrt(1/(a^2*b^2*c^2))*sqrt(a^2*b^2*c^2*abs(c/(a*b))^2 + a^2*b^2*c^2*abs(b/(a*c))^2 + a^2*b^2*c^2*abs(a/(b*c))^2 + 2*a^2*b^2 + 2*a^2*c^2 + 2*b^2*c^2)` (expected `(a^2+b^2+c^2)/(abs(a)*abs(b)*abs(c))`) equivalent but not simplified (ops 61 > 11)
- **R3** sum((-1)^k binomial(2n, k)^2, k = 0..2n) => (-1)^n binomial(2n, n) -- `assume(n,integer) ; sum((-1)**k*binomial(2*n,k)**2,k,0,2*n)` -> `piecewise(Or(n > -1/4,And(n <= -1/4,n > -1/2)),gamma(1 - n)/(gamma(1 - 2*n)*gamma(n + 1)),sum((-1)^k*binomial(2*n,k)^2,k,0,2*n))` (expected `(-1)^n*binomial(2*n,n)`) conditional closed form gamma(1-n)/(gamma(1-2n) gamma(n+1)) is 0/0 (gamma poles) at integer n
- **R5** Dixon's identity => (a+b+c)!/(a! b! c!) -- `sum((-1)**k*binomial(a+b,a+k)*binomial(b+c,b+k)*binomial(c+a,c+k),k,-a,a)` -> `piecewise(2*a + 2*b + 2*c > -2,gamma(b + c + 1)*hypergeom([-2*a,-a - b,-a - c],[-a + b + 1,-a + c + 1],1)/((-1)^a*gamma(2*a + 1)*gamma(-a + b + 1)*gamma(-a + c + 1)),sum((-1)^k*binomial(a + b,a + k)*b` (expected `factorial(a+b+c)/(factorial(a)*factorial(b)*factorial(c))`) unevaluated hypergeom
- **R8** sum(k^2 binomial(n, k), k = 1..n) => n (n+1) 2^(n-2) -- `sum(k**2*binomial(n,k),k,1,n)` -> `piecewise(Or(n > 2,And(n <= 2,n > 1)),-2^(n - 1)*(-n - 1)*gamma(n + 1)/(2*gamma(n)),sum(k^2*binomial(n,k),k,1,n))` (expected `n*(n+1)*2^(n-2)`) unevaluated sum
- **R11** sum(binomial(n, k) fib(k), k = 0..n) => fib(2n) -- `sum(binomial(n,k)*fibonacci(k),k,0,n)` -> `sum(binomial(n,k)*fibonacci(k),k,0,n)` (expected `fibonacci(2*n)`) unevaluated sum
- **R12** sum(fib(k)^2, k = 1..n) => fib(n) fib(n+1) -- `sum(fibonacci(k)**2,k,1,n)` -> `sum(fibonacci(k)^2,k,1,n)` (expected `fibonacci(n)*fibonacci(n+1)`) unevaluated sum
- **R13** sum(sin(k x), k = 1..n) => cot(x/2)/2 - cos((2n+1) x/2)/(2 sin(x/2)) -- `sum(sin(k*x),k,1,n)` -> `sum(sin(k*x),k,1,n)` (expected `cot(x/2)/2-cos((2*n+1)*x/2)/(2*sin(x/2))`) unevaluated sum
- **R14** sum(sin((2k - 1) x), k = 1..n) => sin(n x)^2/sin x -- `sum(sin((2*k-1)*x),k,1,n)` -> `sum(sin(x*(2*k - 1)),k,1,n)` (expected `sin(n*x)^2/sin(x)`) unevaluated sum
- **R15** sum(binomial(n - k, k), k = 0..floor(n/2)) => fib(n + 1) -- `sum(binomial(n-k,k),k,0,floor(n/2))` -> `sum(binomial(-k + n,k),k,0,floor(n/2))` (expected `fibonacci(n+1)`) unevaluated sum
- **R20** sum(binomial(n, 4k), k = 0..inf) => (2^(n-1) + 2^(n/2) cos(n pi/4))/2 -- `sum(binomial(n,4*k),k,0,inf)` -> `piecewise(n > 0,hypergeom([-n/4,1/4 - n/4,1/2 - n/4,3/4 - n/4],[1/4,1/2,3/4],1),sum(binomial(n,4*k),k,0,inf))` (expected `(2^(n-1)+2^(n/2)*cos(n*pi/4))/2`) unevaluated hypergeom
- **R21** sum(1/(sqrt(k (k+1)) (sqrt k + sqrt(k+1))), k = 1..inf) => 1 -- `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` -> `sum(1/(sqrt(k)*(sqrt(k) + sqrt(k + 1))*sqrt(k + 1)),k,1,inf)` (expected `1`) unevaluated sum
- **R22** double sum (capping) => 1/sqrt((1 - x y)^2 - 4 x^2) -- `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x**n*y**(n-2*k),k,0,floor(n/2)),n,0,inf)` -> `sum(piecewise(y^(-2) <= 1/4,x^n*(y^n*hypergeom([-n/2,1/2 - n/2],[1],4/y^2) - y^(n - 2*floor(n/2) - 2)*gamma(n + 1)*hypergeom([1,-n/2 + floor(n/2) + 1,-n/2 + floor(n/2) + 3/2],[floor(n/2) + 2,floor(n/2` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated hypergeom,sum
- **R23** equivalent double sum -- `sum(sum(n!/(k!**2*(n-2*k)!)*(x/y)**k*(x*y)**(n-k),n,2*k,inf),k,0,inf)` -> `sum(piecewise(abs(x*y) < 1,(x/y)^k*(x*y)^k*(-x*y + 1)^(-2*k - 1)*gamma(2*k + 1)/factorial(k)^2,(x/y)^k*sum((x*y)^(-k + n)*factorial(n)/factorial(-2*k + n),n,2*k,inf)/factorial(k)^2),k,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **S1** product(gamma(k/3), k = 1..8) => 640 pi^3/(2187 sqrt 3) -- `product(gamma(k/3),k,1,8)` -> `gamma(1/3)*gamma(2/3)*gamma(4/3)*gamma(5/3)*gamma(7/3)*gamma(8/3)` (expected `640*pi^3/(2187*sqrt(3))`) equivalent but not simplified (ops 17 > 6)
- **S6** product(x^2 - 2x cos(k pi/n) + 1, k = 1..n-1) => (x^(2n) - 1)/(x^2 - 1) -- `product(x**2-2*x*cos(k*pi/n)+1,k,1,n-1)` -> `product(x^2 - 2*x*cos(pi*k/n) + 1,k,1,n - 1)` (expected `(x^(2*n)-1)/(x^2-1)`) unevaluated product
- **S7** product((k^3 - 1)/(k^3 + 1), k = 2..inf) => 2/3 -- `product((k**3-1)/(k**3+1),k,2,inf)` -> `product((k^3 - 1)/(k^3 + 1),k,2,inf)` (expected `2/3`) unevaluated product
- **S8** product(1 - 1/(2k)^2, k = 1..inf) => 2/pi -- `product(1-1/(2*k)**2,k,1,inf)` -> `product(1 - 1/(4*k^2),k,1,inf)` (expected `2/pi`) unevaluated product
- **S9** product(1 + (-1)^(k+1)/(2k - 1), k = 1..inf) => sqrt(2) -- `product(1+(-1)**(k+1)/(2*k-1),k,1,inf)` -> `product((-1)^(k + 1)/(2*k - 1) + 1,k,1,inf)` (expected `sqrt(2)`) unevaluated product
- **S10** product((k (k+1) + 1 + i)/(k (k+1) + 1 - i), k = 0..inf) => -1 -- `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` -> `product((k*(k + 1) + 1 + i)/(k*(k + 1) + 1 - i),k,0,inf)` (expected `-1`) unevaluated product
- **T12** limit(zeta(x) - 1/(x - 1), x, 1) => Euler's gamma -- `limit(zeta(x)-1/(x-1),x,1)` -> `limit(zeta(x) - 1/(x - 1),x,1)` (expected `euler_gamma`) unevaluated limit
- **T13** limit(n^x/(x product(1 + x/k, k = 1..n)), n, inf) => gamma(x) -- `limit(n**x/(x*product(1+x/k,k,1,n)),n,inf)` -> `NotImplementedError: Result depends on the sign of -sign(x)` (expected `gamma(x)`) NotImplementedError: Result depends on the sign of -sign(x)
- **V15** int log|x^2 - a^2| dx -- `integrate(ln(abs(x**2-a**2)),x)` -> `integrate(piecewise(a^2 - x^2 <= 0,ln(-a^2 + x^2),ln(a^2 - x^2)),x)` (expected `ln(abs(x^2-a^2))`) unevaluated integrate
- **V17** int cos(5x) Ci(2x) dx => sin(5x) Ci(2x)/5 - (Si(7x) + Si(3x))/10 -- `integrate(cos(5*x)*Ci(2*x),x)` -> `integrate(cos(5*x)*Ci(2*x),x)` (expected `cos(5*x)*Ci(2*x)`) unevaluated integrate
- **V18** int (f' g - f g')/(f^2 - g^2) dx => log((f - g)/(f + g))/2 -- `integrate((diff(f(x),x)*g(x)-f(x)*diff(g(x),x))/(f(x)**2-g(x)**2),x)` -> `integrate((-f(x)*diff(g(x),x) + g(x)*diff(f(x),x))/((f(x) - g(x))*(f(x) + g(x))),x)` (expected ``) unevaluated integrate
- **W3** int sqrt(x + 1/x - 2), x = 0..1 => 4/3 -- `integrate(sqrt(x+1/x-2),x,0,1)` -> `integrate(abs(x - 1)/sqrt(x),x,0,1)` (expected `4/3`) unevaluated integrate
- **W4** int sqrt(x + 1/x - 2), x = 1..2 => (4 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,1,2)` -> `integrate(abs(x - 1)/sqrt(x),x,1,2)` (expected `(4-sqrt(8))/3`) unevaluated integrate
- **W5** int sqrt(x + 1/x - 2), x = 0..2 => (8 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,0,2)` -> `integrate(abs(x - 1)/sqrt(x),x,0,2)` (expected `(8-sqrt(8))/3`) unevaluated integrate
- **W6** int sqrt(2 - 2 cos 2x)/2, x = -3 pi/4..-pi/4 => sqrt 2 (Kahan) -- `integrate(sqrt(2-2*cos(2*x))/2,x,-3*pi/4,-pi/4)` -> `sqrt(2)*integrate(sqrt(1 - cos(2*x)),x,-3*pi/4,-pi/4)/2` (expected `sqrt(2)`) unevaluated integrate
- **W9** PV int 5x^3/(1 + x + x^2 + x^3 + x^4), -inf..inf => -2 pi (sin(pi/5) + sin(2 pi/5)) -- `integrate(5*x**3/(1+x+x**2+x**3+x**4),x,-inf,inf)` -> `undef` (expected `-2*pi*(sin(pi/5)+sin(2*pi/5))`) improper integral reported as undefined; no principal value (same classification as our CAS)
- **W10** int 1/(1 + x + x^2 + x^4), -inf..inf -- `integrate(1/(1+x+x**2+x**4),x,-inf,inf)` -> `integrate(1/(x^4 + x^2 + x + 1),x,-inf,inf)` (expected `1.98667745715246`) unevaluated integrate
- **W11** int sqrt(1 - x^2)/(1 + x^2), x = -1..1 => pi (sqrt 2 - 1) -- `integrate(sqrt(1-x**2)/(1+x**2),x,-1,1)` -> `integrate(sqrt(-(x - 1)*(x + 1))/(x^2 + 1),x,-1,1)` (expected `pi*(sqrt(2)-1)`) unevaluated integrate
- **W13** int 1/log t + 1/(1 - t) - log(log(1/t)), t = 0..1 => 2 gamma -- `integrate(1/ln(t)+1/(1-t)-ln(ln(1/t)),t,0,1)` -> `integrate((2*t - ln(t) - 2)/((t - 1)*ln(t)),t,0,1) + inf` (expected `2*euler_gamma`) unevaluated integrate
- **W15** int log(gamma(x)) cos(6 pi x), x = 0..1 => 1/12 -- `integrate(ln(gamma(x))*cos(6*pi*x),x,0,1)` -> `integrate(ln(gamma(x))*cos(6*pi*x),x,0,1)` (expected `1/12`) unevaluated integrate
- **W19** int Ci(x) J0(2 sqrt(7 x)), x = 0..inf => (cos 7 - 1)/7 -- `integrate(Ci(x)*besselj(0,2*sqrt(7*x)),x,0,inf)` -> `integrate(Ci(x)*besselj(0,2*sqrt(7)*sqrt(x)),x,0,inf)` (expected `(cos(7)-1)/7`) unevaluated integrate
- **W20** int x^2 Li3(1/(x + 1)), x = 0..1 (Mertig) -- `integrate(x**2*polylog(3,1/(x+1)),x,0,1)` -> `integrate(x^2*polylog(3,1/(x + 1)),x,0,1)` (expected `-(17/3+pi^2)/36+ln(2)/9*(35/3-pi^2/2-4*ln(2)+ln(2)^2)+zeta(3)/4`) unevaluated integrate
- **W26** y first, a < 0 < b => (|b| - |a|) pi -- `assume(a<0) ; assume(b>0) ; integrate(integrate(x/(x**2+y**2),y,-inf,inf),x,a,b)` -> `integrate(piecewise(And(Or(arg(x) >= 0,arg(x) > -pi/2),Or(arg(x) < 0,arg(x) < pi/2)),pi,integrate(x/(x^2 + y^2),y,-inf,inf)),x,a,b)` (expected `pi*(b+a)`) unevaluated integrate
- **W30** int(int(sqrt(x^2 + y^2), x = 0..1), y = 0..1) => (log(sqrt 2 + 1) + sqrt 2)/3 -- `integrate(integrate(sqrt(x**2+y**2),x,0,1),y,0,1)` -> `integrate(y^2*asinh(1/y),y,0,1)/2 + integrate(sqrt(y^2 + 1),y,0,1)/2` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) unevaluated integrate
- **X7** series of f'(a x) a + g(b x) + int(h(c y), y = 0..x) at x = d -- `series(diff(f(a*x),x)+g(b*x)+integrate(h(c*y),y,0,x),x,d,2)` -> `NotImplementedError: Don't know how to calculate the mrv of 'Integral(h(c*y), (y, 0, d + 1/_x))'` (expected ``) NotImplementedError: Don't know how to calculate the mrv of 'Integral(h(c*y), (y, 0, d + 1/_x))'
- **X16** Wallis: binomial(2n, n)/2^(2n) => 1/sqrt(pi n) -- `series(binomial(2*n,n)/4**n,n,inf,1)` -> `NotImplementedError: MRV set computation for functions in several variables not implemented.` (expected `1/sqrt(pi*n)`) NotImplementedError: MRV set computation for functions in several variables not implemented.
- **X17** e^x int(e^-t/t, t = x..inf) => 1/x - 1/x^2 + 2/x^3 - 6/x^4 -- `series(exp(x)*integrate(exp(-t)/t,t,x,inf),x,inf,5)` -> `NotImplementedError: Result depends on the sign of -sign(exp_polar(I*pi) + 1)` (expected `1/x-1/x^2+2/x^3-6/x^4`) NotImplementedError: Result depends on the sign of -sign(exp_polar(I*pi) + 1)
- **Y7** Laplace transform of a square wave (infinite sum of steps) -- `laplace(1+2*sum((-1)**n*heaviside(t-n*a),n,1,inf),t,s)` -> `2*LaplaceTransform(sum((-1)^n*heaviside(-a*n + t),n,1,inf),t,s) + 1/s` (expected `0`) unevaluated LaplaceTransform,sum
- **Y8** Fourier transform of 1 => sqrt(2 pi) delta(z) -- `fourier(1,x,z)` -> `FourierTransform(1,x,z)` (expected `sqrt(2*pi)*dirac(z)`) unevaluated FourierTransform
- **Y11** Mellin transform of 1/(1 - x) => pi cot(pi s) (PV) -- `assume(s>0) ; integrate(x**(s-1)/(1-x),x,0,inf)` -> `-integrate(x^(s - 1)/(x - 1),x,0,inf)` (expected `pi*cot(pi*s)`) unevaluated integrate
- **Z11** nonlinear y'' + y y'^3 = 0 => y^3/6 + A y = x + B -- `dsolve_([Eq(Derivative(y(x),x,2)+y(x)*Derivative(y(x),x)**3,0)],{},[y(x)],x)` -> `NotImplementedError: The given ODE (-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 - sqrt(3)*I*(-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 + Derivative(y(x), x) cannot be solved by the factorable group method` (expected `Y2+Y*Y1**3`) NotImplementedError: The given ODE (-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 - sqrt(3)*I*(-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 + Derivative(y(x), x) cannot be solved by the factorable group method
- **Z12** same with y(0) = 0, y'(0) = 2 => (3x + sqrt(1 + 9x^2))^(1/3) - 1/(3x + sqrt(1 + 9x^2))^(1/3) -- `dsolve_([Eq(Derivative(y(x),x,2)+y(x)*Derivative(y(x),x)**3,0)],{y(x).subs(x,0):0,y(x).diff(x,1).subs(x,0):2},[y(x)],x)` -> `NotImplementedError: The given ODE (-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 - sqrt(3)*I*(-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 + Derivative(y(x), x) cannot be solved by the factorable group method` (expected `Y2+Y*Y1**3`) NotImplementedError: The given ODE (-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 - sqrt(3)*I*(-Derivative(y(x), (x, 2))/y(x))**(1/3)/2 + Derivative(y(x), x) cannot be solved by the factorable group method
- **Z14** BVP y'' + k^2 y = 0, y(0) = 0, y'(1) = 0 => A sin(k x) with cos k = 0 -- `dsolve_([Eq(Derivative(y(x),x,2)+k**2*y(x),0)],{y(x).subs(x,0):0,y(x).diff(x,1).subs(x,1):0},[y(x)],x)` -> `y=0` (expected ``) only the trivial solution; no eigenvalue condition cos(k) = 0

## UNSUPPORTED (53)

- **A5** (w and x) implies (y and z) => not(w and x) or (y and z) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical implication operator)
- **A6** x iff y => (x and y) or not(x or y) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no logical equivalence operator)
- **D5** natural cubic spline through (1,1),(2,4),(4,2),(5,3) at 3 => 27/8 -- `` -> `` (expected ``) no natural cubic spline; interpolating_spline(3,...) uses other end conditions (10/3 at x = 3, not 27/8)
- **D12** interval arithmetic ([-4,2] x + [1,3])^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no interval arithmetic)
- **E4** mode([3, 7, 11, 7, 3, 5, 7]) => 7 -- `mode([3,7,11,7,3,5,7])` -> `` (expected `7`) no SymPy counterpart for mode
- **E5** unbiased sample standard deviation of [1..5] => sqrt(5/2) -- `stddev([1,2,3,4,5])` -> `` (expected `sqrt(5/2)`) sympy.stats has only the population standard deviation of a finite distribution, no sample (n-1) one
- **E12** linear regression => y = 0.7365 x + 6.964 -- `linreg([3.33,3.25,3.92,3.50,4.33,4.92,6.08,7.42,8.33,8.00,9.25,10.75],[8.61,9.40,9.86,9.91,10.53,10.61,10.59,13.28,12.76,13.44,14.27,14.13])` -> `` (expected `0.7365*x+6.964`) no SymPy counterpart for linreg
- **E13** multiple linear regression => -16278.7 + 960.925 x1 + 2975.66 x2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no multiple regression in the CAS)
- **E14** L1 (least absolute deviations) regression -- `` -> `` (expected ``) not attempted: no input in problems.txt (no L1 regression)
- **E15** nonlinear least squares fit -- `` -> `` (expected ``) not attempted: no input in problems.txt (no nonlinear regression in the CAS)
- **G3** (a + b)^p mod p => a^p + b^p for p prime -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular polynomial arithmetic with symbolic modulus)
- **G9** linear system mod 7 => x = 1, y = 2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G10** 2x + 3y = 1 mod 5 => 5 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G11** 2x + 3y = 1 mod 6 => 6 solutions -- `` -> `` (expected ``) not attempted: no input in problems.txt (no modular equation solving)
- **G14** Pythagorean system => (3,4,5,12,13), ... -- `` -> `` (expected ``) not attempted: no input in problems.txt (no Diophantine solver)
- **G15** rational approximation of sqrt(3) within 1/500 => 26/15 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no best rational approximation with tolerance)
- **G19** continued fraction of tanh(1/(2x)) => [0; 2x, 6x, 10x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **G20** continued fraction of sqrt(x^2+1) - x => [0; 2x, 2x, ...] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no continued fractions of symbolic expressions)
- **I9** cos(3x)/cos(x) via user rewrite rules -- `` -> `` (expected ``) not attempted: no input in problems.txt (no user-defined rewrite rules)
- **J3** d/du dn(u, k) => -k^2 sn(u, k) cn(u, k) -- `diff(jacobidn(u,k),u)` -> `` (expected `0`) no SymPy counterpart for jacobidn
- **J18** define an antisymmetric function -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symmetry declarations for functions)
- **K11** sqrt(e^z) - e^(z/2) for complex z (nonzero in general) -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K14** log(e^z) for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **K17** (x y)^(1/n) - x^(1/n) y^(1/n) for complex x, y -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **M1** (x = 2)/2 + (1 = 1) => x/2 + 1 = 2 -- `(Eq(x,2))/2+(Eq(1,1))` -> `TypeError: unsupported operand type(s) for /: 'Equality' and 'Integer'` (expected `x/2+1=2`) SymPy equations (Eq) do not support arithmetic: (x=2)/2 raises TypeError (auto: ERROR TypeError: unsupported operand type(s) for /: 'Equality' and 'Integer')
- **O3** (a x b) . (c x d) for abstract vectors -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O5** div(f x g) = g . curl f - f . curl g for abstract fields -- `` -> `` (expected ``) not attempted: no input in problems.txt (no abstract (basis-free) vectors)
- **O6** divergence in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **O7** dR/dt in spherical coordinates -- `` -> `` (expected ``) not attempted: no input in problems.txt (no curvilinear coordinate systems)
- **O9** vector potential of the O4 curl -- `vpotential([2*y*z**3-2*x**2*y**2*z,x*y,2*x*y**2*z**2-x*z],[x,y,z])` -> `` (expected `[x*y*z,x^2*y^2*z^2,y^2*z^3]`) no SymPy counterpart for vpotential
- **P3** build a 7x6 matrix from submatrices of a 4x4 matrix -- `` -> `` (expected ``) not attempted: no input in problems.txt (no submatrix indexing / block assembly)
- **P12** inverse of a block triangular matrix with symbolic blocks -- `` -> `` (expected ``) not attempted: no input in problems.txt (no symbolic (noncommutative) matrix blocks)
- **P20** minimal polynomial => (lambda - 1)^2 (lambda + 1) -- `minpoly(Matrix([[17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]]),x)` -> `` (expected `(x-1)^2*(x+1)`) no SymPy counterpart for minpoly
- **P28** generalized eigenvectors (3x3, lambda = 1 triple) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P29** generalized eigenvectors (6x6) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no generalized eigenvectors (Jordan chains))
- **P42** Wronskian matrix of (cos t, sin t) -- `wronskian([cos(t),sin(t)],t)` -> `` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) SymPy's wronskian() returns the determinant, not the Wronskian matrix
- **R1** sum((x[i] - xbar)^2) => sum(x[i]^2) - sum(x[i])^2/n -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R2** least squares fit of a line to (x[i], y[i]) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indexed (subscripted) symbols)
- **R4** Gosper: indefinite sum of binomial(n,k)/2^n - binomial(n+1,k)/2^(n+1) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no indefinite summation (Gosper))
- **U7** exact differential d V(P, T) => V_P dP + V_T dT -- `` -> `` (expected ``) not attempted: no input in problems.txt (no total differentials)
- **U14** [min, max] of 1/(x^2 + y^2 + 1) => [0, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U15** minimize a + b x + c y + d x y on [-1,1]^2 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U16** [min, max] of x^2 y^3 on [-1,1]^2 => [-1, 1] -- `` -> `` (expected ``) not attempted: no input in problems.txt (no optimization commands)
- **U17** linear programming (symbolic simplex) -- `` -> `` (expected ``) not attempted: no input in problems.txt (no linear programming)
- **V2** int |z| dz for complex z -- `` -> `` (expected ``) not attempted: no input in problems.txt (symbols cannot be declared complex)
- **X18** multivariate Taylor cos(x + y) => 1 - (x + y)^2/2 -- `series(cos(x+y),[x,y],[0,0],4)` -> `series: SymPy has no multivariate Taylor series` (expected `1-(x+y)^2/2`) series: SymPy has no multivariate Taylor series
- **X19** power series of log(sin x/x), general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X20** power series of e^-x sin x, general term -- `` -> `` (expected ``) not attempted: no input in problems.txt (no formal power series (general term))
- **X21** series reversion of x = sin y + cos y -- `` -> `` (expected ``) not attempted: no input in problems.txt (no series reversion)
- **X22** Pade approximation of e^-x => (2 - x)/(2 + x) -- `pade(exp(-x),x,1,1)` -> `` (expected `(2-x)/(2+x)`) no SymPy counterpart for pade
- **Y13** Z transform of H(t - 3) -- `ztrans(heaviside(t-3),t,z)` -> `` (expected `0`) no SymPy counterpart for ztrans
- **Y14** Z transform of H(t - m T) => z/(z^m (z - 1)) -- `ztrans(heaviside(t-m),t,z)` -> `` (expected `0`) no SymPy counterpart for ztrans
- **Z16** delay equation y'(t) + a y(t - 1) = 0 -- `` -> `` (expected ``) not attempted: no input in problems.txt (no delay differential equations)

## ERROR (5)

- **O10** Gram-Schmidt of [0,1,2,1], [0,1,3,1], [1,1,1,0], [1,3,6,2] -- `gramschmidt(Matrix([[0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]]))` -> `ValueError: GramSchmidt: vector set not linearly independent` (expected `[[0,1,2,1],[0,-1,1,-1],[2,1,0,-1]]`) ValueError: GramSchmidt: vector set not linearly independent
- **P26** eigenvalues of a hypercompanion matrix => roots of x^2+x+1 (twice), RootOf(quintic) -- `eigenvals(Matrix([[-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,1,0]]))` -> `MatrixError: It is not always possible to express the eigenvalues of a matrix of size 5x5 or higher in radicals. We have CRootOf, but domains other than the rationals are not currently supported. If t` (expected `[-1/2-sqrt(3)*i/2,-1/2+sqrt(3)*i/2]`) MatrixError: It is not always possible to express the eigenvalues of a matrix of size 5x5 or higher in radicals. We have CRootOf, but domains other than the rationals are not currently supported. If there are no symbols in the matrix, it should still be possible to compute numeric approximations of 
- **P38** square root of the singular [[0,1,0],[0,0,0],[0,0,0]] => [[0,a,b],[0,0,0],[0,1/b,0]] -- `Matrix([[0,1,0],[0,0,0],[0,0,0]])**(1/2)` -> `NonInvertibleMatrixError: Non-invertible matrix can only be raised to a nonnegative integer` (expected `[[0,1,0],[0,0,0],[0,0,0]]`) NonInvertibleMatrixError: Non-invertible matrix can only be raised to a nonnegative integer
- **X14** log(x)^a exp(-b x) at x = 1 => (x-1)^a e^-b (1 - (a + 2b)(x - 1)/2) -- `series(ln(x)**a*exp(-b*x),x,1,2)` -> `PoleError: Cannot expand exp(a*log(log(_x + 1))) around 0` (expected `(x-1)^a*exp(-b)*(1-(a+2*b)*(x-1)/2)`) PoleError: Cannot expand exp(a*log(log(_x + 1))) around 0
- **Z4** Yu-Israel nonlinear-coefficient recurrence -- `rsolve(Eq(f(n),(1+c-c**(n-1)-c**(n+1))/(1-c**n)*f(n-1)-c*(1-c**(n-2))/(1-c**(n-1))*f(n-2)+1),f(n),[Eq(f(1),1),Eq(f(2),(2+2*c+c**2)/(1+c))])` -> `ValueError: Polynomial or rational function expected, got '(-c + c**(n - 1))/(c**(n - 1) - 1)'` (expected `(c^(n+1)*(c^(n+1)-2*c-2)+(n+1)*c^2+2*c-n)/((c-1)^3*(c+1))`) ValueError: Polynomial or rational function expected, got '(-c + c**(n - 1))/(c**(n - 1) - 1)'

## TIMEOUT (4)

- **I11** limit of the 0/0 expression at 0 => undefined -- `limit((tan(x)**2+1-sec(x)**2)/(sin(x)**2+cos(x)**2-1),x,0)` -> `` (expected ``) wall-clock timeout
- **P34** sine of a Jordan matrix -- `sinm(Matrix([[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]))` -> `` (expected `[[sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-sin(c)/2],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)]]`) wall-clock timeout
- **W31** int(int(sin a sin y/sqrt(1 - sin^2 a sin^2 x sin^2 y), x = 0..pi/2), y = 0..pi/2) => pi a/2 -- `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)**2*sin(x)**2*sin(y)**2),x,0,pi/2),y,0,pi/2)` -> `` (expected `pi*a/2`) wall-clock timeout
- **Z10** exact y' = -(1 + 2 x sin y)/(1 + x^2 cos y) => x + x^2 sin y + y = A -- `dsolve_([Eq(Derivative(y(x),x),-(1+2*x*sin(y(x)))/(1+x**2*cos(y(x))))],{},[y(x)],x)` -> `` (expected `Y1+(1+2*x*sp.sin(Y))/(1+x**2*sp.cos(Y))`) wall-clock timeout

