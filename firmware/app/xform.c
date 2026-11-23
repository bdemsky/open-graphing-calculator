/* Row-list form and cell grid widgets (see xform.h).
 *
 * Layout (large font, CH = 20 px rows):
 *   xform: XF_ROWS (9) rows from BODY_Y + 1, then a 19 px grey footer above
 *          the soft-key bar with the message, else the selected row's hint
 *          (ops->hint), else the form's hint; text too wide for font_medium
 *          is drawn in font_small (x_footer_text). The selected XR_EDIT
 *          row shows its label and a live line editor; other rows draw
 *          "label value" as one text run clipped to the screen width.
 *   xgrid: dimensions line at BODY_Y + 1 (plus an optional grey note), an
 *          optional 13 px column header (colname), GROWS (8, or 7 with the
 *          header) rows x GCOLS (4) cells of GW px (6 visible characters,
 *          "~" marks truncation) after a row number column, and a bottom
 *          line with "r,c=" and the selected cell's full value or its
 *          editor (on the dimensions line: how to set the size).
 * No state of its own beyond the structs the apps pass in. */
#include "xform.h"
#include "input.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Cold UI code: optimise for size on the firmware build. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif

/* visible form rows (the footer takes 20 px) and the top of row r */
#define XF_ROWS ((BODY_H - 20) / CH)
#define XF_Y(r) (BODY_Y + 1 + (r) * CH)

/* MODE-formatted number of at most 16 characters: with FIX/SCI/ENG the
 * MODE format (numfmt_double), unless that is wider; in NORMAL FLOAT (and as
 * the fallback) the shortest form with up to 10 significant digits. */
void x_fmt(double v, char *buf, size_t n)
{
    if (g.fmt != FMT_NORMAL || g.fix >= 0) {
        char t[64];
        numfmt_double(v, t, sizeof t);
        if (utf8_len(t) <= 16) {
            snprintf(buf, n, "%s", t);
            return;
        }
    }
    fmt_num(v, 16, buf, n);
}

/* Evaluate a field's CAS text to a double (eval_number: no effect on ans).
 * An empty field is an error ("empty"). Returns true on success. */
bool x_number(const char *cas, double *out, char *err, size_t errn)
{
    if (!cas[0]) {
        snprintf(err, errn, "empty");
        return false;
    }
    return eval_number(cas, out, err, errn) == 0;
}

/* Display text -> CAS text by tokenising it in a scratch editor (so "π",
 * "√(" etc. convert exactly as typed ones would). Not reentrant. */
void x_cas_of(const char *disp, char *cas, size_t n)
{
    static editor_t tmp;
    ed_set_text(&tmp, disp);
    ed_cas(&tmp, cas, n);
}

void xform_msg(xform_t *f, bool err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(f->msg, sizeof f->msg, fmt, ap);
    va_end(ap);
    f->err = err;
}

/* One footer / status line of text at y: font_medium when it fits the
 * width, else the narrower font_small (52 instead of 45 characters), so
 * hints and messages are not cut off; still longer text is clipped. */
void x_footer_text(const char *s, int y, uint16_t color)
{
    const font_t *fo = gfx_text_width(&font_medium, s) <= LCD_WIDTH - 6 ? &font_medium : &font_small;
    gfx_set_clip(0, y - 2, LCD_WIDTH, 18);
    gfx_text(fo, 4, fo == &font_small ? y + 1 : y, s, color, GFX_TRANSPARENT);
    gfx_reset_clip();
}

/* Load the selected row's value into the editor (empty for non-edit rows),
 * cursor at the end, marked fresh so the first typed key replaces it. */
static void load(xform_t *f)
{
    char b[ED_MAX * 2];
    b[0] = 0;
    if (f->sel < f->o->count() && f->o->kind(f->sel) == XR_EDIT)
        f->o->get(f->sel, b, sizeof b);
    ed_set_text(&f->ed, b);
    ed_end(&f->ed);
    input_undo_forget(&f->ed); /* undo is per row: it stops at the loaded value */
    f->fresh = true;
    f->loaded = true;
}

