# giac suite results

binary: `build/cas_repl`; full per-case records: `build/suites/giac/results.json`

| group | PASS | WRONG | CRASH | TIMEOUT | ERROR | UNEVAL | UNSUPPORTED | TRANSLATION-UNCERTAIN | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|---|
| testlimit | 14 | 0 | 0 | 0 | 0 | 12 | 0 | 0 | 0 | 26 | 52 |
| testintegrate | 59 | 0 | 0 | 0 | 0 | 4 | 0 | 0 | 0 | 4 | 67 |
| testcas | 140 | 0 | 0 | 0 | 0 | 0 | 105 | 2 | 0 | 25 | 272 |
| testnormalize | 2 | 0 | 0 | 0 | 1 | 0 | 2 | 0 | 0 | 10 | 15 |
| testfactor | 30 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 1 | 32 |
| testpartfrac | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 1 |
| testother | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 4 | 4 |
| intind | 1474 | 0 | 0 | 0 | 0 | 317 | 0 | 0 | 0 | 0 | 1791 |
| intinda | 90 | 0 | 0 | 0 | 0 | 3 | 0 | 0 | 0 | 0 | 93 |
| intindbo | 7 | 0 | 0 | 0 | 0 | 21 | 1 | 0 | 0 | 0 | 29 |
| intindbr | 6 | 0 | 0 | 0 | 0 | 5 | 0 | 0 | 0 | 0 | 11 |
| intindc | 20 | 0 | 0 | 0 | 0 | 29 | 0 | 0 | 0 | 0 | 49 |
| intindh | 146 | 0 | 0 | 0 | 0 | 18 | 0 | 0 | 0 | 0 | 164 |
| intindm | 38 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 38 |
| intinds | 178 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 179 |
| intindt | 330 | 0 | 0 | 0 | 0 | 78 | 0 | 0 | 0 | 0 | 408 |
| intindw | 37 | 0 | 0 | 0 | 0 | 72 | 0 | 0 | 0 | 0 | 109 |
| intheb1 | 2331 | 0 | 0 | 0 | 0 | 626 | 0 | 0 | 3 | 0 | 2960 |
| intheb2 | 2331 | 0 | 0 | 0 | 0 | 633 | 0 | 0 | 3 | 0 | 2967 |
| intheb3 | 2307 | 0 | 0 | 0 | 0 | 659 | 0 | 0 | 5 | 0 | 2971 |
| intheb4 | 1020 | 0 | 0 | 0 | 0 | 301 | 0 | 0 | 1 | 0 | 1322 |
| intmit | 227 | 0 | 0 | 0 | 0 | 39 | 0 | 0 | 0 | 0 | 266 |
| **total** | **10788** | **0** | **0** | **0** | **2** | **2818** | **108** | **2** | **12** | **70** | **13800** |

## TRANSLATION-UNCERTAIN (2)

- **giac/cas/39** `ker([[1,2],[3,6]])` -> `[[-2,1]]` (expected `[[2, -1]]`) WRONG?  [kernel basis is not unique (giac rows vs ours)]
- **giac/cas/40** `ker([[1,2,3],[1,3,6],[2,5,9]])` -> `[[3,-3,1]]` (expected `[[-3, 3, -1]]`) WRONG?  [kernel basis is not unique (giac rows vs ours)]

## ERROR (2)

- **giac/normalize/3** `normal(((x+y)^2-(z+1)^2)^10/(x+y+z+1)^20)` -> `ERROR 4: out of memory` (expected `(x^10 + 10*x^9*y - 10*x^9*z - 10*x^9 + 45*x^8*y^2 - 90*x^8*y*z - 90*x^8*y + 45*x^8*z^2 + 90*x^8*z + 45*x^8 + 120*x^7*y^3 - 360*x^7*y^2*z - 360*x^7*y^2 + 360*x^7*y*z^2 + 720*x^7*y*z + 360*x^7*y - 120*x^7*z^3 - 360*x^7*z^2 - 360*x^7*z - 120*x^7 + 210*x^6*y^4 - 840*x^6*y^3*z - 840*x^6*y^3 + 1260*x^6*y^`) ERROR 4: out of memory
- **giac/factor/32** `factor((x^202+x^101+1)/(x^2+x+1))` -> `ERROR 4: out of memory` (expected `x^200 - x^199 + x^197 - x^196 + x^194 - x^193 + x^191 - x^190 + x^188 - x^187 + x^185 - x^184 + x^182 - x^181 + x^179 - x^178 + x^176 - x^175 + x^173 - x^172 + x^170 - x^169 + x^167 - x^166 + x^164 - x^163 + x^161 - x^160 + x^158 - x^157 + x^155 - x^154 + x^152 - x^151 + x^149 - x^148 + x^146 - x^14`) ERROR 4: out of memory

## UNGRADED (12)

