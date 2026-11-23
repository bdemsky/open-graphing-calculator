# Wester benchmark results

binary: `build/cas_repl_asan`

| section | PASS | WRONG | UNEVAL | UNSUPPORTED | ERROR | TIMEOUT | CRASH | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|
| A Boolean logic | 1 | 0 | 4 | 3 | 0 | 0 | 0 | 0 | 5 | 13 |
| B Set theory | 0 | 0 | 0 | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| C Numbers | 16 | 0 | 6 | 1 | 0 | 0 | 0 | 0 | 1 | 24 |
| D Numerical analysis | 5 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 6 | 14 |
| E Statistics | 8 | 0 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 15 |
| F Combinatorial theory | 2 | 0 | 2 | 4 | 0 | 0 | 0 | 0 | 0 | 8 |
| G Number theory | 2 | 0 | 0 | 18 | 0 | 0 | 0 | 0 | 0 | 20 |
| H Algebra | 16 | 0 | 7 | 3 | 6 | 0 | 0 | 0 | 2 | 34 |
| I Trigonometry | 6 | 0 | 6 | 2 | 0 | 0 | 0 | 0 | 0 | 14 |
| J Special functions | 1 | 0 | 3 | 15 | 0 | 0 | 0 | 0 | 0 | 19 |
| K Complex domain | 10 | 0 | 9 | 5 | 0 | 0 | 0 | 0 | 0 | 24 |
| L Zero equivalence | 4 | 0 | 5 | 0 | 0 | 0 | 0 | 0 | 0 | 9 |
| M Equations | 25 | 0 | 12 | 1 | 1 | 0 | 0 | 0 | 0 | 39 |
| N Inequalities | 6 | 0 | 6 | 6 | 0 | 0 | 0 | 0 | 0 | 18 |
| O Vector analysis | 3 | 0 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 10 |
| P Matrix theory | 21 | 0 | 6 | 17 | 2 | 0 | 0 | 0 | 0 | 46 |
| Q Tensor analysis | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| R Sums | 3 | 0 | 16 | 5 | 0 | 0 | 0 | 0 | 0 | 24 |
| S Products | 3 | 0 | 7 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| T Limits | 8 | 0 | 8 | 0 | 0 | 0 | 0 | 0 | 0 | 16 |
| U Calculus | 5 | 0 | 5 | 7 | 0 | 0 | 0 | 0 | 2 | 19 |
| V Indefinite integrals | 11 | 0 | 6 | 1 | 0 | 0 | 0 | 0 | 0 | 18 |
| W Definite integrals | 8 | 0 | 20 | 6 | 0 | 0 | 0 | 0 | 0 | 34 |
| X Series | 17 | 0 | 4 | 4 | 0 | 0 | 0 | 0 | 1 | 26 |
| Y Transforms | 2 | 0 | 6 | 6 | 0 | 0 | 0 | 0 | 0 | 14 |
| Z Difference and differential equations | 7 | 0 | 6 | 11 | 0 | 0 | 0 | 0 | 0 | 24 |
| **total** | **190** | **0** | **144** | **136** | **9** | **0** | **0** | **0** | **18** | **497** |

## UNEVAL (144)

