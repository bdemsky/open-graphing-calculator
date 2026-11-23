/*
 * UNITS menu ({SHIFT} {vars}, also APPS > Units; {SHIFT} {CAS} opens the
 * constants directly): units by category,
 * physical constants (CODATA 2022, see cas/src/units.c) and the conversion
 * commands. Items insert CAS unit syntax such as _km or _m/s^2.
 *
 * Two-level dynamic menu: the top page lists ± (uncertainty, item 1),
 * convert( (item 2: the most used command), the categories of cats[] and
 * then the other unit and uncertainty commands (deltaconvert, usimplify,
 * ..., nominal, uncert, ucov); labels say what each command gives. A category opens a
 * second menu (menu_usub) whose items are parsed on demand from that
 * category's packed string. Selecting a unit inserts "_name" into the editor
 * of the screen the menu was opened from. The unit and constant definitions
 * themselves live in the CAS (cas/src/units.c); this file only lists them.
 * State: `cur`, the category shown in the second level. Registered as the
 * app "units" (app_registry) and reached through the keys' global actions
 * units_open / units_open_constants (app/keys.c).
 */
#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "sections.h"
#include "ui.h"

/* Cold, table-driven code: optimise for size on the firmware build. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* "name=description|..." (the description is optional); name is inserted as _name.
 * Packed into one string per category to keep the table small. The last
 * category must stay "Constants" (its items get the help topic "constants"
 * and units_open_constants() opens it). */
static const struct {
    const char *title, *items;
} cats[] = {
    {"Length", "m|cm|mm|km|um=micrometre|nm|in=inch|ft=foot|yd=yard|mi=mile|nmi=nautical mile|"
               "au=astronomical unit|ly=light year|pc=parsec|Ang=angstrom"},
    {"Area", "m^2|cm^2|km^2|ha=hectare|acre|ft^2|in^2|mi^2"},
    {"Volume", "L=litre|mL|m^3|cm^3|gal=US gallon|qt=US quart|pt=US pint|cup|floz=US fluid ounce|"
               "tbsp|tsp|ft^3|in^3"},
    {"Time", "s|ms|us|ns|min|h=hour|d=day|wk=week|yr=Julian year"},
    {"Speed, acceleration", "m/s|km/h|mph|knot|ft/s|m/s^2|ft/s^2"},
    {"Mass", "kg|g|mg|ug|t=tonne|lb=pound|oz=ounce|ton=short ton|slug|ct=carat|u=atomic mass unit|Da=dalton"},
    {"Force", "N=newton|kN|dyn=dyne|lbf=pound-force|kgf=kilogram-force"},
    {"Energy", "J=joule|kJ|MJ|cal=calorie|kcal|eV=electronvolt|keV|MeV|Wh|kWh|Btu|erg"},
    {"Power", "W=watt|mW|kW|MW|hp=horsepower"},
    {"Pressure", "Pa=pascal|hPa|kPa|MPa|bar|mbar|atm|psi|mmHg|Torr|inHg"},
    {"Temperature", "K=kelvin|mK|degC=Celsius|degF=Fahrenheit|degR=Rankine"},
    {"Electric", "A=ampere|mA|uA|V=volt|mV|kV|ohm|kohm|Mohm|C=coulomb|Ah|mAh|F=farad|uF|nF|pF|"
                 "S=siemens|H=henry|mH"},
    {"Magnetic", "T=tesla|mT|gauss|Wb=weber"},
    {"Frequency, activity", "Hz|kHz|MHz|GHz|rpm|Bq=becquerel|Ci=curie"},
    {"Angle", "rad|mrad|deg=degree|arcmin|arcsec|rev=revolution|sr=steradian"},
    {"Amount, light, dose", "mol|mmol|cd=candela|lm=lumen|lx=lux|Gy=gray|Sv=sievert|mSv"},
    {"Constants", "c=speed of light|hP=Planck constant h|hbar=reduced Planck h/2pi|qe=elementary charge|"
                  "kB=Boltzmann constant|NA=Avogadro constant|R=molar gas constant|Fc=Faraday constant|"
                  "G=Newtonian gravitation|g0=standard gravity|eps0=vacuum permittivity|"
                  "mu0=vacuum permeability|ke=Coulomb constant|Z0=vacuum impedance|me=electron mass|"
                  "mp=proton mass|mn=neutron mass|mu=atomic mass constant|sigma=Stefan-Boltzmann|"
                  "Rinf=Rydberg constant|a0=Bohr radius|alpha=fine-structure constant|muB=Bohr magneton|"
                  "muN=nuclear magneton|Phi0=magnetic flux quantum|Vm=molar volume (0 C, 1 atm)"},
};
#define NCATS ((int)(sizeof cats / sizeof cats[0]))

