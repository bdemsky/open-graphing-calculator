/*
 * 3D graphing entry editor (scr_g3d_eq, title "3D Z=", help "graph3d-eq") and
 * 3D WINDOW form (scr_g3d_win, title "3D WINDOW", help "graph3d-window").
 *
 * Entry editor. A header line on top: the graph editor's 2D/3D switch at its
 * right ("3D" lit; a tap on "2D", or F1 "2D", opens the 2D entries, Y=:
 * graph_editor_switch, yeq.c). Six entries Z1..Z6, each of one kind: z = f(x,y) (one line
 * "z1(x,y)="), a parametric surface (three lines x2(u,v)=, y2(u,v)=,
 * z2(u,v)=) or a space curve (x3(t)=, y3(t)=, z3(t)=). A line, left to right:
 * colour swatch (first line of an entry), the label in the entry's colour, the
 * '=' toggle (filled when the entry is enabled), the expression and, under the
 * last line of the entry, its compile error. ROW_H-high lines, scrolled to
 * keep the focused line visible.
 *   Keys: {▲}{▼} move between lines, ENTER moves down (or, on '=' / the
 *   swatch, toggles the entry / cycles its colour), {◀} at the start of a
 *   first line moves the focus to '=' and then the swatch, {▶} back. CLEAR
 *   empties the line. The X key types the entry's first parameter (x, u or t).
 *   SHIFT F1 "type" cycles the kind of the focused entry (z(x,y) -> surface ->
 *   curve), SHIFT F2 "on/off" toggles it, SHIFT F3 "color" cycles its colour.
 *   Touch: tap the swatch (colour), the label (kind), '=' (on/off) or the
 *   expression (edit there); drag scrolls.
 * The focused line is edited in the scratch editor `sed` and stored back into
 * the entry's gtext_t after every change (an edit beyond GTOK tokens is
 * undone and "too long" shown); every change invalidates the meshes.
 *
 * WINDOW form. Fifteen numbers in two columns: x, y, z ranges (the box),
 * u, v ranges (parametric surfaces), t range (curves), grid (cells per side,
 * 4..64), tsteps (curve segments, 8..1000) and levels (contour levels, 1..30). Like the 2D WINDOW form
 * (form.c): fields accept expressions (2π), the first key typed into a field
 * replaces it, ▲▼ / ENTER commit and move (wrapping), an invalid value is
 * refused with a red message; tapping a field selects it. Leaving through a
 * soft key commits the field being edited.
 *
 * Both screens share the graph screen's first soft-key row (z= window view
 * trace graph). State: module statics (focus, scroll, form selection); the
 * values themselves are in g3 (g3d_model.c, saved there).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "g3d.h"
#include "theme.h"

/* ================================================================ entries */
#define ROW_H 34
#define HDR_H 22 /* the header: the 2D/3D switch */
#define LINES_MAX (3 * G3_NENT)

static int row;          /* focused line */
static int focus = 2;    /* 0 swatch, 1 '=', 2 expression */
static int yoff;         /* scroll (content pixels above the body) */
static editor_t sed;     /* scratch editor of the focused line */
static int sed_line = -1;
static bool too_long;

/* lines: entries in order, 1 or 3 lines each */
static int nlines(void)
{
    int n = 0;
    for (int i = 0; i < G3_NENT; i++)
        n += g3_parts(i);
    return n;
}
static void line_at(int l, int *ent, int *part)
{
    for (int i = 0; i < G3_NENT; i++) {
        if (l < g3_parts(i)) {
            *ent = i, *part = l;
            return;
        }
        l -= g3_parts(i);
    }
    *ent = G3_NENT - 1, *part = 0;
}
static int line_of(int ent)
{
    int l = 0;
    for (int i = 0; i < ent; i++)
        l += g3_parts(i);
    return l;
}

static void changed(void)
{
    g3_invalidate();
    g3d_redraw();
}

/* the editor of line l (loads the scratch editor) */
static editor_t *line_editor(int l)
{
    if (sed_line != l) {
        int i, p;
        line_at(l, &i, &p);
        gtext_to_editor(&g3.ent[i].ex[p], &sed);
        sed_line = l;
    }
    return &sed;
}

/* store the scratch editor back; false (undone, too_long) if it is too long */
static bool store_back(void)
{
    if (sed_line < 0 || sed_line >= nlines())
        return true;
    int i, p;
    line_at(sed_line, &i, &p);
    if (gtext_from_editor(&g3.ent[i].ex[p], &sed))
        return true;
    too_long = true;
    int cur = sed.cur;
    gtext_to_editor(&g3.ent[i].ex[p], &sed);
    sed.cur = cur < sed.len ? cur : sed.len;
    return false;
}

