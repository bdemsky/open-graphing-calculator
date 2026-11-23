/*
 * MATRIX (SHIFT math): NAMES / MATH / EDIT menus and the matrix editor.
 * Matrices [A]..[J] are the CAS variables matA..matJ (the editor token "[A]"
 * sends " matA " to the CAS), so they are saved with the CAS session and work
 * in every CAS command; the editor can also edit any CAS variable holding a
 * matrix or vector. Cells are entered as expressions and evaluated by the CAS
 * (setElem / redim).
 *
 * User interaction: the MATRIX key (SHIFT math) or APPS > Matrix opens the
 * MATRIX menu: NAMES inserts [A]..[J] (with their dimensions) into the screen
 * below, MATH inserts matrix command templates, EDIT picks a matrix for the
 * editor. The editor screen (scr_matedit) is the shared cell grid xgrid_t of
 * app/xform.c, which owns the layout, cursor movement, cell editing and touch:
 * the top line holds the name and the dimensions (digits set rows / cols),
 * then the cells; ENTER edits the selected cell, typing replaces it, and
 * CLEAR outside an edit goes home. Its soft keys
 * reopen the NAMES / MATH / EDIT menus; F5 (→home) goes home with the
 * matrix name typed at the cursor, ready for [A]⁻¹, det([A]), ...
 *
 * Framework hooks (app/registry.c): app_matrix (APPS), vars_matrix (VARS >
 * Matrix: the defined matrices), persist_matrix (the last edited name only).
 *
 * Editor data flow: the CAS variable is the only copy of the matrix. reload()
 * prints it into mtext and splits that text into cells in place (moff[] holds
 * each cell's offset); every committed cell or new size is one CAS assignment
 * (`matA:=setElem(matA,r,c,(v))`, `matA:=redim(matA,r,c)`) followed by a
 * reload, so the grid always shows what the CAS holds. An undefined matrix is
 * shown as 3x3 zeros and only defined by the first change (mundef).
 */
#include <stdio.h>
#include <string.h>
#include "registry.h"
#include "session.h"
#include "ui.h"
#include "xform.h"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

#define NMAT 10   /* [A]..[J] */
#define MAXD 20   /* largest editable size: MAXD x MAXD */
#define MBUF 4096 /* longest matrix text the editor can show (larger: "too large to edit") */

static char mname[24] = "matA";  /* CAS variable being edited */
static char mtitle[24] = "[A]";
static char mtext[MBUF];         /* matrix text, split into cells in place */
static uint16_t moff[MAXD * MAXD]; /* offset in mtext of cell (r, c) at r * MAXD + c */
static bool mvalid;              /* cells parsed */
static bool mundef;              /* the variable is undefined: the grid shows 3x3 zeros until the first change */
static uint8_t last_mat;         /* saved: last matrix edited */

/* Intern the editor tokens "[A]".."[J]" (CAS text " matA ".. " matJ ": the
 * spaces keep the name a separate word next to other tokens, e.g. 2[A]).
 * Idempotent; called before anything may insert or display such a token. */
static void mat_tokens(void)
{
    char d[4], c[8];
    for (int i = 0; i < NMAT; i++) {
        snprintf(d, sizeof d, "[%c]", 'A' + i);
        snprintf(c, sizeof c, " mat%c ", 'A' + i);
        tok_intern(d, c);
    }
}

/* parse "[[a,b],[c,d]]" (or "[a,b]") into cells; returns false if not a matrix
 * Splits mtext in place: every top-level ',' and closing ']' of a row becomes
 * a NUL and the start of each cell is recorded in moff. `depth` counts all
 * brackets and parentheses, so commas inside a cell (f(a,b), nested lists) do
 * not split it; `base` is the depth of a row's elements (1 for a vector, which
 * is a single row; 2 for a matrix). Rows of different lengths, or more than
 * MAXD rows / columns, are refused. Sets gr->rows / gr->cols on success. */
