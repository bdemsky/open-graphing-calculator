/* TABLE: the independent variable (x, T, θ or n) and the enabled entries of the
 * current graph mode in columns (PAR: X and Y of each curve), scrolling,
 * selected-cell readout. From the graph, CALC "table at cursor" (or a tap on
 * the trace coordinates) opens it centred on the traced value. */
/*
 * Screen scr_table (SHIFT F5 on the graph screens; soft keys softkeys_graph).
 * Rows are X = start + r * ΔTbl (TBLSET's TblStart / ΔTbl in g; rounded to
 * integers n in SEQ mode). Columns: X, then the enabled non-empty entries of
 * the current mode (PAR: an X1T and a Y1T column per curve; POL r(θ); SEQ
 * u(n); IMPL has no table). Three Y columns are visible at once; LEFT/RIGHT
 * move the selection and scroll the columns, UP/DOWN move it and scroll the
 * rows past the top/bottom (start changes by ΔTbl, there is no limit). The
 * line at the bottom shows the selected cell with up to 30 characters.
 * Typing a number ("go to", goto_ed) replaces that line with the typed X;
 * ENTER makes it the first row without changing TblStart.
 * Touch: tap a cell to select it, drag vertically to scroll X.
 * CLEAR returns home (while typing: clears, then cancels the typed X).
 *
 * Values are computed on every draw with the compiled functions (gx_* in
 * gr_model.c); nothing is cached. State: the statics below (not saved; the
 * table starts at TblStart on every fresh visit).
 *
 * Layout (font_medium, 18 px rows): header row at HEAD_Y, NROWS data rows
 * from FIRST_Y, the readout line just above the soft-key bar; four columns of
 * LCD_WIDTH / 4 = 80 px separated by grey lines.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "graph.h"

#define TF (&font_medium)
#define TROW 18
#define HEAD_Y (BODY_Y + 2)
#define FIRST_Y (HEAD_Y + TROW + 2)
#define NROWS ((UI_BOTTOM - FIRST_Y - TROW - 2) / TROW) /* rows between the header and the readout line */
#define NCOLS 3 /* Y columns visible next to X */
#define COLW (LCD_WIDTH / (NCOLS + 1))

static int selrow, selcol; /* selcol 0 = X, 1.. = visible Y columns */
static int colstart;       /* index into the column list for the first Y column */
static double start;       /* X of the first row */
typedef struct {
    uint8_t fn, comp; /* entry, component (PAR: 0 = x(t), 1 = y(t)) */
} tcol_t;
static tcol_t cols[2 * GNENT]; /* every Y column (rebuilt by collect() on each draw) */
static int ncols;
/* "table at cursor" request from the graph screen, applied by the next table_enter() */
static bool center_pending;
static double center_v;
static int center_fn = -1;
/* "go to": a number typed on the table (readout line) makes it the first row */
static editor_t goto_ed;
static bool goto_active;
static char goto_err[48]; /* why the last typed value was refused (until the next key) */

/* Make the next TABLE visit centre on X = v (selected row in the middle) and
 * start the visible columns at entry fn's column. */
void table_center_on(double v, int fn)
{
    center_pending = true;
    center_v = v;
    center_fn = fn;
}

/* Compile the entries and rebuild the column list from the enabled,
 * non-empty entries of the current mode (also those with compile errors,
 * which show ERR); keep colstart inside the list. */
static void collect(void)
{
    gx_compile();
    ncols = 0;
    if (gx.mode != GM_IMPL && gx.mode != GM_DE) /* (no columns for relations and DE curves) */
        for (int i = 0; i < GNENT; i++)
            if (gx_enabled(i) && !gx_entry_empty(i)) {
                cols[ncols++] = (tcol_t){(uint8_t)i, 0};
                if (gx.mode == GM_PAR)
                    cols[ncols++] = (tcol_t){(uint8_t)i, 1};
            }
    if (colstart > ncols - 1)
        colstart = ncols > 0 ? ncols - 1 : 0;
}