- **A2** x or (not x) => true -- `x or not(x)` -> `x or not(x)` (expected `true`) returned unevaluated (output = input)
- **A3** x or y or (x and y) => x or y -- `x or y or (x and y)` -> `(x or y) or (x and y)` (expected `x or y`) equivalent but not simplified
- **A7** x and 1 > 2 => false -- `x and 1>2` -> `x and false` (expected `false`) no truth value
- **A12** simplify the QEPCAD output => v > 0 and w > abs(ww) -- `simplify(v>0 and 4*w*v>0 and 4*w*(4*w^2*v^2+3*ww^2+w^2)>0 and 64*w^2*v^2*(w^2-ww^2)*(w^2*v^2+ww^2)>0 and 64*w^2*v^2*(w^2-ww^2)^3*(w^2*v^2+ww^2)>0)` -> `(((v>0 and 4*v*w>0) and 4*w*(4*v^2*w^2+w^2+3*ww^2)>0) and 64*v^2*w^2*(v^2*w^2+ww^2)*(w^2-ww^2)>0) and 64*v^2*w^2*(v^2*w^2+ww^2)*(w^2-ww^2)^3>0` (expected `v>0 and w>abs(ww)`) conjunction returned unsimplified
- **C16** Jeffrey-Rich denesting => sqrt(2) + sqrt(3) + sqrt(5) -- `simplify(sqrt(10+2*sqrt(6)+2*sqrt(10)+2*sqrt(15)))` -> `sqrt(2*sqrt(6)+2*sqrt(10)+2*sqrt(15)+10)` (expected `sqrt(2)+sqrt(3)+sqrt(5)`) equivalent but not simplified (ops 14 > 8)
- **C18** sqrt(-2 + sqrt(-5)) sqrt(-2 - sqrt(-5)) => 3 -- `simplify(sqrt(-2+sqrt(-5))*sqrt(-2-sqrt(-5)))` -> `sqrt(sqrt(5)*i-2)*sqrt(-sqrt(5)*i-2)` (expected `3`) equivalent but not simplified (ops 14 > 0)
- **C19** (90 + 34 sqrt(7))^(1/3) => 3 + sqrt(7) -- `simplify((90+34*sqrt(7))^(1/3))` -> `(34*sqrt(7)+90)^(1/3)` (expected `3+sqrt(7)`) equivalent but not simplified (ops 6 > 3)
- **C20** nontrivial way of writing 12 -- `simplify(((135+78*sqrt(3))^(2/3)+3)*sqrt(3)/(135+78*sqrt(3))^(1/3))` -> `sqrt(3)*((78*sqrt(3)+135)^(2/3)+3)/(78*sqrt(3)+135)^(1/3)` (expected `12`) equivalent but not simplified (ops 17 > 0)
- **C21** (41 + 29 sqrt(2))^(1/5) => 1 + sqrt(2) -- `simplify((41+29*sqrt(2))^(1/5))` -> `(29*sqrt(2)+41)^(1/5)` (expected `1+sqrt(2)`) equivalent but not simplified (ops 6 > 3)
- **C22** nasty Axiom example => (log(sqrt(2) + 1) + sqrt(2))/3 -- `simplify(((6-4*sqrt(2))*ln(3-2*sqrt(2))+(3-2*sqrt(2))*ln(17-12*sqrt(2))+32-24*sqrt(2))/(48*sqrt(2)-72))` -> `sqrt(2)/3-ln(17-12*sqrt(2))/24-ln(3-2*sqrt(2))/12` (expected `(ln(sqrt(2)+1)+sqrt(2))/3`) equivalent but not simplified (ops 17 > 8)
- **F4** 2^n n! product(2k-1, k=1..n) => (2n)! -- `simplify(2^n*n!*product(2*k-1,k,1,n))` -> `2^n*n!*product(2*k-1,k,1,n)` (expected `factorial(2*n)`) unevaluated product
- **F5** gamma(n+1/2)/(sqrt(pi) n!) => (2n)!/(2^(2n) (n!)^2) -- `simplify(gamma(n+1/2)/(sqrt(pi)*n!))` -> `gamma(n+1/2)/(n!*sqrt(pi))` (expected `factorial(2*n)/(4^n*factorial(n)^2)`) equivalent but not simplified (ops 8 > 7)
- **H1** 2 2^n => 2^(n+1) -- `simplify(2*2^n)` -> `2*2^n` (expected `2^(n+1)`) equivalent but not in the requested form
- **H2** 4 2^n => 2^(n+2) -- `simplify(4*2^n)` -> `4*2^n` (expected `2^(n+2)`) equivalent but not in the requested form
- **H3** (-1)^(n (n+1)) => 1 for integer n -- `(-1)^(n*(n+1))` -> `(-1)^(n*(n+1))` (expected `1`) returned unevaluated (output = input)
- **H7** resultant(p1 q, p2 q) => 0 -- `resultant(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),expand((72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)*(34*x^19-25*x^16+70*x^7+20*x^3-91*x-86)),x)` -> `ERROR 6: resultant: degree too large` (expected `0`) ERROR 6: resultant: degree too large
- **H15** (e^x - 1)/(e^(x/2) + 1) => e^(x/2) - 1 -- `simplify((exp(x)-1)/(exp(x/2)+1))` -> `(exp(x)-1)/(exp(x/2)+1)` (expected `exp(x/2)-1`) equivalent but not simplified (ops 6 > 3)
- **H17** factor x^3 + x^2 - 7 via its roots, multiply them back together -- `expand(product(map(R,u)))` -> `x^3+x^2+x/3-3*(187/54-sqrt(3885)/18)^(1/3)*(sqrt(3885)/18+187/54)^(1/3)*x-(187/54-sqrt(3885)/18)^(1/3)*(sqrt(3885)/18+187/54)^(1/3)-62/9` (expected `x^3+x^2-7`) equivalent but not simplified (ops 41 > 4)
- **H21** (x^3 + (sqrt2-2)x^2 - (2sqrt2+3)x - 3sqrt2)/(x^2-2) => (x^2-2x-3)/(x-sqrt2) -- `simplify((x^3+(sqrt(2)-2)*x^2-(2*sqrt(2)+3)*x-3*sqrt(2))/(x^2-2))` -> `(x-3)*(x+1)*(x+sqrt(2))/(x^2-2)` (expected `(x^2-2*x-3)/(x-sqrt(2))`) equivalent but not simplified (ops 10 > 8)
- **I1b** simplify(tan(7 pi/10)) -- `simplify(tan(7*pi/10))` -> `(-sqrt(5)-1)/sqrt(10-2*sqrt(5))` (expected `-sqrt(1+2/sqrt(5))`) equivalent but not simplified (ops 11 > 7)
- **I2** sqrt((1 + cos 6)/2) => -cos 3 -- `simplify(sqrt((1+cos(6))/2))` -> `sqrt(cos(6)/2+1/2)` (expected `-cos(3)`) equivalent but not simplified (ops 6 > 2)
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
- **K16** log(e^(10 i)) => (10 - 4 pi) i -- `ln(exp(10*i))` -> `ln(exp(10*i))` (expected `(10-4*pi)*i`) returned unevaluated (output = input)
- **K19** (x y)^n - x^n y^n => 0 for integer n -- `simplify((x*y)^n-x^n*y^n)` -> `(x*y)^n-x^n*y^n` (expected `0`) equivalent but not simplified (ops 6 > 0)
- **K22** atan(tan(10)) => 10 - 3 pi -- `atan(tan(10))` -> `atan(tan(10))` (expected `10-3*pi`) returned unevaluated (output = input)
- **K23** atan(tan(11 + 30 i)) => 11 - 4 pi + 30 i -- `atan(tan(11+30*i))` -> `atan(tan(11+30*i))` (expected `11-4*pi+30*i`) returned unevaluated (output = input)
- **K24** Kahan: w + sqrt(w+1) sqrt(w-1), w = (z + 1/z)/2 => z (z > 1) -- `simplify((z+1/z)/2+sqrt((z+1/z)/2+1)*sqrt((z+1/z)/2-1))` -> `z/2+sqrt(z/2-1+1/(2*z))*sqrt(z/2+1+1/(2*z))+1/(2*z)` (expected `z`) equivalent but not simplified (ops 20 > 0)
- **L5** log(tan(x/2 + pi/4)) - asinh(tan x) => 0 for -pi/2 < x < pi/2 -- `simplify(ln(tan(x/2+pi/4))-asinh(tan(x)))` -> `ln(tan(x/2+pi/4))-asinh(tan(x))` (expected `0`) equivalent but not in the requested form
- **L6** d/dx of the L5 expression => 0 -- `simplify(diff(ln(tan(x/2+pi/4))-asinh(tan(x)),x))` -> `(tan(x/2+pi/4)^2/2+1/2)/tan(x/2+pi/4)-sqrt(tan(x)^2+1)` (expected `0`) equivalent but not in the requested form
- **L7** log((2 sqrt(r) + 1)/sqrt(4r + 4 sqrt(r) + 1)) => 0 -- `simplify(ln((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1)))` -> `ln((2*sqrt(r)+1)/sqrt(4*r+4*sqrt(r)+1))` (expected `0`) equivalent but not in the requested form
- **L8** (4r + 4 sqrt r + 1)^(sqrt r/(2 sqrt r + 1)) (2 sqrt r + 1)^(1/(2 sqrt r + 1)) - 2 sqrt r - 1 => 0 -- `simplify((4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))*(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))-2*sqrt(r)-1)` -> `-2*sqrt(r)+(2*sqrt(r)+1)^(1/(2*sqrt(r)+1))*(4*r+4*sqrt(r)+1)^(sqrt(r)/(2*sqrt(r)+1))-1` (expected `0`) equivalent but not in the requested form
- **L9** 2^(1-z) gamma(z) zeta(z) cos(z pi/2) - pi^z zeta(1-z) => 0 -- `simplify(2^(1-z)*gamma(z)*zeta(z)*cos(z*pi/2)-pi^z*zeta(1-z))` -> `2^(-z+1)*cos(pi*z/2)*gamma(z)*zeta(z)-pi^z*zeta(-z+1)` (expected `0`) equivalent but not in the requested form
- **M3** x^4 + x^3 + x^2 + x + 1 = 0 (exact roots) -- `csolve(x^4+x^3+x^2+x+1=0,x)` -> `[0.309016994375-0.951056516295*i,-0.809016994375-0.587785252292*i,0.587785252292*i-0.809016994375,0.309016994375+0.951056516295*i]` (expected `[exp(2*pi*i/5),exp(4*pi*i/5),exp(6*pi*i/5),exp(8*pi*i/5)]`) numeric instead of exact
- **M4** x^6 - 9x^4 - 4x^3 + 27x^2 - 36x - 23 = 0 (Rayes) -- `csolve(x^6-9*x^4-4*x^3+27*x^2-36*x-23=0,x)` -> `[-0.472129757674,2.99197185746,-2.36201133252-1.09112363597*i,1.10209028262-1.09112363597*i,1.09112363597*i-2.36201133252,1.10209028262+1.09112363597*i]` (expected `[2^(1/3)+sqrt(3),2^(1/3)-sqrt(3),sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)+i*sqrt(3)*2^(-2/3),-sqrt(3)-2^(-2/3)-i*sqrt(3)*2^(-2/3)]`) numeric instead of exact
- **M5** x^7 - 1 = 0 => 7th roots of unity -- `csolve(x^7-1=0,x)` -> `[1,-0.222520933956-0.974927912182*i,0.623489801859-0.781831482468*i,-0.900968867902-0.433883739118*i,0.433883739118*i-0.900968867902,0.623489801859+0.781831482468*i,0.974927912182*i-0.222520933956]` (expected `[1,exp(2*pi*i/7),exp(4*pi*i/7),exp(6*pi*i/7),exp(8*pi*i/7),exp(10*pi*i/7),exp(12*pi*i/7)]`) numeric instead of exact
- **M6** Liska's degree-8 polynomial => 1 +- sqrt(+-sqrt(+-4 sqrt(3) - 3) - 3)/sqrt(2) -- `csolve(x^8-8*x^7+34*x^6-92*x^5+175*x^4-236*x^3+226*x^2-140*x+46=0,x)` -> `[1.0-1.57828538819*i,1.58108852474-1.35560461551*i,0.418911475263-1.35560461551*i,1.0-0.713453035197*i,1.0+0.713453035197*i,0.418911475263+1.35560461551*i,1.58108852474+1.35560461551*i,1.0+1.578285388` (expected `[1+sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1+sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2),1-sqrt(-sqrt(-4*sqrt(3)-3)-3)/sqrt(2)]`) numeric instead of exact
- **M9** e^x = x => x = -W(-1) -- `csolve(exp(x)=x,x)` -> `ERROR 6: cannot solve` (expected `[-LambertW(-1)]`) ERROR 6: cannot solve
- **M16** asin x = atan x => 0 -- `solve(asin(x)=atan(x),x)` -> `[0.0]` (expected `[0]`) numeric instead of exact
- **M17** acos x = atan x => sqrt((sqrt(5) - 1)/2) -- `solve(acos(x)=atan(x),x)` -> `[0.786151377757]` (expected `[sqrt((sqrt(5)-1)/2)]`) numeric instead of exact
- **M25** sqrt(log x) = log(sqrt x) => {1, e^4} -- `solve(sqrt(ln(x))=ln(sqrt(x)),x)` -> `[1.0,54.5981500331]` (expected `[1,exp(4)]`) numeric instead of exact
- **M29** |2x + 5| = |x - 2| => {-1, -7} -- `solve(abs(2*x+5)=abs(x-2),x)` -> `[-7.0,-1.0]` (expected `[-1,-7]`) numeric instead of exact
- **M30** 1 - |x| = max(-x - 2, x - 2) => {-3/2, 3/2} -- `solve(1-abs(x)=max(-x-2,x-2),x)` -> `[-1.5,1.5]` (expected `[-3/2,3/2]`) numeric instead of exact
- **M31** max(2 - x^2, x) = max(-x, x^3/9) => {-1, 3} -- `solve(max(2-x^2,x)=max(-x,x^3/9),x)` -> `[-1.0,3.0]` (expected `[-1,3]`) numeric instead of exact
- **M32** max(2 - x^2, x) = x^3/9 => {-3, 3, -1.554894} -- `solve(max(2-x^2,x)=x^3/9,x)` -> `[-3.0,-1.55489408847,3.0]` (expected `[-3,3,-3*(1+sqrt(3)*sin((atan(sqrt(5)/2)-pi)/3)+cos((atan(sqrt(5)/2)-pi)/3))]`) numeric instead of exact
- **N2** x^4 - x + 1 > 0 for real x => true -- `x^4-x+1>0` -> `x^4-x+1>0` (expected `true`) returned unevaluated (output = input)
- **N3** x^4 - x + 1 > 1 for all real x => false -- `x^4-x+1>1` -> `x^4-x+1>1` (expected `false`) returned unevaluated (output = input)
- **N15** sin x < 1 => x != pi/2 + 2 n pi -- `solve(sin(x)<1,x)` -> `ERROR 6: cannot solve` (expected `sin(x)<1`) ERROR 6: cannot solve
- **N16** |2 A (cos t - 1) + 1| <= 1 => 0 <= A <= 1/(1 - cos t) -- `solve(abs(2*a*(cos(t)-1)+1)<=1,a)` -> `ERROR 6: inequality with symbolic critical points` (expected `0<=a and a<=1/(1-cos(t))`) ERROR 6: inequality with symbolic critical points
- **N17** A^2 (cos t - 4)^2 sin^2 t < 9 => |A| < 3/|(cos t - 4) sin t| -- `solve(a^2*(cos(t)-4)^2*sin(t)^2<9,a)` -> `ERROR 6: inequality with symbolic critical points` (expected `abs(a)<3/abs((cos(t)-4)*sin(t))`) ERROR 6: inequality with symbolic critical points
- **N18** x + y > 0 and x - y < 0 => |x| < y -- `solve([x+y>0,x-y<0],[x,y])` -> `ERROR 6: cannot solve this system` (expected `abs(x)<y`) ERROR 6: cannot solve this system
- **P5** [[7, 11], [3, 8]] mod 2 => [[1, 1], [1, 0]] -- `mod([[7,11],[3,8]],2)` -> `mod([[7,11],[3,8]],2)` (expected `[[1,1],[1,0]]`) returned unevaluated (output = input)
- **P9** Frobenius norm => (a^2 + b^2 + c^2)/(|a| |b| |c|) -- `factor(norm([[a/(b*c),1/c,1/b],[1/c,b/(a*c),1/a],[1/b,1/a,c/(a*b)]]))` -> `sqrt(2*abs(1/a)^2+2*abs(1/b)^2+2*abs(1/c)^2+abs(a/(b*c))^2+abs(b/(a*c))^2+abs(c/(a*b))^2)` (expected `(a^2+b^2+c^2)/(abs(a)*abs(b)*abs(c))`) equivalent but not simplified (ops 34 > 11)
- **P19** factor the 4x4 Vandermonde determinant -- `factor(det([[1,1,1,1],[w,x,y,z],[w^2,x^2,y^2,z^2],[w^3,x^3,y^3,z^3]]))` -> `-(w-y)*(w-z)*(y-z)*(x^3-x^2*y-x^2*z-w*x^2+x*y*z+w*x*y+w*x*z-w*y*z)` (expected `(w-x)*(w-y)*(w-z)*(x-y)*(x-z)*(y-z)`) factor count 4, expected 6
- **P36** matrix square root of [[10, 7], [7, 17]] -- `[[10,7],[7,17]]^(1/2)` -> `sqrt([[10,7],[7,17]])` (expected `[[10,7],[7,17]]`) function of a matrix left unevaluated
- **P37** square root of [[1,1,0],[0,1,0],[0,0,1]] => [[1,1/2,0],[0,1,0],[0,0,1]] -- `[[1,1,0],[0,1,0],[0,0,1]]^(1/2)` -> `sqrt([[1,1,0],[0,1,0],[0,0,1]])` (expected `[[1,1,0],[0,1,0],[0,0,1]]`) function of a matrix left unevaluated
- **P38** square root of the singular [[0,1,0],[0,0,0],[0,0,0]] => [[0,a,b],[0,0,0],[0,1/b,0]] -- `[[0,1,0],[0,0,0],[0,0,0]]^(1/2)` -> `sqrt([[0,1,0],[0,0,0],[0,0,0]])` (expected `[[0,1,0],[0,0,0],[0,0,0]]`) function of a matrix left unevaluated
- **R3** sum((-1)^k binomial(2n, k)^2, k = 0..2n) => (-1)^n binomial(2n, n) -- `sum((-1)^k*binomial(2*n,k)^2,k,0,2*n)` -> `sum((-1)^k*binomial(2*n,k)^2,k,0,2*n)` (expected `(-1)^n*binomial(2*n,n)`) unevaluated sum
- **R5** Dixon's identity => (a+b+c)!/(a! b! c!) -- `sum((-1)^k*binomial(a+b,a+k)*binomial(b+c,b+k)*binomial(c+a,c+k),k,-a,a)` -> `sum((-1)^k*binomial(a+b,a+k)*binomial(a+c,c+k)*binomial(b+c,b+k),k,-a,a)` (expected `factorial(a+b+c)/(factorial(a)*factorial(b)*factorial(c))`) unevaluated sum
- **R6** telescoping sum(g(k+1) - g(k), k = 0..n) => g(n+1) - g(0) -- `sum(g(k+1)-g(k),k,0,n)` -> `sum(g(k+1)-g(k),k,0,n)` (expected `g(n+1)-g(0)`) unevaluated sum
- **R8** sum(k^2 binomial(n, k), k = 1..n) => n (n+1) 2^(n-2) -- `sum(k^2*binomial(n,k),k,1,n)` -> `sum(binomial(n,k)*k^2,k,1,n)` (expected `n*(n+1)*2^(n-2)`) unevaluated sum
- **R9** sum(binomial(n, k)/(k + 1), k = 0..n) => (2^(n+1) - 1)/(n + 1) -- `sum(binomial(n,k)/(k+1),k,0,n)` -> `sum(binomial(n,k)/(k+1),k,0,n)` (expected `(2^(n+1)-1)/(n+1)`) unevaluated sum
- **R10** Vandermonde: sum(binomial(n, k) binomial(m, r - k), k = 0..r) => binomial(n + m, r) -- `sum(binomial(n,k)*binomial(m,r-k),k,0,r)` -> `sum(binomial(m,r-k)*binomial(n,k),k,0,r)` (expected `binomial(n+m,r)`) unevaluated sum
- **R13** sum(sin(k x), k = 1..n) => cot(x/2)/2 - cos((2n+1) x/2)/(2 sin(x/2)) -- `sum(sin(k*x),k,1,n)` -> `sum(sin(k*x),k,1,n)` (expected `cot(x/2)/2-cos((2*n+1)*x/2)/(2*sin(x/2))`) unevaluated sum
- **R14** sum(sin((2k - 1) x), k = 1..n) => sin(n x)^2/sin x -- `sum(sin((2*k-1)*x),k,1,n)` -> `sum(sin((2*k-1)*x),k,1,n)` (expected `sin(n*x)^2/sin(x)`) unevaluated sum
- **R15** sum(binomial(n - k, k), k = 0..floor(n/2)) => fib(n + 1) -- `sum(binomial(n-k,k),k,0,floor(n/2))` -> `sum(binomial(n-k,k),k,0,floor(n/2))` (expected `fibonacci(n+1)`) unevaluated sum
- **R18** sum(1/(2^k k^2), k = 1..inf) => pi^2/12 - log(2)^2/2 -- `sum(1/(2^k*k^2),k,1,inf)` -> `sum(2^(-k)/k^2,k,1,inf)` (expected `pi^2/12-ln(2)^2/2`) unevaluated sum
- **R19** sum(1/((3k+1)(3k+2)(3k+3)), k = 0..inf) => sqrt(3) pi/12 - log(3)/4 -- `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` -> `sum(1/((3*k+1)*(3*k+2)*(3*k+3)),k,0,inf)` (expected `sqrt(3)*pi/12-ln(3)/4`) unevaluated sum
- **R20** sum(binomial(n, 4k), k = 0..inf) => (2^(n-1) + 2^(n/2) cos(n pi/4))/2 -- `sum(binomial(n,4*k),k,0,inf)` -> `sum(binomial(n,4*k),k,0,inf)` (expected `(2^(n-1)+2^(n/2)*cos(n*pi/4))/2`) unevaluated sum
- **R21** sum(1/(sqrt(k (k+1)) (sqrt k + sqrt(k+1))), k = 1..inf) => 1 -- `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` -> `sum(1/(sqrt(k*(k+1))*(sqrt(k)+sqrt(k+1))),k,1,inf)` (expected `1`) unevaluated sum
- **R22** double sum (capping) => 1/sqrt((1 - x y)^2 - 4 x^2) -- `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` -> `sum(sum(binomial(n,k)*binomial(n-k,n-2*k)*x^n*y^(n-2*k),k,0,floor(n/2)),n,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R23** equivalent double sum -- `sum(sum(n!/(k!^2*(n-2*k)!)*(x/y)^k*(x*y)^(n-k),n,2*k,inf),k,0,inf)` -> `sum(sum(n!*(x/y)^k*(x*y)^(n-k)/(k!^2*(n-2*k)!),n,2*k,inf),k,0,inf)` (expected `1/sqrt((1-x*y)^2-4*x^2)`) unevaluated sum
- **R24** sum(product(k/(2k - 1), k = 1..m), m = 2..inf) => pi/2 -- `sum(product(k/(2*k-1),k,1,m),m,2,inf)` -> `sum(product(k/(2*k-1),k,1,m),m,2,inf)` (expected `pi/2`) unevaluated product,sum
- **S1** product(gamma(k/3), k = 1..8) => 640 pi^3/(2187 sqrt 3) -- `product(gamma(k/3),k,1,8)` -> `gamma(1/3)*gamma(2/3)*gamma(4/3)*gamma(5/3)*gamma(7/3)*gamma(8/3)` (expected `640*pi^3/(2187*sqrt(3))`) equivalent but not simplified (ops 17 > 6)
- **S5** product((2k - 1)/(2k), k = 1..n) => binomial(2n, n)/2^(2n) -- `product((2*k-1)/(2*k),k,1,n)` -> `product((2*k-1)/(2*k),k,1,n)` (expected `binomial(2*n,n)/4^n`) unevaluated product
- **S6** product(x^2 - 2x cos(k pi/n) + 1, k = 1..n-1) => (x^(2n) - 1)/(x^2 - 1) -- `product(x^2-2*x*cos(k*pi/n)+1,k,1,n-1)` -> `product(x^2-2*cos(k*pi/n)*x+1,k,1,n-1)` (expected `(x^(2*n)-1)/(x^2-1)`) unevaluated product
- **S7** product((k^3 - 1)/(k^3 + 1), k = 2..inf) => 2/3 -- `product((k^3-1)/(k^3+1),k,2,inf)` -> `product((k^3-1)/(k^3+1),k,2,inf)` (expected `2/3`) unevaluated product
- **S8** product(1 - 1/(2k)^2, k = 1..inf) => 2/pi -- `product(1-1/(2*k)^2,k,1,inf)` -> `product(1-1/(4*k^2),k,1,inf)` (expected `2/pi`) unevaluated product
- **S9** product(1 + (-1)^(k+1)/(2k - 1), k = 1..inf) => sqrt(2) -- `product(1+(-1)^(k+1)/(2*k-1),k,1,inf)` -> `product((-1)^(k+1)/(2*k-1)+1,k,1,inf)` (expected `sqrt(2)`) unevaluated product
- **S10** product((k (k+1) + 1 + i)/(k (k+1) + 1 - i), k = 0..inf) => -1 -- `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` -> `product((k*(k+1)+1+i)/(k*(k+1)+1-i),k,0,inf)` (expected `-1`) unevaluated product
- **T5** Gruntz: limit((exp(x e^-x/(e^-x + e^(-2x^2/(x+1)))) - e^x)/x, x, inf) => -e^2 -- `limit((exp(x*exp(-x)/(exp(-x)+exp(-2*x^2/(x+1))))-exp(x))/x,x,inf)` -> `ERROR 6: limit could not be determined` (expected `-exp(2)`) ERROR 6: limit could not be determined
- **T7** limit(n!^(1/n)/n, n, inf) => 1/e -- `limit(n!^(1/n)/n,n,inf)` -> `ERROR 6: limit could not be determined` (expected `exp(-1)`) ERROR 6: limit could not be determined
- **T8** limit(gamma(n + 1)^(1/n)/n, n, inf) => 1/e -- `limit(gamma(n+1)^(1/n)/n,n,inf)` -> `ERROR 6: limit could not be determined` (expected `exp(-1)`) ERROR 6: limit could not be determined
- **T9** limit(gamma(z + a)/gamma(z) exp(-a log z), z, inf) => 1 (a > 0) -- `limit(gamma(z+a)/gamma(z)*exp(-a*ln(z)),z,inf)` -> `ERROR 6: limit could not be determined` (expected `1`) ERROR 6: limit could not be determined
- **T10** same for a < 0 => 1 -- `limit(gamma(z+a)/gamma(z)*exp(-a*ln(z)),z,inf)` -> `ERROR 6: limit could not be determined` (expected `1`) ERROR 6: limit could not be determined
- **T11** limit(2F1(1, k; 1; z/k), k, inf) => e^z -- `limit(hypergeom([1,k],[1],z/k),k,inf)` -> `ERROR 6: limit could not be determined` (expected `exp(z)`) ERROR 6: limit could not be determined
- **T12** limit(zeta(x) - 1/(x - 1), x, 1) => Euler's gamma -- `limit(zeta(x)-1/(x-1),x,1)` -> `ERROR 6: limit could not be determined` (expected `euler_gamma`) ERROR 6: limit could not be determined
- **T13** limit(n^x/(x product(1 + x/k, k = 1..n)), n, inf) => gamma(x) -- `limit(n^x/(x*product(1+x/k,k,1,n)),n,inf)` -> `ERROR 6: limit could not be determined` (expected `gamma(x)`) ERROR 6: limit could not be determined
- **U2** derivative of the piecewise function (-x for x < 0, x otherwise) -- `diff(piecewise(x<0,-x,x),x)` -> `diff(piecewise(x<0,-x,x),x)` (expected `piecewise(x<0,-1,1)`) unevaluated diff
- **U3b** same f: f'(1) => 3 -- `subst(diff(f(x),x),x,1)` -> `subst(diff(piecewise(x=1,x^2-1,x^3),x),x,1)` (expected `3`) unevaluated diff
- **U5** chain rule: d^2/dt^2 f(g(t)) => f''(g) g'^2 + f'(g) g'' -- `diff(f(g(t)),t,2)` -> `diff(diff(f(g(t)),t),t)` (expected ``) unevaluated diff
- **U6** d/dx int(f(y), y = g(x)..h(x)) => f(h(x)) h'(x) - f(g(x)) g'(x) -- `diff(integrate(f(y),y,g(x),h(x)),x)` -> `diff(integrate(f(y),y,g(x),h(x)),x)` (expected ``) unevaluated diff,integrate
- **U9** f_x + f_y with f(x, y) = g(x^2 + y^2) => 2 (x + y) g'(x^2 + y^2) -- `factor(diff(g(x^2+y^2),x)+diff(g(x^2+y^2),y))` -> `diff(g(x^2+y^2),x)+diff(g(x^2+y^2),y)` (expected ``) no chain rule for unknown functions
- **V3** int of the piecewise function (-x for x < 0, x otherwise) -- `integrate(piecewise(x<0,-x,x),x)` -> `integrate(piecewise(x<0,-x,x),x)` (expected `piecewise(x<0,-x,x)`) unevaluated integrate
- **V4** int -x H(-x) + x H(x) dx -- `integrate(-x*heaviside(-x)+x*heaviside(x),x)` -> `integrate(heaviside(x)*x-heaviside(-x)*x,x)` (expected `-x*heaviside(-x)+x*heaviside(x)`) unevaluated integrate
- **V5** int 1/(x^3 + 2) dx -- `integrate(1/(x^3+2),x)` -> `integrate(1/(x^3+2),x)` (expected `1/(x^3+2)`) unevaluated integrate
- **V6** int 2^x/sqrt(1 + 4^x) dx => asinh(2^x)/log 2 -- `integrate(2^x/sqrt(1+4^x),x)` -> `integrate(2^x/sqrt(4^x+1),x)` (expected `2^x/sqrt(1+4^x)`) unevaluated integrate
- **V15** int log|x^2 - a^2| dx -- `integrate(ln(abs(x^2-a^2)),x)` -> `integrate(ln(abs(x^2-a^2)),x)` (expected `ln(abs(x^2-a^2))`) unevaluated integrate
- **V18** int (f' g - f g')/(f^2 - g^2) dx => log((f - g)/(f + g))/2 -- `integrate((diff(f(x),x)*g(x)-f(x)*diff(g(x),x))/(f(x)^2-g(x)^2),x)` -> `integrate((diff(f(x),x)*g(x)-diff(g(x),x)*f(x))/(f(x)^2-g(x)^2),x)` (expected ``) unevaluated integrate
- **W1** int 1/(x - a), x = a-1..a+1 => 0 (principal value) -- `integrate(1/(x-a),x,a-1,a+1)` -> `ERROR 2: integral diverges (pole inside the interval)` (expected `0`) divergent as an improper integral (as for numeric bounds); the principal value 0 is not computed
- **W3** int sqrt(x + 1/x - 2), x = 0..1 => 4/3 -- `integrate(sqrt(x+1/x-2),x,0,1)` -> `1.33333332244` (expected `4/3`) numeric instead of exact
- **W4** int sqrt(x + 1/x - 2), x = 1..2 => (4 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,1,2)` -> `0.390524291751` (expected `(4-sqrt(8))/3`) numeric instead of exact
- **W5** int sqrt(x + 1/x - 2), x = 0..2 => (8 - sqrt 8)/3 -- `integrate(sqrt(x+1/x-2),x,0,2)` -> `1.72385761419` (expected `(8-sqrt(8))/3`) numeric instead of exact
- **W6** int sqrt(2 - 2 cos 2x)/2, x = -3 pi/4..-pi/4 => sqrt 2 (Kahan) -- `integrate(sqrt(2-2*cos(2*x))/2,x,-3*pi/4,-pi/4)` -> `1.41421356237` (expected `sqrt(2)`) numeric instead of exact
- **W7** int cos x/(x^2 + a^2), x = -inf..inf => pi e^(-a)/a (a > 0) -- `integrate(cos(x)/(x^2+a^2),x,-inf,inf)` -> `integrate(cos(x)/(x^2+a^2),x,-inf,inf)` (expected `pi*exp(-a)/a`) unevaluated integrate
- **W8** int t^(a-1)/(1 + t), t = 0..inf => pi/sin(pi a) (0 < a < 1) -- `integrate(t^(a-1)/(1+t),t,0,inf)` -> `integrate(t^(a-1)/(t+1),t,0,inf)` (expected `pi/sin(pi*a)`) unevaluated integrate
- **W9** PV int 5x^3/(1 + x + x^2 + x^3 + x^4), -inf..inf => -2 pi (sin(pi/5) + sin(2 pi/5)) -- `integrate(5*x^3/(1+x+x^2+x^3+x^4),x,-inf,inf)` -> `undef` (expected `-2*pi*(sin(pi/5)+sin(2*pi/5))`) improper integral reported as undefined; no principal value (defensible)
- **W11** int sqrt(1 - x^2)/(1 + x^2), x = -1..1 => pi (sqrt 2 - 1) -- `integrate(sqrt(1-x^2)/(1+x^2),x,-1,1)` -> `1.30129028457` (expected `pi*(sqrt(2)-1)`) numeric instead of exact
- **W12** int x exp(-p x^2 + 2 q x), x = -inf..inf => q/p sqrt(pi/p) e^(q^2/p) -- `integrate(x*exp(-p*x^2+2*q*x),x,-inf,inf)` -> `integrate(exp(-p*x^2+2*q*x)*x,x,-inf,inf)` (expected `q/p*sqrt(pi/p)*exp(q^2/p)`) unevaluated integrate
- **W13** int 1/log t + 1/(1 - t) - log(log(1/t)), t = 0..1 => 2 gamma -- `integrate(1/ln(t)+1/(1-t)-ln(ln(1/t)),t,0,1)` -> `1.1544313298` (expected `2*euler_gamma`) numeric instead of exact
- **W14** int sin t/t exp(2 i t), t = -inf..inf => 0 -- `integrate(sin(t)/t*exp(2*i*t),t,-inf,inf)` -> `integrate(exp(2*i*t)*sin(t)/t,t,-inf,inf)` (expected `0`) unevaluated integrate
- **W15** int log(gamma(x)) cos(6 pi x), x = 0..1 => 1/12 -- `integrate(ln(gamma(x))*cos(6*pi*x),x,0,1)` -> `0.0833333333318` (expected `1/12`) numeric instead of exact
- **W22** int s(t) cos t, t = 0..u for the step s = 1 on [1, 2] -- `integrate(piecewise(t>=1 and t<=2,1,0)*cos(t),t,0,u)` -> `integrate(cos(t)*piecewise(t>=1 and t<=2,1,0),t,0,u)` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) unevaluated integrate
- **W23** same with s(t) = H(t - 1) - H(t - 2) -- `integrate((heaviside(t-1)-heaviside(t-2))*cos(t),t,0,u)` -> `integrate(cos(t)*(heaviside(t-1)-heaviside(t-2)),t,0,u)` (expected `piecewise(u<1,0,u<=2,sin(u)-sin(1),sin(2)-sin(1))`) unevaluated integrate
- **W24** int(int(x/(x^2 + y^2), y = -inf..inf), x = a..b), a, b > 0 => (b - a) pi -- `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` (expected `pi*(b-a)`) unevaluated integrate
- **W26** y first, a < 0 < b => (|b| - |a|) pi -- `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` (expected `pi*(b+a)`) unevaluated integrate
- **W28** y first, a, b < 0 => (|b| - |a|) pi -- `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` -> `integrate(integrate(x/(x^2+y^2),y,-inf,inf),x,a,b)` (expected `pi*(a-b)`) unevaluated integrate
- **W31** int(int(sin a sin y/sqrt(1 - sin^2 a sin^2 x sin^2 y), x = 0..pi/2), y = 0..pi/2) => pi a/2 -- `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,pi/2),y,0,pi/2)` -> `integrate(integrate(sin(a)*sin(y)/sqrt(1-sin(a)^2*sin(x)^2*sin(y)^2),x,0,pi/2),y,0,pi/2)` (expected `pi*a/2`) unevaluated integrate
- **W32** int(int(|y - x^2|, y = 0..2), x = -1..1) => 46/15 -- `integrate(integrate(abs(y-x^2),y,0,2),x,-1,1)` -> `3.06666666667` (expected `46/15`) numeric instead of exact
- **X7** series of f'(a x) a + g(b x) + int(h(c y), y = 0..x) at x = d -- `series(diff(f(a*x),x)+g(b*x)+integrate(h(c*y),y,0,x),x,d,2)` -> `ERROR 6: series expansion failed` (expected ``) ERROR 6: series expansion failed
- **X14** log(x)^a exp(-b x) at x = 1 => (x-1)^a e^-b (1 - (a + 2b)(x - 1)/2) -- `series(ln(x)^a*exp(-b*x),x,1,2)` -> `ERROR 6: series expansion failed` (expected `(x-1)^a*exp(-b)*(1-(a+2*b)*(x-1)/2)`) ERROR 6: series expansion failed
- **X16** Wallis: binomial(2n, n)/2^(2n) => 1/sqrt(pi n) -- `series(binomial(2*n,n)/4^n,n,inf,1)` -> `ERROR 6: series expansion failed` (expected `1/sqrt(pi*n)`) ERROR 6: series expansion failed
- **X17** e^x int(e^-t/t, t = x..inf) => 1/x - 1/x^2 + 2/x^3 - 6/x^4 -- `series(exp(x)*integrate(exp(-t)/t,t,x,inf),x,inf,5)` -> `ERROR 6: series expansion failed` (expected `1/x-1/x^2+2/x^3-6/x^4`) ERROR 6: series expansion failed
- **Y3** laplace(sinh(w t) cosh(w t)) => w/(s^2 - 4 w^2) -- `laplace(sinh(w*t)*cosh(w*t),t,s)` -> `ERROR 6: laplace: transform not found` (expected `w/(s^2-4*w^2)`) ERROR 6: laplace: transform not found
- **Y4** laplace(erf(3/sqrt t)) => 1/s - e^(-6 sqrt s)/s -- `laplace(erf(3/sqrt(t)),t,s)` -> `ERROR 6: laplace: transform not found` (expected `1/s-exp(-6*sqrt(s))/s`) ERROR 6: laplace: transform not found
- **Y5** y'' + y = 4 (H(t - 1) - H(t - 2)), y(0) = 1, y'(0) = 0 -- `dsolve([y''+y=4*(heaviside(x-1)-heaviside(x-2)),y(0)=1,y'(0)=0],y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y-4*(sp.Heaviside(x-1)-sp.Heaviside(x-2))`) ERROR 6: dsolve: cannot solve this equation
- **Y6** inverse Laplace of the transformed solution of Y5 -- `ilaplace(s/(s^2+1)+4*(1/s-s/(s^2+1))*(exp(-s)-exp(-2*s)),s,t)` -> `ERROR 6: ilaplace: not rational` (expected `cos(t)+4*((1-cos(t-1))*heaviside(t-1)-(1-cos(t-2))*heaviside(t-2))`) ERROR 6: ilaplace: not rational
- **Y7** Laplace transform of a square wave (infinite sum of steps) -- `laplace(1+2*sum((-1)^n*heaviside(t-n*a),n,1,inf),t,s)` -> `ERROR 6: laplace: transform not found` (expected `0`) ERROR 6: laplace: transform not found
- **Y11** Mellin transform of 1/(1 - x) => pi cot(pi s) (PV) -- `integrate(x^(s-1)/(1-x),x,0,inf)` -> `integrate(x^(s-1)/(-x+1),x,0,inf)` (expected `pi*cot(pi*s)`) unevaluated integrate
- **Z6** same via the Laplace transform: ilaplace(2/(s^2 + 4)^2) -- `ilaplace(2/(s^2+4)^2,s,t)` -> `ERROR 6: ilaplace: repeated quadratic factors` (expected `sin(2*t)/8-t*cos(2*t)/4`) ERROR 6: ilaplace: repeated quadratic factors
- **Z11** nonlinear y'' + y y'^3 = 0 => y^3/6 + A y = x + B -- `dsolve(y''+y*y'^3=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y*Y1**3`) ERROR 6: dsolve: cannot solve this equation
- **Z12** same with y(0) = 0, y'(0) = 2 => (3x + sqrt(1 + 9x^2))^(1/3) - 1/(3x + sqrt(1 + 9x^2))^(1/3) -- `dsolve([y''+y*y'^3=0,y(0)=0,y'(0)=2],y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y*Y1**3`) ERROR 6: dsolve: cannot solve this equation
- **Z14** BVP y'' + k^2 y = 0, y(0) = 0, y'(1) = 0 => A sin(k x) with cos k = 0 -- `dsolve([y''+k^2*y=0,y(0)=0,y'(1)=0],y,x)` -> `0` (expected ``) only the trivial solution; no eigenvalue condition cos(k) = 0
- **Z15** y'' + y'/x + (1 - v^2/x) y/(4x) = 0 => Z_v(sqrt x) -- `dsolve(y''+y'/x+(1-v^2/x)*y/(4*x)=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected `Y2+Y1/x+(1-v**2/x)*Y/(4*x)`) ERROR 6: dsolve: cannot solve this equation
- **Z17** discontinuous ODE y'' + sign(t - T) y = 0 -- `dsolve(y''+sign(x-q)*y=0,y,x)` -> `ERROR 6: dsolve: cannot solve this equation` (expected ``) ERROR 6: dsolve: cannot solve this equation

## UNSUPPORTED (136)

- **A4** xor(xor(x, y), y) => x -- `xor(xor(x,y),y)` -> `ERROR 2: expected an integer` (expected `x`) xor is bitwise only; no logical xor
- **A5** (w and x) implies (y and z) => not(w and x) or (y and z) -- `None` -> `` (expected ``) no logical implication operator
- **A6** x iff y => (x and y) or not(x or y) -- `None` -> `` (expected ``) no logical equivalence operator
- **B1** union of [a,b,b,c,c,c], [d,c,b], [b,e,b] => {a,b,c,d,e} -- `union([a,b,b,c,c,c],[d,c,b],[b,e,b])` -> `union([a,b,b,c,c,c],[d,c,b],[b,e,b])` (expected `[a,b,c,d,e]`) unknown function(s) echoed: union
- **B2** intersection => {b} -- `intersect([a,b,b,c,c,c],[d,c,b],[b,e,b])` -> `intersect([a,b,b,c,c,c],[d,c,b],[b,e,b])` (expected `[b]`) unknown function(s) echoed: intersect
- **B3** set difference {a,b,c,d,e} - {b} => {a,c,d,e} -- `setdiff([a,b,c,d,e],[b])` -> `setdiff([a,b,c,d,e],[b])` (expected `[a,c,d,e]`) unknown function(s) echoed: setdiff
- **B4** Cartesian product {a,b} x {c,d} -- `cartprod([a,b],[c,d])` -> `cartprod([a,b],[c,d])` (expected `[[a,c],[a,d],[b,c],[b,d]]`) unknown function(s) echoed: cartprod
- **C3** double factorials [10!!, 9!!] => [3840, 945] -- `[10!!,9!!]` -> `ERROR 4: factorial argument too large` (expected `[3840,945]`) no double factorial: n!! parses as (n!)!
- **D5** natural cubic spline through (1,1),(2,4),(4,2),(5,3) at 3 => 27/8 -- `None` -> `` (expected ``) no spline interpolation
- **D8** Horner form of sum(a[i] x^i, i=1..5) -- `horner(a1*x+a2*x^2+a3*x^3+a4*x^4+a5*x^5,x)` -> `a5*x^5+a4*x^4+a3*x^3+a2*x^2+a1*x` (expected `x*(a1+x*(a2+x*(a3+x*(a4+a5*x))))`) horner() only evaluates at a point; no Horner-form rewriting
- **D12** interval arithmetic ([-4,2] x + [1,3])^2 -- `None` -> `` (expected ``) no interval arithmetic
- **E3** first quartile of [1..8] => 2 or 5/2 -- `quartile1([1,2,3,4,5,6,7,8])` -> `quartile1([1,2,3,4,5,6,7,8])` (expected `[2,5/2]`) unknown function(s) echoed: quartile1
- **E4** mode([3, 7, 11, 7, 3, 5, 7]) => 7 -- `mode([3,7,11,7,3,5,7])` -> `mode([3,7,11,7,3,5,7])` (expected `7`) unknown function(s) echoed: mode
- **E5** unbiased sample standard deviation of [1..5] => sqrt(5/2) -- `stddev([1,2,3,4,5])` -> `sqrt(2)` (expected `sqrt(5/2)`) stddev is the population standard deviation; no sample (n-1) variant
- **E12** linear regression => y = 0.7365 x + 6.964 -- `linreg([3.33,3.25,3.92,3.50,4.33,4.92,6.08,7.42,8.33,8.00,9.25,10.75],[8.61,9.40,9.86,9.91,10.53,10.61,10.59,13.28,12.76,13.44,14.27,14.13])` -> `linreg([3.33,3.25,3.92,3.5,4.33,4.92,6.08,7.42,8.33,8.0,9.25,10.75],[8.61,9.4,9.86,9.91,10.53,10.61,10.59,13.28,12.76,13.44,14.27,14.13])` (expected `0.7365*x+6.964`) unknown function(s) echoed: linreg
- **E13** multiple linear regression => -16278.7 + 960.925 x1 + 2975.66 x2 -- `None` -> `` (expected ``) no multiple regression in the CAS
- **E14** L1 (least absolute deviations) regression -- `None` -> `` (expected ``) no L1 regression
- **E15** nonlinear least squares fit -- `None` -> `` (expected ``) no nonlinear regression in the CAS
- **F1** Pochhammer (a)_3 => a (a+1) (a+2) -- `pochhammer(a,3)` -> `pochhammer(a,3)` (expected `a*(a+1)*(a+2)`) unknown function(s) echoed: pochhammer
- **F3** 2^n n! (2n-1)!! => (2n)! -- `simplify(2^n*n!*factorial2(2*n-1))` -> `2^n*n!*factorial2(2*n-1)` (expected `factorial(2*n)`) unknown function(s) echoed: factorial2
- **F6** number of partitions of 4 => 5 -- `partition(4)` -> `partition(4)` (expected `5`) unknown function(s) echoed: partition
- **F7** Stirling number of the first kind S1(5, 2) => -50 -- `stirling1(5,2)` -> `stirling1(5,2)` (expected `-50`) unknown function(s) echoed: stirling1
- **G2** primitive root of 191 => 19 -- `primroot(191)` -> `primroot(191)` (expected `19`) unknown function(s) echoed: primroot
- **G3** (a + b)^p mod p => a^p + b^p for p prime -- `None` -> `` (expected ``) no modular polynomial arithmetic with symbolic modulus
- **G4** 9 x = 15 mod 21 => x = 4 mod 7 -- `None` -> `` (expected ``) no modular equation solving
- **G5** 7 x = 22 mod 39 => x = 31 mod 39 -- `None` -> `` (expected ``) no modular equation solving
- **G6** x^2 + x + 4 = 0 mod 8 => x = {3, 4} mod 8 -- `None` -> `` (expected ``) no modular equation solving
- **G7** x^3 + 2x^2 + 5x + 6 = 0 mod 11 => x = 3 mod 11 -- `None` -> `` (expected ``) no modular equation solving
- **G9** linear system mod 7 => x = 1, y = 2 -- `None` -> `` (expected ``) no modular equation solving
- **G10** 2x + 3y = 1 mod 5 => 5 solutions -- `None` -> `` (expected ``) no modular equation solving
- **G11** 2x + 3y = 1 mod 6 => 6 solutions -- `None` -> `` (expected ``) no modular equation solving
- **G12** Diophantine x^4 + 9 = y^2 => (2, 5) -- `None` -> `` (expected ``) no Diophantine solver
- **G13** Diophantine x^2 + 4 = y^3 => (11, 5) -- `None` -> `` (expected ``) no Diophantine solver
- **G14** Pythagorean system => (3,4,5,12,13), ... -- `None` -> `` (expected ``) no Diophantine solver
- **G15** rational approximation of sqrt(3) within 1/500 => 26/15 -- `None` -> `` (expected ``) no best rational approximation with tolerance
- **G16** continued fraction of pi => [3; 7, 15, 1, 292, ...] -- `contfrac(pi,5)` -> `contfrac(pi,5)` (expected `[3,7,15,1,292]`) unknown function(s) echoed: contfrac
- **G17** continued fraction of sqrt(23) => [4; 1, 3, 1, 8, ...] -- `contfrac(sqrt(23),5)` -> `contfrac(sqrt(23),5)` (expected `[4,1,3,1,8]`) unknown function(s) echoed: contfrac
- **G18** continued fraction of the golden ratio => [1; 1, 1, ...] -- `contfrac((1+sqrt(5))/2,5)` -> `contfrac(sqrt(5)/2+1/2,5)` (expected `[1,1,1,1,1]`) unknown function(s) echoed: contfrac
- **G19** continued fraction of tanh(1/(2x)) => [0; 2x, 6x, 10x, ...] -- `None` -> `` (expected ``) no continued fractions of symbolic expressions
- **G20** continued fraction of sqrt(x^2+1) - x => [0; 2x, 2x, ...] -- `None` -> `` (expected ``) no continued fractions of symbolic expressions
- **H24** factor x^4 - 3x^2 + 1 mod 5 => (x-2)^2 (x+2)^2 -- `None` -> `` (expected ``) no factorization over finite fields
- **H25** factor x^11 + x + 1 mod 65537 -- `None` -> `` (expected ``) no factorization over finite fields
- **H30** factor 4x^2 - 21xy + 20y^2 mod 3 => (x + y)(x - y) -- `None` -> `` (expected ``) no factorization over finite fields
- **I6** |cos x|, |sin x| for -3 pi < x < -5/2 pi => -cos x, -sin x -- `[abs(cos(x)),abs(sin(x))]` -> `[abs(cos(x)),abs(sin(x))]` (expected `[-cos(x),-sin(x)]`) assumptions only of the form x > 0, x < 0, integer
- **I9** cos(3x)/cos(x) via user rewrite rules -- `None` -> `` (expected ``) no user-defined rewrite rules
- **J1** Bernoulli B_16 => -3617/510 -- `bernoulli(16)` -> `bernoulli(16)` (expected `-3617/510`) unknown function(s) echoed: bernoulli
- **J2** d/dk E(phi, k) => (E(phi, k) - F(phi, k))/k -- `diff(ellipticE(p,k^2),k)` -> `diff(ellipticE(p,k^2),k)` (expected `(elliptic_e(p,k^2)-elliptic_f(p,k^2))/k`) unknown function(s) echoed: ellipticE
- **J3** d/du dn(u, k) => -k^2 sn(u, k) cn(u, k) -- `diff(jacobidn(u,k),u)` -> `diff(jacobidn(u,k),u)` (expected `0`) unknown function(s) echoed: jacobidn
- **J5** psi(1/3) => -gamma - pi/(2 sqrt 3) - 3/2 log 3 -- `digamma(1/3)` -> `digamma(1/3)` (expected `-euler_gamma-pi/(2*sqrt(3))-3*ln(3)/2`) unknown function(s) echoed: digamma
- **J6** J_2(1 + i) => 0.04158 + 0.24740 i -- `approx(besselj(2,1+i))` -> `besselj(2.0,1.0+i)` (expected `0.04157988694396212+0.24739764151330632*i`) unknown function(s) echoed: besselj
- **J7** J_{-5/2}(pi/2) => 12/pi^2 -- `besselj(-5/2,pi/2)` -> `besselj(-5/2,pi/2)` (expected `12/pi^2`) unknown function(s) echoed: besselj
- **J8** J_{3/2}(z) => sqrt(2/(pi z)) (sin z/z - cos z) -- `besselj(3/2,z)` -> `besselj(3/2,z)` (expected `sqrt(2/(pi*z))*(sin(z)/z-cos(z))`) unknown function(s) echoed: besselj
- **J9** d/dz J_0(z) => -J_1(z) -- `diff(besselj(0,z),z)` -> `diff(besselj(0,z),z)` (expected `-besselj(1,z)`) unknown function(s) echoed: besselj
- **J10** associated Legendre P^mu_nu(0) -- `legendrep(n,m,0)` -> `legendrep(n,m,0)` (expected `0`) unknown function(s) echoed: legendrep
- **J11** P^1_3(x) => -3/2 sqrt(1 - x^2) (5 x^2 - 1) -- `legendrep(3,1,x)` -> `legendrep(3,1,x)` (expected `-3/2*sqrt(1-x^2)*(5*x^2-1)`) unknown function(s) echoed: legendrep
- **J12** T_1008 - 2x T_1007 + T_1006 => 0 -- `normal(chebyshevt(1008,x)-2*x*chebyshevt(1007,x)+chebyshevt(1006,x))` -> `-2*chebyshevt(1007,x)*x+chebyshevt(1006,x)+chebyshevt(1008,x)` (expected `0`) unknown function(s) echoed: chebyshevt
- **J13** T_n(-1) => (-1)^n -- `chebyshevt(n,-1)` -> `chebyshevt(n,-1)` (expected `(-1)^n`) unknown function(s) echoed: chebyshevt
- **J14** 2F1(1/2, 1/2; 3/2; z^2) => asin(z)/z -- `hypergeom([1/2,1/2],[3/2],z^2)` -> `hypergeom([1/2,1/2],[3/2],z^2)` (expected `asin(z)/z`) unknown function(s) echoed: hypergeom
- **J15** 2F1((n+2)/2, -(n-2)/2; 3/2; sin^2 z) => sin(n z)/(n sin z cos z) -- `hypergeom([(n+2)/2,-(n-2)/2],[3/2],sin(z)^2)` -> `hypergeom([n/2+1,-n/2+1],[3/2],sin(z)^2)` (expected `sin(n*z)/(n*sin(z)*cos(z))`) unknown function(s) echoed: hypergeom
- **J18** define an antisymmetric function -- `None` -> `` (expected ``) no symmetry declarations for functions
- **K1** [Re(x + i y), Im(x + i y)] for complex x, y => [Re x - Im y, Im x + Re y] -- `[re(x+i*y),im(x+i*y)]` -> `[x,y]` (expected `[re(x)-im(y),im(x)+re(y)]`) symbols are real by default and cannot be declared complex
- **K11** sqrt(e^z) - e^(z/2) for complex z (nonzero in general) -- `None` -> `` (expected ``) symbols cannot be declared complex
- **K14** log(e^z) for complex z -- `None` -> `` (expected ``) symbols cannot be declared complex
- **K17** (x y)^(1/n) - x^(1/n) y^(1/n) for complex x, y -- `None` -> `` (expected ``) symbols cannot be declared complex
- **K21** atan(tan(z)) => z for -pi/2 < z < pi/2 -- `simplify(atan(tan(z)))` -> `atan(tan(z))` (expected `z`) assumptions only of the form x > 0, x < 0, integer
- **M34** f(x)^2 + f(x) - 2 = 0 solved for f(x) => {1, -2} -- `solve(f(x)^2+f(x)-2=0,f(x))` -> `ERROR 2: solve: expected a variable` (expected `[1,-2]`) cannot solve for a function application f(x)
- **N4** |x| < 1 => -1 < x < 1 (true) -- `-1<x and x<1` -> `-1<x and x<1` (expected `true`) setup line assume(abs(x)<1) failed: assume: only comparisons with 0 are supported
- **N5** x > y > 0 => 2 x^2 > 2 y^2 -- `2*x^2>2*y^2` -> `2*x^2>2*y^2` (expected `true`) setup line assume(x>y) failed: assume: only comparisons with 0 are supported
- **N6** x > y > 0, k > 0 => k x^2 > k y^2 -- `k*x^2>k*y^2` -> `k*x^2>k*y^2` (expected `true`) setup line assume(x>y) failed: assume: only comparisons with 0 are supported
- **N7** x > y > 0, k, n > 0 => k x^n > k y^n -- `k*x^n>k*y^n` -> `k*x^n>k*y^n` (expected `true`) setup line assume(x>y) failed: assume: only comparisons with 0 are supported
- **N8** x > 1, y >= x - 1 => y > 0 -- `y>0` -> `y>0` (expected `true`) setup line assume(x>1) failed: assume: only comparisons with 0 are supported
- **N9** x >= y >= z >= x => x = y -- `x=y` -> `x=y` (expected `true`) setup line assume(x>=y) failed: assume: only comparisons with 0 are supported
- **O3** (a x b) . (c x d) for abstract vectors -- `None` -> `` (expected ``) no abstract (basis-free) vectors
- **O5** div(f x g) = g . curl f - f . curl g for abstract fields -- `None` -> `` (expected ``) no abstract (basis-free) vectors
- **O6** divergence in spherical coordinates -- `None` -> `` (expected ``) no curvilinear coordinate systems
- **O7** dR/dt in spherical coordinates -- `None` -> `` (expected ``) no curvilinear coordinate systems
- **O8** scalar potential of (2xy, x^2 + 1, 6z^2) => x^2 y + y + 2z^3 -- `potential([2*x*y,x^2+1,6*z^2],[x,y,z])` -> `potential([2*x*y,x^2+1,6*z^2],[x,y,z])` (expected `x^2*y+y+2*z^3`) unknown function(s) echoed: potential
- **O9** vector potential of the O4 curl -- `vpotential([2*y*z^3-2*x^2*y^2*z,x*y,2*x*y^2*z^2-x*z],[x,y,z])` -> `vpotential([-2*x^2*y^2*z+2*y*z^3,x*y,2*x*y^2*z^2-x*z],[x,y,z])` (expected `[x*y*z,x^2*y^2*z^2,y^2*z^3]`) unknown function(s) echoed: vpotential
- **O10** Gram-Schmidt of [0,1,2,1], [0,1,3,1], [1,1,1,0], [1,3,6,2] -- `gramschmidt([[0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]])` -> `gramschmidt([[0,1,2,1],[0,1,3,1],[1,1,1,0],[1,3,6,2]])` (expected `[[0,1,2,1],[0,-1,1,-1],[2,1,0,-1]]`) unknown function(s) echoed: gramschmidt
- **P1** superdiagonal of [[1,2,3],[4,5,6],[7,8,9]] => [2, 6] -- `diag([[1,2,3],[4,5,6],[7,8,9]],1)` -> `diag([[1,2,3],[4,5,6],[7,8,9]],1)` (expected `[2,6]`) unknown function(s) echoed: diag
- **P2** (2,3)-minor matrix => [[1, 2], [7, 8]] -- `minor([[1,2,3],[4,5,6],[7,8,9]],2,3)` -> `minor([[1,2,3],[4,5,6],[7,8,9]],2,3)` (expected `[[1,2],[7,8]]`) unknown function(s) echoed: minor
- **P3** build a 7x6 matrix from submatrices of a 4x4 matrix -- `None` -> `` (expected ``) no submatrix indexing / block assembly
- **P4** block diagonal matrix from [[a,1],[0,a]], b, [[c,1,0],[0,c,1],[0,0,c]] -- `blockdiag([[a,1],[0,a]],b,[[c,1,0],[0,c,1],[0,0,c]])` -> `blockdiag([[a,1],[0,a]],b,[[c,1,0],[0,c,1],[0,0,c]])` (expected `[[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]]`) unknown function(s) echoed: blockdiag
- **P8** infinity norm of [[1, -2i], [-3i, 4]] => 7 -- `norm([[1,-2*i],[-3*i,4]],inf)` -> `ERROR 2: wrong number of arguments` (expected `7`) norm() has no p/inf argument (Frobenius only)
- **P12** inverse of a block triangular matrix with symbolic blocks -- `None` -> `` (expected ``) no symbolic (noncommutative) matrix blocks
- **P20** minimal polynomial => (lambda - 1)^2 (lambda + 1) -- `minpoly([[17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]],x)` -> `minpoly([[17,-8,-12,14],[46,-22,-35,41],[-2,1,4,-4],[4,-2,-2,3]],x)` (expected `(x-1)^2*(x+1)`) unknown function(s) echoed: minpoly
- **P28** generalized eigenvectors (3x3, lambda = 1 triple) -- `None` -> `` (expected ``) no generalized eigenvectors (Jordan chains)
- **P29** generalized eigenvectors (6x6) -- `None` -> `` (expected ``) no generalized eigenvectors (Jordan chains)
- **P30** Jordan form => diag([[1,1],[0,1]], [[1,1],[0,1]], -1) -- `jordan([[1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2]])` -> `jordan([[1,0,0,1,-1],[0,1,-2,3,-3],[0,0,-1,2,-2],[1,-1,1,0,1],[1,-1,1,-1,2]])` (expected `[[-1,0,0,0,0],[0,1,1,0,0],[0,0,1,0,0],[0,0,0,1,1],[0,0,0,0,1]]`) unknown function(s) echoed: jordan
- **P31** Smith normal form of [[x^2, x - 1], [x + 1, x^2]] => [[1, 0], [0, x^4 - x^2 + 1]] -- `smith([[x^2,x-1],[x+1,x^2]])` -> `smith([[x^2,x-1],[x+1,x^2]])` (expected `[[1,0],[0,x^4-x^2+1]]`) unknown function(s) echoed: smith
- **P32** matrix exponential of [[1, -2], [2, 1]] => e [[cos 2, -sin 2], [sin 2, cos 2]] -- `expm([[1,-2],[2,1]])` -> `expm([[1,-2],[2,1]])` (expected `[[e*cos(2),-e*sin(2)],[e*sin(2),e*cos(2)]]`) unknown function(s) echoed: expm
- **P33** matrix exponential exp(M t) (Niles) -- `expm([[0,1,0,0],[0,0,0,2*w],[0,0,0,1],[0,-2*w,3*w^2,0]]*t)` -> `expm([[0,t,0,0],[0,0,0,2*t*w],[0,0,0,t],[0,-2*t*w,3*t*w^2,0]])` (expected `[[1,4*sin(w*t)/w-3*t,6*(w*t-sin(w*t)),2/w*(1-cos(w*t))],[0,4*cos(w*t)-3,6*w*(1-cos(w*t)),2*sin(w*t)],[0,-2/w*(1-cos(w*t)),4-3*cos(w*t),sin(w*t)/w],[0,-2*sin(w*t),3*w*sin(w*t),cos(w*t)]]`) unknown function(s) echoed: expm
- **P34** sine of a Jordan matrix -- `sinm([[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]])` -> `sinm([[a,1,0,0,0,0],[0,a,0,0,0,0],[0,0,b,0,0,0],[0,0,0,c,1,0],[0,0,0,0,c,1],[0,0,0,0,0,c]])` (expected `[[sin(a),cos(a),0,0,0,0],[0,sin(a),0,0,0,0],[0,0,sin(b),0,0,0],[0,0,0,sin(c),cos(c),-sin(c)/2],[0,0,0,0,sin(c),cos(c)],[0,0,0,0,0,sin(c)]]`) unknown function(s) echoed: sinm
- **P35** sin(pi/2 [[2,1,1],[2,3,2],[1,1,2]]) => identity -- `sinm(pi/2*[[2,1,1],[2,3,2],[1,1,2]])` -> `sinm([[pi,pi/2,pi/2],[pi,3*pi/2,pi],[pi/2,pi/2,pi]])` (expected `[[1,0,0],[0,1,0],[0,0,1]]`) unknown function(s) echoed: sinm
- **P39** singular value decomposition of [[1,1],[2,2],[3,3]] -- `svd([[1,1],[2,2],[3,3]])` -> `svd([[1,1],[2,2],[3,3]])` (expected `[2*sqrt(7),0]`) unknown function(s) echoed: svd
- **P42** Wronskian matrix of (cos t, sin t) -- `wronskian([cos(t),sin(t)],t)` -> `wronskian([cos(t),sin(t)],t)` (expected `[[cos(t),sin(t)],[-sin(t),cos(t)]]`) unknown function(s) echoed: wronskian
- **R1** sum((x[i] - xbar)^2) => sum(x[i]^2) - sum(x[i])^2/n -- `None` -> `` (expected ``) no indexed (subscripted) symbols
- **R2** least squares fit of a line to (x[i], y[i]) -- `None` -> `` (expected ``) no indexed (subscripted) symbols
- **R4** Gosper: indefinite sum of binomial(n,k)/2^n - binomial(n+1,k)/2^(n+1) -- `None` -> `` (expected ``) no indefinite summation (Gosper)
- **R11** sum(binomial(n, k) fib(k), k = 0..n) => fib(2n) -- `sum(binomial(n,k)*fibonacci(k),k,0,n)` -> `sum(binomial(n,k)*fibonacci(k),k,0,n)` (expected `fibonacci(2*n)`) unknown function(s) echoed: fibonacci
- **R12** sum(fib(k)^2, k = 1..n) => fib(n) fib(n+1) -- `sum(fibonacci(k)^2,k,1,n)` -> `sum(fibonacci(k)^2,k,1,n)` (expected `fibonacci(n)*fibonacci(n+1)`) unknown function(s) echoed: fibonacci
- **U7** exact differential d V(P, T) => V_P dP + V_T dT -- `None` -> `` (expected ``) no total differentials
- **U10** residue((z^3 + 5)/((z^4 - 1)(z + 1)), z, -1) => -9/4 -- `residue((z^3+5)/((z^4-1)*(z+1)),z,-1)` -> `residue((z^3+5)/((z+1)*(z^4-1)),z,-1)` (expected `-9/4`) unknown function(s) echoed: residue
- **U13** minimum of x^4 - x + 1 => 1 - 3/8 2^(1/3) -- `minimize(x^4-x+1,x)` -> `minimize(x^4-x+1,x)` (expected `1-3*2^(1/3)/8`) unknown function(s) echoed: minimize
- **U14** [min, max] of 1/(x^2 + y^2 + 1) => [0, 1] -- `None` -> `` (expected ``) no optimization commands
- **U15** minimize a + b x + c y + d x y on [-1,1]^2 -- `None` -> `` (expected ``) no optimization commands
- **U16** [min, max] of x^2 y^3 on [-1,1]^2 => [-1, 1] -- `None` -> `` (expected ``) no optimization commands
- **U17** linear programming (symbolic simplex) -- `None` -> `` (expected ``) no linear programming
- **V2** int |z| dz for complex z -- `None` -> `` (expected ``) symbols cannot be declared complex
- **W16** int (1 + x)^3 P1(x) P2(x), x = -1..1 => 36/35 -- `integrate((1+x)^3*legendre(1,x)*legendre(2,x),x,-1,1)` -> `integrate(legendre(1,x)*legendre(2,x)*(x+1)^3,x,-1,1)` (expected `36/35`) unknown function(s) echoed: legendre
- **W17** int exp(-a x) J0(b x), x = 0..inf => 1/sqrt(a^2 + b^2) -- `integrate(exp(-a*x)*besselj(0,b*x),x,0,inf)` -> `integrate(besselj(0,b*x)*exp(-a*x),x,0,inf)` (expected `1/sqrt(a^2+b^2)`) unknown function(s) echoed: besselj
- **W18** int (J1(x)/x)^2, x = 0..inf => 4/(3 pi) -- `integrate((besselj(1,x)/x)^2,x,0,inf)` -> `integrate(besselj(1,x)^2/x^2,x,0,inf)` (expected `4/(3*pi)`) unknown function(s) echoed: besselj
- **W19** int Ci(x) J0(2 sqrt(7 x)), x = 0..inf => (cos 7 - 1)/7 -- `integrate(Ci(x)*besselj(0,2*sqrt(7*x)),x,0,inf)` -> `integrate(Ci(x)*besselj(0,2*sqrt(7)*sqrt(x)),x,0,inf)` (expected `(cos(7)-1)/7`) unknown function(s) echoed: besselj
- **W20** int x^2 Li3(1/(x + 1)), x = 0..1 (Mertig) -- `integrate(x^2*polylog(3,1/(x+1)),x,0,1)` -> `integrate(polylog(3,1/(x+1))*x^2,x,0,1)` (expected `-(17/3+pi^2)/36+ln(2)/9*(35/3-pi^2/2-4*ln(2)+ln(2)^2)+zeta(3)/4`) unknown function(s) echoed: polylog
- **W21** numeric value of W20 => 0.210883 -- `nint(x^2*polylog(3,1/(x+1)),x,0,1)` -> `ERROR 2: numeric integration failed` (expected `0.210882859565594`) no polylog function
- **X18** multivariate Taylor cos(x + y) => 1 - (x + y)^2/2 -- `series(cos(x+y),[x,y],[0,0],4)` -> `ERROR 2: expected a variable name` (expected `1-(x+y)^2/2`) series() is univariate only
- **X19** power series of log(sin x/x), general term -- `None` -> `` (expected ``) no formal power series (general term)
- **X20** power series of e^-x sin x, general term -- `None` -> `` (expected ``) no formal power series (general term)
- **X21** series reversion of x = sin y + cos y -- `None` -> `` (expected ``) no series reversion
- **Y8** Fourier transform of 1 => sqrt(2 pi) delta(z) -- `fourier(1,x,z)` -> `fourier(1,x,z)` (expected `sqrt(2*pi)*dirac(z)`) unknown function(s) echoed: fourier
- **Y9** Fourier transform of exp(-9 x^2) -- `fourier(exp(-9*x^2),x,z)` -> `fourier(exp(-9*x^2),x,z)` (expected `exp(-z^2/36)/(3*sqrt(2))`) unknown function(s) echoed: fourier
- **Y10** Fourier transform of |x| exp(-3 |x|) -- `fourier(abs(x)*exp(-3*abs(x)),x,z)` -> `fourier(abs(x)*exp(-3*abs(x)),x,z)` (expected `sqrt(2/pi)*(9-z^2)/(9+z^2)^2`) unknown function(s) echoed: fourier
- **Y12** Mellin transform of J3(x)/x^3 => 2^(s-4) gamma(s/2)/gamma(4 - s/2) -- `integrate(besselj(3,x)/x^3*x^(s-1),x,0,inf)` -> `integrate(besselj(3,x)*x^(s-4),x,0,inf)` (expected `2^(s-4)*gamma(s/2)/gamma(4-s/2)`) unknown function(s) echoed: besselj
- **Y13** Z transform of H(t - 3) -- `ztrans(heaviside(t-3),t,z)` -> `ztrans(heaviside(t-3),t,z)` (expected `0`) unknown function(s) echoed: ztrans
- **Y14** Z transform of H(t - m T) => z/(z^m (z - 1)) -- `ztrans(heaviside(t-m),t,z)` -> `ztrans(heaviside(t-m),t,z)` (expected `0`) unknown function(s) echoed: ztrans
- **Z1** r(n+2) - 2 r(n+1) + r(n) = 2, r(0) = 1, r(1) = m => n^2 + (m - 2) n + 1 -- `rsolve(f(n+2)-2*f(n+1)+f(n)=2,f(n),[f(0)=1,f(1)=m])` -> `rsolve(f(n)-2*f(n+1)+f(n+2)=2,f(n),[f(0)=1,f(1)=m])` (expected `n^2+(m-2)*n+1`) unknown function(s) echoed: rsolve
- **Z2** r(n) = 5 r(n-1) - 6 r(n-2), r(0) = 0, r(1) = 1 => 3^n - 2^n -- `rsolve(f(n)=5*f(n-1)-6*f(n-2),f(n),[f(0)=0,f(1)=1])` -> `rsolve(f(n)=5*f(n-1)-6*f(n-2),f(n),[f(0)=0,f(1)=1])` (expected `3^n-2^n`) unknown function(s) echoed: rsolve
- **Z3** Fibonacci recurrence, r(1) = 1, r(2) = 2 => fib(n + 1) -- `rsolve(f(n)=f(n-1)+f(n-2),f(n),[f(1)=1,f(2)=2])` -> `rsolve(f(n)=f(n-2)+f(n-1),f(n),[f(1)=1,f(2)=2])` (expected `fibonacci(n+1)`) unknown function(s) echoed: rsolve
- **Z4** Yu-Israel nonlinear-coefficient recurrence -- `rsolve(f(n)=(1+c-c^(n-1)-c^(n+1))/(1-c^n)*f(n-1)-c*(1-c^(n-2))/(1-c^(n-1))*f(n-2)+1,f(n),[f(1)=1,f(2)=(2+2*c+c^2)/(1+c)])` -> `rsolve(f(n)=-c*(1-c^(n-2))*f(n-2)/(1-c^(n-1))+(c-c^(n-1)-c^(n+1)+1)*f(n-1)/(1-c^n)+1,f(n),[f(1)=1,f(2)=(c^2+2*c+2)/(c+1)])` (expected `(c^(n+1)*(c^(n+1)-2*c-2)+(n+1)*c^2+2*c-n)/((c-1)^3*(c+1))`) unknown function(s) echoed: rsolve
- **Z16** delay equation y'(t) + a y(t - 1) = 0 -- `None` -> `` (expected ``) no delay differential equations
- **Z19** system u' = u - v, v' = u + v -- `dsolve([u'=u-v,v'=u+v],[u,v],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t)-(U-V), V.diff(t)-(U+V)]`) dsolve has no systems of ODEs
- **Z20** triangular system u' = u (1 + cos t/(2 + sin t)), v' = u - v -- `dsolve([u'=u*(1+cos(t)/(2+sin(t))),v'=u-v],[u,v],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t)-U*(1+sp.cos(t)/(2+sp.sin(t))), V.diff(t)-(U-V)]`) dsolve has no systems of ODEs
- **Z21** 3x3 system with roots 2, 1, 3 -- `dsolve([u'=2*u,v'=-2*u+v-2*w,w'=u+3*w],[u,v,w],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t)-2*U, V.diff(t)-(-2*U+V-2*W), W.diff(t)-(U+3*W)]`) dsolve has no systems of ODEs
- **Z22** 3x3 system with roots 0, -1 +- sqrt(2) i -- `dsolve([u'=v,v'=w,w'=-3*v-2*w],[u,v,w],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t)-V, V.diff(t)-W, W.diff(t)-(-3*V-2*W)]`) dsolve has no systems of ODEs
- **Z23** 3x3 system with the triple root 2 -- `dsolve([u'=3*u-w,v'=2*v,w'=u+w],[u,v,w],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t)-(3*U-W), V.diff(t)-2*V, W.diff(t)-(U+W)]`) dsolve has no systems of ODEs
- **Z24** second order system (Niles) u'' = 2 q v', v'' = -2 q u' + 3 q^2 v -- `dsolve([u''=2*q*v',v''=-2*q*u'+3*q^2*v],[u,v],t)` -> `ERROR 2: dsolve: expected names y, x` (expected `[U.diff(t,2)-2*q*V.diff(t), V.diff(t,2)-(-2*q*U.diff(t)+3*q**2*V)]`) dsolve has no systems of ODEs

