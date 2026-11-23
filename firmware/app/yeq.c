/* Y= editor for the current graph mode: FUNC Y1..Y6, PAR X1T/Y1T.., POL r1..,
 * SEQ u1(n) with initial values u1(nMin), IMPL relations I1.. in x and y,
 * DE y1'=f(x, y) with initial points IC1.
 * Each entry has a colour swatch, a line style and an enable toggle ('=').
 * A header line above the first entry ({▲} from it) selects the graph mode
 * and, in SEQ mode, the time/web plot; at its right the 2D/3D switch of the
 * graph editor ("2D" lit; a tap on "3D", or F1 "3D", opens the 3D entries, scr_g3d_eq). */
/*
 * Screen scr_yeq (soft keys: the shared graph bar softkeys_graph, copied by
 * yeq_softkeys() to add the MENU key's "Y= OPTIONS" menu and F1 "3D"; help
 * topic "yeq"). Opened with F1 (graph) from home and y= on the graph screens.
 *
 * Layout. The content is a virtual column scrolled by yoff: the header
 * (HDR_H px: the mode names FUNC PAR POL SEQ IMPL DE, plus TIME WEB in SEQ mode,
 * the active ones highlighted) and then one ROW_H-high line per entry part.
 * PAR, SEQ and DE have two lines per entry (X1T/Y1T, u1(n)/u1(nMin), y1'/IC1), the other
 * modes one, so the list is 6 or 12 lines and scrolls. yoff starts at 0: the
 * header is always in view on arrival (the six FUNC lines fit under it), so
 * the graph modes can be found without knowing that UP leads there. A line,
 * left to right: colour swatch (x 2..8), the label with
 * the line-style glyph under it, the '=' toggle (filled with the colour when
 * the entry is enabled; grey on second lines, which have no toggle), the
 * expression editor and, under the last line of an entry, its compile error.
 *
 * Keys (a small state machine on `row` and `focus`):
 *   row -1 (header): LEFT/RIGHT choose an item, ENTER activates it (switches
 *       the mode and focuses its first entry / switches the SEQ plot type),
 *       DOWN goes to the first entry; a text key or ⌫ goes to the first
 *       entry and edits it.
 *   MENU (any row): on/off, colour, style and clearing of the focused entry,
 *       and the graph modes (ym_select).
 *   row >= 0: UP/DOWN move between lines (UP from line 0 enters the header);
 *       in the expression (focus 2), LEFT at column 0 of a first line moves
 *       focus to '=' (1), then the swatch (0), then the style (-1); RIGHT moves
 *       back. ENTER on style/swatch/'=' cycles the style / cycles the colour /
 *       toggles the entry; on the expression it moves to the next line.
 *       CLEAR empties the line; the X key types the mode's variable (t, θ, n);
 *       any other key edits the expression (focus returns there).
 * Every change invalidates the compiled functions (funcs_invalidate), so the
 * graph and table recompute on their next visit.
 *
 * Editing storage. FUNC lines edit g.f[i].ed directly. The other modes keep
 * their entries as compact gtext_t token strings (graph.h), so the focused
 * line is loaded into the scratch editor `sed` and written back after every
 * change (store_back); an edit that would exceed GTOK tokens is undone and
 * "too long" is shown under the line until the next key.
 * State: the static variables below (not saved; the entries themselves are
 * saved by persist_graph / persist_graphx).
 */
#include <stdio.h>
#include <string.h>
#include "g3d.h"
#include "graph.h"
#include "nbdoc.h"

#define HDR_H 22                       /* mode header height */
#define ROW_H ((BODY_H - HDR_H) / NFUNC) /* the header and six lines fill the body */
#define LINES_MAX (2 * GNENT)

static int row;           /* focused line, -1 = header */
static int focus = 2;     /* -1 style, 0 colour swatch, 1 '=' toggle, 2 expression */
static int yoff;          /* vertical scroll (content pixels above the body) */
static int hsel;          /* header item */
static char yeq_title[16] = "Y=";  /* status-bar title ("Y= PAR" ...), the screen_t points at it */

