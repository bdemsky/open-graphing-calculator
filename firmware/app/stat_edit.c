/*
 * List editor (STAT > Edit): columns L1..L6 and named lists,
 * the entry line at the bottom. Cells: type a value or expression, ENTER;
 * ⌫ deletes the element, SHIFT ⌫ (ins) inserts one. Column header: ENTER
 * shows the list ({1,2,3}) or its formula; type an expression to fill the
 * list once ("L1*2"), or in quotes to attach a column formula that recomputes
 * ("\"L1*2\""); CLEAR, ENTER clears the list. An unchanged line does nothing.
 * The empty column after the last one takes a name for a new named list.
 *
 * The entry line shows a grey hint when there is nothing to show (an
 * empty row, a name, the new-list column "+ new"), so a first-time user
 * sees what to type.
 *
 * Framework: screen scr_stat_edit ("statedit", help "stat-edit"), opened by
 * stat_edit_open() from APPS > Statistics, the STAT menu and CSV import.
 * Soft keys: stat calc tests plots graph / SHIFT: sortA sortD clrlist csv
 * distr. Menus opened from here (MATH, VARS, ...) insert into the entry line
 * through edit_editor(). The data itself lives in app/stat_lists.c; this
 * file only holds the view state below (not saved: the editor reopens at
 * L1 row 1 after a power cycle).
 *
 * UI state machine:
 *   browsing  (editing == false): arrows move the selected cell, ENTER starts
 *             editing with the cell's current value, typing starts editing
 *             with a blank line, ⌫ / SHIFT ⌫ delete / insert elements.
 *   editing   (editing == true): keys go to the entry line; ENTER, UP and DOWN
 *             apply it (apply()) and leave editing; CLEAR empties the line,
 *             or, if already empty, cancels.
 * `row` == -1 is the column header (the whole list or its formula);
 * `col` == sl_ncols is the empty "new list" column.
 *
 * Geometry (320 x 240 display, body y 18..223):
 *   header strip  y = BODY_Y .. BODY_Y+17 (grey), list names centred
 *   cells         NVIS = 3 columns of CWID = 104 px starting at x = 4,
 *                 NROWS = 10 rows of RH = 16 px from ROW0 = 37, right-aligned
 *   entry line    LINE_Y = 203: "L1(3)=" label then the editor / message
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "input.h"
#include "stat.h"

#define FF (&font_medium)
#define CWID 104                 /* column width (3 columns) */
#define NVIS 3
#define HDR_Y (BODY_Y + 1)
#define ROW0 (BODY_Y + 19)
#define RH 16
#define NROWS ((UI_BOTTOM - ROW0 - 22) / RH)
#define LINE_Y (UI_BOTTOM - 21)

static int col, row = 0, left, top; /* row -1 = header; left/top: first visible column/row */
static bool editing, dirty; /* dirty: the entry line was changed */
static editor_t ed;
static char msg[64]; /* error shown on the entry line until the next key */

/* The list of the selected column, NULL on the "new list" column. */
static slist_t *cur_list(void) { return col < sl_ncols ? sl_at(sl_cols[col]) : NULL; }

/* Open the editor. c >= 0 selects column c (0 if out of range) at row 1,
 * scrolled so that column is the first one shown (as far as the columns
 * reach: the last screen ends with the new-list column); c < 0 keeps the
 * previous position. Always leaves editing. */
void stat_edit_open(int c)
{
    if (c >= 0) {
        col = c < sl_ncols ? c : 0;
        row = 0;
        top = 0;
        left = col < sl_ncols + 1 - NVIS ? col : sl_ncols + 1 - NVIS;
        if (left < 0)
            left = 0;
    }
    editing = false;
    ui_switch(&scr_stat_edit);
}

/* Keep the selection valid after lists changed (or the cursor moved), then
 * scroll so it is visible. A list's last selectable row is the empty row
 * after its last element (where a new value is appended), unless the list is
 * full; the "new list" column only has its header (maxrow -1). */
