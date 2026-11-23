/*
 * Spreadsheet screen (APPS > Spreadsheet): a grid of A..Z x 1..999 with an
 * entry line on top that shows the selected cell's input (and its value),
 * the model in app/sheet_core.c (see sheet.h for references and formulas).
 *
 * Keys. Browsing: arrows move the selection; ENTER edits the selected cell
 * (preloaded with its input); typing starts a fresh entry; ⌫ clears the
 * cell; CLEAR returns home. Editing: the entry line is the standard token
 * editor (menus such as MATH, CAS, VARS, CATALOG, UNITS insert into it);
 * ENTER stores the entry and moves down, UP / DOWN store it and move, CLEAR
 * empties the line or, when empty, cancels.
 * Soft keys: $ (absolute marker), : (range colon), fill↓ (asks for the last
 * row, then copies the cell down with relative references moved), copy,
 * paste (relative references moved by the distance); SHIFT: link (column ->
 * statistics list), sheet (CSV import/export, column from a list, clear),
 * plot (the linked columns as a stat plot), stat (list editor), graph.
 * The fill↓ prompt starts with the last row of the neighbouring column's
 * data, and fill, copy and link confirm on the entry line what they did.
 *
 * Geometry (320 x 240, body y 18..223):
 *   entry line  y = LINE_Y, FONT: "A1:" then the editor or the input text
 *   header      y = HDR_Y .. +14: column letters (link name if linked)
 *   cells       NVIS = 4 columns of CWID = 73 px after the RHW = 26 px row
 *               numbers; NROWS = 11 rows of RH = 15 px from ROW0, values in
 *               font_medium (9 characters), numbers right-aligned
 * View state (selection, scroll, edit mode) is module static; the selection
 * is saved with the sheet blob.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat.h"
#include "registry.h"
#include "sdbk.h"
#include "sheet.h"
#include "theme.h"

#define FF (&font_medium)
#define LINE_Y (BODY_Y + 1)
#define HDR_Y (BODY_Y + 23)
#define ROW0 (HDR_Y + 15)
#define RH 15
#define NROWS ((UI_BOTTOM - ROW0) / RH)
#define RHW 26
#define CWID 73
#define NVIS 4
#define CCH ((CWID - 4) / 7) /* characters per cell */

enum { V_BROWSE = 0, V_EDIT, V_FILL };
static int col, row, left, top, mode;
static editor_t ed;
static char msg[64];
static bool msg_ok; /* msg confirms an action (blue), else it is an error (red) */
static char clip[SH_TEXT];
static int clip_c = -1, clip_r;

int sheet_sel_col(void) { return col; }
int sheet_sel_row(void) { return row; }
void sheet_select(int c, int r)
{
    col = c >= 0 && c < SH_COLS ? c : 0;
    row = r >= 0 && r < SH_ROWS ? r : 0;
}

/* Keep the selection on the sheet and visible. */
static void clamp(void)
{
    if (col < 0)
        col = 0;
    if (col >= SH_COLS)
        col = SH_COLS - 1;
    if (row < 0)
        row = 0;
    if (row >= SH_ROWS)
        row = SH_ROWS - 1;
    if (col < left)
        left = col;
    if (col >= left + NVIS)
        left = col - NVIS + 1;
    if (row < top)
        top = row;
    if (row >= top + NROWS)
        top = row - NROWS + 1;
}

/* Start editing: blank (a key was typed) or with the cell's input. */
static void begin_edit(bool blank)
{
    mode = V_EDIT;
    ed_clear(&ed);
    const scell_t *c = sheet_get(col, row);
    if (!blank && c)
        ed_set_text(&ed, c->text);
}