/* x of the expression: after the label (font_medium), a gap and the '=' cell */
static int expr_x(int l)
{
    int i, p;
    char lb[16];
    line_at(l, &i, &p);
    g3_part_label(i, p, lb, sizeof lb);
    return 12 + gfx_text_width(&font_medium, lb) + 4 + CW + 2;
}

static int line_y(int l) { return BODY_Y + HDR_H + l * ROW_H - yoff; }
#define SW_X (LCD_WIDTH - 50) /* "2D" of the switch ("3D" after it) */

/* the header: what the entries are, the 2D/3D switch ("3D" lit: this editor) */
static void draw_header(void)
{
    int y0 = BODY_Y - yoff;
    if (y0 + HDR_H <= BODY_Y)
        return;
    gfx_fill_rect(0, y0, LCD_WIDTH, HDR_H, TH_BG);
    gfx_text(&font_small, 4, y0 + 4, "3D: z(x,y), surfaces, curves", TH_DIM, GFX_TRANSPARENT);
    gfx_text(&font_medium, SW_X + 4, y0 + 4, "2D", TH_FG, GFX_TRANSPARENT);
    gfx_fill_rect(SW_X + 24, y0 + 2, 22, HDR_H - 5, C_SEL_BG);
    gfx_text(&font_medium, SW_X + 28, y0 + 4, "3D", C_WHITE, GFX_TRANSPARENT);
    gfx_hline(0, y0 + HDR_H - 1, LCD_WIDTH, C_LTGRAY);
}

/* Draw line l (see the file comment); the focused field is framed in orange. */
static void draw_line(int l)
{
    int i, p;
    line_at(l, &i, &p);
    int y0 = line_y(l);
    if (y0 >= UI_BOTTOM || y0 + ROW_H <= BODY_Y)
        return;
    int y = y0 + 3;
    bool foc = l == row;
    const g3_entry_t *e = &g3.ent[i];
    uint16_t col = func_colors[e->color % NCOLORS];
    gfx_fill_rect(0, y0, LCD_WIDTH, ROW_H, TH_BG);
    char lb[16];
    g3_part_label(i, p, lb, sizeof lb);
    int ex = expr_x(l), eqx = ex - CW - 2;
    if (p == 0) {
        gfx_fill_rect(2, y0 + 4, 6, ROW_H - 8, col);
        if (foc && focus == 0)
            gfx_rect(0, y0 + 2, 10, ROW_H - 4, TH_FOCUS);
    }
    gfx_text(&font_medium, 12, y + 3, lb, col, GFX_TRANSPARENT);
    if (p == 0 && e->enabled) {
        gfx_fill_rect(eqx, y, CW, CH, col);
        gfx_text(FONT, eqx, y, "=", C_WHITE, GFX_TRANSPARENT);
    } else {
        gfx_text(FONT, eqx, y, "=", p == 0 ? TH_FG : TH_DIM, GFX_TRANSPARENT);
    }
    if (foc && focus == 1)
        gfx_rect(eqx - 1, y - 1, CW + 2, CH + 2, TH_FOCUS);
    editor_t *ed;
    if (foc) {
        ed = line_editor(l);
    } else {
        gtext_to_editor(&e->ex[p], &gx_tmp_ed);
        ed = &gx_tmp_ed;
    }
    ui_draw_editor_line(ed, ex, y, (LCD_WIDTH - ex - 2) / CW, TH_FG, TH_BG, foc && focus == 2);
    const char *err = foc && too_long ? "too long (48 tokens at most)" : g3_error(i);
    if (err[0] && (p == g3_parts(i) - 1 || (foc && too_long)))
        gfx_text(&font_small, ex, y0 + ROW_H - 12, err, TH_ERR, GFX_TRANSPARENT);
    gfx_hline(0, y0 + ROW_H - 1, LCD_WIDTH, C_LTGRAY);
}

static void scroll_to_focus(void)
{
    int max = HDR_H + nlines() * ROW_H - BODY_H;
    int top = row ? HDR_H + row * ROW_H : 0; /* (the first line: the header in view too) */
    if (top < yoff)
        yoff = top;
    if (top + ROW_H > yoff + BODY_H)
        yoff = top + ROW_H - BODY_H;
    if (yoff > max)
        yoff = max;
    if (yoff < 0)
        yoff = 0;
}