void xform_reset(xform_t *f)
{
    int n = f->o->count();
    if (f->sel >= n)
        f->sel = n ? n - 1 : 0;
    load(f);
}

/* Menus insert into the field: keep its text (no longer fresh). */
editor_t *xform_editor(xform_t *f)
{
    f->fresh = false;
    return &f->ed;
}

/* Commit the edited row if it was changed (not fresh). On success the row is
 * reloaded so the field shows the value as the app stored/formatted it. */
bool xform_commit(xform_t *f)
{
    if (!f->loaded || f->fresh || f->sel >= f->o->count() || f->o->kind(f->sel) != XR_EDIT)
        return true;
    char cas[ED_MAX * 4], disp[ED_MAX * 4];
    ed_cas(&f->ed, cas, sizeof cas);
    ed_display(&f->ed, disp, sizeof disp);
    if (!f->o->set(f->sel, cas, disp))
        return false;
    load(f);
    return true;
}

/* Select the row d steps away (wrapping), after committing the current one;
 * stays put if the app refuses the value (its set() leaves a message). */
static void move(xform_t *f, int d)
{
    int n = f->o->count();
    if (!n || !xform_commit(f))
        return;
    f->sel = (f->sel + d + n) % n;
    f->msg[0] = 0;
    load(f);
}

/* The widget drawn last and its screen (xform.h; read by copy/paste). */
xform_t *xform_shown;
xgrid_t *xgrid_shown;
const screen_t *xform_shown_on, *xgrid_shown_on;

/* Draw the form: scroll so the selection is visible, rows, ↑/↓ marks, footer.
 * XR_INFO rows (results) are blue; a selected non-edit row is highlighted. */

void xform_draw(xform_t *f)
{
    xform_shown = f;
    xform_shown_on = ui_current();
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    if (!f->loaded)
        load(f);
    int n = f->o->count();
    if (f->sel < f->top)
        f->top = f->sel;
    if (f->sel >= f->top + XF_ROWS)
        f->top = f->sel - XF_ROWS + 1;
    char lab[48], val[ED_MAX * 2];
    for (int r = 0; r < XF_ROWS && f->top + r < n; r++) {
        int i = f->top + r, y = XF_Y(r), k = f->o->kind(i);
        f->o->label(i, lab, sizeof lab);
        int lw = gfx_text_width(FONT, lab);
        bool on = i == f->sel;
        if (on && k != XR_EDIT)
            gfx_fill_rect(0, y, LCD_WIDTH, CH, C_SEL_BG);
        if (on && k == XR_EDIT) {
            gfx_text(FONT, 4, y, lab, C_DKGRAY, GFX_TRANSPARENT);
            ui_draw_editor_line(&f->ed, 4 + lw, y, (LCD_WIDTH - 8 - lw) / CW, C_BLACK, C_WHITE, true);
        } else {
            /* label and value as one text run, clipped to the line */
            size_t ll = strlen(lab);
            snprintf(val, sizeof val, "%s", lab);
            f->o->get(i, val + ll, sizeof val - ll);
            uint16_t fg = on ? C_WHITE : k == XR_INFO ? C_BLUE : C_BLACK;
            const char *p = val; /* cut after COLS - 1 codepoints */
            for (int c = 0; c < COLS - 1 && *p; c++)
                utf8_next(&p);
            val[p - val] = 0;
            gfx_text(FONT, 4, y, val, fg, GFX_TRANSPARENT);
        }
    }
    if (f->top > 0)
        gfx_text(&font_small, LCD_WIDTH - 8, BODY_Y + 1, "↑", C_GRAY, GFX_TRANSPARENT);
    if (f->top + XF_ROWS < n)
        gfx_text(&font_small, LCD_WIDTH - 8, XF_Y(XF_ROWS) - 14, "↓", C_GRAY, GFX_TRANSPARENT);
    const char *foot = f->msg[0] ? f->msg : NULL;
    if (!foot && f->o->hint && n)
        foot = f->o->hint(f->sel);
    if (!foot || !foot[0])
        foot = f->hint ? f->hint : "";
    gfx_fill_rect(0, UI_BOTTOM - 19, LCD_WIDTH, 19, C_LTGRAY);
    x_footer_text(foot, UI_BOTTOM - 17, f->msg[0] && f->err ? C_RED : C_BLACK);
}