static void draw(void)
{
    clamp();
    ui_cursor_hide();
    gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
    /* column header strip and row numbers */
    gfx_fill_rect(0, HDR_Y, LCD_WIDTH, 14, TH_PANEL);
    for (int c = 0; c < NVIS; c++) {
        int ci = left + c, x = RHW + c * CWID;
        char h[SL_NAME + 4];
        const char *ln = sheet_link_name(ci);
        if (ln[0])
            snprintf(h, sizeof h, "%c>%s", 'A' + ci, ln);
        else
            snprintf(h, sizeof h, "%c", 'A' + ci);
        bool hs = ci == col;
        if (hs)
            gfx_fill_rect(x + 1, HDR_Y, CWID - 1, 14, TH_SEL_BG);
        int w = gfx_text_width(&font_small, h);
        gfx_text(&font_small, x + (CWID - w) / 2, HDR_Y + 1, h, hs ? TH_SEL_FG : TH_FG, GFX_TRANSPARENT);
        gfx_vline(x, HDR_Y, UI_BOTTOM - HDR_Y, TH_GRID);
    }
    for (int r = 0; r < NROWS; r++) {
        int ri = top + r, y = ROW0 + r * RH;
        char h[12];
        snprintf(h, sizeof h, "%d", ri + 1);
        bool rs = ri == row;
        if (rs)
            gfx_fill_rect(0, y, RHW, RH, TH_PANEL);
        gfx_text(&font_small, RHW - 2 - gfx_text_width(&font_small, h), y + 1, h, rs ? TH_FG : TH_DIM,
                 GFX_TRANSPARENT);
        gfx_hline(0, y + RH - 1, LCD_WIDTH, TH_GRID);
        for (int c = 0; c < NVIS; c++) {
            int ci = left + c, x = RHW + c * CWID;
            const scell_t *cell = sheet_get(ci, ri);
            bool sel = ci == col && ri == row;
            if (sel)
                gfx_fill_rect(x + 1, y, CWID - 1, RH - 1, TH_SEL_BG);
            if (!cell)
                continue;
            char b[48];
            sheet_display(cell, CCH, b, sizeof b);
            uint16_t fg = sel ? TH_SEL_FG : (cell->flags & SC_ERR) ? TH_ERR : TH_FG;
            int w = gfx_text_width(FF, b);
            /* labels left-aligned, values right-aligned */
            int tx = (cell->flags & SC_LABEL) ? x + 3 : x + CWID - 3 - w;
            gfx_text(FF, tx, y, b, fg, GFX_TRANSPARENT);
        }
    }
    /* entry line */
    gfx_hline(0, HDR_Y - 2, LCD_WIDTH, TH_GRID);
    char lab[40];
    if (mode == V_FILL)
        snprintf(lab, sizeof lab, "fill %c%d to row:", 'A' + col, row + 1);
    else
        snprintf(lab, sizeof lab, "%c%d:", 'A' + col, row + 1);
    int lw = gfx_text(FONT, 2, LINE_Y, lab, TH_LABEL, GFX_TRANSPARENT) + 4;
    const scell_t *c = sheet_get(col, row);
    if (mode != V_BROWSE) {
        ui_draw_editor_line(&ed, lw, LINE_Y, (LCD_WIDTH - lw) / CW, TH_FG, TH_BG, true);
    } else if (msg[0]) {
        gfx_text(FF, lw, LINE_Y + 3, msg, msg_ok ? TH_LINK : TH_ERR, GFX_TRANSPARENT);
    } else if (c) {
        int x = lw + gfx_text(FONT, lw, LINE_Y, c->text, TH_FG, GFX_TRANSPARENT); /* gfx_text returns the width */
        if (!(c->flags & SC_LABEL)) {
            char v[64], d[72];
            sheet_display(c, 30, v, sizeof v);
            if (strcmp(v, c->text)) {
                snprintf(d, sizeof d, "= %s", v);
                gfx_text(FF, x + 6, LINE_Y + 3, d, (c->flags & SC_ERR) ? TH_ERR : TH_LINK, GFX_TRANSPARENT);
            }
        }
    }
}

/* Store the entry line into the selected cell; false (msg set) if the sheet
 * is full. */
static bool apply(void)
{
    char disp[ED_MAX * 4];
    ed_display(&ed, disp, sizeof disp);
    if (strlen(disp) >= SH_TEXT) {
        snprintf(msg, sizeof msg, "entry too long (max %d)", SH_TEXT - 1);
        return false;
    }
    if (sheet_set(col, row, disp)) {
        snprintf(msg, sizeof msg, "sheet full (%d cells)", sheet_cap());
        return false;
    }
    return true;
}

/* ENTER in the fill prompt: the typed row number, then fill. */
static void apply_fill(void)
{
    char disp[32];
    ed_display(&ed, disp, sizeof disp);
    int r = atoi(disp) - 1;
    mode = V_BROWSE;
    if (!sheet_get(col, row))
        snprintf(msg, sizeof msg, "nothing to fill: the cell is empty");
    else if (r <= row || r >= SH_ROWS)
        snprintf(msg, sizeof msg, "row must be %d..%d", row + 2, SH_ROWS);
    else if (sheet_fill_down(col, row, r))
        snprintf(msg, sizeof msg, "sheet full");
    else
        snprintf(msg, sizeof msg, "filled %c%d to %c%d", 'A' + col, row + 1, 'A' + col, r + 1), msg_ok = true;
}