static bool parse_cells(xgrid_t *gr)
{
    char *s = mtext;
    int depth = 0, r = 0, c = 0, cols = -1;
    bool vec = s[0] == '[' && s[1] != '[';
    if (s[0] != '[')
        return false;
    int base = vec ? 1 : 2;
    char *start = NULL;
    for (char *p = s; *p; p++) {
        char ch = *p;
        if (ch == '[' || ch == '(') {
            depth++;
            if (depth == base && ch == '[') {
                start = p + 1;
                c = 0;
            }
            continue;
        }
        if (ch == ']' || ch == ')') {
            if (depth == base && ch == ']') {
                if (r >= MAXD || c >= MAXD)
                    return false;
                *p = 0;
                moff[r * MAXD + c] = (uint16_t)(start - mtext);
                c++;
                if (cols >= 0 && c != cols)
                    return false;
                cols = c;
                r++;
            }
            depth--;
            continue;
        }
        if (ch == ',' && depth == base) {
            if (r >= MAXD || c >= MAXD - 1)
                return false;
            *p = 0;
            moff[r * MAXD + c] = (uint16_t)(start - mtext);
            c++;
            start = p + 1;
        }
    }
    if (r < 1 || cols < 1)
        return false;
    gr->rows = r;
    gr->cols = cols;
    return true;
}

static xgrid_t mg; /* the editor grid (callbacks cell / set_cell / resize below) */

/* Fetch the matrix from the CAS into mtext and re-split it into cells. An
 * undefined variable (the CAS returns its name unevaluated) is shown as a
 * 3x3 zero matrix without defining it (mundef): just looking at [C] does not
 * create matC. On errors the grid shows one cell and the error message
 * (otherwise any old error state is cleared). Keeps the selection inside the
 * new size. */
static void reload(void)
{
    char cmd[40];
    snprintf(cmd, sizeof cmd, "%s", mname);
    mvalid = false;
    mg.err = false;
    cas_status st = cas_eval_quiet(g.cas, cmd, mtext, sizeof mtext);
    mundef = st == CAS_OK && !strcmp(mtext, mname);
    if (mundef) /* undefined: show a 3x3 zero matrix (the usual default) */
        snprintf(mtext, sizeof mtext, "[[0,0,0],[0,0,0],[0,0,0]]");
    if (st != CAS_OK) {
        snprintf(mg.msg, sizeof mg.msg, "ERR: %s", st == CAS_ERR_OUTPUT_TRUNC ? "too large to edit" : cas_last_error(g.cas));
        mg.err = true;
        mg.rows = mg.cols = 1;
        return;
    }
    mvalid = parse_cells(&mg);
    if (!mvalid) {
        snprintf(mg.msg, sizeof mg.msg, "ERR: not a matrix");
        mg.err = true;
        mg.rows = mg.cols = 1;
    }
    if (mg.sr >= mg.rows)
        mg.sr = mg.rows - 1;
    if (mg.sc >= mg.cols)
        mg.sc = mg.cols - 1;
}

/* xgrid callback: display text of cell (r, c) (prettified CAS text) */
static void cell(int r, int c, char *buf, size_t n)
{
    if (!mvalid || r >= mg.rows || c >= mg.cols) {
        snprintf(buf, n, "?");
        return;
    }
    char tmp[MBUF / 4];
    snprintf(tmp, sizeof tmp, "%s", mtext + moff[r * MAXD + c]);
    pretty_from_cas(tmp, buf, n);
}

/* Run a CAS assignment that changes the matrix, then reload it. The result
 * text (the whole matrix) is not needed, so a small output buffer is used and
 * CAS_ERR_OUTPUT_TRUNC counts as success: the assignment has happened. An
 * undefined matrix is first defined as the 3x3 zeros the grid shows. */
static bool run(const char *cmd, char *err, size_t errn)
{
    char out[64];
    if (mundef) {
        char def[48];
        snprintf(def, sizeof def, "%s:=redim(0,3,3)", mname);
        cas_eval_quiet(g.cas, def, out, sizeof out);
    }
    cas_status st = cas_eval_quiet(g.cas, cmd, out, sizeof out);
    if (st != CAS_OK && st != CAS_ERR_OUTPUT_TRUNC) {
        snprintf(err, errn, "ERR: %s", cas_last_error(g.cas));
        return false;
    }
    reload();
    return true;
}

/* xgrid callback: store the CAS text of an edited cell (0-based r, c; CAS
 * indices are 1-based). false + err: the grid keeps the cell in edit mode. */