- **giac/intheb1/193** `integrate((-exp(4))*exp(x+25)*exp(exp(x+25))*exp(exp(exp(x+25)))+1,x)` -> `x-exp(exp(exp(x+25))+4)` (expected `-exp(4)*exp(x + 25)*exp(exp(x + 25))*exp(exp(exp(x + 25))) + 1`) grader killed by the memory guard at 2335 MB RSS
- **giac/intheb1/934** `integrate(((2*exp(4)^4-2)*exp(2*x*exp(4)^4-2*x)+8)*exp(exp(2*x*exp(4)^4-2*x)+8*x),x)` -> `exp(8*x+exp(2*exp(16)*x-2*x))` (expected `((-2 + 2*exp(16))*exp(-2*x + 2*x*exp(16)) + 8)*exp(8*x + exp(-2*x + 2*x*exp(16)))`) grader stuck for 150 s (in C code) and killed
- **giac/intheb1/2442** `integrate(1/3*((5*exp(x)*exp(exp(x))-5)*exp(5*exp(exp(x))-5*x)+6)*exp(1/3*exp(5*exp(exp(x))-5*x)+2*x-1)*exp(exp(1/3*exp(5*exp(exp(x))-5*x)+2*x-1)),x)` -> `exp(exp(2*x+exp(-5*x+5*exp(exp(x)))/3-1))` (expected `((5*exp(x)*exp(exp(x)) - 5)*exp(-5*x + 5*exp(exp(x)))/3 + 2)*exp(2*x + exp(-5*x + 5*exp(exp(x)))/3 - 1)*exp(exp(2*x + exp(-5*x + 5*exp(exp(x)))/3 - 1))`) grader stuck for 150 s (in C code) and killed
- **giac/intheb2/621** `integrate(1/3*((-2)*x*exp(exp(20))+18*x^2-3)/exp((-2)/3*x*exp(exp(20))+3*x^2),x)` -> `-exp(-3*x^2+2*exp(exp(20))*x/3)*x` (expected `(6*x^2 - 2*x*exp(exp(20))/3 - 1)*exp(-3*x^2 + 2*x*exp(exp(20))/3)`) grader stuck for 150 s (in C code) and killed
- **giac/intheb2/2150** `integrate((2*exp(x)*exp(exp(x))*exp(exp(exp(x)))^2+4*exp(x)*exp(exp(x))*exp(exp(exp(x)))+exp(x))*exp(exp(exp(exp(x)))^2+4*exp(exp(exp(x)))+exp(x)+4)*exp(exp(exp(exp(exp(x)))^2+4*exp(exp(exp(x)))+exp(x)+4))+3,x)` -> `3*x+exp(exp(4*exp(exp(exp(x)))+exp(2*exp(exp(x)))+exp(x)+4))` (expected `(2*exp(x)*exp(exp(x))*exp(2*exp(exp(x))) + 4*exp(x)*exp(exp(x))*exp(exp(exp(x))) + exp(x))*exp(exp(x) + exp(2*exp(exp(x))) + 4*exp(exp(exp(x))) + 4)*exp(exp(exp(x) + exp(2*exp(exp(x))) + 4*exp(exp(exp(x))) + 4)) + 3`) grader killed by the memory guard at 2359 MB RSS
- **giac/intheb2/2189** `integrate((((-126)*x^6*ln(x*exp(5))^8-144*x^6*ln(x*exp(5))^7)*exp(9*x^7*ln(x*exp(5))^8)^2+(126*x^7*ln(x*exp(5))^8+144*x^7*ln(x*exp(5))^7+2)*exp(9*x^7*ln(x*exp(5))^8)-2*x)*exp(-exp(9*x^7*ln(x*exp(5))^8)^2+2*x*exp(9*x^7*ln(x*exp(5))^8)-x^2+1),x)` -> `exp(-x^2+2*exp(9*ln(exp(5)*x)^8*x^7)*x-exp(18*ln(exp(5)*x)^8*x^7)+1)` (expected `(-2*x + (-126*x^6*log(x*exp(5))^8 - 144*x^6*log(x*exp(5))^7)*exp(18*x^7*log(x*exp(5))^8) + (126*x^7*log(x*exp(5))^8 + 144*x^7*log(x*exp(5))^7 + 2)*exp(9*x^7*log(x*exp(5))^8))*exp(-x^2 + 2*x*exp(9*x^7*log(x*exp(5))^8) - exp(18*x^7*log(x*exp(5))^8) + 1)`) grader stuck for 150 s (in C code) and killed
- **giac/intheb3/567** `integrate(1/3*((30*exp(x)*x+6)*exp(5*exp(x))*exp(x*exp(5*exp(x)))^2+(((10*exp(x)*x+2)*exp(3*x)+10*x*exp(1)*exp(x)+2*exp(1))*exp(5*exp(x))+6*exp(3*x))*exp(x*exp(5*exp(x)))+2*exp(3*x)^2+2*exp(1)*exp(3*x))*exp(exp(x*exp(5*exp(x)))^2+1/9*(6*exp(3*x)+6*exp(1))*exp(x*exp(5*exp(x)))+1/9*exp(3*x)^2+2/9*exp(1)*exp(3*x)+1/9*exp(1)^2),x)` -> `exp(exp(2)/9+exp(6*x)/9+exp(2*exp(5*exp(x))*x)+2*exp(3*x+1)/9+exp(exp(5*exp(x))*x)*(2*e/3+2*exp(3*x)/3))` (expected `((30*x*exp(x) + 6)*exp(2*x*exp(5*exp(x)))*exp(5*exp(x))/3 + ((10*E*x*exp(x) + (10*x*exp(x) + 2)*exp(3*x) + 2*E)*exp(5*exp(x)) + 6*exp(3*x))*exp(x*exp(5*exp(x)))/3 + 2*exp(6*x)/3 + 2*E*exp(3*x)/3)*exp((2*exp(3*x)/3 + 2*E/3)*exp(x*exp(5*exp(x))) + exp(6*x)/9 + 2*E*exp(3*x)/9 + exp(2*x*exp(5*exp(x))) +`) grader killed by the memory guard at 3444 MB RSS
- **giac/intheb3/1223** `integrate((13*exp(4)*exp(x*exp(4))+26)*exp(1/2*exp(x*exp(4))+x)*exp(exp(1/2*exp(x*exp(4))+x)),x)` -> `26*exp(exp(x+exp(exp(4)*x)/2))` (expected `(13*exp(4)*exp(x*exp(4)) + 26)*exp(x + exp(x*exp(4))/2)*exp(exp(x + exp(x*exp(4))/2))`) grader killed by the memory guard at 2249 MB RSS
- **giac/intheb3/2420** `integrate(((-16)*ln(5)*ln(10)+((-32)*x+8)*ln(5))*exp(4*ln(10)^2+2*(8*x-2)*ln(10)+16*x^2-8*x+1)*exp(exp(4*ln(10)^2+2*(8*x-2)*ln(10)+16*x^2-8*x+1)+4)*exp(ln(5)*exp(exp(4*ln(10)^2+2*(8*x-2)*ln(10)+16*x^2-8*x+1)+4))+2*x,x)` -> `x^2-5^exp(10^(16*x-4)*exp(16*x^2-8*x+4*ln(10)^2+1)+4)` (expected `2*x + ((8 - 32*x)*log(5) - 16*log(5)*log(10))*exp(exp(exp(16*x^2 - 8*x + (16*x - 4)*log(10) + 1 + 4*log(10)^2) + 4)*log(5))*exp(exp(16*x^2 - 8*x + (16*x - 4)*log(10) + 1 + 4*log(10)^2) + 4)*exp(16*x^2 - 8*x + (16*x - 4)*log(10) + 1 + 4*log(10)^2)`) grader killed by the memory guard at 2885 MB RSS
- **giac/intheb3/2464** `integrate(8*x^3*exp(exp(exp(25)))/ln(2),x)` -> `2*exp(exp(exp(25)))*x^4/ln(2)` (expected `8*x^3*exp(exp(exp(25)))/log(2)`) grader killed by the memory guard at 2529 MB RSS
- **giac/intheb3/2854** `integrate((((-4)*x^3*exp(x)*exp(exp(1))^2-10*x^4*exp(x)*exp(exp(1))-6*x^5*exp(x))*exp(x^4*exp(exp(1))^2+2*x^5*exp(exp(1))+x^6)*exp(exp(x^4*exp(exp(1))^2+2*x^5*exp(exp(1))+x^6))-2*exp(x))*exp(exp(exp(x^4*exp(exp(1))^2+2*x^5*exp(exp(1))+x^6))+x)+3,x)` -> `3*x-exp(2*x+exp(exp(x^6+2*exp(e)*x^5+exp(2*e)*x^4)))` (expected `((-6*x^5*exp(x) - 10*x^4*exp(E)*exp(x) - 4*x^3*exp(2*E)*exp(x))*exp(x^6 + 2*x^5*exp(E) + x^4*exp(2*E))*exp(exp(x^6 + 2*x^5*exp(E) + x^4*exp(2*E))) - 2*exp(x))*exp(x + exp(exp(x^6 + 2*x^5*exp(E) + x^4*exp(2*E)))) + 3`) grader killed by the memory guard at 3314 MB RSS
- **giac/intheb4/168** `integrate((exp(x)*exp(exp(x))+12*x-5)*exp(1/exp(-exp(exp(x))-6*x^2+5*x))*exp(exp(1/exp(-exp(exp(x))-6*x^2+5*x)))/exp(-exp(exp(x))-6*x^2+5*x),x)` -> `exp(exp(exp(6*x^2-5*x+exp(exp(x)))))` (expected `(12*x + exp(x)*exp(exp(x)) - 5)*exp(6*x^2 - 5*x + exp(exp(x)))*exp(exp(6*x^2 - 5*x + exp(exp(x))))*exp(exp(exp(6*x^2 - 5*x + exp(exp(x)))))`) grader killed by the memory guard at 3350 MB RSS