/* scratch editor for PAR/POL/SEQ/IMPL lines (FUNC edits g.f[i].ed directly) */
static editor_t sed;
static int sed_line = -1, sed_mode = -1;
static bool too_long; /* the last edit was undone: more than GTOK tokens (shown until the next key) */

/* lines per entry, number of lines, and line <-> (entry, part) mapping */
static int parts(void) { return gx.mode == GM_PAR || gx.mode == GM_SEQ || gx.mode == GM_DE ? 2 : 1; }
static int nlines(void) { return GNENT * parts(); }
static int line_entry(int l) { return l / parts(); }
static int line_part(int l) { return l % parts(); }

static void set_title(void)
{
    snprintf(yeq_title, sizeof yeq_title, gx.mode == GM_FUNC ? "Y=" : "Y= %s", gmode_names[gx.mode]);
}

/* ------------------------------------------------------------ entry fields */
/* enable flag, colour and style of entry i in the current mode (FUNC: g.f) */
static bool en_enabled(int i) { return gx_enabled(i); }
static void en_toggle(int i)
{
    if (gx.mode == GM_FUNC)
        g.f[i].enabled = !g.f[i].enabled;
    else
        gx_entry(i)->enabled = !gx_entry(i)->enabled;
}
static void en_cycle_color(int i)
{
    if (gx.mode == GM_FUNC)
        g.f[i].color = (uint8_t)((g.f[i].color + 1) % NCOLORS);
    else
        gx_entry(i)->color = (uint8_t)((gx_entry(i)->color + 1) % NCOLORS);
}
static void en_cycle_style(int i)
{
    int s = gx_style(i) + 1;
    /* shading is for functions of x only */
    if (gx.mode != GM_FUNC && (s == GS_ABOVE || s == GS_BELOW))
        s = GS_LINE;
    gx_set_style(i, s % GS_NSTYLES);
}

/* the editor of line l (loads the scratch editor for the other modes) */
static editor_t *line_editor(int l)
{
    int i = line_entry(l);
    if (gx.mode == GM_FUNC)
        return &g.f[i].ed;
    if (sed_line != l || sed_mode != gx.mode) {
        gtext_to_editor(&gx_entry(i)->ex[line_part(l)], &sed);
        sed_line = l, sed_mode = gx.mode;
    }
    return &sed;
}

/* store the scratch editor back; false (reload, too_long set) if it is too long */
static bool store_back(void)
{
    if (gx.mode == GM_FUNC || sed_line < 0 || sed_mode != gx.mode)
        return true;
    gtext_t *t = &gx_entry(line_entry(sed_line))->ex[line_part(sed_line)];
    if (gtext_from_editor(t, &sed))
        return true;
    too_long = true;
    int cur = sed.cur;
    gtext_to_editor(t, &sed);
    sed.cur = cur < sed.len ? cur : sed.len;
    return false;
}

/* x of the expression column of line l: fixed in FUNC (labels Y1..Y6 in the
 * large font), else after the label in font_medium, a gap and the '=' cell
 * (the '=' sits at expr_x - CW - 2). */
static int expr_x(int l)
{
    if (gx.mode == GM_FUNC)
        return 44;
    char lb[16];
    gx_entry_label(line_entry(l), line_part(l), lb, sizeof lb);
    return 12 + gfx_text_width(&font_medium, lb) + 4 + CW + 2;
}

static void yeq_enter(void);

/* ------------------------------------------------------------ drawing */
static int line_y(int l) { return BODY_Y + HDR_H + l * ROW_H - yoff; } /* screen y of line l */

/* 18 px wide sample of a line style (under the label): bar, thick bar, thin
 * line, dots, or a bar with hatching above / below for the shading styles */