static bool set_cell(int r, int c, const char *cas, char *err, size_t errn)
{
    static char cmd[ED_MAX * 4 + 64];
    snprintf(cmd, sizeof cmd, "%s:=setElem(%s,%d,%d,(%s))", mname, mname, r + 1, c + 1, cas);
    return run(cmd, err, errn);
}

/* xgrid callback: new dimensions from the dimensions line (redim keeps the
 * overlapping cells and pads with zeros) */
static bool resize(int r, int c, char *err, size_t errn)
{
    char cmd[80];
    snprintf(cmd, sizeof cmd, "%s:=redim(%s,%d,%d)", mname, mname, r, c);
    return run(cmd, err, errn);
}

/* enter: a fresh visit selects the dimensions line (sr = -1) and loads the
 * matrix named by mname; returning from a menu keeps everything */
static void me_enter(void)
{
    if (ui_returning)
        return;
    mg.cell = cell;
    mg.set = set_cell;
    mg.resize = resize;
    mg.maxr = mg.maxc = MAXD;
    mg.sr = -1;
    mg.sc = 0;
    mg.tr = mg.tc = 0;
    mg.editing = false;
    mg.msg[0] = 0;
    snprintf(mg.title, sizeof mg.title, "%s", mtitle);
    reload();
}
static void me_draw(void) { xgrid_draw(&mg); }
static void me_key(int key, int mod)
{
    if (!xgrid_key(&mg, key, mod) && key == KEY_CLEAR)
        ui_switch(ui_home());
}
static editor_t *me_editor(void) { return xgrid_editor(&mg); }
static void me_touch(const touch_event_t *ev) { xgrid_touch(&mg, ev); }
extern const softkeys_t matedit_soft;
const screen_t scr_matedit = {"matedit", "MATRIX EDIT", me_enter, me_draw, me_key, NULL, me_editor, me_touch,
                              "matrix-edit", &matedit_soft};

/* open the editor on CAS variable `var`, titled `title` (forces a fresh enter) */
static void edit_open(const char *var, const char *title)
{
    snprintf(mname, sizeof mname, "%s", var);
    snprintf(mtitle, sizeof mtitle, "%s", title);
    ui_returning = false;
    ui_switch(&scr_matedit);
}

/* ------------------------------------------------------------ menus */
static char dims[NMAT][10]; /* "r×c" of each defined matA..matJ, "" if undefined (scan_dims) */

/* NAMES item i: label "[A] 2×3", inserts the token "[A]" */
static void names_get(int i, menu_item_t *out)
{
    static char lab[24], ins[4];
    mat_tokens();
    snprintf(ins, sizeof ins, "[%c]", 'A' + i);
    snprintf(lab, sizeof lab, "[%c] %s", 'A' + i, dims[i]);
    *out = (menu_item_t){lab, ins, NULL, i, "matrix-menu"};
}

/* cas_vars() callback: set bit i of *user for each defined variable matA..matJ */
static void defined_cb(void *user, const char *name, int np)
{
    if (np < 0 && !strncmp(name, "mat", 3) && name[3] >= 'A' && name[3] < 'A' + NMAT && !name[4])
        *(unsigned *)user |= 1u << (name[3] - 'A');
}

/* Label the menus with the sizes of matA..matJ: one walk over the CAS
 * variables finds the defined ones, and only those are asked for dim() (a
 * quiet evaluation each); a variable that is not a matrix gives no "[r,c]"
 * and stays "". */
static void scan_dims(void)
{
    unsigned defined = 0;
    cas_vars(g.cas, defined_cb, &defined);
    for (int i = 0; i < NMAT; i++) {
        char cmd[24];
        snprintf(cmd, sizeof cmd, "dim(mat%c)", 'A' + i);
        dims[i][0] = 0;
        char out[24];
        int r, c;
        if ((defined >> i & 1) && cas_eval_quiet(g.cas, cmd, out, sizeof out) == CAS_OK &&
            sscanf(out, "[%d,%d]", &r, &c) == 2)
            snprintf(dims[i], sizeof dims[i], "%d×%d", r, c);
    }
}
static int names_count(void) { return NMAT; }
static const menu_def_t menu_mnames = {"NAMES", NULL, 0, NULL, false, "matrix-menu", names_count, names_get};

