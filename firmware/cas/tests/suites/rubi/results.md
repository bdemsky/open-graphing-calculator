# rubi suite results

binary: `build/cas_repl`; full per-case records: `build/suites/rubi/results.json`

| group | PASS | WRONG | CRASH | TIMEOUT | ERROR | UNEVAL | UNSUPPORTED | TRANSLATION-UNCERTAIN | UNGRADED | NA | total |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 Independent test suites | 1560 | 0 | 0 | 0 | 0 | 309 | 0 | 0 | 0 | 0 | 1869 |
| 1 Algebraic functions | 18473 | 0 | 0 | 0 | 0 | 7214 | 10 | 0 | 0 | 0 | 25697 |
| 2 Exponentials | 485 | 0 | 0 | 0 | 0 | 476 | 4 | 0 | 0 | 0 | 965 |
| 3 Logarithms | 1180 | 0 | 0 | 0 | 0 | 1905 | 0 | 0 | 0 | 0 | 3085 |
| 4 Trig functions | 14613 | 0 | 0 | 0 | 0 | 7854 | 4 | 0 | 1 | 0 | 22472 |
| 5 Inverse trig functions | 1523 | 0 | 0 | 0 | 0 | 3062 | 0 | 0 | 0 | 0 | 4585 |
| 6 Hyperbolic functions | 3356 | 0 | 0 | 0 | 0 | 1720 | 4 | 0 | 0 | 0 | 5080 |
| 7 Inverse hyperbolic functions | 3556 | 0 | 0 | 0 | 0 | 2996 | 0 | 0 | 0 | 0 | 6552 |
| 8 Special functions | 975 | 0 | 0 | 0 | 0 | 837 | 43 | 0 | 0 | 94 | 1949 |
| **total** | **45721** | **0** | **0** | **0** | **0** | **26373** | **65** | **0** | **1** | **94** | **72254** |

## UNGRADED (1)

- **rubi/4.2.4.2_a_b_cos_m_c_d_cos_n_A_B_cos_C_cos_2/1037** `integrate((a+b*cos(c+d*x))^(5/2)*(A+B*cos(c+d*x)+C*cos(c+d*x)^2)*sec(c+d*x)^6,x)` -> `(A*a^2*(a+b*cos(d*x+c))^(5/2)*(8*a*sqrt((a/b+cos(d*x+c))/(a/b+1))*elliptic_f(d*x/2+c/2,2*b/(a+b))/(3*b*sqrt(a/b+cos(d*x+c)))-8*sqrt(a/b+cos(d*x+c))*elliptic_e(d*x/2+c/2,2*b/(a+b))/(3*sqrt((a/b+cos(d*x+c))/(a/b+1)))+4*sqrt((a/b+cos(d*x+c))/(a/b+1))*elliptic_pi(2,d*x/2+c/2,2*b/(a+b))/(3*sqrt(a/b+cos(d` (expected `(a + b*cos(c + d*x))^(5/2)*(A + B*cos(c + d*x) + C*cos(c + d*x)^2)*sec(c + d*x)^6`) grader stuck for 150 s (in C code) and killed