/* Key handling (summary in xform.h). Typed text replaces a fresh field and
 * is appended otherwise; LEFT/RIGHT/⌫ edit in place. Redraws when the key
 * was used. */
bool xform_key(xform_t *f, int key, int mod)
{
    int n = f->o->count();
    if (!n)
        return false;
    if (!f->loaded)
        load(f);
    int k = f->o->kind(f->sel);
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
        move(f, key == KEY_UP ? -1 : 1);
        break;
    case KEY_F5: /* soft key "solve" */
        if (!f->o->action || !xform_commit(f))
            break;
        f->o->action(f->sel);
        load(f);
        break;
    case KEY_ENTER:
        if (mod != MOD_NONE || g.alpha_snapshot_for_key) { /* ALPHA ENTER = SOLVE */
            if (!f->o->action || !xform_commit(f))
                break;
            f->o->action(f->sel);
            load(f);
        } else if (k == XR_CHOICE)
            f->o->cycle(f->sel, 1);
        else
            move(f, 1);
        break;
    case KEY_CLEAR:
        if (k != XR_EDIT || ed_empty(&f->ed))
            return false;
        ed_clear(&f->ed);
        f->fresh = false;
        break;
    case KEY_LEFT:
    case KEY_RIGHT:
        if (k == XR_CHOICE) {
            f->o->cycle(f->sel, key == KEY_LEFT ? -1 : 1);
            break;
        }
        /* fall through */
    default:
        if (k != XR_EDIT)
            return false;
        if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_DEL) {
            f->fresh = false;
            ui_editor_key(&f->ed, key, mod);
            break;
        }
        {
            char t[16];
            if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
                return false;
            if (f->fresh)
                ed_clear(&f->ed);
            f->fresh = false;
            ed_insert_text(&f->ed, t, NULL);
        }
    }
    xform_draw(f);
    return true;
}

/* Drag: scroll the rows (ui_drag_scroll); a selection pushed off screen
 * moves to the nearest visible row after committing the edited one. A value
 * the app refuses keeps its row selected, so the view stops where that row
 * is still visible. */
static void xform_drag(xform_t *f, const touch_event_t *ev)
{
    int n = f->o->count(), top = f->top;
    if (!ui_drag_scroll(ev, UI_DRAG_Y, CH, &top, n - XF_ROWS))
        return;
    int s = ui_drag_focus(f->sel, top, XF_ROWS);
    if (s != f->sel && xform_commit(f)) { /* refused: xform_draw scrolls back to the row */
        f->sel = s;
        load(f);
    }
    f->top = top;
    xform_draw(f);
}

/* Tap a row to select it (committing the current one first); tapping the
 * selected XR_CHOICE row cycles it; drag to scroll. */
void xform_touch(xform_t *f, const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG)
        xform_drag(f, ev);
    if (ev->type != TEV_TAP || ev->y < BODY_Y || ev->y >= XF_Y(XF_ROWS))
        return;
    int i = f->top + (ev->y - BODY_Y - 1) / CH;
    if (i >= f->o->count())
        return;
    if (i != f->sel) {
        if (!xform_commit(f)) {
            xform_draw(f);
            return;
        }
        f->sel = i;
        load(f);
    } else if (f->o->kind(i) == XR_CHOICE)
        f->o->cycle(i, 1);
    xform_draw(f);
}

