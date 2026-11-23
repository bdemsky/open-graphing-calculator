"""Maxima -> our CAS name/argument mapping (used by the maxima and rubi
translators).  Values: our name (same arity), or a function(em, args) -> text.
Anything not listed is Untranslatable (the case becomes UNSUPPORTED with the
Maxima function name as the reason)."""
from mparse import Untranslatable


def _e(em, a):
    return em.emit(a)


def recip(our):
    return lambda em, a: '(1/%s(%s))' % (our, ','.join(_e(em, x) for x in a))


def of_recip(our):
    return lambda em, a: '%s(1/(%s))' % (our, _e(em, a[0]))


def _limit(em, a):
    if len(a) == 1:
        raise Untranslatable('limit of infinitesimal expression')
    if len(a) == 3:
        return 'limit(%s,%s,%s)' % (_e(em, a[0]), _e(em, a[1]), _e(em, a[2]))
    if len(a) == 4:
        d = a[3]
        if d[0] == 'sym' and d[1] in ('plus', 'minus'):
            return 'limit(%s,%s,%s,%s)' % (_e(em, a[0]), _e(em, a[1]), _e(em, a[2]),
                                            '1' if d[1] == 'plus' else '-1')
    raise Untranslatable('limit arguments')


def _taylor(em, a):
    # taylor(f, x, a, n) keeps x^n; our series(f, x, a, n) stops below x^n
    if len(a) == 4 and a[3][0] == 'num' and a[1][0] == 'sym':
        return 'series(%s,%s,%s,%d)' % (_e(em, a[0]), _e(em, a[1]), _e(em, a[2]), int(a[3][1]) + 1)
    raise Untranslatable('taylor arguments')


def _diff(em, a):
    if len(a) == 1:
        raise Untranslatable('total differential')
    return 'diff(%s)' % ','.join(_e(em, x) for x in a)


def _integrate(em, a):
    if len(a) in (2, 4):
        return 'integrate(%s)' % ','.join(_e(em, x) for x in a)
    raise Untranslatable('integrate arguments')


def _subst(em, a):
    if len(a) == 3:          # subst(new, old, expr)
        return 'subst(%s,%s,%s)' % (_e(em, a[2]), _e(em, a[1]), _e(em, a[0]))
    if len(a) == 2:          # subst(eq or [eqs], expr)
        return 'subst(%s,%s)' % (_e(em, a[1]), _e(em, a[0]))
    raise Untranslatable('subst arguments')


def _coeff(em, a):
    if len(a) == 2:
        return 'coeff(%s,%s,1)' % (_e(em, a[0]), _e(em, a[1]))
    return 'coeff(%s)' % ','.join(_e(em, x) for x in a)


def _matrix(em, a):
    return '[' + ','.join(_e(em, r) for r in a) + ']'


def _log(em, a):
    if len(a) != 1:
        raise Untranslatable('log arity')
    return 'ln(%s)' % _e(em, a[0])


def _solve(em, a):
    if len(a) == 1:
        raise Untranslatable('solve without variable')
    return 'solve(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))


def _sum(name):
    def f(em, a):
        if len(a) != 4:
            raise Untranslatable(name + ' arguments')
        return '%s(%s)' % (name, ','.join(_e(em, x) for x in a))
    return f


def _partfrac(em, a):
    return 'partfrac(%s)' % ','.join(_e(em, x) for x in a)


def _gcd(em, a):
    if len(a) > 2:
        if a[2][0] == 'sym':           # gcd(p, q, x): main variable hint
            return 'gcd(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))
        raise Untranslatable('gcd arguments')
    return 'gcd(%s)' % ','.join(_e(em, x) for x in a)


def _laplace(em, a):
    return 'laplace(%s)' % ','.join(_e(em, x) for x in a)


def _ilt(em, a):
    return 'ilaplace(%s)' % ','.join(_e(em, x) for x in a)


def _float(em, a):
    return 'approx(%s)' % _e(em, a[0])


def _rectform(em, a):
    return 'expand(%s)' % _e(em, a[0])


def _atan(em, a):
    if len(a) == 2:
        return 'atan2(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))
    return 'atan(%s)' % _e(em, a[0])


def _charpoly(em, a):
    return 'charpoly(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))


def _divide(em, a):
    return 'divide(%s)' % ','.join(_e(em, x) for x in a[:3])


def _li(em, a):
    raise Untranslatable('li')


def _gamma(em, a):
    # GAMMA(a) / gamma(a); Rubi writes the upper incomplete Gamma(a, z) as GAMMA(a, z)
    if len(a) == 2:
        return 'uppergamma(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))
    if len(a) == 1:
        return 'gamma(%s)' % _e(em, a[0])
    raise Untranslatable('gamma arguments')


def _hyp(em, a):
    # hypergeometric([a, b], [c], z) -> hyp2f1(a, b, c, z)
    if len(a) == 3 and a[0][0] == 'list' and a[1][0] == 'list' and len(a[0][1]) == 2 and len(a[1][1]) == 1:
        return 'hyp2f1(%s,%s,%s,%s)' % (_e(em, a[0][1][0]), _e(em, a[0][1][1]), _e(em, a[1][1][0]), _e(em, a[2]))
    raise Untranslatable('function hypergeometric (not 2F1)')