static void style_glyph(int x, int y, int style, uint16_t c)
{
    switch (style) {
    case GS_THICK: gfx_fill_rect(x, y, 18, 4, c); break;
    case GS_THIN: gfx_hline(x, y + 1, 18, c); break;
    case GS_DOT:
        for (int k = 0; k < 18; k += 4)
            gfx_fill_rect(x + k, y + 1, 2, 2, c);
        break;
    case GS_ABOVE:
        gfx_fill_rect(x, y + 1, 18, 2, c);
        for (int k = 0; k < 18; k += 3)
            gfx_vline(x + k, y - 3, 3, c);
        break;
    case GS_BELOW:
        gfx_fill_rect(x, y + 1, 18, 2, c);
        for (int k = 0; k < 18; k += 3)
            gfx_vline(x + k, y + 3, 3, c);
        break;
    default: gfx_fill_rect(x, y + 1, 18, 2, c); break;
    }
}

/* Draw line l (skipped when scrolled out of the body). The focused field gets
 * an orange frame; the focused line's editor shows the cursor when the
 * expression has the focus. Unfocused non-FUNC lines are rendered through
 * the scratch editor gx_tmp_ed. */
static void draw_line(int l)
{
    int i = line_entry(l), part = line_part(l);
    int y0 = line_y(l);
    if (y0 >= UI_BOTTOM || y0 + ROW_H <= BODY_Y)
        return;
    int y = y0 + (ROW_H - CH) / 2;
    bool foc = l == row;
    gfx_fill_rect(0, y0, LCD_WIDTH, ROW_H, C_WHITE);
    uint16_t col = func_colors[gx_color(i)];
    char lbl[16];
    gx_entry_label(i, part, lbl, sizeof lbl);
    int ex = expr_x(l), eqx = ex - CW - 2;
    if (part == 0) {
        gfx_fill_rect(2, y0 + 4, 6, ROW_H - 8, col);
        if (foc && focus == 0)
            gfx_rect(0, y0 + 2, 10, ROW_H - 4, C_ORANGE);
        style_glyph(12, y0 + ROW_H - 8, gx_style(i), col);
        if (foc && focus == -1)
            gfx_rect(10, y0 + 2, eqx - 11, ROW_H - 4, C_ORANGE);
    }
    if (gx.mode == GM_FUNC)
        gfx_text(FONT, 12, y, lbl, col, GFX_TRANSPARENT);
    else
        gfx_text(&font_medium, 12, y + 3, lbl, col, GFX_TRANSPARENT);
    if (part == 0 && en_enabled(i)) {
        gfx_fill_rect(eqx, y, CW, CH, col);
        gfx_text(FONT, eqx, y, "=", C_WHITE, GFX_TRANSPARENT);
    } else {
        gfx_text(FONT, eqx, y, "=", part == 0 ? C_BLACK : C_GRAY, GFX_TRANSPARENT);
    }
    if (foc && focus == 1)
        gfx_rect(eqx - 1, y - 1, CW + 2, CH + 2, C_ORANGE);
    editor_t *e;
    if (gx.mode == GM_FUNC)
        e = &g.f[i].ed;
    else if (foc)
        e = line_editor(l);
    else {
        gtext_to_editor(&gx_entry(i)->ex[part], &gx_tmp_ed);
        e = &gx_tmp_ed;
    }
    ui_draw_editor_line(e, ex, y, (LCD_WIDTH - ex - 2) / CW, C_BLACK, C_WHITE, foc && focus == 2);
    const char *err = foc && too_long ? "too long (48 tokens at most)" : gx_error(i);
    if (err[0] && (part == parts() - 1 || (foc && too_long)))
        gfx_text(&font_small, ex, y0 + ROW_H - 12, err, C_RED, GFX_TRANSPARENT);
    gfx_hline(0, y0 + ROW_H - 1, LCD_WIDTH, C_LTGRAY);
}

/* Header items: 0..GM_NMODES-1 are the graph modes; in SEQ mode two more,
 * TIME and WEB (gx.seqplot). hdr_x gives each item's x (10 px apart), with an
 * extra 8 px gap between the modes and the plot types (all of it left of the
 * 2D/3D switch). */