static int cur;

/* item i of category cur: name (inserted with '_') and label ("name    description",
 * the name padded to 7 columns). Returns false if i is past the last item. */
static bool item(int i, char *name, size_t nn, char *label, size_t ln)
{
    const char *s = cats[cur].items;
    while (i > 0 && *s)
        if (*s++ == '|')
            i--;
    if (!*s)
        return false;
    const char *e = s;
    while (*e && *e != '|')
        e++;
    const char *eq = memchr(s, '=', (size_t)(e - s));
    size_t l = (size_t)((eq ? eq : e) - s);
    snprintf(name, nn, "_%.*s", (int)l, s);
    if (eq)
        snprintf(label, ln, "%-7.*s %.*s", (int)l, s, (int)(e - eq - 1), eq + 1);
    else
        snprintf(label, ln, "%.*s", (int)l, s);
    return true;
}

/* items of the current category = number of '|' separators + 1 */
static int sub_count(void)
{
    int n = 1;
    for (const char *s = cats[cur].items; *s; s++)
        n += *s == '|';
    return n;
}
static void sub_get(int i, menu_item_t *out)
{
    static char name[24], label[48];
    if (!item(i, name, sizeof name, label, sizeof label))
        name[0] = label[0] = 0;
    *out = (menu_item_t){label, name, NULL, i, cur == NCATS - 1 ? "constants" : "units"};
}
static menu_def_t menu_usub = {"UNITS", NULL, 0, NULL, false, "units", sub_count, sub_get};

/* Commands on the top page: the first two (± for a value with an
 * uncertainty, typed as often as a unit and on no key, and convert() come
 * before the categories, so {SHIFT} {vars} {1} types ± and {SHIFT} {vars}
 * {2} starts a conversion; the other commands come after them. */
#define NFIRST 2
static const menu_item_t cmd_items[] = {
    {"± (uncertainty)", "±", NULL, 0, "pm"},
    {"convert( value_unit,_unit)", "convert(", ")", 0, "convert"},
    {"deltaconvert( temp. change", "deltaconvert(", ")", 0, "deltaconvert"},
    {"usimplify( to SI units", "usimplify(", ")", 0, "usimplify"},
    {"uvalue( number part", "uvalue(", ")", 0, "uvalue"},
    {"uunits( unit part", "uunits(", ")", 0, "uunits"},
    {"nominal( value without ±", "nominal(", ")", 0, "nominal"},
    {"uncert( the ± part", "uncert(", ")", 0, "uncert"},
    {"ucov( x,y) covariance", "ucov(", ")", 0, "ucov"},
};
#define NCMD ((int)(sizeof cmd_items / sizeof cmd_items[0]))

/* Top page: cmd_items[0..NFIRST-1], the categories, then the other
 * cmd_items. top_cat(i) is the category shown at top index i, or -1 for a
 * command. */
static int top_cat(int i) { return i >= NFIRST && i < NFIRST + NCATS ? i - NFIRST : -1; }
static int top_count(void) { return NCATS + NCMD; }
static void top_get(int i, menu_item_t *out)
{
    int c = top_cat(i);
    if (c >= 0)
        *out = (menu_item_t){cats[c].title, NULL, NULL, c, c == NCATS - 1 ? "constants" : "units"};
    else
        *out = cmd_items[i < NFIRST ? i : i - NCATS];
}
static void top_select(const menu_item_t *it);
static const menu_def_t menu_units = {"UNITS", NULL, 0, top_select, false, "units", top_count, top_get};

/* Top page selection. A category opens its submenu. A command is inserted
 * by hand, because the menu has an on_select handler (so the default
 * insertion does not run): into the editor of the screen the menu came from,
 * or the home screen if that one has no editor, returning there with
 * ui_returning set so the screen keeps its state. */
static void top_select(const menu_item_t *it)
{
    int i = top_cat(menu_current_sel());
    if (i >= 0) {
        cur = i;
        menu_usub.title = cats[i].title;
        menu_open(&menu_usub);
        return;
    }
    /* a command: insert it like a plain menu */
    const screen_t *dest = menu_origin() && menu_origin()->editor ? menu_origin() : ui_home();
    editor_t *e = dest->editor ? dest->editor() : NULL;
    if (e)
        ed_insert_text(e, it->insert, it->suffix);
    ui_returning = true;
    ui_switch(dest);
    ui_returning = false;
}

void units_open(void) { menu_open(&menu_units); }

/* {SHIFT} {CAS}: straight to the constants list. */
void units_open_constants(void)
{
    cur = NCATS - 1;
    menu_usub.title = cats[cur].title;
    menu_open(&menu_usub);
}

const app_def_t app_units = {"units", "Units & constants", units_open, {{0}}, "units"};