static void key(int k, int mod)
{
    msg[0] = 0;
    msg_ok = false;
    if (mode != V_BROWSE) {
        switch (k) {
        case KEY_ENTER:
            if (mode == V_FILL)
                apply_fill();
            else if (apply())
                mode = V_BROWSE, row++;
            break;
        case KEY_CLEAR:
            if (!ed_empty(&ed))
                ed_clear(&ed);
            else
                mode = V_BROWSE;
            break;
        case KEY_UP:
        case KEY_DOWN:
            if (mode == V_FILL)
                mode = V_BROWSE;
            else if (apply())
                mode = V_BROWSE, row += k == KEY_UP ? -1 : 1;
            break;
        default:
            if (!ui_editor_key(&ed, k, mod)) {
                char t[16];
                if (!ui_key_text(k, mod, g.alpha_snapshot_for_key, t, sizeof t))
                    return;
                ed_insert_text(&ed, t, ui_key_suffix(k, mod));
            }
        }
        draw();
        return;
    }
    switch (k) {
    case KEY_UP: row--; break;
    case KEY_DOWN: row++; break;
    case KEY_LEFT: col--; break;
    case KEY_RIGHT: col++; break;
    case KEY_ENTER: begin_edit(false); break;
    case KEY_DEL: sheet_set(col, row, ""); break;
    case KEY_CLEAR: ui_switch(ui_home()); return;
    default: {
        char t[16];
        if (!ui_key_text(k, mod, g.alpha_snapshot_for_key, t, sizeof t))
            return;
        begin_edit(true);
        ed_insert_text(&ed, t, ui_key_suffix(k, mod));
    }
    }
    draw();
}

bool sheet_clip_text(char *out, size_t n)
{
    out[0] = 0;
    if (mode != V_BROWSE) {
        ed_display(&ed, out, n);
        return out[0] != 0;
    }
    const scell_t *c = sheet_get(col, row);
    if (!c || (c->flags & SC_ERR))
        return false;
    sheet_display(c, (int)n, out, n);
    return out[0] != 0;
}

/* Insertion target for menus: the entry line (a fresh entry if not editing). */
static editor_t *sheet_editor(void)
{
    if (mode == V_BROWSE)
        begin_edit(true);
    return &ed;
}

/* Tap a cell to select it (an edit in progress is dropped); drag scrolls the
 * grid both ways, the selection pulled into view (which, like a tap, ends an
 * edit or a fill↓ prompt). */
static void touch(const touch_event_t *ev)
{
    if (ev->type == TEV_DRAG) {
        bool moved = ui_drag_scroll(ev, UI_DRAG_Y, RH, &top, SH_ROWS - NROWS);
        moved |= ui_drag_scroll(ev, UI_DRAG_X, CWID, &left, SH_COLS - NVIS);
        if (!moved)
            return;
        int r = ui_drag_focus(row, top, NROWS), c = ui_drag_focus(col, left, NVIS);
        if (r != row || c != col)
            mode = V_BROWSE;
        row = r, col = c;
        draw();
        return;
    }
    if (ev->type != TEV_TAP || ev->y < ROW0 || ev->x < RHW)
        return;
    mode = V_BROWSE;
    col = left + (ev->x - RHW) / CWID;
    row = top + (ev->y - ROW0) / RH;
    draw();
}

static void enter(void)
{
    if (!ui_returning) {
        msg[0] = 0;
        mode = V_BROWSE;
    }
    sheet_ensure();
}

/* ------------------------------------------------------------ soft keys */
/* $ and : go into the entry line (starting an edit of the cell). */
static void ins(const char *s)
{
    if (mode == V_BROWSE)
        begin_edit(false);
    if (mode == V_EDIT)
        ed_insert_text(&ed, s, NULL);
    draw();
}
static void sk_dollar(void) { ins("$"); }
static void sk_colon(void) { ins(":"); }
/* fill↓: ask for the last row, proposing the last row of the data in the
 * column to the left (else to the right) when it is below the selection,
 * so filling a formula along existing data is fill↓ ENTER. */
static void sk_fill(void)
{
    mode = V_FILL;
    ed_clear(&ed);
    int last = -1;
    for (int d = -1; d <= 1 && last <= row; d += 2) {
        last = -1;
        for (int i = 0; i < sheet_cap(); i++) {
            const scell_t *c = sheet_at(i);
            if (c && c->col == col + d && c->row > last)
                last = c->row;
        }
    }
    if (last > row) {
        char b[8];
        snprintf(b, sizeof b, "%d", last + 1);
        ed_set_text(&ed, b);
    }
    draw();
}
static void sk_copy(void)
{
    const scell_t *c = sheet_get(col, row);
    snprintf(clip, sizeof clip, "%s", c ? c->text : "");
    clip_c = col, clip_r = row;
    snprintf(msg, sizeof msg, "copied %c%d", 'A' + col, row + 1);
    msg_ok = true;
    mode = V_BROWSE;
    draw();
}
static void sk_paste(void)
{
    mode = V_BROWSE;
    msg_ok = false;
    if (clip_c < 0) {
        snprintf(msg, sizeof msg, "copy a cell first");
    } else {
        char t[SH_TEXT];
        sheet_shift_refs(clip, col - clip_c, row - clip_r, t, sizeof t);
        if (sheet_set(col, row, t))
            snprintf(msg, sizeof msg, "sheet full");
    }
    draw();
}