static int hdr_items(void) { return GM_NMODES + (gx.mode == GM_SEQ ? 2 : 0); }
static const char *hdr_label(int k) { return k < GM_NMODES ? gmode_names[k] : k == GM_NMODES ? "TIME" : "WEB"; }
static int hdr_x(int k)
{
    int x = 4;
    for (int j = 0; j < k; j++)
        x += gfx_text_width(&font_medium, hdr_label(j)) + 10 + (j == GM_NMODES - 1 ? 8 : 0);
    return x;
}
static bool hdr_on(int k) { return k < GM_NMODES ? gx.mode == k : gx.seqplot == k - GM_NMODES; }
/* the 2D/3D switch at the right edge of the header: "2D" lit (this editor), "3D" taps to the 3D one
 * (the keys: F1 "3D") */
#define HDR_3D_X (LCD_WIDTH - 22)

/* header row: active items highlighted, the keyboard-selected one framed */
static void draw_header(void)
{
    int y0 = BODY_Y - yoff;
    if (y0 + HDR_H <= BODY_Y)
        return;
    gfx_fill_rect(0, y0, LCD_WIDTH, HDR_H, C_WHITE);
    for (int k = 0; k < hdr_items(); k++) {
        int x = hdr_x(k), w = gfx_text_width(&font_medium, hdr_label(k)) + 8;
        if (hdr_on(k))
            gfx_fill_rect(x - 4, y0 + 2, w, HDR_H - 5, C_SEL_BG);
        if (row < 0 && hsel == k)
            gfx_rect(x - 5, y0 + 1, w + 2, HDR_H - 3, C_ORANGE);
        gfx_text(&font_medium, x, y0 + 4, hdr_label(k), hdr_on(k) ? C_WHITE : C_BLACK, GFX_TRANSPARENT);
    }
    gfx_fill_rect(HDR_3D_X - 28, y0 + 2, 22, HDR_H - 5, C_SEL_BG); /* "2D": the editor shown */
    gfx_text(&font_medium, HDR_3D_X - 24, y0 + 4, "2D", C_WHITE, GFX_TRANSPARENT);
    gfx_text(&font_medium, HDR_3D_X, y0 + 4, "3D", C_BLACK, GFX_TRANSPARENT);
    gfx_hline(0, y0 + HDR_H - 1, LCD_WIDTH, C_GRAY);
}

/* Adjust yoff so the focused line (or the header) is fully visible, within
 * [0, content height - body height]. */
static void scroll_to_focus(void)
{
    int max = HDR_H + nlines() * ROW_H - BODY_H;
    if (row < 0) {
        yoff = 0;
        return;
    }
    int top = HDR_H + row * ROW_H;
    if (top < yoff)
        yoff = top;
    if (top + ROW_H > yoff + BODY_H)
        yoff = top + ROW_H - BODY_H;
    if (yoff > max)
        yoff = max;
    if (yoff < 0)
        yoff = 0;
}

/* Full redraw of the body (screen draw callback and after every key). */
static void yeq_draw(void)
{
    ui_cursor_hide();
    set_title();
    if (row >= nlines())
        row = nlines() - 1;
    scroll_to_focus();
    gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
    draw_header();
    for (int l = 0; l < nlines(); l++)
        if (l != row)
            draw_line(l);
    /* the focused line last so the cursor is painted on top */
    if (row >= 0)
        draw_line(row);
    gfx_reset_clip();
}

static void changed(void) { funcs_invalidate(); }

/* Focus line l (-1 = header): save the line being left, keep the focus on
 * the expression for second lines (they have no swatch/'='/style), cursor at
 * the end of the new line. */
static void move_to(int l)
{
    store_back();
    row = l;
    if (row >= 0) {
        if (line_part(row) == 1 && focus < 2)
            focus = 2;
        ed_end(line_editor(row));
    }
}

/* Activate header item k: switch the graph mode (recompiles, puts the focus
 * on the new mode's first entry, ready for typing) or the SEQ plot type
 * (re-plot; the focus stays on the header). */
static void header_activate(int k)
{
    store_back();
    if (k < GM_NMODES) {
        gx_set_mode(k);
        sed_line = -1;
        yeq_enter(); /* resets the row for the new mode */
        focus = 2;
        move_to(0);
    } else {
        gx.seqplot = (uint8_t)(k - GM_NMODES);
        g.graph_valid = false;
    }
    set_title();
    ui_draw_status();
}

static void yeq_key(int key, int mod);