static void clamp(void)
{
    if (col > sl_ncols)
        col = sl_ncols;
    if (col < 0)
        col = 0;
    slist_t *l = cur_list();
    int maxrow = l ? (l->len < sl_cap() ? l->len : l->len - 1) : -1;
    if (row > maxrow)
        row = maxrow;
    if (row < -1)
        row = -1;
    if (col < left)
        left = col;
    if (col >= left + NVIS)
        left = col - NVIS + 1;
    if (row >= 0 && row < top)
        top = row;
    if (row >= top + NROWS)
        top = row - NROWS + 1;
}

/* Enter editing. blank: start from an empty line (a key was typed; the line
 * counts as changed). Otherwise preload the line with what is selected: on
 * the header the quoted formula, or the values as {1,2,3} (left empty if
 * they do not fit in the editor), on a cell its value. Negative numbers are
 * written with "⁻", the token of the (−) key, so the line looks as if it had
 * been typed and re-evaluates to the same value. */
static void begin_edit(bool blank)
{
    editing = true;
    dirty = blank;
    input_undo_new_field(&ed); /* undo is per cell (app/input_undo.c) */
    slist_t *l = cur_list();
    if (blank || !l)
        return;
    if (row < 0) {
        char b[ED_MAX];
        if (l->formula[0]) {
            snprintf(b, sizeof b, "\"%s\"", l->formula);
        } else { /* the values as {1,2,3} when short */
            size_t o = 0;
            b[o++] = '{';
            for (int i = 0; i < l->len && o < sizeof b - 40; i++) {
                char v[24];
                fmt_num(l->v[i], 12, v, sizeof v);
                o += (size_t)snprintf(b + o, sizeof b - o, "%s%s", i ? "," : "", v[0] == '-' ? "⁻" : "");
                o += (size_t)snprintf(b + o, sizeof b - o, "%s", v[0] == '-' ? v + 1 : v);
            }
            if (o >= sizeof b - 40) /* too long to show */
                o = 0;
            else
                b[o++] = '}';
            b[o] = 0;
        }
        ed_set_text(&ed, b);
    } else if (row < l->len) {
        char b[48];
        fmt_num(l->v[row], 16, b, sizeof b);
        if (b[0] == '-') {
            char d[52];
            snprintf(d, sizeof d, "⁻%s", b + 1);
            ed_set_text(&ed, d);
        } else {
            ed_set_text(&ed, b);
        }
    }
    input_undo_forget(&ed); /* the loaded value is where undo stops */
}

/* One cell of the column starting at x: right-aligned text, selection bar. */
static void draw_cell(int x, int y, const char *s, bool sel, uint16_t fg)
{
    if (sel)
        gfx_fill_rect(x + 1, y - 1, CWID - 2, RH, C_SEL_BG);
    int w = gfx_text_width(FF, s);
    gfx_text(FF, x + CWID - 4 - w, y, s, sel ? C_WHITE : fg, GFX_TRANSPARENT);
}

/* Full redraw: header strip with names ("*" marks a formula column; "+ new"
 * is the new-list column), the visible cells (formula columns in blue, so it
 * is clear they are computed), then the entry line, which shows, in order of
 * priority: the editor while editing, an error message, the selected value
 * with full MODE formatting, or the selected header's formula. */