/* SHIFT link: the lists a column can feed, "(none)" first. */
static char link_names[SL_MAX + 1][SL_NAME];
static int nlink;
static int link_count(void) { return nlink; }
static void link_get(int i, menu_item_t *out)
{
    *out = (menu_item_t){i ? link_names[i] : "(no link)", NULL, NULL, i, "sheet"};
}
static void link_select(const menu_item_t *it)
{
    sheet_link(col, it->action ? link_names[it->action] : "");
    if (it->action)
        snprintf(msg, sizeof msg, "column %c feeds %s", 'A' + col, link_names[it->action]);
    else
        snprintf(msg, sizeof msg, "column %c: no link", 'A' + col);
    msg_ok = true;
    ui_returning = true;
    ui_switch(&scr_sheet);
    ui_returning = false;
}
static menu_def_t menu_link = {"LINK COLUMN TO LIST", NULL, 0, link_select, false, "sheet", link_count, link_get};
/* link_names[1..] = the lists in use (RESID excluded) */
static void list_names(void)
{
    nlink = 1;
    for (int i = 0; i < SL_MAX; i++)
        if (sl_at(i) && i != SL_RESID)
            snprintf(link_names[nlink++], SL_NAME, "%s", sl_at(i)->name);
}
static void sk_link(void)
{
    list_names();
    static char title[32];
    snprintf(title, sizeof title, "COLUMN %c FEEDS LIST", 'A' + col);
    menu_link.title = title;
    menu_open(&menu_link);
}

/* SHIFT sheet: CSV and housekeeping. */
#define NFILES 16
static char files[NFILES][13]; /* 8.3 names */
static int nfiles;
static int file_cb(const fs_dirent_t *e, void *user)
{
    const char *dot = strrchr(e->short_name, '.');
    if (!e->is_dir && dot && (!strcmp(dot, ".CSV") || !strcmp(dot, ".TXT")) && nfiles < NFILES)
        strcpy(files[nfiles++], e->short_name);
    return 0;
}
static int fl_count(void) { return nfiles; }
static void fl_get(int i, menu_item_t *out) { *out = (menu_item_t){files[i], NULL, NULL, i, "sheet"}; }
static void fl_select(const menu_item_t *it)
{
    char path[48], m[96];
    snprintf(path, sizeof path, "/%s", files[it->action]);
    int r = sheet_csv_import(path, m, sizeof m);
    sheet_select(0, 0);
    ui_switch(&scr_sheet);
    ui_message(r ? "IMPORT FAILED" : "IMPORTED", m);
}
static const menu_def_t menu_files = {"IMPORT CSV", NULL, 0, fl_select, false, "sheet", fl_count, fl_get};

/* list chooser for "Column from list" */
static void fromlist_select(const menu_item_t *it)
{
    sheet_from_list(col, link_names[it->action]);
    ui_returning = true;
    ui_switch(&scr_sheet);
    ui_returning = false;
}
static int fromlist_count(void) { return nlink - 1; }
static void fromlist_get(int i, menu_item_t *out) { *out = (menu_item_t){link_names[i + 1], NULL, NULL, i + 1, "sheet"}; }
static const menu_def_t menu_fromlist = {"COPY LIST INTO COLUMN", NULL, 0, fromlist_select, false, "sheet",
                                         fromlist_count, fromlist_get};

enum { T_EXPV = 1, T_EXPF, T_IMP, T_FROM, T_CLEAR, T_CLEAR_YES };
static const menu_item_t tools_items[] = {
    {"Export values (SHEET.CSV)", NULL, NULL, T_EXPV, "sheet"},
    {"Export formulas (SHEETF.CSV)", NULL, NULL, T_EXPF, "sheet"},
    {"Import CSV...", NULL, NULL, T_IMP, "sheet"},
    {"Column from list...", NULL, NULL, T_FROM, "sheet"},
    {"Clear sheet...", NULL, NULL, T_CLEAR, "sheet"},
};
static void tools_select(const menu_item_t *it);
static const menu_def_t menu_tools = {"SPREADSHEET", tools_items, sizeof tools_items / sizeof tools_items[0],
                                      tools_select, false, "sheet"};