/* Keys while the header has the focus (row == -1). A key that edits text (or
 * ⌫) moves to the first entry and is handled there, so typing right after
 * choosing a mode is never lost. */
static void header_key(int key, int mod)
{
    switch (key) {
    case KEY_LEFT:
        if (hsel > 0)
            hsel--;
        break;
    case KEY_RIGHT:
        if (hsel < hdr_items() - 1)
            hsel++;
        break;
    case KEY_ENTER:
        header_activate(hsel);
        break;
    case KEY_DOWN:
        move_to(0);
        break;
    case KEY_CLEAR:
        ui_switch(ui_home());
        return;
    default: {
        char t[16];
        if (key != KEY_DEL && (KEY_IS_SOFT(key) || !ui_key_text(key, mod, g.alpha_snapshot_for_key, t, sizeof t)))
            return;
        focus = 2;
        move_to(0);
        yeq_key(key, mod);
        return;
    }
    }
    yeq_draw();
}

/* Screen key handler (after the global keys and soft keys): see the key
 * summary at the top of the file. Keys the editor does not handle return
 * without a redraw. */
static void yeq_key(int key, int mod)
{
    too_long = false;
    if (row < 0) {
        header_key(key, mod);
        return;
    }
    int i = line_entry(row);
    editor_t *e = line_editor(row);
    switch (key) {
    case KEY_UP:
        if (row > 0) {
            move_to(row - 1);
        } else {
            store_back();
            row = -1;
            hsel = gx.mode;
        }
        break;
    case KEY_DOWN:
        if (row < nlines() - 1)
            move_to(row + 1);
        break;
    case KEY_LEFT:
        if (focus == 2 && e->cur == 0 && mod == MOD_NONE && line_part(row) == 0)
            focus = 1;
        else if (focus == 1)
            focus = 0;
        else if (focus == 0)
            focus = -1;
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
        if (focus == -1) {
            en_cycle_style(i);
            changed();
        } else if (focus == 0) {
            en_cycle_color(i);
            changed();
        } else if (focus == 1) {
            en_toggle(i);
            changed();
        } else if (row < nlines() - 1) {
            move_to(row + 1);
        }
        break;
    case KEY_CLEAR:
        ed_clear(e);
        focus = 2;
        store_back();
        changed();
        break;
    case KEY_X:
        if (mod == MOD_NONE && gx.mode != GM_FUNC && gx.mode != GM_IMPL && gx.mode != GM_DE) {
            /* X,T,θ,n types the mode's variable */
            focus = 2;
            ed_insert_text(e, gx.mode == GM_PAR ? "t" : gx.mode == GM_POL ? "θ" : "n", NULL);
            store_back();
            changed();
            break;
        }
        /* fall through - FUNC/IMPL/DE (and SHIFT/ALPHA X) are handled by the editor */
    default:
        if (focus != 2)
            focus = 2;
        if (!ui_editor_key(e, key, mod))
            return;
        store_back();
        changed();
    }
    yeq_draw();
}

/* Where menus (MATH, CAS, CATALOG, VARS) insert: the focused line's editor
 * (the first entry if the header is focused). The scratch editor's text is
 * written back by store_back() in yeq_enter() when the menu returns. */
static editor_t *yeq_editor(void)
{
    focus = 2;
    if (row < 0)
        move_to(0);
    return line_editor(row);
}

/* entering also covers edits made through menus (CAS/MATH/CATALOG inserts) */
/* On a fresh visit (not returning from a menu) the scratch editor is reloaded
 * from the stored entry; when the graph mode changed since the last visit the
 * focus moves to the first entry's expression. Always invalidates the
 * compiled functions, since a menu may have inserted text. */
static void yeq_enter(void)
{
    static int last_mode = -1;
    store_back();
    if (!ui_returning)
        sed_line = -1;
    if (gx.mode != last_mode) { /* another graph mode: start at its first entry */
        last_mode = gx.mode;
        if (row >= 0)
            row = 0;
        focus = 2;
        yoff = 0;
    }
    set_title();
    funcs_invalidate();
}