static void edit_draw(void)
{
    clamp();
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, 18, C_LTGRAY);
    for (int c = 0; c < NVIS; c++) {
        int ci = left + c, x = 4 + c * CWID;
        gfx_vline(x + CWID, BODY_Y, LINE_Y - BODY_Y - 2, C_GRAY);
        if (ci > sl_ncols)
            continue;
        slist_t *l = ci < sl_ncols ? sl_at(sl_cols[ci]) : NULL;
        char h[SL_NAME + 4];
        snprintf(h, sizeof h, "%s%s", l ? l->name : "+ new", l && l->formula[0] ? "*" : "");
        bool hs = ci == col && row < 0;
        if (hs)
            gfx_fill_rect(x + 1, BODY_Y, CWID - 2, 18, C_SEL_BG);
        int w = gfx_text_width(FF, h);
        gfx_text(FF, x + (CWID - w) / 2, HDR_Y + 1, h, hs ? C_WHITE : l ? C_BLACK : C_GRAY, GFX_TRANSPARENT);
        if (!l)
            continue;
        for (int r = 0; r < NROWS; r++) {
            int ri = top + r, y = ROW0 + r * RH;
            bool sel = ci == col && ri == row;
            if (ri < l->len) {
                char b[24];
                fmt_num(l->v[ri], 13, b, sizeof b);
                draw_cell(x, y, b, sel, l->formula[0] ? C_BLUE : C_BLACK);
            } else if (sel) {
                draw_cell(x, y, " ", true, C_BLACK);
            }
        }
    }
    gfx_hline(0, LINE_Y - 2, LCD_WIDTH, C_GRAY);
    slist_t *l = cur_list();
    char lab[32];
    if (!l)
        snprintf(lab, sizeof lab, "Name=");
    else if (row < 0)
        snprintf(lab, sizeof lab, "%s=", l->name);
    else
        snprintf(lab, sizeof lab, "%s(%d)=", l->name, row + 1);
    int lw = gfx_text(FONT, 2, LINE_Y, lab, C_BLACK, GFX_TRANSPARENT) + 2;
    if (editing) {
        ui_draw_editor_line(&ed, lw, LINE_Y, (LCD_WIDTH - lw) / CW, C_BLACK, C_WHITE, true);
        if (msg[0]) { /* the entry was refused: say why above the line, which stays for a fix */
            gfx_fill_rect(0, LINE_Y - 17, LCD_WIDTH, 15, C_WHITE);
            gfx_text(FF, 2, LINE_Y - 16, msg, C_RED, GFX_TRANSPARENT);
        }
    } else if (msg[0]) {
        gfx_text(FF, lw, LINE_Y + 3, msg, C_RED, GFX_TRANSPARENT);
    } else if (l && row >= 0 && row < l->len) {
        char b[40];
        numfmt_double(l->v[row], b, sizeof b);
        gfx_text(FONT, lw, LINE_Y, b, C_BLACK, GFX_TRANSPARENT);
    } else if (l && row < 0 && l->formula[0]) {
        char b[SL_FORMULA + 4];
        snprintf(b, sizeof b, "\"%s\"", l->formula);
        gfx_text(FONT, lw, LINE_Y, b, C_BLUE, GFX_TRANSPARENT);
    } else {
        /* nothing to show: say what the selected place takes */
        const char *h = !l ? "type a name for a new list" : row < 0 ? "ENTER: the whole list, or a formula"
                        : l->len >= sl_cap()  ? "list is full"
                                              : "type a value, ENTER";
        gfx_text(FF, lw, LINE_Y + 3, h, C_GRAY, GFX_TRANSPARENT);
    }
}

/* ENTER while editing: apply the entry line. Returns true when editing may
 * end, false (with msg set) to stay in the editor so the user can fix the
 * input. Cases:
 *   unchanged line       nothing, just move down a row
 *   new-list column      the text is a name: create (or jump to) that list
 *   header, empty        clear the list and its formula
 *   header, "expr"       attach a column formula (evaluated now and on every
 *                        later list change, see sl_recompute)
 *   header, expr         fill the list once with the result
 *   cell                 evaluate a number (a value with an uncertainty: its
 *                        nominal value); on the row after the end it is
 *                        appended. Hand edits remove the column formula.
 * Every change goes through sl_changed() (formulas, graph cache). */
