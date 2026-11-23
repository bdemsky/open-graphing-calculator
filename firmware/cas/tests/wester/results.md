# Wester benchmark results

binary: `build/cas_repl`

| section | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|
| A Boolean logic | 2 | 0 | 3 | 2 | 0 | 0 | 0 | 1 | 5 | 13 |
| B Set theory | 4 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4 |
| C Numbers | 21 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 1 | 24 |
| D Numerical analysis | 5 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 6 | 14 |
| E Statistics | 12 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 15 |
| F Combinatorial theory | 5 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 8 |
| G Number theory | 10 | 0 | 0 | 10 | 0 | 0 | 0 | 0 | 0 | 20 |
| H Algebra | 30 | 0 | 2 | 0 | 0 | 0 | 0 | 0 | 2 | 34 |
| I Trigonometry | 8 | 0 | 5 | 1 | 0 | 0 | 0 | 0 | 0 | 14 |
| J Special functions | 10 | 0 | 3 | 5 | 0 | 0 | 0 | 1 | 0 | 19 |
| K Complex domain | 13 | 0 | 7 | 4 | 0 | 0 | 0 | 0 | 0 | 24 |
| L Zero equivalence | 6 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 9 |
| M Equations | 34 | 0 | 4 | 1 | 0 | 0 | 0 | 0 | 0 | 39 |
| N Inequalities | 12 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 18 |
| O Vector analysis | 7 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 10 |
| P Matrix theory | 37 | 0 | 3 | 5 | 0 | 0 | 0 | 1 | 0 | 46 |
| Q Tensor analysis | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 9 | 0 | 12 | 3 | 0 | 0 | 0 | 0 | 0 | 24 |
| S Products | 4 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| T Limits | 13 | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 16 |
| U Calculus | 12 | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 2 | 19 |
| V Indefinite integrals | 15 | 0 | 2 | 1 | 0 | 0 | 0 | 0 | 0 | 18 |
| W Definite integrals | 23 | 0 | 11 | 0 | 0 | 0 | 0 | 0 | 0 | 34 |
| X Series | 17 | 0 | 4 | 4 | 0 | 0 | 0 | 0 | 1 | 26 |
| Y Transforms | 8 | 0 | 6 | 0 | 0 | 0 | 0 | 0 | 0 | 14 |
| Z Difference and differential equations | 15 | 0 | 8 | 1 | 0 | 0 | 0 | 0 | 0 | 24 |
| **total** | **332** | **0** | **93** | **51** | **0** | **0** | **0** | **3** | **18** | **497** |

## UNEVAL (93)

