#!/usr/bin/env python3
"""Host benchmarks: wall time and peak arena use for heavy CAS operations.
Each item runs in a fresh cas_repl process (24 MB arena)."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPL = os.path.join(os.path.dirname(HERE), "build", "cas_repl")

SD4 = "expand((x-sqrt(2)-sqrt(3)-sqrt(5)-sqrt(7))*(x-sqrt(2)-sqrt(3)-sqrt(5)+sqrt(7))*(x-sqrt(2)-sqrt(3)+sqrt(5)-sqrt(7))*(x-sqrt(2)-sqrt(3)+sqrt(5)+sqrt(7))*(x-sqrt(2)+sqrt(3)-sqrt(5)-sqrt(7))*(x-sqrt(2)+sqrt(3)-sqrt(5)+sqrt(7))*(x-sqrt(2)+sqrt(3)+sqrt(5)-sqrt(7))*(x-sqrt(2)+sqrt(3)+sqrt(5)+sqrt(7))*(x+sqrt(2)-sqrt(3)-sqrt(5)-sqrt(7))*(x+sqrt(2)-sqrt(3)-sqrt(5)+sqrt(7))*(x+sqrt(2)-sqrt(3)+sqrt(5)-sqrt(7))*(x+sqrt(2)-sqrt(3)+sqrt(5)+sqrt(7))*(x+sqrt(2)+sqrt(3)-sqrt(5)-sqrt(7))*(x+sqrt(2)+sqrt(3)-sqrt(5)+sqrt(7))*(x+sqrt(2)+sqrt(3)+sqrt(5)-sqrt(7))*(x+sqrt(2)+sqrt(3)+sqrt(5)+sqrt(7)))"

BENCH = [
    ("bignum", "1000!", []),
    ("bignum", "2^100000 mod 997", []),
    ("bignum", "500!/(250!*250!)", []),
    ("bignum", "isprime(2^521-1)", []),
    ("bignum", "ifactor(2^64+1)", []),
    ("bignum", "ifactor(1000000007*998244353)", []),
    ("bignum", "gcd(3^5000-1,2^7000-1)", []),
    ("expand", "expand((x+y+z+1)^20)", []),
    ("expand", "expand((x+1)^200)", []),
    ("factor", "factor(x^100-1)", []),
    ("factor", "factor(x^120-1)", []),
    ("factor", "factor(expand((x^5+x+1)*(x^7-x^2+3)*(2*x^4-x+5)*(x^3+11)))", []),
    ("factor", "factor(ans)", [SD4]),  # Swinnerton-Dyer polynomial, degree 16, irreducible
    ("factor", "factor(x^4*y^2-x^2*y^4+x^3-y^3)", []),
    ("factor", "factor(expand((x+y+z)^3*(x-y+2*z)^2))", []),
    ("gcd", "gcd(expand((x+1)^20*(x-2)^15),expand((x+1)^12*(x-3)^10))", []),
    ("integrate", "integrate(1/(x^6-1),x)", []),
    ("integrate", "integrate(x^10*exp(x)*sin(x),x)", []),
    ("integrate", "integrate((x^4+3*x^2+1)/(x^5+x^3-x^2-1),x)", []),
    ("integrate", "integrate(sin(x)^7*cos(x)^4,x)", []),
    ("integrate", "integrate(x^3*atan(x)^2,x)", []),
    ("integrate", "integrate(sqrt(x^2+2*x+5),x)", []),
    ("integrate", "integrate(1/(x^4+1),x)", []),
    ("integrate", "integrate(exp(-x^2),x,-inf,inf)", []),
    ("series", "series(tan(x),x,0,25)", []),
    ("series", "series(exp(sin(x)),x,0,15)", []),
    ("limit", "limit((1+1/x)^(x^2)/exp(x),x,inf)", []),
    ("solve", "solve(x^5-5*x+1=0,x)", []),
    ("solve", "solve([x^2+y^2=25,x*y=12],[x,y])", []),
    ("linalg", "det(matrix(10,10,f))", ["f(r,c):=(r+c)^2+r*c^3 mod 7"]),
    ("linalg", "inv([[1,2,3,4],[2,3,4,1],[3,4,1,2],[4,1,2,3]])", []),
    ("linalg", "det([[a,b,c],[d,e1,f1],[g,h,k]])", []),
    ("linalg", "det(matrix(12,12,f))", ["f(r,c):=1/(r+c-1)"]),
    ("sum", "sum(k^10,k,1,n)", []),
    ("numeric", "nint(exp(-x^2)*cos(x),x,0,10)", []),
]


def run(pre, cmd):
    lines = pre + [cmd, ":mem"]
    p = subprocess.run([REPL, "-b", "-T", "-t", "60000"], input="\n".join(lines) + "\n", capture_output=True,
                       text=True)
    outs = p.stdout.strip().split("\n")
    times = [float(m) for m in re.findall(r"\[([0-9.]+) ms\]", p.stderr)]
    res = outs[len(pre)] if len(outs) > len(pre) else "?"
    mem = re.search(r"peak (\d+)", outs[-1])
    return res, times[len(pre)] if len(times) > len(pre) else -1, int(mem.group(1)) if mem else -1


def main():
    rows = []
    print("| area | command | ms | peak KB | result (truncated) |")
    print("|---|---|---|---|---|")
    for area, cmd, pre in BENCH:
        res, ms, peak = run(pre, cmd)
        shown = cmd if len(cmd) < 70 else cmd[:67] + "..."
        if cmd == "factor(ans)":
            shown = "factor(Swinnerton-Dyer S4, deg 16)"
        print("| %s | `%s` | %.1f | %d | `%s` |" % (area, shown, ms, peak // 1024, res[:50].replace("|", "/")))
        rows.append((area, cmd, ms, peak, res))
    return 0


if __name__ == "__main__":
    sys.exit(main())