/* Touch: tap the header to choose the mode; tap a row's swatch to cycle its
 * colour, its label to cycle the line style, its '=' to toggle it, or its
 * expression to edit it (cursor at the tapped column); drag to scroll. */
static void yeq_touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DOWN)
        return;
    if (ev->type == TEV_DRAG) {
        ui_drag_scroll(ev, UI_DRAG_Y, 1, &yoff, HDR_H + nlines() * ROW_H - BODY_H); /* pixel scroll */
        ui_cursor_hide();
        /* draw without yeq_draw's scroll_to_focus, which would scroll back to the focus line */
        gfx_set_clip(0, BODY_Y, LCD_WIDTH, BODY_H);
        gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, C_WHITE);
        draw_header();
        for (int l = 0; l < nlines(); l++)
            draw_line(l);
        gfx_reset_clip();
        return;
    }
    if (ev->type != TEV_TAP || ev->y < BODY_Y)
        return;
    int cy = ev->y - BODY_Y + yoff; /* content y */
    if (cy < HDR_H && ev->x >= HDR_3D_X - 30) { /* the 2D/3D switch */
        if (ev->x >= HDR_3D_X - 5) {
            store_back();
            graph_editor_switch(true);
        }
        return;
    }
    if (cy < HDR_H) { /* header: the rightmost item starting left of the tap */
        for (int k = hdr_items() - 1; k >= 0; k--)
            if (ev->x >= hdr_x(k) - 5) {
                hsel = k;
                store_back();
                row = -1;
                header_activate(k);
                break;
            }
        yeq_draw();
        return;
    }
    int l = (cy - HDR_H) / ROW_H;
    if (l >= nlines())
        return;
    move_to(l);
    int i = line_entry(l), ex = expr_x(l), eqx = ex - CW - 2;
    editor_t *e = line_editor(l);
    /* hit zones of a first line: swatch | label/style | '=' | expression; the
     * swatch and the 10 px '=' get a few pixels of slack for a fingertip */
    if (line_part(l) == 0 && ev->x < 12) {
        focus = 0;
        en_cycle_color(i);
        changed();
    } else if (line_part(l) == 0 && ev->x < eqx - 4) {
        focus = -1;
        en_cycle_style(i);
        changed();
    } else if (line_part(l) == 0 && ev->x < ex + 3) {
        focus = 1;
        en_toggle(i);
        changed();
    } else {
        focus = 2;
        ui_editor_tap(e, e->scroll + (ev->x - ex) / CW);
    }
    yeq_draw();
}

/* Console `state` keys for UI tests: yeq_mode, yeq_row, yN_style, and for the
 * non-FUNC modes the entry texts eN_P and enable flags eN_on. */
void yeq_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[ED_MAX * 4], k[24];
    kv("yeq_mode", gmode_names[gx.mode], user);
    snprintf(b, sizeof b, "%d", row);
    kv("yeq_row", b, user);
    for (int i = 0; i < GNENT; i++) {
        snprintf(k, sizeof k, "y%d_style", i + 1);
        kv(k, gstyle_names[gx_style(i)], user);
        if (gx.mode == GM_FUNC)
            continue;
        for (int p = 0; p < parts(); p++) {
            gtext_to_editor(&gx_entry(i)->ex[p], &gx_tmp_ed);
            ed_display(&gx_tmp_ed, b, sizeof b);
            snprintf(k, sizeof k, "e%d_%d", i + 1, p + 1);
            kv(k, b, user);
        }
        snprintf(k, sizeof k, "e%d_on", i + 1);
        kv(k, gx_entry(i)->enabled ? "1" : "0", user);
    }
}

/* ------------------------------------------------------------ MENU */
/* The MENU key's context menu on Y=: what the focused entry's '=', swatch and
 * style mark do (otherwise only reachable with {◀} or a tap), clearing it,
 * and the graph modes of the header; then Help; in DE mode also removing the
 * solution curves added on the graph. Item actions: */
enum { YM_TOGGLE, YM_COLOR, YM_STYLE, YM_CLEAR, YM_MODE, YM_HELP = YM_MODE + GM_NMODES, YM_TAPS, YM_N };