static const menu_item_t confirm_items[] = {
    {"No", NULL, NULL, 0, "sheet"},
    {"Yes, clear every cell", NULL, NULL, T_CLEAR_YES, "sheet"},
};
static const menu_def_t menu_confirm = {"Clear the sheet?", confirm_items, 2, tools_select, false, "sheet"};

static void tools_select(const menu_item_t *it)
{
    char m[96];
    switch (it->action) {
    case T_EXPV:
    case T_EXPF: {
        int r = sheet_csv_export(it->action == T_EXPV ? "/SHEET.CSV" : "/SHEETF.CSV", it->action == T_EXPF, m,
                                 sizeof m);
        ui_switch(&scr_sheet);
        ui_message(r ? "EXPORT FAILED" : "EXPORTED", m);
        return;
    }
    case T_IMP: {
        nfiles = 0;
        int r = fs_list("/", file_cb, NULL);
        fs_unmount();
        if (r || !nfiles) {
            if (r)
                sdbk_card_error(r, m, sizeof m);
            else
                snprintf(m, sizeof m, "No .CSV or .TXT files in the card's root folder.");
            ui_switch(&scr_sheet);
            ui_message("IMPORT", m);
            return;
        }
        menu_open(&menu_files);
        return;
    }
    case T_FROM:
        list_names();
        menu_open(&menu_fromlist);
        return;
    case T_CLEAR: menu_open(&menu_confirm); return;
    case T_CLEAR_YES: sheet_clear_all(); sheet_select(0, 0); break;
    }
    ui_switch(&scr_sheet);
}
static void sk_tools(void) { menu_open(&menu_tools); }

/* SHIFT plot: the linked columns on the graph as a stat plot. The selected
 * column is Y and the nearest linked column to its left X (a scatter plot);
 * with none on the left, the selected one is X and the nearest on the right
 * Y; a column linked alone gives a histogram. An unlinked column is
 * refused with a hint, since only lists can be plotted. */
static void sk_plot(void)
{
    const char *me = sheet_link_name(col);
    if (!me[0]) {
        snprintf(msg, sizeof msg, "link the column first: SHIFT link");
        msg_ok = false;
        mode = V_BROWSE;
        draw();
        return;
    }
    char x[SL_NAME] = "", y[SL_NAME];
    for (int c = col - 1; c >= 0 && !x[0]; c--)
        snprintf(x, sizeof x, "%s", sheet_link_name(c));
    if (x[0]) {
        snprintf(y, sizeof y, "%s", me);
    } else {
        snprintf(x, sizeof x, "%s", me);
        y[0] = 0;
        for (int c = col + 1; c < SH_COLS && !y[0]; c++)
            snprintf(y, sizeof y, "%s", sheet_link_name(c));
    }
    mode = V_BROWSE;
    stat_plot_show(y[0] ? SP_SCATTER : SP_HIST, x, y[0] ? y : NULL);
}
static void sk_stat(void) { stat_edit_open(0); }
static void sk_graph(void) { ui_switch(&scr_graph); }

static const softkeys_t sheet_soft = {
    {{"$", sk_dollar}, {":", sk_colon}, {"fill↓", sk_fill}, {"copy", sk_copy}, {"paste", sk_paste}},
    {{"link", sk_link}, {"sheet", sk_tools}, {"plot", sk_plot}, {"stat", sk_stat}, {"graph", sk_graph}},
};

const screen_t scr_sheet = {"sheet", "SPREADSHEET", enter, draw, key, NULL, sheet_editor, touch, "sheet",
                            &sheet_soft};

void sheet_open(void) { ui_switch(&scr_sheet); }
const app_def_t app_sheet = {"sheet", "Spreadsheet", sheet_open, {{0}}, "sheet"};

/* Console `state` keys: selection, mode, the selected cell's input, raw
 * result and displayed text, and the number of cells. */
void sheet_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[80];
    snprintf(b, sizeof b, "%c%d", 'A' + col, row + 1);
    kv("sheet_sel", b, user);
    kv("sheet_mode", mode == V_EDIT ? "edit" : mode == V_FILL ? "fill" : "browse", user);
    const scell_t *c = sheet_get(col, row);
    kv("sheet_text", c ? c->text : "", user);
    kv("sheet_res", c ? c->res : "", user);
    sheet_display(c, CCH, b, sizeof b);
    kv("sheet_disp", b, user);
    snprintf(b, sizeof b, "%d", sheet_count());
    kv("sheet_cells", b, user);
}