static bool apply(void)
{
    char disp[ED_MAX * 4], cas[ED_MAX * 4], err[64];
    ed_display(&ed, disp, sizeof disp);
    slist_t *l = cur_list();
    if (!dirty) { /* unchanged: nothing to do */
        if (l && row >= 0 && row < l->len)
            row++;
        return true;
    }
    if (!l) { /* new named list */
        slist_t *n = disp[0] ? sl_create(disp) : NULL;
        if (!n) {
            snprintf(msg, sizeof msg, "%s", disp[0] ? "bad name or no room" : "");
            return false;
        }
        for (int i = 0; i < sl_ncols; i++)
            if (sl_cols[i] == (int)(n - sl_at(0)))
                return (col = i), true;
        sl_cols[sl_ncols++] = (int)(n - sl_at(0));
        return true;
    }
    if (row < 0) {
        size_t n = strlen(disp);
        if (!n) {
            l->formula[0] = 0;
            l->len = 0;
            sl_changed(l);
            return true;
        }
        bool quoted = disp[0] == '"';
        /* strip the quotes (the closing one is optional); longer formulas are cut to SL_FORMULA-1 */
        char expr[SL_FORMULA];
        snprintf(expr, sizeof expr, "%.*s", (int)(quoted ? n - (disp[n - 1] == '"' ? 2 : 1) : n), disp + quoted);
        if (sl_eval_into(l, expr, err, sizeof err)) {
            snprintf(msg, sizeof msg, "ERR: %.50s", err);
            return false;
        }
        if (quoted)
            snprintf(l->formula, sizeof l->formula, "%s", expr);
        else
            l->formula[0] = 0;
        sl_changed(l);
        return true;
    }
    ed_cas(&ed, cas, sizeof cas);
    if (!cas[0])
        return true;
    /* a value with an uncertainty is stored as its value: lists hold plain numbers */
    const char *num = cas;
    if ((strstr(cas, "\xc2\xb1") || strstr(cas, "pm(")) &&
        (size_t)snprintf(disp, sizeof disp, "nominal(%s)", cas) < sizeof disp)
        num = disp;
    double v;
    if (eval_number(num, &v, err, sizeof err)) {
        snprintf(msg, sizeof msg, "ERR: %.50s", err);
        return false;
    }
    if (row >= l->len) {
        if (l->len >= sl_cap()) {
            snprintf(msg, sizeof msg, "list is full");
            return false;
        }
        l->len++;
    }
    l->v[row] = v;
    l->formula[0] = 0; /* edited by hand: no longer a formula */
    sl_changed(l);
    row++;
    return true;
}

/* Key handler; see the state machine in the file header. In editing mode
 * UP/DOWN on a cell apply the line and move one row up / down from the
 * edited row (apply() may or may not have advanced: it does not after an
 * unchanged edit on the append row or an empty one). On the header UP/DOWN
 * just leave editing (DOWN to row 1) without applying. CLEAR while browsing
 * returns to the home screen. */