static int ym_count(void) { return gx.mode == GM_DE ? YM_N : YM_N - 1; }

/* Item i; the label names the focused entry (Y1, X1T, r1 ...). */
static void ym_get(int i, menu_item_t *out)
{
    static char b[48];
    static const char *const mode_text[GM_NMODES] = {
        "functions y(x)", "parametric x(t), y(t)", "polar r(θ)", "sequences u(n)", "relations in x and y",
        "slope fields y'=f(x,y)",
    };
    char lb[16];
    gx_entry_label(line_entry(row < 0 ? 0 : row), 0, lb, sizeof lb);
    const char *help = "yeq";
    switch (i) {
    case YM_TOGGLE: snprintf(b, sizeof b, "%s on/off", lb); break;
    case YM_COLOR: snprintf(b, sizeof b, "%s next colour", lb); break;
    case YM_STYLE: snprintf(b, sizeof b, "%s next line style", lb), help = "graph-styles"; break;
    case YM_CLEAR: snprintf(b, sizeof b, "clear %s", lb); break;
    case YM_HELP: snprintf(b, sizeof b, "Help"); break;
    case YM_TAPS: snprintf(b, sizeof b, "remove the curves added on the graph"), help = "graph-de"; break;
    default:
        snprintf(b, sizeof b, "%-5s %s%s", gmode_names[i - YM_MODE], mode_text[i - YM_MODE],
                 gx.mode == i - YM_MODE ? " ◀" : "");
        help = "graph-modes";
    }
    *out = (menu_item_t){b, NULL, NULL, i, help};
}

/* Run a context-menu item on the focused entry (the first one when the header
 * has the focus), after closing the menu, and redraw. */
static void ym_select(const menu_item_t *it)
{
    menu_close();
    if (it->action == YM_HELP) {
        help_context();
        return;
    }
    if (row < 0)
        move_to(0);
    int i = line_entry(row);
    switch (it->action) {
    case YM_TOGGLE: en_toggle(i), focus = line_part(row) ? 2 : 1; break;
    case YM_COLOR: en_cycle_color(i), focus = line_part(row) ? 2 : 0; break;
    case YM_STYLE: en_cycle_style(i), focus = line_part(row) ? 2 : -1; break;
    case YM_CLEAR:
        focus = 2;
        ed_clear(line_editor(row));
        store_back();
        break;
    case YM_TAPS: gx_de_clear_taps(-1); break;
    default:
        hsel = it->action - YM_MODE;
        header_activate(hsel);
        break;
    }
    changed();
    yeq_draw();
}

static const menu_def_t menu_yeq = {"Y= OPTIONS", NULL, 0, ym_select, false, "yeq", ym_count, ym_get};

/* The shared graph bar (softkeys_graph) with this context menu: a copy made
 * on first use, so changes to the shared bar carry over. */
static void sk_3d(void)
{
    store_back();
    graph_editor_switch(true);
}
static const softkeys_t *yeq_softkeys(void)
{
    static softkeys_t sk;
    if (!sk.menu) {
        sk = softkeys_graph;
        sk.key[0] = (softkey_t){"3D", sk_3d}; /* (the 2D/3D switch: this is 2D) */
        sk.menu = &menu_yeq;
    }
    return &sk;
}

/* The graph editor's 2D/3D switch (Y= header and F1 "3D", Z= header and F1 "2D"): the other
 * editor, in the same flow (graphing from the home screen goes on); the editor of a graph cell
 * (ENTER on a cell, a new empty cell) stays of the cell's kind. */
void graph_editor_switch(bool d3)
{
    if (gcell_bound()[0]) {
        ui_toast(d3 ? "this graph cell is 2D: graph it, then add a 3D one" : "this graph cell is 3D: graph it, then add a 2D one");
        return;
    }
    ui_switch(d3 ? &scr_g3d_eq : &scr_yeq);
}


const screen_t scr_yeq = {"yeq", yeq_title, yeq_enter, yeq_draw, yeq_key, NULL, yeq_editor, yeq_touch, "yeq",
                          &softkeys_graph, yeq_softkeys};