static void draw_lines(void)
{
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    draw_header();
    for (int l = 0; l < nlines(); l++)
        if (l != row)
            draw_line(l);
    draw_line(row); /* last, so its cursor is on top */
    gfx_reset_clip();
}

static void eq_draw(void)
{
    ui_cursor_hide();
    if (row >= nlines())
        row = nlines() - 1;
    if (row < 0)
        row = 0;
    scroll_to_focus();
    draw_lines();
}

/* focus line l: store the line being left, cursor at the end of the new one;
 * lines other than an entry's first have no swatch / '=' */
static void move_to(int l)
{
    store_back();
    row = l;
    int i, p;
    line_at(row, &i, &p);
    if (p > 0 && focus < 2)
        focus = 2;
    ed_end(line_editor(row));
}

/* SHIFT F1: next kind for the focused entry; focus its first line */
static void cycle_kind(void)
{
    int i, p;
    store_back();
    line_at(row, &i, &p);
    g3.ent[i].kind = (uint8_t)((g3.ent[i].kind + 1) % G3K_N);
    sed_line = -1;
    row = line_of(i);
    changed();
}

static void eq_key(int key, int mod)
{
    too_long = false;
    int i, p;
    line_at(row, &i, &p);
    g3_entry_t *en = &g3.ent[i];
    editor_t *e = line_editor(row);
    switch (key) {
    case KEY_F1:
    case KEY_F2:
    case KEY_F3:
        if (mod != MOD_SHIFT)
            return;
        if (key == KEY_F1)
            cycle_kind();
        else if (key == KEY_F2)
            en->enabled = !en->enabled;
        else
            en->color = (uint8_t)((en->color + 1) % NCOLORS);
        changed();
        break;
    case KEY_UP:
        if (row > 0)
            move_to(row - 1);
        break;
    case KEY_DOWN:
        if (row < nlines() - 1)
            move_to(row + 1);
        break;
    case KEY_LEFT:
        if (focus == 2 && e->cur == 0 && mod == MOD_NONE && p == 0)
            focus = 1;
        else if (focus == 1)
            focus = 0;
        else if (focus == 2)
            ui_editor_key(e, key, mod);
        break;
    case KEY_RIGHT:
        if (focus < 2)
            focus++;
        else
            ui_editor_key(e, key, mod);
        break;
    case KEY_ENTER:
        if (focus == 0)
            en->color = (uint8_t)((en->color + 1) % NCOLORS), changed();
        else if (focus == 1)
            en->enabled = !en->enabled, changed();
        else if (row < nlines() - 1)
            move_to(row + 1);
        break;
    case KEY_CLEAR:
        ed_clear(e);
        focus = 2;
        store_back();
        changed();
        break;
    case KEY_X:
        if (mod == MOD_NONE && en->kind != G3K_FUNC) {
            focus = 2;
            ed_insert_text(e, en->kind == G3K_PSURF ? "u" : "t", NULL);
            store_back();
            changed();
            break;
        }
        /* fall through - x for z = f(x,y) (and SHIFT/ALPHA X) */
    default:
        focus = 2;
        if (!ui_editor_key(e, key, mod))
            return;
        store_back();
        changed();
    }
    eq_draw();
}

/* menus insert into the focused expression */
static editor_t *eq_editor(void)
{
    focus = 2;
    return line_editor(row);
}

/* Fresh visit: reload the scratch editor. Always invalidate: a menu may have
 * inserted text. */
static void eq_enter(void)
{
    store_back();
    if (!ui_returning)
        sed_line = -1;
    changed();
}

static void eq_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        ui_drag_scroll(ev, UI_DRAG_Y, 1, &yoff, HDR_H + nlines() * ROW_H - BODY_H); /* pixel scroll */
        ui_cursor_hide();
        draw_lines();
        return;
    }
    if (ev->type != TEV_TAP || ev->y < BODY_Y)
        return;
    int cy = ev->y - BODY_Y + yoff;
    if (cy < HDR_H) { /* the header: "2D" switches to the 2D entries */
        if (ev->x >= SW_X - 4 && ev->x < SW_X + 22) {
            store_back();
            graph_editor_switch(false);
        }
        return;
    }
    int l = (cy - HDR_H) / ROW_H;
    if (l >= nlines())
        return;
    move_to(l);
    int i, p;
    line_at(l, &i, &p);
    int ex = expr_x(l), eqx = ex - CW - 2;
    if (p == 0 && ev->x < 11) {
        focus = 0;
        g3.ent[i].color = (uint8_t)((g3.ent[i].color + 1) % NCOLORS);
        changed();
    } else if (ev->x < eqx - 1) {
        cycle_kind();
        focus = 2;
    } else if (p == 0 && ev->x < ex) {
        focus = 1;
        g3.ent[i].enabled = !g3.ent[i].enabled;
        changed();
    } else {
        focus = 2;
        editor_t *e = line_editor(l);
        ui_editor_tap(e, e->scroll + (ev->x - ex) / CW);
    }
    eq_draw();
}