/* EDIT: [A]..[J], then CAS variables that hold matrices or vectors */
#define NEXTRA 16
static char extra[NEXTRA][20];
static int nextra;

/* cas_vars() callback: collect up to NEXTRA user variables (np < 0; not
 * functions) other than matA..matJ whose CAS type is "matrix" (vectors
 * included) */
static void extra_cb(void *user, const char *name, int np)
{
    if (np >= 0 || nextra >= NEXTRA || strlen(name) >= sizeof extra[0] ||
        (!strncmp(name, "mat", 3) && name[3] >= 'A' && name[3] < 'A' + NMAT && !name[4]))
        return;
    char cmd[40], out[24];
    snprintf(cmd, sizeof cmd, "type(%s)", name);
    if (cas_eval_quiet(g.cas, cmd, out, sizeof out) == CAS_OK && !strcmp(out, "\"matrix\""))
        snprintf(extra[nextra++], sizeof extra[0], "%s", name);
}

static int edit_count(void) { return NMAT + nextra; }
/* EDIT item i: [A]..[J] (without insert text: selecting opens the editor),
 * then the extra variables */
static void edit_get(int i, menu_item_t *out)
{
    if (i < NMAT) {
        names_get(i, out);
        out->insert = NULL;
        return;
    }
    *out = (menu_item_t){extra[i - NMAT], NULL, NULL, i, "matrix-edit"};
}
/* EDIT item chosen: open the editor on it (remember [A]..[J] for the session) */
static void edit_select(const menu_item_t *it)
{
    char v[24], t[24];
    if (it->action < NMAT) {
        snprintf(v, sizeof v, "mat%c", 'A' + it->action);
        snprintf(t, sizeof t, "[%c]", 'A' + it->action);
        last_mat = (uint8_t)it->action;
    } else {
        snprintf(v, sizeof v, "%s", extra[it->action - NMAT]);
        snprintf(t, sizeof t, "%s", v);
    }
    edit_open(v, t);
}
static const menu_def_t menu_medit = {"EDIT", NULL, 0, edit_select, false, "matrix-edit", edit_count, edit_get};

/* MATRIX MATH: the commands used most come first (keys 1..9, 0 reach the
 * first ten); labels show the argument order where it is not just one
 * matrix, and the names other calculators use (Fill(, randM(, row+( ...). */
static const menu_item_t math_items[] = {
    {"det(  determinant", "det(", ")", 0, "det"},
    {"inv(  inverse, also [A]⁻¹", "inv(", ")", 0, "inv"},
    {"transpose(", "transpose(", ")", 0, "transpose"},
    {"rref(  reduced row echelon", "rref(", ")", 0, "rref"},
    {"identity( n)", "identity(", ")", 0, "identity"},
    {"dim(  [rows,cols]", "dim(", ")", 0, "dim"},
    {"augment( A,B)", "augment(", ")", 0, "augment"},
    {"rank(", "rank(", ")", 0, "rank"},
    {"trace(", "trace(", ")", 0, "trace"},
    {"eigenvals(", "eigenvals(", ")", 0, "eigenvals"},
    {"eigenvects(", "eigenvects(", ")", 0, "eigenvects"},
    {"ref(  row echelon", "ref(", ")", 0, "ref"},
    {"norm(", "norm(", ")", 0, "norm"},
    {"cond(  condition number", "cond(", ")", 0, "cond"},
    {"lu(", "lu(", ")", 0, "lu"},
    {"qr(", "qr(", ")", 0, "qr"},
    {"cholesky(", "cholesky(", ")", 0, "cholesky"},
    {"fill( v,M)  Fill(", "fill(", ")", 0, "fill"},
    {"randMat( r,c)  randM(", "randMat(", ")", 0, "randMat"},
    {"rowSwap( M,i,j)", "rowSwap(", ")", 0, "rowSwap"},
    {"rowAdd( M,i,j)  row+(", "rowAdd(", ")", 0, "rowAdd"},
    {"mRow( k,M,i)  *row(", "mRow(", ")", 0, "mRow"},
    {"mRowAdd( k,M,i,j)  *row+(", "mRowAdd(", ")", 0, "mRowAdd"},
    {"cumSum(  cumulative sums", "cumSum(", ")", 0, "cumSum"},
};
static const menu_def_t menu_mmath = {"MATRIX MATH", math_items, sizeof math_items / sizeof math_items[0], NULL, false,
                                      "matrix-menu"};