/* independent variable of visible row r */
static double row_x(int r)
{
    double v = start + r * g.tbldelta;
    return gx.mode == GM_SEQ ? round(v) : v;
}

/* value of column k at x (NAN where undefined; SEQ: NAN below nMin) */
static double col_value(int k, double x)
{
    int fi = cols[k].fn;
    switch (gx.mode) {
    case GM_PAR: {
        double a, b;
        gx_par(fi, x, &a, &b);
        return cols[k].comp ? b : a;
    }
    case GM_POL: return gx_pol_r(fi, x);
    case GM_SEQ: return x >= gx.nmin ? gx_seq(fi, (long)x) : (double)NAN;
    default: return func_eval(fi, x);
    }
}

/* header of column k: Y1, X1T / Y1T, r1, u1 */
static void col_name(int k, char *b, size_t n)
{
    int fi = cols[k].fn;
    switch (gx.mode) {
    case GM_PAR: snprintf(b, n, cols[k].comp ? "Y%dT" : "X%dT", fi + 1); break;
    case GM_POL: snprintf(b, n, "r%d", fi + 1); break;
    case GM_SEQ: snprintf(b, n, "u%d", fi + 1); break;
    default: snprintf(b, n, "Y%d", fi + 1); break;
    }
}

/* Text of the cell in visible row r, visible column c (0 = X), formatted to
 * at most `width` characters; empty past the last column, ERR for an entry
 * that did not compile. */
static void cell_text(int r, int c, char *b, size_t n, int width)
{
    double x = row_x(r);
    if (c == 0) {
        fmt_num(x, width, b, n);
        return;
    }
    int k = colstart + c - 1;
    if (k >= ncols) {
        b[0] = 0;
        return;
    }
    if (gx_error(cols[k].fn)[0]) {
        snprintf(b, n, "ERR");
        return;
    }
    fmt_num(col_value(k, x), width, b, n);
}

/* Full redraw: header, the grid of NROWS x 4 cells with the selection
 * highlighted, and the full-precision readout of the selected cell (the
 * compile error instead for an entry in error). */