static void win_state(void (*kv)(const char *, const char *, void *), void *user);

/* `state` key g3d_row (focused line), and the WINDOW form's keys */
void g3d_eq_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16];
    snprintf(b, sizeof b, "%d", row);
    kv("g3d_row", b, user);
    win_state(kv, user);
}

static void nav_trace(void) { g3d_trace_key(); }
static void eq_sk_graph(void)
{
    store_back();
    if (!home_graph_embed(true)) /* (graphing from the home screen: into its notebook) */
        ui_switch(&scr_g3d);
}
static void eq_sk_2d(void)
{
    store_back();
    graph_editor_switch(false);
}
static const softkeys_t eq_soft = {
    {{"2D", eq_sk_2d}, {"window", NULL, &scr_g3d_win}, {"view", g3d_view_menu}, {"trace", nav_trace},
     {"graph", eq_sk_graph}},

    {{"type"}, {"on/off"}, {"color"}, {NULL}, {NULL}},
    NULL,
};
const screen_t scr_g3d_eq = {"graph3d_eq", "3D Z=", eq_enter, eq_draw, eq_key, NULL, eq_editor, eq_touch,
                             "graph3d-eq", &eq_soft};

/* ================================================================ WINDOW */
typedef struct {
    const char *label;
    double *val;
} wfield_t;
static const wfield_t wf[] = {
    {"xmin=", &g3.xmin}, {"xmax=", &g3.xmax}, {"ymin=", &g3.ymin},   {"ymax=", &g3.ymax},
    {"zmin=", &g3.zmin}, {"zmax=", &g3.zmax}, {"umin=", &g3.umin},   {"umax=", &g3.umax},
    {"vmin=", &g3.vmin}, {"vmax=", &g3.vmax}, {"tmin=", &g3.tmin},   {"tmax=", &g3.tmax},
    {"grid=", &g3.grid}, {"tsteps=", &g3.tsteps}, {"levels=", &g3.levels},
};
#define NWF ((int)(sizeof wf / sizeof wf[0]))
#define WPITCH 23
#define COLW (LCD_WIDTH / 2)

static int wsel;
static editor_t wed;
static bool wfresh;
static char werr[48];

/* the selected field's value into the editor; the next typed key replaces it */
static void wload(void)
{
    char b[32], d[40];
    fmt_num(*wf[wsel].val, 11, b, sizeof b);
    if (b[0] == '-')
        snprintf(d, sizeof d, "⁻%s", b + 1);
    else
        snprintf(d, sizeof d, "%s", b);
    ed_set_text(&wed, d);
    ed_home(&wed);
    wfresh = true;
}

/* All ranges non-empty, grid and tsteps within their limits (they are stored
 * rounded). NULL if valid, else what is wrong ("xmin must be less than xmax";
 * the names come from the field labels without their '='). */
static const char *wproblem(void)
{
    static char b[40];
    for (int k = 0; k < 12; k += 2)
        if (!(*wf[k].val < *wf[k + 1].val)) {
            snprintf(b, sizeof b, "%.4s must be less than %.4s", wf[k].label, wf[k + 1].label);
            return b;
        }
    if (!(g3.grid >= G3_GRID_MIN && g3.grid <= G3_GRID_MAX))
        snprintf(b, sizeof b, "grid must be %d to %d", G3_GRID_MIN, G3_GRID_MAX);
    else if (!(g3.tsteps >= G3_TSTEPS_MIN && g3.tsteps <= G3_TSTEPS_MAX))
        snprintf(b, sizeof b, "tsteps must be %d to %d", G3_TSTEPS_MIN, G3_TSTEPS_MAX);
    else if (!(g3.levels >= G3_LEVELS_MIN && g3.levels <= G3_LEVELS_MAX))
        snprintf(b, sizeof b, "levels must be %d to %d", G3_LEVELS_MIN, G3_LEVELS_MAX);
    else
        return NULL;
    return b;
}