- **A2** x or (not x) => true -- `x or not(x)` -> `x or not(x)` (expected `true`) returned unevaluated (output = input)
- **A3** x or y or (x and y) => x or y -- `x or y or (x and y)` -> `x or y or (x and y)` (expected `x or y`) returned unevaluated (output = input)
- **A4** xor(xor(x, y), y) => x -- `xor(xor(x,y),y)` -> `xor(xor(x,y),y)` (expected `x`) returned unevaluated (output = input)
- **C16** Jeffrey-Rich denesting => sqrt(2) + sqrt(3) + sqrt(5) -- `simplify(sqrt(10+2*sqrt(6)+2*sqrt(10)+2*sqrt(15)))` -> `sqrt(2*sqrt(6)+2*sqrt(10)+2*sqrt(15)+10)` (expected `sqrt(2)+sqrt(3)+sqrt(5)`) equivalent but not simplified (ops 14 > 8)
- **C22** nasty Axiom example => (log(sqrt(2) + 1) + sqrt(2))/3 -- `simplify(((6-4*sqrt(2))*ln(3-2*sqrt(2))+(3-2*sqrt(2))*ln(17-12*sqrt(2))+32-24*sqrt(2))/(48*sqrt(2)-72))` -> `sqrt(2)/3-ln(17-12*sqrt(2))/24-ln(3-2*sqrt(2))/12` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) equivalent but not simplified (ops 17 > 8)
- **F3** 2^n n! (2n-1)!! => (2n)! -- `simplify(2^n*n!*factorial2(2*n-1))` -> `2^n*n!*factorial2(2*n-1)` (expected `factorial(2*n)`) equivalent but not simplified (ops 7 > 2)
- **F4** 2^n n! product(2k-1, k=1..n) => (2n)! -- `simplify(2^n*n!*product(2*k-1,k,1,n))` -> `2^(2*n)*n!*gamma(n+1/2)/sqrt(pi)` (expected `factorial(2*n)`) equivalent but not simplified (ops 11 > 2)
- **F5** gamma(n+1/2)/(sqrt(pi) n!) => (2n)!/(2^(2n) (n!)^2) -- `simplify(gamma(n+1/2)/(sqrt(pi)*n!))` -> `gamma(n+1/2)/(n!*sqrt(pi))` (expected `factorial(2*n)/(4^n*factorial(n)^2)`) equivalent but not simplified (ops 8 > 7)
- **H3** (-1)^(n (n+1)) => 1 for integer n -- `(-1)^(n*(n+1))` -> `(-1)^(n*(n+1))` (expected `1`) returned unevaluated (output = input)
- **H17** factor x^3 + x^2 - 7 via its roots, multiply them back together -- `expand(product(map(R,u)))` -> `x^3+x^2+x/3-3*(187/54-sqrt(3885)/18)^(1/3)*(sqrt(3885)/18+187/54)^(1/3)*x-(187/54-sqrt(3885)/18)^(1/3)*(sqrt(3885)/18+187/54)^(1/3)-62/9` (expected `x^3+x^2-7`) equivalent but not simplified (ops 41 > 4)
- **I1b** simplify(tan(7 pi/10)) -- `simplify(tan(7*pi/10))` -> `(-sqrt(5)-1)/sqrt(10-2*sqrt(5))` (expected `-sqrt(1+2/sqrt(5))`) equivalent but not simplified (ops 11 > 7)
- **I3** cos(n pi) + sin((4n - 1) pi/2) => (-1)^n - 1 for integer n -- `cos(n*pi)+sin((4*n-1)/2*pi)` -> `(-1)^n+sin((2*n-1/2)*pi)` (expected `(-1)^n-1`) equivalent but not simplified (ops 8 > 3)
- **I4** cos(cos(n pi) pi) + sin(cos(n pi) pi/2) => (-1)^n - 1 -- `cos(cos(n*pi)*pi)+sin(cos(n*pi)*pi/2)` -> `cos((-1)^n*pi)+sin((-1)^n*pi/2)` (expected `(-1)^n-1`) equivalent but not simplified (ops 10 > 3)
- **I5** sin((n^5/5 + n^4/2 + n^3/3 - n/30) pi) => 0 for integer n -- `sin((n^5/5+n^4/2+n^3/3-n/30)*pi)` -> `sin((n^5/5+n^4/2+n^3/3-n/30)*pi)` (expected `0`) returned unevaluated (output = input)
- **I12** derivative of the 0/0 expression => undefined -- `diff((tan(x)^2+1-sec(x)^2)/(sin(x)^2+cos(x)^2-1),x)` -> `(2*tan(x)*(tan(x)^2+1)-2*sec(x)^2*tan(x))/(cos(x)^2+sin(x)^2-1)` (expected ``) identically-zero denominator not detected (Macsyma also returns an expression)
- **J16** zeta'(0) => -log(2 pi)/2 -- `subst(diff(zeta(x),x),x,0)` -> `subst(diff(zeta(x),x),x,0)` (expected `-ln(2*pi)/2`) unevaluated diff
- **J17** Dirac delta: int f((x+2)/5) delta((x-2)/3) - g(x) delta'(x-1), 0..3 => 3 f(4/5) + g'(1) -- `integrate(f((x+2)/5)*dirac((x-2)/3)-g(x)*diff(dirac(x-1),x),x,0,3)` -> `integrate(dirac(x/3-2/3)*f(x/5+2/5)-diff(dirac(x-1),x)*g(x),x,0,3)` (expected `0`) unevaluated diff,integrate
- **J17b** same with f = exp, g = sin => 3 exp(4/5) + cos(1) -- `integrate(exp((x+2)/5)*dirac((x-2)/3)-sin(x)*diff(dirac(x-1),x),x,0,3)` -> `integrate(dirac(x/3-2/3)*exp(x/5+2/5)-diff(dirac(x-1),x)*sin(x),x,0,3)` (expected `3*exp(4/5)+cos(1)`) unevaluated diff,integrate
- **K4** log(3 + 4i) in rectangular form => log 5 + i atan(4/3) -- `ln(3+4*i)` -> `ln(3+4*i)` (expected `ln(5)+i*atan(4/3)`) returned unevaluated (output = input)
- **K5** tan(x + i y) in rectangular form -- `tan(x+i*y)` -> `tan(x+i*y)` (expected `(sin(2*x)+i*sinh(2*y))/(cos(2*x)+cosh(2*y))`) returned unevaluated (output = input)
- **K7** same for y >= 0 => sqrt(y) -- `simplify(sqrt(x*y*abs(z)^2)/(sqrt(x)*abs(z)))` -> `sqrt(abs(z)^2*x*y)/(abs(z)*sqrt(x))` (expected `sqrt(y)`) equivalent but not simplified (ops 11 > 2)
- **K10** sqrt(1/z) + 1/sqrt(z) => 0 for z < 0 -- `simplify(sqrt(1/z)+1/sqrt(z))` -> `sqrt(1/z)+1/sqrt(z)` (expected `0`) equivalent but not simplified (ops 7 > 0)
- **K19** (x y)^n - x^n y^n => 0 for integer n -- `simplify((x*y)^n-x^n*y^n)` -> `(x*y)^n-x^n*y^n` (expected `0`) equivalent but not simplified (ops 6 > 0)
- **K23** atan(tan(11 + 30 i)) => 11 - 4 pi + 30 i -- `atan(tan(11+30*i))` -> `atan(tan(11+30*i))` (expected `11-4*pi+30*i`) returned unevaluated (output = input)
- **K24** Kahan: w + sqrt(w+1) sqrt(w-1), w = (z + 1/z)/2 => z (z > 1) -- `simplify((z+1/z)/2+sqrt((z+1/z)/2+1)*sqrt((z+1/z)/2-1))` -> `z/2+sqrt(z/2-1+1/(2*z))*sqrt(z/2+1+1/(2*z))+1/(2*z)` (expected `z`) equivalent but not simplified (ops 20 > 0)
- **L5** log(tan(x/2 + pi/4)) - asinh(tan x) => 0 for -pi/2 < x < pi/2 -- `simplify(ln(tan(x/2+pi/4))-asinh(tan(x)))` -> `ln(tan(x/2+pi/4))-asinh(tan(x))` (expected `0`) equivalent but not in the requested form
- **L6** d/dx of the L5 expression => 0 -- `simplify(diff(ln(tan(x/2+pi/4))-asinh(tan(x)),x))` -> `(tan(x/2+pi/4)^2/2+1/2)/tan(x/2+pi/4)-sqrt(tan(x)^2+1)` (expected `0`) equivalent but not in the requested form
- **L9** 2^(1-z) gamma(z) zeta(z) cos(z pi/2) - pi^z zeta(1-z) => 0 -- `simplify(2^(1-z)*gamma(z)*zeta(z)*cos(z*pi/2)-pi^z*zeta(1-z))` -> `2^(-z+1)*cos(pi*z/2)*gamma(z)*zeta(z)-pi^z*zeta(-z+1)` (expected `0`) equivalent but not in the requested form
- **M4** x^6 - 9x^4 - 4x^3 + 27x^2 - 36x - 23 = 0 (Rayes) -- `csolve(x^6-9*x^4-4*x^3+27*x^2-36*x-23=0,x)` -> `[-0.472129757674,2.99197185746,-2.36201133252-1.09112363597*i,1.10209028262-1.09112363597*i,1.09112363597*i-2.36201133252,1.10209028262+1.09112363597*i]` (expected `[2^(1/3)+sqrt(3),2^(1/3)-sqrt(3),sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3)]`) numeric instead of exact
- **M9** e^x = x => x = -W(-1) -- `csolve(exp(x)=x,x)` -> `ERROR 6: cannot solve` (expected `[-LambertW(-1)]`) ERROR 6: cannot solve
- **M17** acos x = atan x => sqrt((sqrt(5) - 1)/2) -- `solve(acos(x)=atan(x),x)` -> `[0.786151377757]` (expected `[sqrt((sqrt(5)-1)/2)]`) numeric instead of exact
- **M32** max(2 - x^2, x) = x^3/9 => {-3, 3, -1.554894} -- `solve(max(2-x^2,x)=x^3/9,x)` -> `[-3,-1.55489408847,3]` (expected `[-3,3,-3*(1+sqrt(3)*sin((atan(sqrt(5)/2)-pi)/3)+cos((atan(sqrt(5)/2)-pi)/3))]`) numeric instead of exact
- **N3** x^4 - x + 1 > 1 for all real x => false -- `x^4-x+1>1` -> `x^4-x+1>1` (expected `false`) returned unevaluated (output = input)
- **N9** x >= y >= z >= x => x = y -- `x=y` -> `x=y` (expected `true`) returned unevaluated (output = input)
- **N15** sin x < 1 => x != pi/2 + 2 n pi -- `solve(sin(x)<1,x)` -> `ERROR 6: cannot solve` (expected `sin(x)<1`) ERROR 6: cannot solve
- **N16** |2 A (cos t - 1) + 1| <= 1 => 0 <= A <= 1/(1 - cos t) -- `solve(abs(2*a*(cos(t)-1)+1)<=1,a)` -> `ERROR 6: inequality with symbolic critical points` (expected `0<=a and a<=1/(1-cos(t))`) ERROR 6: inequality with symbolic critical points
- **N17** A^2 (cos t - 4)^2 sin^2 t < 9 => |A| < 3/|(cos t - 4) sin t| -- `solve(a^2*(cos(t)-4)^2*sin(t)^2<9,a)` -> `ERROR 6: inequality with symbolic critical points` (expected `abs(a)<3/abs((cos(t)-4)*sin(t))`) ERROR 6: inequality with symbolic critical points
- **N18** x + y > 0 and x - y < 0 => |x| < y -- `solve([x+y>0,x-y<0],[x,y])` -> `ERROR 6: cannot solve this system` (expected `abs(x)<y`) ERROR 6: cannot solve this system
- **P9** Frobenius norm => (a^2 + b^2 + c^2)/(|a| |b| |c|) -- `factor(norm([[a/(b*c),1/c,1/b],[1/c,b/(a*c),1/a],[1/b,1/a,c/(a*b)]]))` -> `sqrt(2*abs(1/a)^2+2*abs(1/b)^2+2*abs(1/c)^2+abs(a/(b*c))^2+abs(b/(a*c))^2+abs(c/(a*b))^2)` (expected `(a^2+b^2+c^2)/(abs(a)*abs(b)*abs(c))`) equivalent but not simplified (ops 34 > 11)
- **P26** eigenvalues of a hypercompanion matrix => roots of x^2+x+1 (twice), RootOf(quintic) -- `eigenvals([[-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,1,0]])` -> `eigenvals([[-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,` (expected `[-1/2-sqrt(3)*i/2,-1/2+sqrt(3)*i/2]`) unevaluated eigenvals
- **P38** square root of the singular [[0,1,0],[0,0,0],[0,0,0]] => [[0,a,b],[0,0,0],[0,1/b,0]] -- `[[0,1,0],[0,0,0],[0,0,0]]^(1/2)` -> `sqrt([[0,1,0],[0,0,0],[0,0,0]])` (expected `[[0,1,0],[0,0,0],[0,0,0]]`) function of a matrix left unevaluated
- **R3** sum((-1)^k binomial(2n, k)^2, k = 0..2n) => (-1)^n binomial(2n, n) -- `sum((-1)^k*binomial(2*n,k)^2,k,0,2*n)` -> `sum((-1)^k*binomial(2*n,k)^2,k,0,2*n)` (expected `(-1)^n*binomial(2*n,n)`) unevaluated sum
- **R5** Dixon's identity => (a+b+c)!/(a! b! c!) -- `sum((-1)^k*binomial(a+b,a+k)*binomial(b+c,b+k)*binomial(c+a,c+k),k,-a,a)` -> `sum((-1)^k*binomial(a+b,a+k)*binomial(a+c,c+k)*binomial(b+c,b+k),k,-a,a)` (expected `factorial(a+b+c)/(factorial(a)*factorial(b)*factorial(c))`) unevaluated sum
- **R11** sum(binomial(n, k) fib(k), k = 0..n) => fib(2n) -- `sum(binomial(n,k)*fibonacci(k),k,0,n)` -> `sum(binomial(n,k)*fibonacci(k),k,0,n)` (expected `fibonacci(2*n)`) unevaluated sum
- **R12** sum(fib(k)^2, k = 1..n) => fib(n) fib(n+1) -- `sum(fibonacci(k)^2,k,1,n)` -> `sum(fibonacci(k)^2,k,1,n)` (expected `fibonacci(n)*fibonacci(n+1)`) unevaluated sum
- **R15** sum(binomial(n - k, k), k = 0..floor(n/2)) => fib(n + 1) -- `sum(binomial(n-k,k),k,0,floor(n/2))` -> `sum(binomial(n-k,k),k,0,floor(n/2))` (expected `fibonacci(n+1)`) unevaluated sum
- **R18** sum(1/(2^k k^2), k = 1..inf) => pi^2/12 - log(2)^2/2 -- `sum(1/(2^k*k^2),k,1,inf)` -> `sum(2^(-k)/k^2,k,1,inf)` (expected `pi^2/12-ln(2)^2/2`) unevaluated sum
- **R19** sum(1/((3k+1)(3k+2)(3k+3)), k = 0..inf) => sqrt(3) pi/12 - log(3)/4 -- `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` -> `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` (expected `sqrt(3)*pi/12-ln(3)/4`) unevaluated sum
- **R20** sum(binomial(n, 4k), k = 0..inf) => (2^(n-1) + 2^(n/2) cos(n pi/4))/2 -- `sum(binomial(n,4*k),k,0,inf)` -> `sum(binomial(n,4*k),k,0,inf)` (expected `(2^(n-1)+2^(n/2)*cos(n*pi/4))/2`) unevaluated sum
- **R21** sum(1/(sqrt(k (k+1)) (sqrt k + sqrt(k+1))), k = 1..inf) => 1 -- `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` -> `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` (expected `1`) unevaluated sum
- **R22** double sum (capping) => 1/sqrt((1 - x y)^2 - 4 x^2) -- `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` -> `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R23** equivalent double sum -- `sum(sum(n!/(k!^2*(n-2*k)!)*(x/y)^k*(x*y)^(n-k),n,2*k,inf),k,0,inf)` -> `sum(sum(n!*(x/y)^k*(x*y)^(n-k)/(k!^2*(n-2*k)!),n,2*k,inf),k,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R24** sum(product(k/(2k - 1), k = 1..m), m = 2..inf) => pi/2 -- `sum(product(k/(2*k-1),k,1,m),m,2,inf)` -> `sum(2^(-m)*gamma(m+1)*sqrt(pi)/gamma(m+1/2),m,2,inf)` (expected `pi/2`) unevaluated sum
- **S1** product(gamma(k/3), k = 1..8) => 640 pi^3/(2187 sqrt 3) -- `product(gamma(k/3),k,1,8)` -> `gamma(1/3)*gamma(2/3)*gamma(4/3)*gamma(5/3)*gamma(7/3)*gamma(8/3)` (expected `640*pi^3/(2187*sqrt(3))`) equivalent but not simplified (ops 17 > 6)
- **S6** product(x^2 - 2x cos(k pi/n) + 1, k = 1..n-1) => (x^(2n) - 1)/(x^2 - 1) -- `product(x^2-2*x*cos(k*pi/n)+1,k,1,n-1)` -> `product(x^2-2*cos(k*pi/n)*x+1,k,1,n-1)` (expected `(x^(2*n)-1)/(x^2-1)`) unevaluated product
- **S7** product((k^3 - 1)/(k^3 + 1), k = 2..inf) => 2/3 -- `product((k^3-1)/(k^3+1),k,2,inf)` -> `product((k^3-1)/(k^3+1),k,2,inf)` (expected `2/3`) unevaluated product
- **S8** product(1 - 1/(2k)^2, k = 1..inf) => 2/pi -- `product(1-1/(2*k)^2,k,1,inf)` -> `product(1-1/(4*k^2),k,1,inf)` (expected `2/pi`) unevaluated product
- **S9** product(1 + (-1)^(k+1)/(2k - 1), k = 1..inf) => sqrt(2) -- `product(1+(-1)^(k+1)/(2*k-1),k,1,inf)` -> `product((-1)^(k+1)/(2*k-1)+1,k,1,inf)` (expected `sqrt(2)`) unevaluated product
- **S10** product((k (k+1) + 1 + i)/(k (k+1) + 1 - i), k = 0..inf) => -1 -- `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` -> `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` (expected `-1`) unevaluated product
- **T11** limit(2F1(1, k; 1; z/k), k, inf) => e^z -- `limit(hypergeom([1,k],[1],z/k),k,inf)` -> `ERROR 6: limit could not be determined` (expected `exp(z)`) ERROR 6: limit could not be determined
- **T12** limit(zeta(x) - 1/(x - 1), x, 1) => Euler's gamma -- `limit(zeta(x)-1/(x-1),x,1)` -> `ERROR 6: limit could not be determined` (expected `euler_gamma`) ERROR 6: limit could not be determined
- **T13** limit(n^x/(x product(1 + x/k, k = 1..n)), n, inf) => gamma(x) -- `limit(n^x/(x*product(1+x/k,k,1,n)),n,inf)` -> `ERROR 6: limit could not be determined` (expected `gamma(x)`) ERROR 6: limit could not be determined
- **V15** int log|x^2 - a^2| dx -- `integrate(ln(abs(x^2-a^2)),x)` -> `integrate(ln(abs(x^2-a^2)),x)` (expected `ln(abs(x^2-a^2))`) unevaluated integrate
- **V18** int (f' g - f g')/(f^2 - g^2) dx => log((f - g)/(f + g))/2 -- `integrate((diff(f(x),x)*g(x)-f(x)*diff(g(x),x))/(f(x)^2-g(x)^2),x)` -> `integrate((diff(f(x),x)*g(x)-diff(g(x),x)*f(x))/(f(x)^2-g(x)^2),x)` (expected ``) unevaluated integrate
- **W1** int 1/(x - a), x = a-1..a+1 => 0 (principal value) -- `integrate(1/(x-a),x,a-1,a+1)` -> `ERROR 2: integral diverges (pole inside the interval)` (expected `0`) divergent as an improper integral (as for numeric bounds); the principal value 0 is not computed
- **W8** int t^(a-1)/(1 + t), t = 0..inf => pi/sin(pi a) (0 < a < 1) -- `integrate(t^(a-1)/(1+t),t,0,inf)` -> `integrate(t^(a-1)/(t+1),t,0,inf)` (expected `pi/sin(pi*a)`) unevaluated integrate
- **W9** PV int 5x^3/(1 + x + x^2 + x^3 + x^4), -inf..inf => -2 pi (sin(pi/5) + sin(2 pi/5)) -- `integrate(5*x^3/(1+x+x^2+x^3+x^4),x,-inf,inf)` -> `undef` (expected `-2*pi*(sin(pi/5)+sin(2*pi/5))`) improper integral reported as undefined; no principal value (defensible)
- **W11** int sqrt(1 - x^2)/(1 + x^2), x = -1..1 => pi (sqrt 2 - 1) -- `integrate(sqrt(1-x^2)/(1+x^2),x,-1,1)` -> `1.30129028457` (expected `pi*(sqrt(2)-1)`) numeric instead of exact
- **W15** int log(gamma(x)) cos(6 pi x), x = 0..1 => 1/12 -- `integrate(ln(gamma(x))*cos(6*pi*x),x,0,1)` -> `0.0833333333318` (expected `1/12`) numeric instead of exact
- **W17** int exp(-a x) J0(b x), x = 0..inf => 1/sqrt(a^2 + b^2) -- `integrate(exp(-a*x)*besselj(0,b*x),x,0,inf)` -> `integrate(besselj(0,b*x)*exp(-a*x),x,0,inf)` (expected `1/sqrt(a^2+b^2)`) unevaluated integrate
- **W18** int (J1(x)/x)^2, x = 0..inf => 4/(3 pi) -- `integrate((besselj(1,x)/x)^2,x,0,inf)` -> `integrate(besselj(1,x)^2/x^2,x,0,inf)` (expected `4/(3*pi)`) unevaluated integrate
- **W19** int Ci(x) J0(2 sqrt(7 x)), x = 0..inf => (cos 7 - 1)/7 -- `integrate(Ci(x)*besselj(0,2*sqrt(7*x)),x,0,inf)` -> `integrate(Ci(x)*besselj(0,2*sqrt(7)*sqrt(x)),x,0,inf)` (expected `(cos(7)-1)/7`) unevaluated integrate
- **W20** int x^2 Li3(1/(x + 1)), x = 0..1 (Mertig) -- `integrate(x^2*polylog(3,1/(x+1)),x,0,1)` -> `0.210882859566` (expected `-(17/3+pi^2)/36+ln(2)/9*(35/3-pi^2/2-4*ln(2)+ln(2)^2)+zeta(3)/4`) numeric instead of exact
- **W22** int s(t) cos t, t = 0..u for the step s = 1 on [1, 2] -- `integrate(piecewise(t>=1 and t<=2,1,0)*cos(t),t,0,u)` -> `integrate(cos(t)*piecewise(t>=1 and t<=2,1,0),t,0,u)` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) unevaluated integrate
- **W31** int(int(sin a sin y/sqrt(1 - sin^2 a sin^2 x sin^2 y), x = 0..pi/2), y = 0..pi/2) => pi a/2 -- `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,pi/2),y,0,pi/2)` -> `integrate(elliptic_f(pi/2,sin(a)^2*sin(y)^2)*sin(a)*sin(y),y,0,pi/2)` (expected `pi*a/2`) unevaluated integrate
- **X7** series of f'(a x) a + g(b x) + int(h(c y), y = 0..x) at x = d -- `series(diff(f(a*x),x)+g(b*x)+integrate(h(c*y),y,0,x),x,d,2)` -> `ERROR 6: series expansion failed` (expected ``) ERROR 6: series expansion failed
- **X14** log(x)^a exp(-b x) at x = 1 => (x-1)^a e^-b (1 - (a + 2b)(x - 1)/2) -- `series(ln(x)^a*exp(-b*x),x,1,2)` -> `ERROR 6: series expansion failed` (expected `(x-1)^a*exp(-b)*(1-(a+2*b)*(x-1)/2)`) ERROR 6: series expansion failed
- **X16** Wallis: binomial(2n, n)/2^(2n) => 1/sqrt(pi n) -- `series(binomial(2*n,n)/4^n,n,inf,1)` -> `ERROR 6: series expansion failed` (expected `1/sqrt(pi*n)`) ERROR 6: series expansion failed
- **X17** e^x int(e^-t/t, t = x..inf) => 1/x - 1/x^2 + 2/x^3 - 6/x^4 -- `series(exp(x)*integrate(exp(-t)/t,t,x,inf),x,inf,5)` -> `ERROR 6: series expansion failed` (expected `1/x-1/x^2+2/x^3-6/x^4`) ERROR 6: series expansion failed
- **Y4** laplace(erf(3/sqrt t)) => 1/s - e^(-6 sqrt s)/s -- `laplace(erf(3/sqrt(t)),t,s)` -> `ERROR 6: laplace: transform not found` (expected `1/s-exp(-6*sqrt(s))/s`) ERROR 6: laplace: transform not found
- **Y7** Laplace transform of a square wave (infinite sum of steps) -- `laplace(1+2*sum((-1)^n*heaviside(t-n*a),n,1,inf),t,s)` -> `ERROR 6: laplace: transform not found` (expected `0`) ERROR 6: laplace: transform not found
- **Y11** Mellin transform of 1/(1 - x) => pi cot(pi s) (PV) -- `integrate(x^(s-1)/(1-x),x,0,inf)` -> `integrate(x^(s-1)/(-x+1),x,0,inf)` (expected `pi*cot(pi*s)`) unevaluated integrate
- **Y12** Mellin transform of J3(x)/x^3 => 2^(s-4) gamma(s/2)/gamma(4 - s/2) -- `integrate(besselj(3,x)/x^3*x^(s-1),x,0,inf)` -> `integrate(besselj(3,x)*x^(s-4),x,0,inf)` (expected `2^(s-4)*gamma(s/2)/gamma(4-s/2)`) unevaluated integrate
- **Y13** Z transform of H(t - 3) -- `ztrans(heaviside(t-3),t,z)` -> `ERROR 6: ztrans: transform not found` (expected `0`) ERROR 6: ztrans: transform not found
- **Y14** Z transform of H(t - m T) => z/(z^m (z - 1)) -- `ztrans(heaviside(t-m),t,z)` -> `ERROR 6: ztrans: transform not found` (expected `0`) ERROR 6: ztrans: transform not found
- **Z4** Yu-Israel nonlinear-coefficient recurrence -- `rsolve(f(n)=(1+c-c^(n-1)-c^(n+1))/(1-c^n)*f(n-1)-c*(1-c^(n-2))/(1-c^(n-1))*f(n-2)+1,f(n),[f(1)=1,f(2)=(2+2*c+c^2)/(1+c)])` -> `rsolve(f(n)=-c*(1-c^(n-2))*f(n-2)/(1-c^(n-1))+(c-c^(n-1)-c^(n+1)+1)*f(n-1)/(1-c^n)+1,f(n),[f(1)=1,f(2)=(c^2+2*c+2)/(c+1)])` (expected `(c^(n+1)*(c^(n+1)-2*c-2)+(n+1)*c^2+2*c-n)/((c-1)^3*(c+1))`) rsolve handles constant coefficients only
- **Z11** nonlinear y'' + y y'^3 = 0 => y^3/6 + A y = x + B -- `dsolve(y''+y*y'^3=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y*Y1**3`) ERROR 6: dsolve: cannot solve this equation
- **Z12** same with y(0) = 0, y'(0) = 2 => (3x + sqrt(1 + 9x^2))^(1/3) - 1/(3x + sqrt(1 + 9x^2))^(1/3) -- `dsolve([y''+y*y'^3=0,y(0)=0,y'(0)=2],y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y*Y1**3`) ERROR 6: dsolve: cannot solve this equation
- **Z14** BVP y'' + k^2 y = 0, y(0) = 0, y'(1) = 0 => A sin(k x) with cos k = 0 -- `dsolve([y''+k^2*y=0,y(0)=0,y'(1)=0],y,x)` -> `0` (expected ``) only the trivial solution; no eigenvalue condition cos(k) = 0
- **Z15** y'' + y'/x + (1 - v^2/x) y/(4x) = 0 => Z_v(sqrt x) -- `dsolve(y''+y'/x+(1-v^2/x)*y/(4*x)=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y1/x+(1-v**2/x)*Y/(4*x)`) ERROR 6: dsolve: cannot solve this equation
- **Z17** discontinuous ODE y'' + sign(t - T) y = 0 -- `dsolve(y''+sign(x-q)*y=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected ``) ERROR 6: dsolve: cannot solve this equation
- **Z20** triangular system u' = u (1 + cos t/(2 + sin t)), v' = u - v -- `dsolve([u'=u*(1+cos(t)/(2+sin(t))),v'=u-v],[u,v],t)` -> `ERROR 6: dsolve: only linear systems with constant coefficients` (expected `[U.diff(t)-U*(1+sp.cos(t)/(2+sp.sin(t))), V.diff(t)-(U-V)]`) ERROR 6: dsolve: only linear systems with constant coefficients
- **Z24** second order system (Niles) u'' = 2 q v', v'' = -2 q u' + 3 q^2 v -- `dsolve([u''=2*q*v',v''=-2*q*u'+3*q^2*v],[u,v],t)` -> `ERROR 6: dsolve: only first-order systems` (expected `[U.diff(t,2)-2*q*V.diff(t), V.diff(t,2)-(-2*q*U.diff(t)+3*q**2*V)]`) ERROR 6: dsolve: only first-order systems

## UNSUPPORTED (51)

- **A5** (w and x) implies (y and z) => not(w and x) or (y and z) -- `None` -> `` (expected ``) no logical implication operator
- **A6** x iff y => (x and y) or not(x or y) -- `None` -> `` (expected ``) no logical equivalence operator
- **D5** natural cubic spline through (1,1),(2,4),(4,2),(5,3) at 3 => 27/8 -- `None` -> `` (expected ``) no spline interpolation
- **D8** Horner form of sum(a[i] x^i, i=1..5) -- `horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x)` -> `a5*x^5+a4*x^4+a3*x^3+a2*x^2+a1*x` (expected `x*(a1+x*(a2+x*(a3+x*(a4+a5*x))))`) horner() only evaluates at a point; no Horner-form rewriting
- **D12** interval arithmetic ([-4,2] x + [1,3])^2 -- `None` -> `` (expected ``) no interval arithmetic
- **E13** multiple linear regression => -16278.7 + 960.925 x1 + 2975.66 x2 -- `None` -> `` (expected ``) no multiple regression in the CAS
- **E14** L1 (least absolute deviations) regression -- `None` -> `` (expected ``) no L1 regression
- **E15** nonlinear least squares fit -- `None` -> `` (expected ``) no nonlinear regression in the CAS
- **G3** (a + b)^p mod p => a^p + b^p for p prime -- `None` -> `` (expected ``) no modular polynomial arithmetic with symbolic modulus
- **G9** linear system mod 7 => x = 1, y = 2 -- `None` -> `` (expected ``) no modular equation solving
- **G10** 2x + 3y = 1 mod 5 => 5 solutions -- `None` -> `` (expected ``) no modular equation solving
- **G11** 2x + 3y = 1 mod 6 => 6 solutions -- `None` -> `` (expected ``) no modular equation solving
- **G12** Diophantine x^4 + 9 = y^2 => (2, 5) -- `None` -> `` (expected ``) no Diophantine solver
- **G13** Diophantine x^2 + 4 = y^3 => (11, 5) -- `None` -> `` (expected ``) no Diophantine solver
- **G14** Pythagorean system => (3,4,5,12,13), ... -- `None` -> `` (expected ``) no Diophantine solver
- **G15** rational approximation of sqrt(3) within 1/500 => 26/15 -- `None` -> `` (expected ``) no best rational approximation with tolerance
- **G19** continued fraction of tanh(1/(2x)) => [0; 2x, 6x, 10x, ...] -- `None` -> `` (expected ``) no continued fractions of symbolic expressions
- **G20** continued fraction of sqrt(x^2+1) - x => [0; 2x, 2x, ...] -- `None` -> `` (expected ``) no continued fractions of symbolic expressions
- **I9** cos(3x)/cos(x) via user rewrite rules -- `None` -> `` (expected ``) no user-defined rewrite rules
- **J2** d/dk E(phi, k) => (E(phi, k) - F(phi, k))/k -- `diff(ellipticE(p,k^2),k)` -> `diff(ellipticE(p,k^2),k)` (expected `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k`) unknown function(s) echoed: ellipticE
- **J3** d/du dn(u, k) => -k^2 sn(u, k) cn(u, k) -- `diff(jacobidn(u,k),u)` -> `diff(jacobidn(u,k),u)` (expected `0`) unknown function(s) echoed: jacobidn
- **J14** 2F1(1/2, 1/2; 3/2; z^2) => asin(z)/z -- `hypergeom([1/2,1/2],[3/2],z^2)` -> `hypergeom([1/2,1/2],[3/2],z^2)` (expected `asin(z)/z`) unknown function(s) echoed: hypergeom
- **J15** 2F1((n+2)/2, -(n-2)/2; 3/2; sin^2 z) => sin(n z)/(n sin z cos z) -- `hypergeom([(n+2)/2,-(n-2)/2],[3/2],sin(z)^2)` -> `hypergeom([n/2+1,-n/2+1],[3/2],sin(z)^2)` (expected `sin(n*z)/(n*sin(z)*cos(z))`) unknown function(s) echoed: hypergeom
- **J18** define an antisymmetric function -- `None` -> `` (expected ``) no symmetry declarations for functions
- **K1** [Re(x + i y), Im(x + i y)] for complex x, y => [Re x - Im y, Im x + Re y] -- `[re(x+i*y),im(x+i*y)]` -> `[x,y]` (expected `[re(x)-im(y),im(x)+re(y)]`) symbols are real by default and cannot be declared complex
- **K11** sqrt(e^z) - e^(z/2) for complex z (nonzero in general) -- `None` -> `` (expected ``) symbols cannot be declared complex
- **K14** log(e^z) for complex z -- `None` -> `` (expected ``) symbols cannot be declared complex
- **K17** (x y)^(1/n) - x^(1/n) y^(1/n) for complex x, y -- `None` -> `` (expected ``) symbols cannot be declared complex
- **M34** f(x)^2 + f(x) - 2 = 0 solved for f(x) => {1, -2} -- `solve(f(x)^2+f(x)-2=0,f(x))` -> `ERROR 2: solve: expected a variable` (expected `[1,-2]`) cannot solve for a function application f(x)
- **O3** (a x b) . (c x d) for abstract vectors -- `None` -> `` (expected ``) no abstract (basis-free) vectors
- **O5** div(f x g) = g . curl f - f . curl g for abstract fields -- `None` -> `` (expected ``) no abstract (basis-free) vectors
- **O7** dR/dt in spherical coordinates -- `None` -> `` (expected ``) no curvilinear coordinate systems
- **P3** build a 7x6 matrix from submatrices of a 4x4 matrix -- `None` -> `` (expected ``) no submatrix indexing / block assembly
- **P12** inverse of a block triangular matrix with symbolic blocks -- `None` -> `` (expected ``) no symbolic (noncommutative) matrix blocks
- **P28** generalized eigenvectors (3x3, lambda = 1 triple) -- `None` -> `` (expected ``) no generalized eigenvectors (Jordan chains)
- **P29** generalized eigenvectors (6x6) -- `None` -> `` (expected ``) no generalized eigenvectors (Jordan chains)
- **P31** Smith normal form of [[x^2, x - 1], [x + 1, x^2]] => [[1, 0], [0, x^4 - x^2 + 1]] -- `smith([[x^2,x-1],[x+1,x^2]])` -> `smith([[x^2,x-1],[x+1,x^2]])` (expected `[[1,0],[0,x^4-x^2+1]]`) unknown function(s) echoed: smith
- **R1** sum((x[i] - xbar)^2) => sum(x[i]^2) - sum(x[i])^2/n -- `None` -> `` (expected ``) no indexed (subscripted) symbols
- **R2** least squares fit of a line to (x[i], y[i]) -- `None` -> `` (expected ``) no indexed (subscripted) symbols
- **R4** Gosper: indefinite sum of binomial(n,k)/2^n - binomial(n+1,k)/2^(n+1) -- `None` -> `` (expected ``) no indefinite summation (Gosper)
- **U7** exact differential d V(P, T) => V_P dP + V_T dT -- `None` -> `` (expected ``) no total differentials
- **U14** [min, max] of 1/(x^2 + y^2 + 1) => [0, 1] -- `None` -> `` (expected ``) no optimization commands
- **U15** minimize a + b x + c y + d x y on [-1,1]^2 -- `None` -> `` (expected ``) no optimization commands
- **U16** [min, max] of x^2 y^3 on [-1,1]^2 => [-1, 1] -- `None` -> `` (expected ``) no optimization commands
- **U17** linear programming (symbolic simplex) -- `None` -> `` (expected ``) no linear programming
- **V2** int |z| dz for complex z -- `None` -> `` (expected ``) symbols cannot be declared complex
- **X18** multivariate Taylor cos(x + y) => 1 - (x + y)^2/2 -- `series(cos(x+y),[x,y],[0,0],4)` -> `ERROR 2: expected a variable name` (expected `1-(x+y)^2/2`) series() is univariate only
- **X19** power series of log(sin x/x), general term -- `None` -> `` (expected ``) no formal power series (general term)
- **X20** power series of e^-x sin x, general term -- `None` -> `` (expected ``) no formal power series (general term)
- **X21** series reversion of x = sin y + cos y -- `None` -> `` (expected ``) no series reversion
- **Z16** delay equation y'(t) + a y(t - 1) = 0 -- `None` -> `` (expected ``) no delay differential equations

## UNGRADED (3)

- **A12** simplify the QEPCAD output => v > 0 and w > abs(ww) -- `simplify(v>0 and 4*w*v>0 and 4*w*(4*w^2*v^2+3*ww^2+w^2)>0 and 64*w^2*v^2*(w^2-ww^2)*(w^2*v^2+ww^2)>0 and 64*w^2*v^2*(w^2-ww^2)^3*(w^2*v^2+ww^2)>0)` -> `v>0 and 4*v*w>0 and 4*w*(4*v^2*w^2+w^2+3*ww^2)>0 and 64*v^2*w^2*(v^2*w^2+ww^2)*(w^2-ww^2)>0 and 64*v^2*w^2*(v^2*w^2+ww^2)*(w^2-ww^2)^3>0` (expected `v>0 and w>abs(ww)`) manual review
- **J10** associated Legendre P^mu_nu(0) -- `legendrep(n,m,0)` -> `legendrep(n,m,0)` (expected `0`) manual review
- **P39** singular value decomposition of [[1,1],[2,2],[3,3]] -- `svd([[1,1],[2,2],[3,3]])` -> `[[[0.267261241912,0.963624111659],[0.534522483825,-0.148249863332],[0.801783725737,-0.222374794998]],[[5.29150262213,0.0],[0.0,0.0]],[[0.707106781187,0.707106781187],[0.707106781187,-0.707106781187]]]` (expected `[2*sqrt(7),0]`) manual review

## NA (18)

- **A8** QE: forall y in C (a y^2 + b y + c = 0 => Re y < 0) -- `None` -> `` (expected ``) quantifier elimination
- **A9** QE: exists w in R (v > 0 and w > 0 and -5v^2-13v+vw-w > 0) => v > 1 -- `None` -> `` (expected ``) quantifier elimination
- **A10** QE: forall c in [-1,1] (...) => a^2 <= 1/2 -- `None` -> `` (expected ``) quantifier elimination
- **A11** QE: forall y in C (quartic stability) => v > 0 and w > |W| -- `None` -> `` (expected ``) quantifier elimination
- **A13** QE: exists y, n in C, e in R (...) => B < 0 and a b > 0 -- `None` -> `` (expected ``) quantifier elimination
- **C24** 2^aleph_0 => aleph_1 -- `None` -> `` (expected ``) transfinite cardinals
- **D6** translate sum(a[i] x^i) to FORTRAN -- `None` -> `` (expected ``) code generation
- **D7** translate sum(a[i] x^i) to C -- `None` -> `` (expected ``) code generation
- **D9** Horner form to FORTRAN -- `None` -> `` (expected ``) code generation
- **D10** Horner form to C -- `None` -> `` (expected ``) code generation
- **D11** count floating point operations -- `None` -> `` (expected ``) operation counting
- **D13** discretize the heat equation -- `None` -> `` (expected ``) finite difference scheme generation
- **H33** noncommutative (ABC - (ABC)^-1) ACB -- `None` -> `` (expected ``) noncommutative algebra
- **H34** Jacobi identity for commutators -- `None` -> `` (expected ``) noncommutative algebra
- **Q1** tensor analysis problems (Q1-Q6 in Wester) -- `None` -> `` (expected ``) tensor analysis (also absent from SymPy's transcription)
- **U11** wedge product of 1-forms => 8 dx dy dz -- `None` -> `` (expected ``) differential forms
- **U12** exterior derivative => (15x^4 + 10xy + 8) dx dy dz -- `None` -> `` (expected ``) differential forms
- **X8** noncommutative exp((A + B) t) - exp(A t) exp(B t) -- `None` -> `` (expected ``) noncommutative algebra