static void edit_key(int key, int mod)
{
    msg[0] = 0;
    slist_t *l = cur_list();
    if (editing) {
        switch (key) {
        case KEY_ENTER:
            if (apply())
                editing = false;
            break;
        case KEY_CLEAR:
            if (!ed_empty(&ed))
                ed_clear(&ed), dirty = true;
            else
                editing = false;
            break;
        case KEY_UP:
        case KEY_DOWN: {
            int r0 = row;
            if (row >= 0 && apply()) {
                editing = false;
                row = r0 + (key == KEY_UP ? -1 : 1);
            } else if (row < 0) {
                editing = false;
                row += key == KEY_DOWN;
            }
            break;
        }
        default:
            if (ui_editor_key(&ed, key, mod)) {
                dirty |= key == KEY_DEL;
                break;
            }
            dirty = true;
            {
                char t[16];
                if (ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
                    ed_insert_text(&ed, t, NULL);
                else
                    return;
            }
        }
        edit_draw();
        return;
    }
    switch (key) {
    case KEY_UP: row--; break;
    case KEY_DOWN: row++; break;
    case KEY_LEFT: if (col > 0) col--; break;
    case KEY_RIGHT: if (col < sl_ncols) col++; break;
    case KEY_ENTER: begin_edit(false); break;
    case KEY_CLEAR:
        ui_switch(ui_home());
        return;
    case KEY_DEL:
        if (!l)
            break;
        if (mod == MOD_SHIFT) { /* INS */
            if (row >= 0 && row <= l->len && l->len < sl_cap()) {
                memmove(&l->v[row + 1], &l->v[row], sizeof(double) * (size_t)(l->len - row));
                l->v[row] = 0;
                l->len++;
                l->formula[0] = 0;
                sl_changed(l);
            }
        } else if (row < 0) { /* remove the column from the editor (the list is kept) */
            memmove(&sl_cols[col], &sl_cols[col + 1], sizeof(int) * (size_t)(sl_ncols - col - 1));
            sl_ncols--;
        } else if (row < l->len) {
            memmove(&l->v[row], &l->v[row + 1], sizeof(double) * (size_t)(l->len - row - 1));
            l->len--;
            l->formula[0] = 0;
            sl_changed(l);
        }
        break;
    default: {
        char t[16];
        if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
            return;
        begin_edit(true);
        ed_insert_text(&ed, t, NULL);
    }
    }
    edit_draw();
}

/* Insertion target for menus (MATH, VARS, CATALOG ...): starts editing with a
 * blank line if needed and marks the line changed. */
static editor_t *edit_editor(void)
{
    if (!editing)
        begin_edit(true);
    dirty = true;
    return &ed;
}

/* Tap selects the tapped cell or header (above ROW0) and cancels any edit;
 * taps on the entry line are ignored. clamp() in edit_draw() fixes rows
 * below the end of the list. */
static void edit_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        /* drag: rows and columns (lists plus the "new list" column) follow
         * the finger; a selection pushed off screen moves into view and, like
         * a tap, ends editing. Rows go down to the selected list's empty row
         * (to the longest list's while the header is selected). */
        int nrow = 0;
        for (int c = 0; c < sl_ncols; c++) {
            const slist_t *l = sl_at(sl_cols[c]);
            int n = l->len < sl_cap() ? l->len + 1 : l->len;
            if ((row < 0 || c == col) && n > nrow)
                nrow = n;
        }
        bool moved = ui_drag_scroll(ev, UI_DRAG_Y, RH, &top, nrow - NROWS);
        moved |= ui_drag_scroll(ev, UI_DRAG_X, CWID, &left, sl_ncols + 1 - NVIS);
        if (!moved)
            return;
        int r = row >= 0 ? ui_drag_focus(row, top, NROWS) : row, c = ui_drag_focus(col, left, NVIS);
        if (r != row || c != col)
            editing = false;
        row = r, col = c;
        edit_draw();
        return;
    }
    if (ev->type != TEV_TAP || ev->y >= LINE_Y - 2)
        return;
    editing = false;
    int c = left + (ev->x - 4) / CWID;
    if (c > sl_ncols)
        return;
    col = c;
    row = ev->y < ROW0 ? -1 : top + (ev->y - ROW0) / RH;
    edit_draw();
}

static void edit_enter(void)
{
    if (!ui_returning)
        msg[0] = 0;
}

static void sk_graph(void) { ui_switch(&scr_graph); }
static const softkeys_t edit_soft = {
    {{"stat", stat_menu_open}, {"calc", stat_calc_open}, {"tests", stat_tests_open}, {"plots", stat_plot_menu_open},
     {"graph", sk_graph}},
    {{"sortA", stat_sorta}, {"sortD", stat_sortd}, {"clrlist", stat_clrlist}, {"csv", stat_csv_menu_open},
     {"distr", stat_distr_open}},
};
const screen_t scr_stat_edit = {"statedit", "LIST EDITOR", edit_enter, edit_draw, edit_key, NULL, edit_editor,
                                edit_touch, "stat-edit", &edit_soft};

/* for the STAT menu's Sort / ClrList when called from the editor */
slist_t *stat_edit_current(void) { return ui_current() == &scr_stat_edit || ui_previous() == &scr_stat_edit ? cur_list() : NULL; }

/* Console `state` keys: statedit_list (selected list name) and statedit_row
 * (1-based; 0 = header). */
void stat_edit_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[32];
    slist_t *l = cur_list();
    kv("statedit_list", l ? l->name : "", user);
    snprintf(b, sizeof b, "%d", row + 1);
    kv("statedit_row", b, user);
}

/* Copy hook (stat.h): while editing, the entry line; otherwise the text
 * begin_edit(false) would load, after which the browsing state is put back
 * (editing off, line empty). The caller pauses the undo log around it. */
bool stat_edit_clip_text(char *out, size_t n)
{
    if (editing) {
        ed_display(&ed, out, n);
        return out[0] != 0;
    }
    begin_edit(false);
    ed_display(&ed, out, n);
    editing = dirty = false;
    ed_clear(&ed);
    return out[0] != 0;
}