static const menu_item_t top_items[] = {
    {"NAMES  [A]..[J]", NULL, NULL, 0, "matrix-menu"},
    {"MATH   det, rref, ...", NULL, NULL, 1, "matrix-menu"},
    {"EDIT   matrix editor", NULL, NULL, 2, "matrix-edit"},
};
/* MATRIX menu item chosen: refresh what the submenu shows (dimensions, the
 * list of other matrix variables), then open it */
static void top_select(const menu_item_t *it)
{
    if (it->action == 0) {
        scan_dims();
        menu_open(&menu_mnames);
    } else if (it->action == 1)
        menu_open(&menu_mmath);
    else {
        scan_dims();
        nextra = 0;
        cas_vars(g.cas, extra_cb, NULL);
        menu_open(&menu_medit);
    }
}
static const menu_def_t menu_matrix = {"MATRIX", top_items, 3, top_select, false, "matrix-menu"};

static void sk_names(void) { mat_tokens(); top_select(&top_items[0]); }
static void sk_mmath(void) { mat_tokens(); top_select(&top_items[1]); }
static void sk_medit(void) { mat_tokens(); top_select(&top_items[2]); }
/* F5 →home: use the matrix being edited in a calculation. A cell edit in
 * progress is stored first (a refused value keeps the editor open with its
 * message); then the home screen gets the name ([A], or the variable) at
 * its cursor, keeping what was typed there, so "[A]" x⁻¹ ENTER inverts it. */
static void sk_home(void)
{
    if (mg.editing) {
        xgrid_key(&mg, KEY_ENTER, MOD_NONE);
        if (mg.editing)
            return;
    }
    mat_tokens();
    const screen_t *h = ui_home();
    editor_t *e = h->editor ? h->editor() : NULL;
    if (e)
        ed_insert_text(e, mtitle, NULL);
    ui_returning = true; /* home keeps its entry and history */
    ui_switch(h);
    ui_returning = false;
}
const softkeys_t matedit_soft = {
    {{"names", sk_names}, {"math", sk_mmath}, {"edit", sk_medit}, {NULL}, {"→home", sk_home}}};

static void matrix_open(void)
{
    mat_tokens();
    menu_open(&menu_matrix);
}
const app_def_t app_matrix = {"matrix", "Matrix", matrix_open, {{0}}, "matrix-menu"};

/* VARS > Matrix: the defined matrices (count() re-scans the CAS, which costs
 * one cas_vars() walk plus a dim() per defined matrix; get() uses that scan;
 * selecting inserts the [A] token) */
static int vm_count(void)
{
    scan_dims();
    int n = 0;
    for (int i = 0; i < NMAT; i++)
        n += dims[i][0] != 0;
    return n;
}
static void vm_get(int i, menu_item_t *out)
{
    for (int k = 0; k < NMAT; k++)
        if (dims[k][0] && i-- == 0) {
            names_get(k, out);
            return;
        }
    *out = (menu_item_t){"?", NULL, NULL, 0, "matrix-menu"};
}
const vars_provider_t vars_matrix = {"Matrix", vm_count, vm_get};

/* saved state: the matrices are CAS variables (saved with the CAS); this keeps the editor's last matrix */
static void ms_save(pw_t *w) { pw_u8(w, last_mat); }
static void ms_load(pr_t *r, unsigned version)
{
    unsigned m = pr_u8(r);
    if (!r->err && m < NMAT) {
        last_mat = (uint8_t)m;
        snprintf(mname, sizeof mname, "mat%c", 'A' + m);
        snprintf(mtitle, sizeof mtitle, "[%c]", 'A' + m);
    }
    mat_tokens(); /* restored history may contain [A] tokens */
}
/* MEM > Reset all: the editor goes back to [A] */
static void ms_reset(void)
{
    last_mat = 0;
    snprintf(mname, sizeof mname, "matA");
    snprintf(mtitle, sizeof mtitle, "[A]");
}
const persist_def_t persist_matrix = {"matrix", 1, ms_save, ms_load, ms_reset};
