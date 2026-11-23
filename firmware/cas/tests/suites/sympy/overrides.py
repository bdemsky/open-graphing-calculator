"""Manual per-case overrides for the SymPy suite: id -> fields merged into
the case after translation (e.g. {"na": "reason"} or {"uncertain": "reason"}).
Filled after reviewing WRONG results."""
OVERRIDES = {
    # SymPy returns [] for identities; ours returns [x] ("every x")
    'sympy/test_solvers/test_issue_4793/5': {'na': 'identity: SymPy convention [] vs ours [x]'},
    'sympy/test_solvers/test_unrad1/26': {'na': 'identity for x>0: SymPy convention [] vs ours [x]'},
    # float argument is ln(10) rounded; floor/ceil sit on the integer boundary
    'sympy/test_evalf/test_issue_13098/3': {'na': 'float rounding at an integer boundary'},
    'sympy/test_evalf/test_issue_13098/4': {'na': 'float rounding at an integer boundary'},
    # -oo + I*pi/2: SymPy keeps the finite imaginary part, ours absorbs it
    'sympy/test_simplify/test_issue_22020/1': {'uncertain': 'infinity arithmetic convention'},
    # ln(1 - tanh x) + ln(1 + e^(2x)) = ln 2 holds for every x: the reference [] is wrong
    'sympy/test_solvers/test_uselogcombine/2': {'na': 'identity; reference [] is not the solution set'},
    # SymPy's solve_poly_system returns [] when it cannot solve the quintic; ours finds the real root
    'sympy/test_polysys/test_solve_poly_system/11': {'na': 'SymPy gives up ([]); not a reference'},
    'sympy/test_polysys/test_solve_poly_system/12': {'na': 'SymPy gives up on the quintic factor'},
    # real cube root vs our principal (complex) cube root of negatives
    'sympy/test_solveset/test_issue_9778/3': {'uncertain': 'real vs principal cube root convention'},
    'sympy/test_solvers/test_unrad_fail/1': {'uncertain': 'real vs principal cube root convention'},
    # y is declared positive, yet the reference is y = 0 (solveset ignores the assumption)
    'sympy/test_solveset/test_exponential_symbols/4': {'na': 'reference contradicts assume(y>0)'},
    # ln(x) + acosh(1/x) -> oo + i pi/2 (complex infinity, ours undef); SymPy drops the imaginary part
    'sympy/test_limits/test_issue_10868/3': {'uncertain': 'complex infinity vs oo convention'},
    # an XFAIL test: the pole of exp(z)/(z - i pi a/4)^n is at i pi a/4, so the residue at
    # i pi a is 0 (ours); the asserted value is the residue at the pole
    'sympy/test_residues/test_expressions_failing/1': {'na': 'XFAIL test; the residue at a regular point is 0'},
}
