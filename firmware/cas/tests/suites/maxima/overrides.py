"""Manual adjustments for the Maxima suite translation.

SKIP_FILES: rtest files that test Maxima internals (display, plotting, the
Lisp translator, parser continuations, random state, structures, ...); no
case in them has a counterpart in our CAS.

OVERRIDES: case id -> fields merged into the generated case after review,
e.g. {'na': 'reason'} or {'uncertain': 'reason'} or a corrected 'lines'.
"""
SKIP_FILES = {
    'rtest_tex', 'rtest_unicode_display', 'rtest_plot', 'rtest_plotoptions', 'rtest_translator',
    'rtestparser_continuations_cr', 'rtestparser_continuations_crnl', 'rtestparser_continuations_nl',
    'rtestmt19937', 'rtestdefstruct', 'rtestlabels', 'rtestmcond_display', 'rtest_lapack',
    'rtest_mset', 'rtest_setvar', 'rtest_index_properties', 'rtest_everysome', 'rtest_map',
    'rtest_rules', 'rtest_extensions', 'rtest_trace', 'rtest_scalarp', 'rtest_dot',
    'rtest_nfloat', 'rtest_ask1',
}

OVERRIDES = {
    # the rtest expectation has the wrong sign: (erf(x - e^-e^x) - erf(x)) e^(e^x) e^(x^2)
    # ~ -erf'(x) e^(x^2) = -2/sqrt(pi)  (SymPy agrees)
    'maxima/rtest_limit_gruntz/25': {'uncertain': 'reference answer has the wrong sign'},
    # numeric quadrature (mpmath) gives 3*pi/4 = 2.35619449..., our answer
    'maxima/rtest_atan2/65': {'uncertain': 'reference pi/4 disagrees with numeric quadrature 3*pi/4'},
    # Maxima returns the Cauchy principal value; the integrals diverge
    'maxima/rtestint/131': {'check': 'error'},
    'maxima/rtestint/253': {'check': 'error'},
    # the limit depends on the integration constant of Maxima's own antiderivative
    'maxima/rtest_limit_extra/63': {'uncertain': 'antiderivative constant differs between systems'},
    'maxima/rtest_limit_extra/64': {'uncertain': 'antiderivative constant differs between systems'},
    # real-domain convention: ln(x) -> -inf at 0 (the left side is complex);
    # Maxima answers complex infinity
    'maxima/rtest_limit_extra/2': {'na': 'real-domain convention for ln(x) at 0'},
    # `piece` is a Maxima system variable (part functions)
    'maxima/rtest6a/19': {'na': 'maxima system variable piece'},
    # Maxima takes the real cube root of -1.0 (domain real); ours is principal
    'maxima/rtest16/100': {'uncertain': 'real vs principal odd root convention'},
    'maxima/rtest16/101': {'uncertain': 'real vs principal odd root convention'},
    # trigsimp(integrate(...)): our antiderivative (derivative checked) differs from Maxima's
    # erfc / abs form by a constant, which the equivalence check of this case cannot allow
    'maxima/rtest3/142': {'uncertain': 'antiderivative differs from the reference by a constant'},
    # taylor(f, z, 0, 9) keeps terms up to z^9; our series(f, z, 0, 10) keeps exponents < 10, so
    # for the Puiseux series of Gamma(1/2, z) it has one more term, z^(19/2)
    'maxima/rtest_taylor/94': {'na': 'Puiseux truncation order differs between taylor and series'},
}