static void table_draw(void)
{
    collect();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(0, HEAD_Y, LCD_WIDTH, TROW, C_LTGRAY);
    static const char *const indep[GM_NMODES] = {"X", "T", "θ", "n", "X", "X"};
    gfx_text(TF, 4, HEAD_Y + 2, indep[gx.mode], C_BLACK, GFX_TRANSPARENT);
    for (int c = 1; c <= NCOLS; c++) {
        int k = colstart + c - 1;
        if (k >= ncols)
            break;
        char h[16];
        col_name(k, h, sizeof h);
        gfx_text(TF, c * COLW + 4, HEAD_Y + 2, h, func_colors[gx_color(cols[k].fn)], GFX_TRANSPARENT);
    }
    for (int c = 1; c <= NCOLS && c - 1 <= ncols - colstart; c++) /* right of X and of every used column */
        gfx_vline(c * COLW - 1, HEAD_Y, FIRST_Y + NROWS * TROW - HEAD_Y, C_LTGRAY);
    char b[32];
    int width = COLW / TF->w - 1; /* characters per cell, leaving a margin */
    for (int r = 0; r < NROWS; r++) {
        int y = FIRST_Y + r * TROW;
        for (int c = 0; c <= NCOLS; c++) {
            cell_text(r, c, b, sizeof b, width);
            bool sel = r == selrow && c == selcol;
            if (sel)
                gfx_fill_rect(c * COLW, y, COLW - 1, TROW, C_SEL_BG);
            gfx_text(TF, c * COLW + 3, y + 2, b, sel ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
        }
    }
    /* full-precision readout of the selected cell */
    int ry = UI_BOTTOM - TROW;
    gfx_hline(0, ry - 1, LCD_WIDTH, C_LTGRAY);
    char full[64], label[16];
    if (selcol == 0) {
        snprintf(label, sizeof label, "%s=", indep[gx.mode]);
        fmt_num(row_x(selrow), 30, full, sizeof full);
    } else if (colstart + selcol - 1 < ncols) {
        int k = colstart + selcol - 1;
        col_name(k, label, sizeof label - 1);
        strcat(label, "=");
        if (!gx_error(cols[k].fn)[0])
            fmt_num(col_value(k, row_x(selrow)), 30, full, sizeof full);
        else
            snprintf(full, sizeof full, "%s", gx_error(cols[k].fn));
    } else {
        label[0] = full[0] = 0;
    }
    char line[96];
    uint16_t lc = C_BLACK;
    if (goto_active) { /* the value being typed, with a block for the cursor */
        char disp[ED_MAX * 4];
        ed_display(&goto_ed, disp, sizeof disp);
        snprintf(line, sizeof line, "go to %s=%.60s█", indep[gx.mode], disp);
        lc = C_BLUE;
    } else if (goto_err[0]) {
        snprintf(line, sizeof line, "%s", goto_err);
        lc = C_RED;
    } else {
        snprintf(line, sizeof line, "%s%s", label, full);
    }
    gfx_text(TF, 4, ry + 2, line, lc, GFX_TRANSPARENT);
    if (!goto_active && !goto_err[0] && ncols > 0) { /* how to jump, when there is room for it */
        static const char hint[] = "type a number: go there";
        int hw = gfx_text_width(&font_small, hint);
        if (gfx_text_width(TF, line) + hw + 16 <= LCD_WIDTH)
            gfx_text(&font_small, LCD_WIDTH - hw - 4, ry + 3, hint, C_GRAY, GFX_TRANSPARENT);
    }
    if (ncols == 0)
        gfx_text(TF, COLW + 4, FIRST_Y + 2, gx.mode == GM_IMPL ? "(no table for relations)" : gx.mode == GM_DE ? "(no table in DE mode)" : "(no functions in Y=)",
                 C_GRAY, GFX_TRANSPARENT);
}

/* Evaluate the typed "go to" value (expressions such as 2π work) and make it
 * the first row (SEQ: rounded, at least nMin). An error is shown in the
 * readout line instead. Closes the editor. */
static void goto_typed(void)
{
    char cas[ED_MAX * 4], err[40];
    double v;
    goto_active = false;
    ed_cas(&goto_ed, cas, sizeof cas);
    if (!cas[0])
        return;
    if (eval_number(cas, &v, err, sizeof err)) {
        snprintf(goto_err, sizeof goto_err, "ERR: %s", err);
        return;
    }
    if (gx.mode == GM_SEQ)
        v = fmax(round(v), gx.nmin);
    start = v;
    selrow = 0;
}

/* Arrows move the selected cell; at the top/bottom row UP/DOWN scroll X by
 * ΔTbl, at the first/last visible column LEFT/RIGHT scroll the Y columns.
 * A digit, `.`, (−) or π starts typing an X to go to (readout line): ENTER
 * goes there, CLEAR clears it or, when empty, cancels; ⌫ and menus edit it. */
static void table_key(int key, int mod)
{
    goto_err[0] = 0;
    if (goto_active) {
        if (key == KEY_ENTER) {
            goto_typed();
        } else if (key == KEY_CLEAR) {
            if (ed_empty(&goto_ed))
                goto_active = false;
            else
                ed_clear(&goto_ed);
        } else if (key == KEY_UP || key == KEY_DOWN) {
            goto_active = false; /* leave typing, then move */
            table_key(key, mod);
            return;
        } else if (!ui_editor_key(&goto_ed, key, mod)) {
            return;
        }
        table_draw();
        return;
    }
    switch (key) {
    case KEY_UP:
        if (selrow > 0)
            selrow--;
        else
            start -= g.tbldelta;
        break;
    case KEY_DOWN:
        if (selrow < NROWS - 1)
            selrow++;
        else
            start += g.tbldelta;
        break;
    case KEY_LEFT:
        if (selcol > 0)
            selcol--;
        else if (colstart > 0)
            colstart--;
        break;
    case KEY_RIGHT:
        if (selcol < NCOLS && colstart + selcol < ncols)
            selcol++;
        else if (colstart + NCOLS < ncols)
            colstart++;
        break;
    case KEY_CLEAR:
        ui_switch(ui_home());
        return;
    default: {
        char t[16];
        if (gx.mode == GM_IMPL || gx.mode == GM_DE || mod == MOD_ALPHA || !ui_key_text(key, mod, 0, t, sizeof t) ||
            !(strchr("0123456789.", t[0]) || !strcmp(t, "⁻") || !strcmp(t, "π")))
            return;
        goto_active = true;
        ed_clear(&goto_ed);
        ed_insert_text(&goto_ed, t, NULL);
        break;
    }
    }
    table_draw();
}

/* menus (MATH, CATALOG, VARS) insert into the typed "go to" value, if any */
static editor_t *table_editor(void) { return goto_active ? &goto_ed : NULL; }

/* Fresh visit (not returning from a menu/message): select the first Y column
 * and start at TblStart (at least nMin in SEQ mode), or apply a pending
 * table_center_on() request (in SEQ mode the start is also kept at nMin or
 * above, and the selected row is the one showing the requested n). */
static void table_enter(void)
{
    if (ui_returning)
        return;
    goto_active = false;
    goto_err[0] = 0;
    selcol = 1;
    colstart = 0;
    if (center_pending) {
        center_pending = false;
        selrow = NROWS / 2;
        start = center_v - selrow * g.tbldelta;
        if (gx.mode == GM_SEQ && start < gx.nmin) {
            start = gx.nmin;
            double r = g.tbldelta > 0 ? round((center_v - start) / g.tbldelta) : 0;
            selrow = r < 0 ? 0 : r > NROWS - 1 ? NROWS - 1 : (int)r;
        }
        collect();
        for (int k = 0; k < ncols; k++)
            if (cols[k].fn == center_fn) {
                colstart = k;
                break;
            }
        return;
    }
    start = g.tblstart;
    if (gx.mode == GM_SEQ && start < gx.nmin)
        start = gx.nmin;
    selrow = 0;
}

/* Touch: tap a cell to select it; drag vertically to scroll the X values:
 * every full row height (TROW px) scrolls one ΔTbl step (ui_drag_steps:
 * finger up = larger X, like scrolling a list; the table has no end). */
static void table_touch(const touch_event_t *ev)
{
    switch (ev->type) {
    case TEV_TAP: {
        if (ev->y < FIRST_Y)
            return;
        int r = (ev->y - FIRST_Y) / TROW, c = ev->x / COLW;
        if (r >= NROWS || c > NCOLS || (c > 0 && colstart + c - 1 >= ncols))
            return;
        selrow = r, selcol = c;
        break;
    }
    case TEV_DRAG: {
        int n = ui_drag_steps(ev, UI_DRAG_Y, TROW, -1000000, 1000000);
        if (!n)
            return;
        for (; n > 0; n--) /* step by step, as the arrow keys do */
            start += g.tbldelta;
        for (; n < 0; n++)
            start -= g.tbldelta;
        break;
    }
    default:
        return;
    }
    table_draw();
}

const screen_t scr_table = {"table", "TABLE", table_enter, table_draw, table_key, NULL, table_editor, table_touch, NULL,
                            &softkeys_graph};

/* Console `state` keys for UI tests: table_start, table_row, table_col. */
void table_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[48];
    snprintf(b, sizeof b, "%.10g", start);
    kv("table_start", b, user);
    snprintf(b, sizeof b, "%d", selrow);
    kv("table_row", b, user);
    snprintf(b, sizeof b, "%d", selcol);
    kv("table_col", b, user);
}