## ERROR (9)

- **H8** factor(expand(p1 p2)) => p1 p2 -- `factor(expand((64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)))` -> `ERROR 4: out of memory` (expected `(64*x^34-21*x^47-126*x^8-46*x^5-16*x^60-81)*(72*x^60-25*x^25-19*x^23-22*x^39-83*x^52+54*x^10+81)`) ERROR 4: out of memory
- **H9** multivariate gcd(p1, p2) => 1 -- `gcd(24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5,34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)` -> `ERROR 4: out of memory` (expected `1`) ERROR 4: out of memory
- **H10** multivariate gcd(p1 q, p2 q) => q -- `gcd(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)),expand((34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)*(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)))` -> `ERROR 4: out of memory` (expected `[11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8,-(11*x^12*y^7*z^13-23*x^2*y^8*z^10+47*x^17*y^5*z^8)]`) ERROR 4: out of memory
- **H11** multivariate factor(expand(p1 p2)) => p1 p2 -- `factor(expand((24*x*y^19*z^8-47*x^17*y^5*z^8+6*x^15*y^9*z^2-3*x^22+5)*(34*x^5*y^8*z^13+20*x^7*y^7*z^7+12*x^9*y^16*z^4+80*y^14*z)))` -> `ERROR 4: out of memory` (expected `-2*z*y^7*(6*x^9*y^9*z^3+10*x^7*z^6+17*y*x^5*z^12+40*y^7)*(3*x^22+47*x^17*y^5*z^8-6*x^15*y^9*z^2-24*x*y^19*z^8-5)`) ERROR 4: out of memory
- **H27** factor(expand((x - 2y^2 + 3z^3)^20)) -- `factor(expand((x-2*y^2+3*z^3)^20))` -> `ERROR 4: out of memory` (expected `(x-2*y^2+3*z^3)^20`) ERROR 4: out of memory
- **H28** factor(expand((sin x - 2 cos^2 y + 3 tan^3 z)^20)) -- `factor(expand((sin(x)-2*cos(y)^2+3*tan(z)^3)^20))` -> `ERROR 4: out of memory` (expected `(sin(x)-2*cos(y)^2+3*tan(z)^3)^20`) ERROR 4: out of memory
- **M27** 5x + exp((x - 5)/2) = 8x^3 => {-0.784966, -0.016291, 0.802557, 29.454528} -- `solve(5*x+exp((x-5)/2)=8*x^3,x)` -> `ERROR 4: out of memory` (expected `[-0.78496614648634752,-0.016290737729935251,0.8025567019143254,29.454527572105314]`) ERROR 4: out of memory
- **P22** eigenvalues of (2 - a) I_100 => 2 - a -- `eigenvals((2-a)*identity(100))` -> `ERROR 4: out of memory` (expected `[2-a]`) ERROR 4: out of memory
- **P26** eigenvalues of a hypercompanion matrix => roots of x^2+x+1 (twice), RootOf(quintic) -- `eigenvals([[-a4,-a3,-a2,-a1,-a0,0,0,0,0],[1,0,0,0,0,0,0,0,0],[0,1,0,0,0,0,0,0,0],[0,0,1,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0],[0,0,0,0,0,-1,-1,0,0],[0,0,0,0,0,1,0,0,0],[0,0,0,0,0,0,1,-1,-1],[0,0,0,0,0,0,0,1,0]])` -> `ERROR 4: out of memory` (expected `[-1/2-sqrt(3)*i/2,-1/2+sqrt(3)*i/2]`) ERROR 4: out of memory

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