/* Commit the edited field (false with werr set if the value is refused). */
static bool wcommit(void)
{
    char cas[ED_MAX * 4], err[48];
    ed_cas(&wed, cas, sizeof cas);
    double v, old = *wf[wsel].val;
    if (eval_number(cas, &v, err, sizeof err)) {
        snprintf(werr, sizeof werr, "ERR: %.40s", err);
        return false;
    }
    if (wf[wsel].val == &g3.grid || wf[wsel].val == &g3.tsteps || wf[wsel].val == &g3.levels)
        v = round(v);
    *wf[wsel].val = v;
    const char *bad = wproblem();
    if (bad) {
        *wf[wsel].val = old;
        snprintf(werr, sizeof werr, "ERR: %s", bad);
        return false;
    }
    werr[0] = 0;
    if (v != old) {
        if (wf[wsel].val != &g3.levels) /* (the levels only change the drawing) */
            g3_invalidate();
        g3d_redraw();
    }
    return true;
}

static int wx(int k) { return (k % 2) * COLW; }
static int wy(int k) { return BODY_Y + 4 + (k / 2) * WPITCH; }

static void win_draw(void)
{
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    for (int k = 0; k < NWF; k++) {
        int x = wx(k) + 4, y = wy(k);
        gfx_text(&font_medium, x, y + 3, wf[k].label, TH_LABEL, GFX_TRANSPARENT);
        int lx = x + gfx_text_width(&font_medium, wf[k].label) + 2, cols = (wx(k) + COLW - 2 - lx) / CW;
        if (k == wsel) {
            ui_draw_editor_line(&wed, lx, y, cols, TH_FG, TH_BG, true);
        } else {
            char b[32];
            fmt_num(*wf[k].val, cols, b, sizeof b);
            gfx_text(FONT, lx, y, b, TH_FG, GFX_TRANSPARENT);
        }
    }
    if (werr[0])
        gfx_text(&font_medium, 8, UI_BOTTOM - 16, werr, TH_ERR, GFX_TRANSPARENT);
}

static void win_key(int key, int mod)
{
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
    case KEY_ENTER:
        if (!wcommit())
            break;
        wsel = key == KEY_UP ? (wsel + NWF - 1) % NWF : (wsel + 1) % NWF;
        wload();
        break;
    case KEY_CLEAR:
        ed_clear(&wed);
        wfresh = false;
        break;
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_DEL:
        wfresh = false;
        ui_editor_key(&wed, key, mod);
        break;
    default: {
        char t[16];
        if (!ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t))
            return;
        if (wfresh) {
            ed_clear(&wed);
            wfresh = false;
        }
        ed_insert_text(&wed, t, NULL);
    }
    }
    win_draw();
}

static void win_touch(const touch_event_t *ev)
{
    if (ev->type != TEV_TAP)
        return;
    for (int k = 0; k < NWF; k++) {
        if (ev->x < wx(k) || ev->x >= wx(k) + COLW || ev->y < wy(k) - 2 || ev->y >= wy(k) + WPITCH - 2)
            continue;
        if (k != wsel && wcommit()) {
            wsel = k;
            wload();
        }
        win_draw();
        return;
    }
}

static editor_t *win_editor(void)
{
    wfresh = false;
    return &wed;
}

static void win_enter(void)
{
    if (ui_returning)
        return;
    werr[0] = 0;
    wload();
}

/* soft keys leaving the form commit the field first (a refused value stays) */
static void win_leave(const screen_t *to)
{
    if (wcommit())
        ui_switch(to);
    else
        win_draw();
}
static void win_sk_eq(void) { win_leave(&scr_g3d_eq); }
static void win_sk_graph(void)
{
    if (!wcommit())
        win_draw();
    else if (!home_graph_embed(true))
        ui_switch(&scr_g3d);
}
static void win_sk_trace(void)
{
    if (wcommit())
        g3d_trace_key();
    else
        win_draw();
}
static void win_sk_view(void)
{
    if (wcommit())
        g3d_view_menu();
    else
        win_draw();
}

/* `state` keys: g3d_win_sel (selected field label), g3d_win_err */
static void win_state(void (*kv)(const char *, const char *, void *), void *user)
{
    kv("g3d_win_sel", wf[wsel].label, user);
    kv("g3d_win_err", werr, user);
}

static const softkeys_t win_soft = {
    {{"z=", win_sk_eq}, {"window", NULL, &scr_g3d_win}, {"view", win_sk_view}, {"trace", win_sk_trace},
     {"graph", win_sk_graph}},
    {{NULL}},
    NULL,
};
const screen_t scr_g3d_win = {"graph3d_win", "3D WINDOW", win_enter, win_draw, win_key, NULL, win_editor,
                              win_touch, "graph3d-window", &win_soft};