/* ================================================================== grid */
/* Grid geometry: GROWS rows below the dimensions line (and the optional
 * 13 px column header, GHDR) and above the bottom line, GCOLS cells per row;
 * GY(r) is the top of visible row r. GHDR, GROWS and GY use the grid `gr` in
 * scope. Without a header: 8 rows; with one: 7. */
#define GW 70  /* cell width (7 columns of text) */
#define GX0 34 /* row-number column */
#define GHDR ((gr)->colname ? 13 : 0)
#define GROWS ((BODY_H - 2 * CH - 4 - GHDR) / CH)
#define GCOLS ((LCD_WIDTH - GX0) / GW)
#define GY(r) (BODY_Y + CH + 2 + GHDR + (r) * CH)

/* screen_t.editor for grid screens: a menu selection starts a new (empty)
 * entry in the selected cell unless one is already being edited. NULL is
 * never returned, but on the dimensions line the editor is not shown. */
editor_t *xgrid_editor(xgrid_t *gr)
{
    if (!gr->editing && gr->sr >= 0) {
        input_undo_new_field(&gr->ed); /* undo is per cell */
        gr->editing = true;
    }
    return &gr->ed;
}

/* Copy at most `cols` codepoints of s into out (n bytes); if s is longer, the
 * last visible character is replaced by "~". */
static void clip_text(const char *s, int cols, char *out, size_t n)
{
    const char *p = s;
    int c = 0;
    while (*p && c < cols) {
        utf8_next(&p);
        c++;
    }
    size_t l = (size_t)(p - s) < n - 4 ? (size_t)(p - s) : n - 4;
    memcpy(out, s, l);
    out[l] = 0;
    if (*p && l >= 1) {
        /* mark truncation */
        const char *q = out;
        int k = 0;
        while (*q && k < cols - 1) {
            utf8_next(&q);
            k++;
        }
        snprintf(out + (q - out), n - (size_t)(q - out), "~");
    }
}

/* Draw the grid: dimensions line (the selected dimension highlighted; a
 * number being typed is shown instead of the value, with a cursor), the
 * optional note, scroll (tr, tc) so the selected cell is visible, the
 * optional column header, the cells, ◀/▶ marks for hidden columns, and the
 * bottom line (message, else the selected cell, else on the dimensions line
 * a hint how to set the size). */
