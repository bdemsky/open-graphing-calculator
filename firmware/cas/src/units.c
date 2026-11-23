/*
 * units.c -- physical units and constants.
 *
 * Representation: a quantity is an ordinary product  value * unit-symbols,
 * e.g. 9.8_m/s^2 is 9.8*_m*_s^-2.  The parser reads a number with its unit
 * tail as one primary, so it binds tighter than '*' and '/': 1/2_s is
 * 1/(2*_s) and 6_m/2_s is (6*_m)/(2*_s) (rule documented in parse.c).  Unit symbols are symbols whose name starts
 * with '_' and is found in the unit table (optionally with an SI prefix:
 * _km, _uF); constants (_c, _hP, ...) likewise.  They are flagged in the
 * symbol table (assume bits U_UNIT / U_CONST, plus "positive"), so the
 * automatic simplifier treats them as positive real factors: products,
 * quotients and integer/rational powers of quantities work with no special
 * code, and like units combine (2_m+3_m = 5_m).
 *
 * After every top-level evaluation units_post() (via ext_post in cas.c)
 *   - checks dimensions of sums ("incompatible units") and converts sums of
 *     different but compatible units to SI base units,
 *   - converts products that mix units of the same dimension (_km*_m,
 *     _m/_ft) to SI,
 *   - converts dimensionless unit arguments of transcendental functions
 *     (sin(30_deg) = 1/2) and rejects dimensioned ones (in degree mode
 *     eval.c leaves such angle-unit arguments unscaled, units_has_angle),
 *   - replaces constants by value*units in approximate mode or when the
 *     result contains floats.
 * Conversion factors are exact (rationals, pi); a converted coefficient whose
 * decimal expansion does not terminate becomes a float.
 *
 * Uncertainty propagation (unc.c): the value part of a quantity is any
 * expression, and an uncertain value is the linear form v + s*_uncN, so
 * (2.0±0.1)_m is 2.0_m + 0.1_m*_unc1.  Everything here only multiplies the
 * value by exact factors (s_mul2) and splits terms with usplit (exported as
 * units_split for unc.c's printer), so units carry through unchanged.
 *
 * Who calls what:
 *   parse.c    units_mark() for every identifier starting with '_' (flags
 *              it), units_name_sym() while reading a unit tail such as
 *              _kg*m^2/s^2 (the names after the first '_' need no '_');
 *   xcmds.c    ext_post() calls units_post() after every top-level
 *              evaluation; the commands convert, deltaconvert, usimplify,
 *              uvalue and uunits are dispatched to units_cmd();
 *   print.c    units_print() for products, so 29.4*_m*_s^-1 prints as
 *              29.4_m/s (which the parser reads back).
 *
 * Dimensions are vectors of 7 small integers, the exponents of the SI base
 * quantities (length, mass, time, current, temperature, amount, luminous
 * intensity); a unit's SI factor is stored as a string in CAS syntax and
 * parsed on demand, so the tables stay small, exact and readable.  Two
 * expressions have compatible units when their dimension vectors are equal.
 *
 * Memory: everything is computed in scratch during the evaluation; the
 * only persistent effect is the U_UNIT / U_CONST / positivity flags stored
 * in the symbol table by units_mark (they survive the symbol GC and are
 * written by cas_save like other assumption flags).  The static ubudget
 * counter makes the module single-threaded like the rest of the library.
 */
#include "cas_int.h"
#include <math.h>
#include <string.h>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* Extra bits in symrec.assume (cas_int.h uses 1..8 for A_POS, A_NEG,
 * A_NONNEG, A_INT): symbol is a unit / a physical constant. */
#define U_UNIT 0x40
#define U_CONST 0x20
#define NDIM 7 /* L M T I Theta N J */

enum { UF_P = 1, UF_TEMP = 2 }; /* SI prefixes allowed; absolute temperature with offset */

/* One unit: name without the leading '_', dimension exponents (index order
 * L M T I Theta N J as in base_names below), UF_* flags, and the factor that
 * converts 1 of this unit to the coherent SI unit of the same dimension
 * (e.g. 1 ft = 381/1250 m).  Factors are exact: definitions (inch = 2.54 cm,
 * pound = 0.45359237 kg, standard gravity 9.80665 m/s^2, US gallon = 231
 * in^3, ...) are written out as rationals or products so nothing is rounded;
 * only pc, deg, arcmin, arcsec and rev involve pi. */
typedef struct {
    char name[7];
    int8_t d[NDIM];
    uint8_t fl;
    const char *f; /* factor to SI (CAS syntax) */
} unit_t;

/* D(...) spells out a dimension vector in base-quantity order. */
#define D(L, M, T, I, H, N, J) {L, M, T, I, H, N, J}
/* Unit table.  Lookup is linear (about 100 entries; done only while parsing
 * and post-processing).  Note the SI base unit of mass is the kilogram, so the
 * table entry is the gram (factor 1/1000) and _kg is "k" + "g".
 * Temperatures: degC / degF / degR have the factor of a temperature
 * *difference* to kelvin; their offsets are applied only by convert
 * (convert1). */