def _ei(em, a):
    # Ei(z) is the exponential integral; Rubi writes ExpIntegralE(n, z) as Ei(n, z)
    if len(a) == 2:
        return 'expint(%s,%s)' % (_e(em, a[0]), _e(em, a[1]))
    return 'Ei(%s)' % _e(em, a[0])


FUNCS = {
    # elementary
    'sqrt': 'sqrt', 'exp': 'exp', 'log': _log, 'sin': 'sin', 'cos': 'cos', 'tan': 'tan',
    'cot': 'cot', 'sec': 'sec', 'csc': 'csc', 'asin': 'asin', 'acos': 'acos', 'atan': _atan,
    'acot': 'acot', 'asec': 'asec', 'acsc': 'acsc', 'sinh': 'sinh', 'cosh': 'cosh', 'tanh': 'tanh',
    'coth': recip('tanh'), 'sech': recip('cosh'), 'csch': recip('sinh'),
    'asinh': 'asinh', 'acosh': 'acosh', 'atanh': 'atanh',
    'acoth': of_recip('atanh'), 'asech': of_recip('acosh'), 'acsch': of_recip('asinh'),
    'abs': 'abs', 'signum': 'sign', 'floor': 'floor', 'ceiling': 'ceil', 'round': 'round',
    'realpart': 're', 'imagpart': 'im', 'conjugate': 'conj', 'carg': 'arg', 'atan2': 'atan2',
    'cabs': 'abs',
    'gamma': _gamma, 'GAMMA': _gamma, 'gamma_incomplete': 'uppergamma', 'hypergeometric': _hyp,
    'factorial': 'factorial', 'binomial': 'binomial', 'erf': 'erf',
    'erfc': 'erfc', 'erfi': 'erfi', 'zeta': 'zeta', 'expintegral_si': 'Si', 'expintegral_ci': 'Ci',
    'expintegral_ei': 'Ei', 'max': 'max', 'min': 'min', 'mod': 'mod', 'hstep': None,
    # commands
    'integrate': _integrate, 'diff': _diff, 'limit': _limit, 'taylor': _taylor,
    'sum': _sum('sum'), 'product': _sum('product'), 'solve': _solve, 'subst': _subst,
    'ratsimp': 'ratsimp', 'fullratsimp': 'ratsimp', 'factor': 'factor', 'expand': 'expand',
    'partfrac': _partfrac, 'gcd': _gcd, 'lcm': 'lcm', 'determinant': 'det', 'invert': 'inv',
    'transpose': 'transpose', 'matrix': _matrix, 'rank': 'rank', 'charpoly': _charpoly,
    'trigsimp': 'trigsimp', 'trigexpand': 'texpand', 'trigreduce': 'tlin', 'logcontract': 'lncollect',
    'radcan': 'simplify', 'coeff': _coeff, 'hipow': 'degree', 'resultant': 'resultant',
    'primep': 'isprime', 'next_prime': 'nextprime', 'prev_prime': 'prevprime', 'num': 'numer',
    'denom': 'denom', 'quotient': 'quo', 'remainder': 'rem', 'divide': _divide,
    'laplace': _laplace, 'ilt': _ilt, 'float': _float, 'rectform': _rectform,
    'sqfr': 'sqrfree', 'discriminant': 'discriminant', 'ratexpand': 'expand', 'exponentialize': 'trig2exp',
    'demoivre': 'exp2trig', 'logexpand': 'lnexpand', 'mat_trace': 'trace', 'trace': None,
    'ident': 'identity', 'nullspace': 'ker', 'echelon': None, 'inv_mod': 'invmod', 'power_mod': 'powmod',
    'divisors': 'divisors', 'totient': 'euler', 'jacobian': 'jacobian', 'hessian': 'hessian',
    'Ei': _ei, 'Si': 'Si', 'Ci': 'Ci', 'tlimit': _limit, 'li': _li, 'eigenvalues': None, 'polylog': 'polylog',
    'ProductLog': 'lambertw', 'lambert_w': 'lambertw', 'FresnelS': 'fresnels', 'FresnelC': 'fresnelc',
    'fresnel_s': 'fresnels', 'fresnel_c': 'fresnelc', 'Shi': 'Shi', 'Chi': 'Chi', 'expintegral_shi': 'Shi',
    'expintegral_chi': 'Chi', 'expintegral_e': 'expint', 'expintegral_li': 'li', 'Li': 'li',
    'elliptic_f': 'elliptic_f', 'elliptic_e': 'elliptic_e', 'elliptic_pi': 'elliptic_pi',
    'bessel_j': 'besselj', 'bessel_y': 'bessely', 'bessel_i': 'besseli', 'bessel_k': 'besselk', 'xthru': 'together',
    'combine': None,
}
FUNCS = {k: v for k, v in FUNCS.items() if v is not None}

CONSTS = {'%pi': 'pi', '%e': 'e', '%i': 'i', '%gamma': 'euler_gamma', 'inf': 'inf', 'minf': '(-inf)',
          'true': 'true', 'false': 'false', 'infinity': None, '%phi': '((1+sqrt(5))/2)',
          'und': None, 'ind': None, 'zeroa': None, 'zerob': None}