void xgrid_draw(xgrid_t *gr)
{
    xgrid_shown = gr;
    xgrid_shown_on = ui_current();
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    char b[96], t[32];
    /* dimensions line */
    gfx_text(FONT, 4, BODY_Y + 1, gr->title, C_BLACK, GFX_TRANSPARENT);
    int x = 4 + gfx_text_width(FONT, gr->title) + CW;
    for (int k = 0; k < 2; k++) {
        bool on = gr->sr < 0 && gr->sc == k, pend = on && gr->dpend && gr->dsel == k;
        snprintf(t, sizeof t, "%d%s", pend ? gr->dacc : k ? gr->cols : gr->rows, pend ? "_" : "");
        int w = gfx_text_width(FONT, t);
        if (on)
            gfx_fill_rect(x - 1, BODY_Y + 1, w + 2, CH, C_SEL_BG);
        gfx_text(FONT, x, BODY_Y + 1, t, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
        x += w;
        if (!k)
            x += gfx_text(FONT, x, BODY_Y + 1, "×", C_BLACK, GFX_TRANSPARENT);
    }
    if (gr->note[0])
        gfx_text(&font_medium, x + CW, BODY_Y + 4, gr->note, C_GRAY, GFX_TRANSPARENT);
    if (gr->sr >= 0) {
        if (gr->sr < gr->tr)
            gr->tr = gr->sr;
        if (gr->sr >= gr->tr + GROWS)
            gr->tr = gr->sr - GROWS + 1;
        if (gr->sc < gr->tc)
            gr->tc = gr->sc;
        if (gr->sc >= gr->tc + GCOLS)
            gr->tc = gr->sc - GCOLS + 1;
    }
    if (gr->colname) /* column header: small grey names over the cells, aligned with their text */
        for (int c = 0; c < GCOLS && gr->tc + c < gr->cols; c++) {
            gr->colname(gr->tc + c, t, sizeof t);
            gfx_text(&font_small, GX0 + c * GW + 2, GY(0) - GHDR, t, C_GRAY, GFX_TRANSPARENT);
        }
    for (int r = 0; r < GROWS && gr->tr + r < gr->rows; r++) {
        snprintf(t, sizeof t, "%d", gr->tr + r + 1);
        gfx_text(&font_medium, 4, GY(r) + 3, t, C_GRAY, GFX_TRANSPARENT);
        for (int c = 0; c < GCOLS && gr->tc + c < gr->cols; c++) {
            int rr = gr->tr + r, cc = gr->tc + c, cx = GX0 + c * GW;
            bool on = rr == gr->sr && cc == gr->sc;
            if (on)
                gfx_fill_rect(cx, GY(r), GW - 2, CH, C_SEL_BG);
            gr->cell(rr, cc, b, sizeof b);
            clip_text(b, GW / CW - 1, t, sizeof t);
            gfx_text(FONT, cx + 2, GY(r), t, on ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
        }
    }
    if (gr->tc > 0)
        gfx_text(&font_small, GX0 - 8, GY(0), "◀", C_GRAY, GFX_TRANSPARENT);
    if (gr->tc + GCOLS < gr->cols)
        gfx_text(&font_small, LCD_WIDTH - 7, GY(0), "▶", C_GRAY, GFX_TRANSPARENT);
    /* bottom line: the selected cell (full text) or its editor */
    int yb = UI_BOTTOM - CH - 1;
    gfx_fill_rect(0, yb - 1, LCD_WIDTH, CH + 2, C_LTGRAY);
    if (gr->msg[0]) {
        x_footer_text(gr->msg, yb + 3, gr->err ? C_RED : C_BLACK);
    } else if (gr->sr < 0) {
        x_footer_text(gr->dpend   ? "ENTER or an arrow sets it, CLEAR cancels"
                      : gr->sc == 0 ? "size: type the rows, ▶ columns, ▼ cells"
                                    : "size: type the columns, ◀ rows, ▼ cells",
                      yb + 3, C_DKGRAY);
    } else {
        snprintf(t, sizeof t, "%d,%d=", gr->sr + 1, gr->sc + 1);
        int lw = gfx_text(FONT, 4, yb, t, C_DKGRAY, GFX_TRANSPARENT);
        if (gr->editing)
            ui_draw_editor_line(&gr->ed, 4 + lw, yb, (LCD_WIDTH - 8 - lw) / CW, C_BLACK, C_LTGRAY, true);
        else {
            gr->cell(gr->sr, gr->sc, b, sizeof b);
            char v[COLS * 4];
            clip_text(b, (LCD_WIDTH - 8 - lw) / CW, v, sizeof v);
            gfx_text(FONT, 4 + lw, yb, v, C_BLACK, GFX_TRANSPARENT);
        }
    }
}

/* Store the cell being edited. An empty entry leaves the cell unchanged. On a
 * refused value the editor stays open with the error message. */
static bool commit_cell(xgrid_t *gr)
{
    if (!gr->editing)
        return true;
    char cas[ED_MAX * 4];
    ed_cas(&gr->ed, cas, sizeof cas);
    gr->editing = false;
    if (!cas[0])
        return true;
    if (!gr->set(gr->sr, gr->sc, cas, gr->msg, sizeof gr->msg)) {
        gr->err = true;
        gr->editing = true;
        return false;
    }
    return true;
}

/* Set the selected dimension (sc 0 = rows, 1 = columns) to d, within
 * 1..maxr / 1..maxc, through the app's resize() callback. */
static void set_dim(xgrid_t *gr, int d)
{
    int r = gr->rows, c = gr->cols;
    if (gr->sc == 0)
        r = d;
    else
        c = d;
    if (r < 1 || c < 1 || r > gr->maxr || c > gr->maxc) {
        snprintf(gr->msg, sizeof gr->msg, "ERR: %s 1..%d", gr->sc == 0 ? "rows" : "columns",
                 gr->sc == 0 ? gr->maxr : gr->maxc);
        gr->err = true;
        return;
    }
    if (gr->resize(r, c, gr->msg, sizeof gr->msg)) {
        gr->rows = r;
        gr->cols = c;
    } else
        gr->err = true;
}

/* Apply a number typed on the dimensions line that is still pending
 * (dpend). Returns false if the size was refused (message set). */
static bool flush_dim(xgrid_t *gr)
{
    if (!gr->dpend)
        return true;
    gr->dpend = false;
    int sc = gr->sc;
    gr->sc = gr->dsel;
    set_dim(gr, gr->dacc);
    gr->sc = sc;
    gr->dacc = 0;
    return !gr->err;
}

/* Key handling, three states:
 *   dimensions line (sr < 0): digits set the value (two digits typed in a row
 *     on the same field accumulate, up to 99; any other key ends the number).
 *     A number that could still get a second digit within the maximum (1 or
 *     2 when the maximum is 20) is pending (dpend) until the next key, so
 *     typing 12 does not first shrink the matrix to 1 row; CLEAR drops a
 *     pending number. LEFT/RIGHT pick rows/cols, ENTER goes rows -> cols ->
 *     first cell, DOWN to the first cell; arriving there with UP always
 *     selects rows;
 *   editing a cell: ENTER commits and moves right (wrapping to the next
 *     row), UP/DOWN commit and move, CLEAR empties then cancels;
 *   browsing: arrows move (UP from row 0 to the dimensions line), ENTER
 *     edits the cell's current value, a text key starts a new entry.
 * Returns false for keys it does not use. */
bool xgrid_key(xgrid_t *gr, int key, int mod)
{
    gr->msg[0] = 0; /* a message lasts until the next key */
    gr->err = false;
    if (gr->sr < 0) {
        /* dimensions: digits set the value, arrows move */
        static const uint8_t dk[10] = {KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
        for (int d = 0; d < 10; d++)
            if (key == dk[d] && mod == MOD_NONE) {
                int v = (gr->dacc && gr->dsel == gr->sc && gr->dacc * 10 + d <= 99) ? gr->dacc * 10 + d : d;
                int mx = gr->sc == 0 ? gr->maxr : gr->maxc;
                gr->dacc = v ? v : 0;
                gr->dsel = gr->sc;
                gr->dpend = false;
                if (v >= 1 && v * 10 <= mx && v < 10)
                    gr->dpend = true; /* a second digit may follow */
                else if (v >= 1)
                    set_dim(gr, v);
                xgrid_draw(gr);
                return true;
            }
        if (key == KEY_CLEAR && gr->dpend) {
            gr->dpend = false;
            gr->dacc = 0;
            xgrid_draw(gr);
            return true;
        }
        if (gr->dpend && (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_DOWN || key == KEY_ENTER) &&
            !flush_dim(gr)) {
            xgrid_draw(gr); /* refused: stay on the field with the message */
            return true;
        }
        gr->dacc = 0;
        gr->dpend = false;
        switch (key) {
        case KEY_LEFT: gr->sc = 0; break;
        case KEY_RIGHT: gr->sc = 1; break;
        case KEY_DOWN:
        case KEY_ENTER:
            if (key == KEY_ENTER && gr->sc == 0) {
                gr->sc = 1;
                break;
            }
            gr->sr = 0;
            gr->sc = 0;
            break;
        default: return false;
        }
        xgrid_draw(gr);
        return true;
    }
    if (gr->editing) {
        switch (key) {
        case KEY_ENTER:
        case KEY_UP:
        case KEY_DOWN:
            if (!commit_cell(gr))
                break;
            if (key == KEY_UP && gr->sr > 0)
                gr->sr--;
            else if (key == KEY_UP)
                gr->sr = -1, gr->sc = 0, gr->dacc = 0;
            else if (key == KEY_DOWN)
                gr->sr = gr->sr + 1 < gr->rows ? gr->sr + 1 : gr->sr;
            else if (gr->sc + 1 < gr->cols)
                gr->sc++;
            else if (gr->sr + 1 < gr->rows)
                gr->sr++, gr->sc = 0;
            break;
        case KEY_CLEAR:
            if (ed_empty(&gr->ed))
                gr->editing = false;
            else
                ed_clear(&gr->ed);
            break;
        default:
            if (!ui_editor_key(&gr->ed, key, mod))
                return false;
        }
        xgrid_draw(gr);
        return true;
    }
    switch (key) {
    case KEY_UP:
        gr->sr--;
        if (gr->sr < 0)
            gr->sc = 0, gr->dacc = 0;
        break;
    case KEY_DOWN: if (gr->sr + 1 < gr->rows) gr->sr++; break;
    case KEY_LEFT: if (gr->sc > 0) gr->sc--; break;
    case KEY_RIGHT: if (gr->sc + 1 < gr->cols) gr->sc++; break;
    case KEY_ENTER: {
        char b[ED_MAX * 2];
        gr->cell(gr->sr, gr->sc, b, sizeof b);
        ed_set_text(&gr->ed, b);
        ed_end(&gr->ed);
        input_undo_forget(&gr->ed); /* the cell's value is where undo stops */
        gr->editing = true;
        break;
    }
    default: {
        char t[16];
        if (key == KEY_DEL || key == KEY_CLEAR || !ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
            return false;
        input_undo_new_field(&gr->ed); /* a new entry: undo goes back to "" at most */
        ed_insert_text(&gr->ed, t, NULL);
        gr->editing = true;
    }
    }
    xgrid_draw(gr);
    return true;
}

/* Drag: scroll rows and columns (ui_drag_scroll, one per row height / cell
 * width); a selected cell pushed off screen moves to the nearest visible one
 * after committing an edit (a refused value keeps the cell and stops the
 * view where it is still visible). On the dimensions line the view scrolls
 * freely. */
static void xgrid_drag(xgrid_t *gr, const touch_event_t *ev)
{
    int tr = gr->tr, tc = gr->tc;
    bool moved = ui_drag_scroll(ev, UI_DRAG_Y, CH, &tr, gr->rows - GROWS);
    moved |= ui_drag_scroll(ev, UI_DRAG_X, GW, &tc, gr->cols - GCOLS);
    if (!moved)
        return;
    if (gr->sr >= 0) {
        int sr = ui_drag_focus(gr->sr, tr, GROWS), sc = ui_drag_focus(gr->sc, tc, GCOLS);
        if ((sr == gr->sr && sc == gr->sc) || commit_cell(gr))
            gr->sr = sr, gr->sc = sc; /* else xgrid_draw scrolls back to the cell */
    }
    gr->tr = tr;
    gr->tc = tc;
    xgrid_draw(gr);
}

/* Tap a visible cell to select it (after committing an edit); drag to
 * scroll. */
void xgrid_touch(xgrid_t *gr, const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG)
        xgrid_drag(gr, ev);
    if (ev->type != TEV_TAP)
        return;
    if (gr->dpend && !flush_dim(gr)) {
        xgrid_draw(gr);
        return;
    }
    int r = (ev->y - GY(0)) / CH, c = (ev->x - GX0) / GW;
    if (ev->y < GY(0) || r >= GROWS || ev->x < GX0 || c >= GCOLS || gr->tr + r >= gr->rows || gr->tc + c >= gr->cols)
        return;
    if (!commit_cell(gr)) {
        xgrid_draw(gr);
        return;
    }
    gr->sr = gr->tr + r;
    gr->sc = gr->tc + c;
    xgrid_draw(gr);
}