static const unit_t units[] = {
    /* length */
    {"m", D(1, 0, 0, 0, 0, 0, 0), UF_P, "1"},
    {"in", D(1, 0, 0, 0, 0, 0, 0), 0, "127/5000"},
    {"ft", D(1, 0, 0, 0, 0, 0, 0), 0, "381/1250"},
    {"yd", D(1, 0, 0, 0, 0, 0, 0), 0, "1143/1250"},
    {"mi", D(1, 0, 0, 0, 0, 0, 0), 0, "201168/125"},
    {"nmi", D(1, 0, 0, 0, 0, 0, 0), 0, "1852"},
    {"au", D(1, 0, 0, 0, 0, 0, 0), 0, "149597870700"},
    {"ly", D(1, 0, 0, 0, 0, 0, 0), 0, "9460730472580800"},
    {"pc", D(1, 0, 0, 0, 0, 0, 0), 0, "149597870700*648000/pi"},
    {"Ang", D(1, 0, 0, 0, 0, 0, 0), 0, "10^-10"},
    /* area, volume */
    {"ha", D(2, 0, 0, 0, 0, 0, 0), 0, "10000"},
    {"acre", D(2, 0, 0, 0, 0, 0, 0), 0, "316160658/78125"},
    {"L", D(3, 0, 0, 0, 0, 0, 0), UF_P, "1/1000"},
    {"l", D(3, 0, 0, 0, 0, 0, 0), UF_P, "1/1000"},
    {"gal", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/125000000000"},
    {"qt", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/500000000000"},
    {"pt", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/1000000000000"},
    {"cup", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/2000000000000"},
    {"floz", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/16000000000000"},
    {"tbsp", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/32000000000000"},
    {"tsp", D(3, 0, 0, 0, 0, 0, 0), 0, "473176473/96000000000000"},
    /* time, frequency, speed */
    {"s", D(0, 0, 1, 0, 0, 0, 0), UF_P, "1"},
    {"min", D(0, 0, 1, 0, 0, 0, 0), 0, "60"},
    {"h", D(0, 0, 1, 0, 0, 0, 0), 0, "3600"},
    {"hr", D(0, 0, 1, 0, 0, 0, 0), 0, "3600"},
    {"d", D(0, 0, 1, 0, 0, 0, 0), 0, "86400"},
    {"wk", D(0, 0, 1, 0, 0, 0, 0), 0, "604800"},
    {"yr", D(0, 0, 1, 0, 0, 0, 0), 0, "31557600"},
    {"Hz", D(0, 0, -1, 0, 0, 0, 0), UF_P, "1"},
    {"rpm", D(0, 0, -1, 0, 0, 0, 0), 0, "1/60"},
    {"Bq", D(0, 0, -1, 0, 0, 0, 0), UF_P, "1"},
    {"Ci", D(0, 0, -1, 0, 0, 0, 0), 0, "37*10^9"},
    {"mph", D(1, 0, -1, 0, 0, 0, 0), 0, "1397/3125"},
    {"kph", D(1, 0, -1, 0, 0, 0, 0), 0, "5/18"},
    {"knot", D(1, 0, -1, 0, 0, 0, 0), 0, "463/900"},
    /* mass */
    {"g", D(0, 1, 0, 0, 0, 0, 0), UF_P, "1/1000"},
    {"t", D(0, 1, 0, 0, 0, 0, 0), 0, "1000"},
    {"lb", D(0, 1, 0, 0, 0, 0, 0), 0, "45359237/10^8"},
    {"oz", D(0, 1, 0, 0, 0, 0, 0), 0, "45359237/16*10^-8"},
    {"ton", D(0, 1, 0, 0, 0, 0, 0), 0, "45359237/50000"},
    {"slug", D(0, 1, 0, 0, 0, 0, 0), 0, "45359237*980665/(381/1250)*10^-13"},
    {"ct", D(0, 1, 0, 0, 0, 0, 0), 0, "1/5000"},
    {"u", D(0, 1, 0, 0, 0, 0, 0), 0, "166053906892*10^-38"},
    {"Da", D(0, 1, 0, 0, 0, 0, 0), UF_P, "166053906892*10^-38"},
    /* force, energy, power */
    {"N", D(1, 1, -2, 0, 0, 0, 0), UF_P, "1"},
    {"dyn", D(1, 1, -2, 0, 0, 0, 0), 0, "10^-5"},
    {"lbf", D(1, 1, -2, 0, 0, 0, 0), 0, "45359237*980665*10^-13"},
    {"kgf", D(1, 1, -2, 0, 0, 0, 0), 0, "980665*10^-5"},
    {"J", D(2, 1, -2, 0, 0, 0, 0), UF_P, "1"},
    {"cal", D(2, 1, -2, 0, 0, 0, 0), UF_P, "4184/1000"},
    {"eV", D(2, 1, -2, 0, 0, 0, 0), UF_P, "1602176634*10^-28"},
    {"Wh", D(2, 1, -2, 0, 0, 0, 0), UF_P, "3600"},
    {"Btu", D(2, 1, -2, 0, 0, 0, 0), 0, "105505585262*10^-8"},
    {"erg", D(2, 1, -2, 0, 0, 0, 0), 0, "10^-7"},
    {"W", D(2, 1, -3, 0, 0, 0, 0), UF_P, "1"},
    {"hp", D(2, 1, -3, 0, 0, 0, 0), 0, "550*381/1250*45359237*980665*10^-13"},
    /* pressure */
    {"Pa", D(-1, 1, -2, 0, 0, 0, 0), UF_P, "1"},
    {"bar", D(-1, 1, -2, 0, 0, 0, 0), UF_P, "10^5"},
    {"atm", D(-1, 1, -2, 0, 0, 0, 0), 0, "101325"},
    {"psi", D(-1, 1, -2, 0, 0, 0, 0), 0, "45359237*980665*10^-13/(127/5000)^2"},
    {"mmHg", D(-1, 1, -2, 0, 0, 0, 0), 0, "101325/760"},
    {"Torr", D(-1, 1, -2, 0, 0, 0, 0), 0, "101325/760"},
    {"inHg", D(-1, 1, -2, 0, 0, 0, 0), 0, "3386389/1000"},
    /* temperature */
    {"K", D(0, 0, 0, 0, 1, 0, 0), UF_P, "1"},
    {"degC", D(0, 0, 0, 0, 1, 0, 0), UF_TEMP, "1"},
    {"degF", D(0, 0, 0, 0, 1, 0, 0), UF_TEMP, "5/9"},
    {"degR", D(0, 0, 0, 0, 1, 0, 0), UF_TEMP, "5/9"},
    /* electromagnetism */
    {"A", D(0, 0, 0, 1, 0, 0, 0), UF_P, "1"},
    {"C", D(0, 0, 1, 1, 0, 0, 0), UF_P, "1"},
    {"Ah", D(0, 0, 1, 1, 0, 0, 0), UF_P, "3600"},
    {"V", D(2, 1, -3, -1, 0, 0, 0), UF_P, "1"},
    {"ohm", D(2, 1, -3, -2, 0, 0, 0), UF_P, "1"},
    {"S", D(-2, -1, 3, 2, 0, 0, 0), UF_P, "1"},
    {"F", D(-2, -1, 4, 2, 0, 0, 0), UF_P, "1"},
    {"H", D(2, 1, -2, -2, 0, 0, 0), UF_P, "1"},
    {"Wb", D(2, 1, -2, -1, 0, 0, 0), UF_P, "1"},
    {"T", D(0, 1, -2, -1, 0, 0, 0), UF_P, "1"},
    {"gauss", D(0, 1, -2, -1, 0, 0, 0), 0, "10^-4"},
    /* angle (dimensionless), amount, light, radiation */
    {"rad", D(0, 0, 0, 0, 0, 0, 0), UF_P, "1"},
    {"deg", D(0, 0, 0, 0, 0, 0, 0), 0, "pi/180"},
    {"arcmin", D(0, 0, 0, 0, 0, 0, 0), 0, "pi/10800"},
    {"arcsec", D(0, 0, 0, 0, 0, 0, 0), 0, "pi/648000"},
    {"rev", D(0, 0, 0, 0, 0, 0, 0), 0, "2*pi"},
    {"sr", D(0, 0, 0, 0, 0, 0, 0), 0, "1"},
    {"mol", D(0, 0, 0, 0, 0, 1, 0), UF_P, "1"},
    {"cd", D(0, 0, 0, 0, 0, 0, 1), UF_P, "1"},
    {"lm", D(0, 0, 0, 0, 0, 0, 1), UF_P, "1"},
    {"lx", D(-2, 0, 0, 0, 0, 0, 1), UF_P, "1"},
    {"Gy", D(2, 0, -2, 0, 0, 0, 0), UF_P, "1"},
    {"Sv", D(2, 0, -2, 0, 0, 0, 0), UF_P, "1"},
};
#define NUNITS ((int)(sizeof units / sizeof units[0]))

/* CODATA 2022 (exact where the 2019 SI defines them).  Each constant is
 * name (without '_'), value in CAS syntax and SI units as a small string
 * parsed by ustr ("J/mol/K"; "" = dimensionless).  Derived constants are
 * written as exact expressions of the defining ones (R = NA kB,
 * Fc = NA qe, hbar = hP/(2 pi), Stefan-Boltzmann sigma = 2 pi^5 kB^4 /
 * (15 hP^3 c^2), Phi0 = hP/(2 qe)), so they agree exactly with them. */
typedef struct {
    char name[6];
    const char *val, *u;
} const_t;
static const const_t consts[] = {
    {"c", "299792458", "m/s"},
    {"hP", "662607015*10^-42", "J*s"},
    {"hbar", "662607015*10^-42/(2*pi)", "J*s"},
    {"qe", "1602176634*10^-28", "C"},
    {"kB", "1380649*10^-29", "J/K"},
    {"NA", "602214076*10^15", "mol^-1"},
    {"R", "602214076*1380649*10^-14", "J/mol/K"},
    {"Fc", "602214076*1602176634*10^-13", "C/mol"},
    {"G", "667430*10^-16", "m^3/kg/s^2"},
    {"g0", "980665*10^-5", "m/s^2"},
    {"eps0", "88541878188*10^-22", "F/m"},
    {"mu0", "125663706127*10^-17", "N/A^2"},
    {"ke", "89875517862/10", "N*m^2/C^2"},
    {"Z0", "376730313412*10^-9", "ohm"},
    {"me", "91093837139*10^-41", "kg"},
    {"mp", "167262192595*10^-38", "kg"},
    {"mn", "167492750056*10^-38", "kg"},
    {"mu", "166053906892*10^-38", "kg"},
    {"sigma", "2*pi^5*1380649^4*10^-116/(15*662607015^3*10^-126*299792458^2)", "W/m^2/K^4"},
    {"Rinf", "10973731568157*10^-6", "m^-1"},
    {"a0", "529177210544*10^-22", "m"},
    {"alpha", "72973525643*10^-13", ""},
    {"muB", "92740100657*10^-34", "J/T"},
    {"muN", "50507837393*10^-37", "J/T"},
    {"Phi0", "662607015*10^-42/(2*1602176634*10^-28)", "Wb"},
    {"Vm", "2241396954*10^-11", "m^3/mol"},
};
#define NCONSTS ((int)(sizeof consts / sizeof consts[0]))

/* SI prefixes and their powers of ten ("u" = micro, "da" = deca). */
static const struct {
    char p[3];
    int8_t e;
} prefixes[] = {
    {"da", 1}, {"Y", 24}, {"Z", 21}, {"E", 18}, {"P", 15}, {"T", 12}, {"G", 9}, {"M", 6}, {"k", 3},
    {"h", 2},  {"d", -1}, {"c", -2}, {"m", -3}, {"u", -6}, {"n", -9}, {"p", -12}, {"f", -15}, {"a", -18},
};

/* Index of the unit named exactly s[0..n) (no prefix), or -1. */
static int exact_unit(const char *s, size_t n) {
    for (int i = 0; i < NUNITS; i++)
        if (strlen(units[i].name) == n && !memcmp(units[i].name, s, n)) return i;
    return -1;
}

/* unit name (without '_') -> index, *pe = power-of-ten prefix; -1 if unknown.
 * An exact table name is tried first (_min, _nmi, _mmHg, _cd are entries
 * of their own); otherwise the first prefix whose remainder is a unit
 * flagged UF_P is taken (_km, _dam, _uF, _hPa, _kg).  Units without UF_P
 * (ft, mi, min, ...) accept no prefix. */
static int ulook(const char *s, size_t n, int *pe) {
    *pe = 0;
    int i = exact_unit(s, n);
    if (i >= 0) return i;
    for (size_t k = 0; k < sizeof prefixes / sizeof prefixes[0]; k++) {
        size_t pl = strlen(prefixes[k].p);
        if (n > pl && !memcmp(s, prefixes[k].p, pl)) {
            i = exact_unit(s + pl, n - pl);
            if (i >= 0 && (units[i].fl & UF_P)) {
                *pe = prefixes[k].e;
                return i;
            }
        }
    }
    return -1;
}

/* Index of the constant named s[0..n) (without '_'), or -1. */
static int clook(const char *s, size_t n) {
    for (int i = 0; i < NCONSTS; i++)
        if (strlen(consts[i].name) == n && !memcmp(consts[i].name, s, n)) return i;
    return -1;
}

/* Called by the parser for every identifier starting with '_': flags units
 * and constants. Returns 1 for a unit, 2 for a constant, 0 otherwise.
 * Constants are looked up first (so _mu is the atomic mass constant, not a
 * prefixed unit).  The symbol gets A_POS | A_NONNEG (the simplifier then
 * treats it as a positive real, e.g. sqrt(_m^2) = _m) plus U_UNIT or U_CONST.
 * Builtin ids are never flagged. */
int units_mark(int id) {
    const char *nm = sym_name(id);
    if (nm[0] != '_') return 0;
    size_t n = strlen(nm + 1);
    int pe, r = clook(nm + 1, n) >= 0 ? 2 : ulook(nm + 1, n, &pe) >= 0 ? 1 : 0;
    if (r && id >= S__COUNT) K->syms[id].assume |= (uint8_t)(A_POS | A_NONNEG | (r == 1 ? U_UNIT : U_CONST));
    return r;
}

/* unit name inside a unit tail (with or without '_') -> symbol id, -1 if not
 * a unit.  Interns "_name" (names longer than 20 characters are rejected
 * before the local buffer is used) and flags it via units_mark. */
int units_name_sym(const char *s, size_t n) {
    char buf[24];
    if (n && s[0] == '_') s++, n--;
    int pe;
    if (!n || n > 20 || ulook(s, n, &pe) < 0) return -1;
    buf[0] = '_';
    memcpy(buf + 1, s, n);
    int id = sym_intern(buf, n + 1);
    units_mark(id);
    return id;
}

/* Unit symbol node for a table name such as "m" or "kg"; throws
 * CAS_ERR_DOMAIN "unknown unit" if the name is not a unit. */
static E *usym(const char *name) {
    int id = units_name_sym(name, strlen(name));
    if (id < 0) cas_throw(CAS_ERR_DOMAIN, "unknown unit");
    return mk_sym(id);
}

/* Is e a unit symbol / a constant symbol (flags set by units_mark)? */
static int is_unit(E *e) { return e->t == E_SYM && e->s >= S__COUNT && (K->syms[e->s].assume & U_UNIT); }
static int is_const(E *e) { return e->t == E_SYM && e->s >= S__COUNT && (K->syms[e->s].assume & U_CONST); }

/* unit factor u or u^k (k integer): returns 1 and sets *u, *k if f has this
 * form.  Rational powers (sqrt(_Hz)) are not unit factors and stay in the
 * value part. */
static int unit_factor(E *f, E **u, long *k) {
    if (is_unit(f)) {
        *u = f, *k = 1;
        return 1;
    }
    if (f->t == E_POW && is_unit(K0(f)) && num_is_small_int(K1(f), k)) {
        *u = K0(f);
        return 1;
    }
    return 0;
}

/* term -> value * units: splits a (canonical) product into the product of
 * its unit factors (*un, 1 if none) and everything else (*val, which may
 * still contain constants or variables).  A non-product is either all unit
 * or all value.  Both results are canonical (s_mul) and in scratch. */
static void usplit(E *t, E **val, E **un) {
    if (t->t != E_MUL) {
        E *u;
        long k;
        if (unit_factor(t, &u, &k)) *val = K->c1, *un = t;
        else *val = t, *un = K->c1;
        return;
    }
    E **v = salloc(sizeof(E *) * t->n), **w = salloc(sizeof(E *) * t->n);
    uint32_t nv = 0, nw = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        E *u;
        long k;
        if (unit_factor(EK(t)[i], &u, &k)) w[nw++] = EK(t)[i];
        else v[nv++] = EK(t)[i];
    }
    *val = s_mul(nv, v);
    *un = s_mul(nw, w);
}

/* usplit for unc.c (printing value±unc with a unit tail). */
void units_split(E *t, E **val, E **un);
void units_split(E *t, E **val, E **un) { usplit(t, val, un); }

/* Expressions are DAGs (shared subtrees), so walks are budgeted: when the
 * budget runs out, units are assumed absent and the result is left alone. */
static uint32_t ubudget;

/* 1 if e contains a unit or constant symbol (budgeted walk, see above). */
static int has_units(E *e) {
    stack_check();
    if (!ubudget) return 0;
    ubudget--;
    if (is_unit(e) || is_const(e)) return 1;
    if (e->t <= E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_units(EK(e)[i])) return 1;
    return 0;
}

/* any unit or constant symbol interned at all? (cheap pre-check) */
static int units_in_use(void) {
    for (uint32_t i = S__COUNT; i < K->nsyms; i++)
        if (K->syms[i].assume & (U_UNIT | U_CONST)) return 1;
    return 0;
}

/* Parse a table string (factor or constant value) into a simplified exact
 * expression in scratch. */
static E *parse_val(const char *s) { return simp(parse(s)); }

/* A resolved unit symbol: table index and power-of-ten prefix. */
typedef struct {
    int idx, pe;
} uref;

/* Resolve a unit symbol node (name "_<prefix><unit>"); throws
 * CAS_ERR_DOMAIN if the name is not a unit. */
static uref uinfo(E *u) {
    const char *nm = sym_name(u->s) + 1;
    uref r;
    r.idx = ulook(nm, strlen(nm), &r.pe);
    if (r.idx < 0) cas_throw(CAS_ERR_DOMAIN, "unknown unit");
    return r;
}

/* dimension and SI factor of a units product (factor may be NULL).
 * For un = prod u_i^k_i:  d = sum k_i dim(u_i)  and
 * *fac = prod (factor(u_i) * 10^prefix(u_i))^k_i  (exact).  *ntemp, if
 * requested, counts the temperature units with an offset (degC, degF, degR)
 * among the factors.  Non-unit factors are ignored. */
static void udim(E *un, int d[NDIM], E **fac, int *ntemp) {
    memset(d, 0, sizeof(int) * NDIM);
    if (fac) *fac = K->c1;
    if (ntemp) *ntemp = 0;
    uint32_t n = un->t == E_MUL ? un->n : 1;
    E **fs = un->t == E_MUL ? EK(un) : &un;
    for (uint32_t i = 0; i < n; i++) {
        E *u;
        long k;
        if (!unit_factor(fs[i], &u, &k)) continue;
        uref r = uinfo(u);
        for (int j = 0; j < NDIM; j++) d[j] += (int)k * units[r.idx].d[j];
        if (fac) *fac = s_mul2(*fac, s_powi(s_mul2(parse_val(units[r.idx].f), s_powi(mk_int(10), r.pe)), k));
        if (ntemp && (units[r.idx].fl & UF_TEMP)) *ntemp += 1;
    }
}

/* 1 if e contains a dimensionless unit, i.e. an angle (_rad, _deg, _arcmin,
 * _arcsec, _rev; also _sr).  eval.c uses it to skip the degree-mode scaling
 * of trig arguments that already carry an angle unit (sin(30_deg) in DEG
 * mode).  Walk limited to 1000 nodes (DAGs). */
static int angle_walk(E *e, int *budget) {
    stack_check();
    if (--*budget < 0) return 0;
    if (is_unit(e)) {
        const char *nm = sym_name(e->s) + 1;
        int pe, idx = ulook(nm, strlen(nm), &pe);
        if (idx < 0) return 0;
        for (int j = 0; j < NDIM; j++)
            if (units[idx].d[j]) return 0;
        return 1;
    }
    if (e->t <= E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (angle_walk(EK(e)[i], budget)) return 1;
    return 0;
}
int units_has_angle(E *e) {
    int budget = 1000;
    return units_in_use() && angle_walk(e, &budget);
}

/* Equal dimension vectors? */
static int dim_eq(const int *a, const int *b) { return !memcmp(a, b, sizeof(int) * NDIM); }
/* Dimensionless (all exponents zero)? */
static int dim_zero(const int *a) {
    for (int j = 0; j < NDIM; j++)
        if (a[j]) return 0;
    return 1;
}

static const char *const base_names[NDIM] = {"m", "kg", "s", "A", "K", "mol", "cd"};

/* The product of SI base units m^d0 kg^d1 s^d2 ... for a dimension vector
 * (1 for a dimensionless one). */
static E *si_base(const int d[NDIM]) {
    E *r = K->c1;
    for (int j = 0; j < NDIM; j++)
        if (d[j]) r = s_mul2(r, s_powi(usym(base_names[j]), d[j]));
    return r;
}

/* Converted coefficients: rationals with a non-terminating decimal expansion
 * and irrational constants (pi from pc, deg) become floats, unless `keep`
 * (dimensionless results such as 30_deg -> pi/6 stay exact). */
static E *fixcoef(E *v, int keep) {
    if (v->t != E_RAT) return !keep && !is_num(v) && is_const_expr(v) ? evalf(v) : v;
    E *d = K1(v);
    while (int_mod_u(d, 2) == 0) d = int_tdiv(d, K->c2);
    while (int_mod_u(d, 5) == 0) d = int_tdiv(d, mk_int(5));
    return int_is_one(d) ? v : mk_flt(num_to_double(v));
}

/* Convert one term value * units to value * factor * (SI base units). */
static E *to_si(E *t) {
    E *val, *un, *f;
    int d[NDIM];
    usplit(t, &val, &un);
    udim(un, d, &f, NULL);
    return s_mul2(fixcoef(s_mul2(val, f), dim_zero(d)), si_base(d));
}

/* Raise the standard dimension error (never returns). */
static void incompatible(void) { cas_throw(CAS_ERR_DOMAIN, "incompatible units"); }

/* dimension of a term: 1 known, 0 unknown (free variables without units).
 * A term whose value part still contains units or constants (e.g. inside a
 * function call) is "unknown"; a unitless term is dimensionless only if it
 * is a constant expression (so x + 3_m is accepted, 5 + 3_m is not). */
static int term_dim(E *t, int d[NDIM]) {
    E *val, *un;
    usplit(t, &val, &un);
    if (has_units(val)) return 0;
    if (is_one(un)) {
        memset(d, 0, sizeof(int) * NDIM);
        return is_const_expr(val);
    }
    udim(un, d, NULL, NULL);
    return 1;
}

/* Dimension check of a sum (called bottom-up from unorm):
 *  - all terms of known dimension must agree, else "incompatible units";
 *  - if every unit-carrying term has the very same unit product (as in
 *    x*_m + 3_m) and no term is a unitless constant, the sum is left alone;
 *  - otherwise each term is converted to SI base units so the simplifier can
 *    combine them (1_ft + 1_in -> 0.3302_m), except that sums involving a
 *    bare offset temperature (degC/degF/degR) are refused: adding absolute
 *    temperatures is ambiguous, convert / deltaconvert must be used.
 * Returns the (possibly rewritten) sum. */
static E *check_sum(E *e) {
    int d0[NDIM], d[NDIM], known = 0, any = 0, same = 1, temp = 0;
    E *first = NULL;
    for (uint32_t i = 0; i < e->n; i++) {
        E *t = EK(e)[i], *val, *un;
        usplit(t, &val, &un);
        int hu = !is_one(un);
        any |= hu;
        if (hu) {
            int nt;
            udim(un, d, NULL, &nt);
            if (nt && un->t != E_MUL) temp = 1;
            if (!first) first = un;
            else if (!expr_eq(first, un)) same = 0;
        } else {
            if (!term_dim(t, d)) continue;
            same = 0;
        }
        if (!known) memcpy(d0, d, sizeof d0), known = 1;
        else if (!dim_eq(d0, d)) incompatible();
    }
    if (!any || same) return e;
    if (temp) cas_throw(CAS_ERR_DOMAIN, "incompatible units: use convert for temperatures");
    E **k = salloc(sizeof(E *) * e->n);
    for (uint32_t i = 0; i < e->n; i++) k[i] = to_si(EK(e)[i]);
    return s_add(e->n, k);
}

/* 1 if the dimension vectors are proportional with the same zero pattern
 * (a = lambda b; both zero counts too), e.g. m and ft, Hz and s, m^3 and
 * L.  Used to detect products that mix units of one kind. */
static int parallel(const int8_t *a, const int8_t *b) {
    /* a = lambda * b for some lambda (including both zero) */
    for (int i = 0; i < NDIM; i++)
        for (int j = 0; j < NDIM; j++)
            if (a[i] * b[j] != a[j] * b[i]) return 0;
    for (int i = 0; i < NDIM; i++)
        if ((a[i] == 0) != (b[i] == 0)) return 0;
    return 1;
}

/* A product whose unit part contains two units of parallel dimension
 * (_km*_m, _m/_ft, _Hz*_s, _deg*_rad) is converted to SI base units so the
 * units combine or cancel; other products are returned unchanged.
 * O(k^2) in the number k of unit factors. */
static E *check_prod(E *e) {
    E *val, *un;
    usplit(e, &val, &un);
    if (un->t != E_MUL) return e;
    for (uint32_t i = 0; i < un->n; i++)
        for (uint32_t j = i + 1; j < un->n; j++) {
            E *a, *b;
            long ka, kb;
            unit_factor(EK(un)[i], &a, &ka);
            unit_factor(EK(un)[j], &b, &kb);
            if (parallel(units[uinfo(a).idx].d, units[uinfo(b).idx].d)) return to_si(e);
        }
    return e;
}

/* Builtin functions whose arguments must be dimensionless: the exp/log,
 * trigonometric and hyperbolic block of BUILTIN_SYMS (F_EXP .. F_ATANH),
 * gamma, erf and zeta .. erfi (Si, Ci, Ei, erfc).  Relies on the symbol
 * order in cas_int.h. */
static int transcendental(int id) {
    return (id >= F_EXP && id <= F_ATANH) || id == F_GAMMA || id == F_ERF || (id >= F_ZETA && id <= F_ERFI);
}

/* Arguments of transcendental functions: a dimensioned argument is an
 * error; a dimensionless one with units (30_deg, 2_rad, a sum of such terms)
 * is converted to a plain number with to_si, which keeps exact results
 * exact (sin(30_deg) -> sin(pi/6) -> 1/2).  Arguments of unknown dimension
 * are left alone.  The dimension of a sum is taken from its first term
 * (check_sum has already verified that all terms agree). */
static E *check_fn(E *e) {
    if (!transcendental(e->s)) return e;
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *a = EK(e)[i];
        k[i] = a;
        if (!has_units(a)) continue;
        int d[NDIM];
        if (!term_dim(a->t == E_ADD ? K0(a) : a, d)) continue;
        if (!dim_zero(d)) incompatible();
        if (a->t == E_ADD) {
            E **q = salloc(sizeof(E *) * a->n);
            for (uint32_t j = 0; j < a->n; j++) q[j] = to_si(EK(a)[j]);
            k[i] = s_add(a->n, q);
        } else
            k[i] = to_si(a);
        ch = 1;
    }
    return ch ? s_fn(e->s, e->n, k) : e;
}

/* Bottom-up unit normalisation of a whole expression: children first, then
 * the node itself through check_sum / check_prod / check_fn; a power with
 * units in the exponent is an error.  Subtrees without units are returned
 * as-is (so the walk is cheap for unit-free results).  Recursion guarded by
 * stack_check; result rebuilt in scratch. */
static E *unorm(E *e) {
    if (e->t <= E_SYM || e->t == E_STR || !has_units(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = unorm(EK(e)[i]);
        ch |= k[i] != EK(e)[i];
    }
    if (ch) e = rebuild(e, k);
    switch (e->t) {
    case E_ADD: return check_sum(e);
    case E_MUL: return check_prod(e);
    case E_FN: return check_fn(e);
    case E_POW:
        if (has_units(K1(e))) incompatible();
        return e;
    }
    return e;
}

/* "J/mol/K" -> _J*_mol^-1*_K^-1.  Mini-parser for the unit strings in the
 * constant table: names separated by '*' or '/', each optionally followed
 * by ^k or ^-k (integer).  '/' applies to the next factor only, so
 * "J/mol/K" means J mol^-1 K^-1.  Throws "unknown unit" for a bad name
 * (only possible if the table is wrong). */
static E *ustr(const char *s) {
    E *r = K->c1;
    int div = 0;
    while (*s) {
        char nm[8];
        size_t n = 0;
        while (*s && *s != '*' && *s != '/' && *s != '^' && n < 7) nm[n++] = *s++;
        nm[n] = 0;
        long k = 1;
        if (*s == '^') {
            s++;
            int neg = *s == '-';
            if (neg) s++;
            k = 0;
            while (*s >= '0' && *s <= '9') k = k * 10 + (*s++ - '0');
            if (neg) k = -k;
        }
        r = s_mul2(r, s_powi(usym(nm), div ? -k : k));
        if (*s) div = *s++ == '/';
    }
    return r;
}

/* value * SI units of a constant symbol (c itself if it is not one). */
static E *const_value(E *c) {
    const char *nm = sym_name(c->s) + 1;
    int i = clook(nm, strlen(nm));
    if (i < 0) return c;
    return s_mul2(parse_val(consts[i].val), ustr(consts[i].u));
}

static int has_consts(E *e);
/* Replace every constant symbol in e by value * SI units (rebuilding and
 * re-simplifying only the paths that change). */
static E *subst_consts(E *e) {
    if (is_const(e)) return const_value(e);
    if (e->t <= E_SYM || e->t == E_STR || !has_consts(e)) return e;
    stack_check();
    E **k = salloc(sizeof(E *) * (e->n + 1));
    int ch = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        k[i] = subst_consts(EK(e)[i]);
        ch |= k[i] != EK(e)[i];
    }
    return ch ? rebuild(e, k) : e;
}

/* 1 if e contains a constant symbol (budgeted walk, like has_units). */
static int has_consts(E *e) {
    stack_check();
    if (!ubudget) return 0;
    ubudget--;
    if (is_const(e)) return 1;
    if (e->t <= E_SYM || e->t == E_STR) return 0;
    for (uint32_t i = 0; i < e->n; i++)
        if (has_consts(EK(e)[i])) return 1;
    return 0;
}

/* Post-processing of a top-level result (ext_post in xcmds.c).  Cheap exits
 * when no unit/constant symbol has ever been interned or r has none.
 * Constants are replaced by their values when `force` (top-level approx /
 * evalf) is set, in approximate mode, or when r already contains floats
 * (an exact symbolic _c next to 2.5_m would be pointless); then unorm does
 * the dimension checks and conversions.  May throw CAS_ERR_DOMAIN
 * ("incompatible units").  Resets the walk budget ubudget. */
E *units_post(E *r, int force) {
    ubudget = 100000;
    if (!units_in_use() || !has_units(r)) return r;
    if ((force || !K->set.exact || has_float(r)) && has_consts(r)) r = subst_consts(r);
    return unorm(r);
}

/* ---------------------------------------------------------- commands --- */
/* Named coherent SI units preferred by convert(q, "SI") over a product of
 * base units when the dimension matches exactly. */
static const char *const derived[] = {"N", "J", "W", "Pa", "C", "V", "ohm", "F", "H", "Wb", "T", "S"};

/* Convert one term q = value * units to the target units.  target is a
 * unit product (1_ft or _ft, the value part must be 1) or one of the
 * strings "SI" (named derived unit if one fits, else base units), "base"
 * (SI base units) or "US" (ft, lb, s, A, degR, mol, cd).  Dimensions must
 * match ("incompatible units" otherwise).
 * The new value is value * factor(source) / factor(target), except for
 * *absolute* temperatures (delta = 0, both sides a single temperature unit
 * and at least one with an offset): with kelvin = f * (x + offset), where
 * offset is 273.15 for degC, 459.67 for degF and 0 for K and degR,
 *   x_target = f_s (x_s + off_s) / f_t - off_t.
 * deltaconvert (delta = 1) treats temperatures as differences (no offsets).
 * The coefficient goes through fixcoef: rationals with a non-terminating
 * decimal expansion become floats, and so do irrational constant
 * coefficients (pi from _pc, _deg) unless the result is dimensionless. */
static E *convert1(E *q, E *target, int delta) {
    E *val, *un, *fs, *ft, *tval, *tun;
    int ds[NDIM], dt[NDIM], nts, ntt;
    usplit(q, &val, &un);
    if (has_units(val)) cas_throw(CAS_ERR_DOMAIN, "convert: expected a quantity");
    udim(un, ds, &fs, &nts);
    if (target->t == E_STR) {
        int us = !strcmp(ESTR(target), "US"), si = !strcmp(ESTR(target), "SI");
        if (!us && !si && strcmp(ESTR(target), "base"))
            cas_throw(CAS_ERR_DOMAIN, "convert: target \"SI\", \"base\" or \"US\"");
        if (us) {
            static const char *const usn[NDIM] = {"ft", "lb", "s", "A", "degR", "mol", "cd"};
            E *t = K->c1;
            for (int j = 0; j < NDIM; j++)
                if (ds[j]) t = s_mul2(t, s_powi(usym(usn[j]), ds[j]));
            target = t;
        } else {
            target = si_base(ds);
            if (si)
                for (size_t i = 0; i < sizeof derived / sizeof derived[0]; i++) {
                    int dd[NDIM];
                    E *u = usym(derived[i]);
                    udim(u, dd, NULL, NULL);
                    if (dim_eq(dd, ds)) target = u;
                }
        }
        if (is_one(target)) return fixcoef(s_mul2(val, fs), 1);
    }
    usplit(target, &tval, &tun);
    if (!is_one(tval) || has_units(tval)) cas_throw(CAS_ERR_DOMAIN, "convert: target must be units");
    udim(tun, dt, &ft, &ntt);
    if (!dim_eq(ds, dt)) incompatible();
    E *v;
    if (!delta && un->t == E_SYM && tun->t == E_SYM && ds[4] == 1 && (nts || ntt)) {
        /* absolute temperatures: kelvin = f*(x + offset) */
        E *off[2] = {K->c0, K->c0};
        for (int i = 0; i < 2; i++) {
            const char *nm = units[uinfo(i ? tun : un).idx].name;
            if (!strcmp(nm, "degC")) off[i] = mk_frac(27315, 100);
            if (!strcmp(nm, "degF")) off[i] = mk_frac(45967, 100);
        }
        v = s_sub(s_div(s_mul2(fs, s_add2(val, off[0])), ft), off[1]);
    } else
        v = s_div(s_mul2(val, fs), ft);
    v = fixcoef(v, dim_zero(dt));
    return s_mul2(v, tun);
}

/* convert / deltaconvert on an arbitrary value: constants are substituted
 * and the quantity normalised first; sums are converted termwise (after
 * unorm they have compatible dimensions), lists elementwise. */
static E *convert(E *q, E *target, int delta) {
    stack_check();
    q = unorm(subst_consts(q));
    target = subst_consts(target);
    if (q->t == E_ADD) {
        E **k = salloc(sizeof(E *) * q->n);
        for (uint32_t i = 0; i < q->n; i++) k[i] = convert1(EK(q)[i], target, delta);
        return s_add(q->n, k);
    }
    if (q->t == E_LIST) {
        E **k = salloc(sizeof(E *) * (q->n + 1));
        for (uint32_t i = 0; i < q->n; i++) k[i] = convert(EK(q)[i], target, delta);
        return s_list(q->n, k);
    }
    return convert1(q, target, delta);
}

/* op: 0 convert, 1 deltaconvert, 2 usimplify, 3 uvalue, 4 uunits.
 * Unit commands, dispatched by name from xcmds.c with evaluated arguments
 * (xcmds.c checks the argument count).  usimplify is convert(q, "SI");
 * uvalue / uunits return the value part / the unit part of a single
 * quantity (constants replaced by value * units first). */
E *units_cmd(int op, uint32_t n, E **a) {
    ubudget = 100000;
    if (op <= 1) return convert(a[0], a[1], op);
    if (op == 2) return convert(a[0], mk_str("SI", 2), 0);
    E *val, *un;
    usplit(subst_consts(a[0]), &val, &un);
    return op == 3 ? val : un;
}

/* ------------------------------------------------------------ printing --- */
/* 29.4_m/s, 1.524_m, 5_kg*m^2/s^2 for value*units with a plain number; 0 if
 * the default printer should be used. ctx: print.c precedence (P_SUM = 2,
 * P_PROD = 3).
 * Applies only to products consisting of one number and integer powers of
 * units with at least one positive power.  Output: the number, then '_'
 * and the positive-power units joined by '*', then "/unit^k" for each
 * negative power (the parser's unit-tail syntax reads this back).  A
 * rational coefficient with a terminating decimal expansion is printed as
 * an exact decimal (3/8 -> 0.375) when it has at most `digits` digits,
 * otherwise rounded as a float; a non-terminating one falls back to the
 * default printer.  A coefficient of +-1 is omitted (_m, -_m). */
int units_print(sbuf *b, E *e, int ctx) {
    E *c = K->c1;
    uint32_t npos = 0;
    for (uint32_t i = 0; i < e->n; i++) {
        E *f = EK(e)[i], *u;
        long k;
        if (is_num(f)) c = f;
        else if (!unit_factor(f, &u, &k)) return 0;
        else npos += k > 0;
    }
    if (!npos) return 0;
    char dec[64];
    size_t dn = 0;
    if (c->t == E_RAT) {
        /* terminating decimal?  num/den with den = 2^p2 5^p5 equals
         * (num * 10^k / den) / 10^k for k = max(p2, p5): print the integer
         * m = num * 10^k / den and insert a decimal point k places from
         * the right (padding with leading zeros, e.g. 0.05). */
        E *num = int_abs(K0(c)), *d = K1(c);
        int p2 = 0, p5 = 0;
        while (int_mod_u(d, 2) == 0) d = int_tdiv(d, K->c2), p2++;
        while (int_mod_u(d, 5) == 0) d = int_tdiv(d, mk_int(5)), p5++;
        int k = p2 > p5 ? p2 : p5;
        if (!int_is_one(d) || k > 30) return 0;
        E *m = int_tdiv(int_mul(num, int_pow(mk_int(10), (uint32_t)k)), K1(c));
        sbuf t = {dec, sizeof dec - 4, 0, 0};
        int_to_dec(&t, m);
        if (t.overflow) return 0;
        dn = t.len;
        if (dn > (size_t)(K->set.digits > 0 ? K->set.digits : 12)) /* long: show rounded */
            c = mk_flt(num_to_double(c)), dn = 0, k = -1;
        while (k >= 0 && dn <= (size_t)k) { /* left-pad with zeros */
            memmove(dec + 1, dec, dn);
            dec[0] = '0';
            dn++;
        }
        if (k >= 0) {
            memmove(dec + dn - k + 1, dec + dn - k, (size_t)k);
            dec[dn - k] = '.';
            dn++;
        }
    }
    int neg = num_sgn(c) < 0;
    int paren = ctx > 3 || (neg && ctx > 2);
    if (paren) sb_putc(b, '(');
    if (neg) sb_putc(b, '-');
    if (dn) sb_putn(b, dec, dn);
    else if (!(c->t == E_INT && int_is_one(int_abs(c)))) {
        E *ac = num_abs(c);
        if (ac->t == E_FLT) fmt_float_node(b, ac, K->set.digits > 0 ? K->set.digits : 12);
        else int_to_dec(b, ac);
    }
    int first = 1;
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < e->n; i++) {
            E *u;
            long k;
            if (!unit_factor(EK(e)[i], &u, &k) || (pass == 0) != (k > 0)) continue;
            sb_putc(b, pass ? '/' : first ? '_' : '*');
            sb_puts(b, sym_name(u->s) + 1);
            if (k < 0) k = -k;
            if (k != 1) {
                sb_putc(b, '^');
                sb_int(b, k);
            }
            first = 0;
        }
    if (paren) sb_putc(b, ')');
    return 1;
}
