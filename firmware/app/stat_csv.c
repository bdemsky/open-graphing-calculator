/*
 * CSV import/export of lists on the microSD card (STAT > CSV on SD card).
 *
 * Export: one column per list, a header row with the list names, rows
 * separated by CRLF, numbers with up to 17 significant digits (exact
 * round trip). Import: separator ',' ';' or TAB (detected from the first
 * line; with ';' or TAB a decimal comma is accepted), optional quotes; a first
 * row containing text is a header: columns named like lists (L1, RESID, a
 * valid name) go to those lists (named lists are created), others to L1, L2,
 * ... by position. Interior empty cells become undef; trailing ones are cut.
 *
 * microSD safety (core/fat.h): a written file goes to fresh clusters and only
 * replaces the old file in fs_close(), so a card pulled during an export
 * keeps the previous file (fs_abort() drops a failed write). The card is
 * unmounted (powered down) after every operation (card_done), so it can be
 * removed whenever no operation is running. Import reads the file in 512-byte
 * blocks through reader_t; the target lists are cleared before the rows are
 * read, so a read error mid-file leaves the rows read so far.
 *
 * UI: STAT soft key SHIFT csv opens the menu "CSV ON MICROSD": Export lists
 * (form: list names + file name, written to the card's root), Export all
 * non-empty lists to /ALL.CSV, Import (a menu of *.CSV / *.TXT in the root),
 * Card info. Console: `stat import|export` (stat_console.c).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat.h"
#include "sdbk.h"
#include "stat.h"

static int first_slot = -1; /* list slot of the first imported column (-1: none) */

#define LINE 512

static void card_done(void) { fs_unmount(); } /* power the card down after each operation */

/* write a string; 0 ok, -1 on a short write / error */
static int put(fs_file_t *f, const char *s)
{
    size_t n = strlen(s);
    return fs_write(f, s, n) == (int)n ? 0 : -1;
}

/* Write the lists in `slots` (n of them, all in use) to `path` as CSV columns.
 * Rows run to the longest list; shorter lists and NaN cells give empty
 * cells. Each number is printed with %.15g, or %.17g if that does not read
 * back as the same double. msg gets a summary or the error. Returns 0 or an
 * FS_E* code. Inside the loops r is used as a boolean "a write failed". */
int stat_csv_export(const char *path, const int *slots, int n, char *msg, size_t msgn)
{
    fs_file_t f;
    int r = fs_open(&f, path, FS_WRITE);
    if (r) {
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
        card_done();
        return r;
    }
    int rows = 0;
    char cell[48];
    for (int c = 0; c < n && !r; c++) {
        const slist_t *l = sl_at(slots[c]);
        if (l->len > rows)
            rows = l->len;
        r = put(&f, c ? "," : "") || put(&f, l->name);
    }
    r = r || put(&f, "\r\n");
    for (int i = 0; i < rows && !r; i++) {
        for (int c = 0; c < n && !r; c++) {
            const slist_t *l = sl_at(slots[c]);
            cell[0] = 0;
            if (i < l->len && !isnan(l->v[i])) {
                snprintf(cell, sizeof cell, "%.15g", l->v[i]);
                if (strtod(cell, NULL) != l->v[i])
                    snprintf(cell, sizeof cell, "%.17g", l->v[i]);
            }
            r = (c && put(&f, ",")) || put(&f, cell);
        }
        r = r || put(&f, "\r\n");
    }
    if (r) {
        fs_abort(&f);
        snprintf(msg, msgn, "Write failed (card full or removed?).");
        card_done();
        return FS_EIO;
    }
    r = fs_close(&f);
    if (r)
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
    else
        snprintf(msg, msgn, "%d lists, %d rows -> %s.", n, rows, path);
    card_done();
    return r;
}

/* ------------------------------------------------------------ import */
/* Buffered line reader over one open file. */
typedef struct {
    fs_file_t f;
    char buf[LINE];  /* block read from the file */
    int len, pos;    /* bytes in buf; next byte to consume */
    bool eof;        /* fs_read returned 0 or an error */
    int err;         /* the fs_read error (< 0), 0 if none */
} reader_t;

/* next line (without CR/LF) into out; false at end. LF ends a line, CRs are
 * dropped (CRLF and LF files both work; old Mac CR-only files read as one
 * line). Characters beyond n-1 are discarded, so an over-long line is cut,
 * not split. A read error ends the file like EOF and is kept in rd->err. */
static bool next_line(reader_t *rd, char *out, size_t n)
{
    size_t o = 0;
    bool any = false;
    for (;;) {
        if (rd->pos >= rd->len) {
            if (rd->eof)
                break;
            rd->len = fs_read(&rd->f, rd->buf, sizeof rd->buf);
            rd->pos = 0;
            if (rd->len <= 0) {
                rd->eof = true;
                if (rd->len < 0)
                    rd->err = rd->len;
                rd->len = 0;
                break;
            }
        }
        char c = rd->buf[rd->pos++];
        any = true;
        if (c == '\n')
            break;
        if (c != '\r' && o + 1 < n)
            out[o++] = c;
    }
    out[o] = 0;
    return any;
}

/* split a line into cells (in place); returns the count. Leading spaces are
 * skipped and trailing spaces trimmed. A quoted cell may contain the
 * separator, and "" inside quotes is one quote (RFC 4180); anything between
 * the closing quote and the next separator is ignored. Cells beyond `max`
 * are dropped (the count returned is at most max). */
static int split(char *s, char sep, char **cells, int max)
{
    int n = 0;
    for (;;) {
        while (*s == ' ')
            s++;
        char *start = s;
        if (*s == '"') { /* quoted: "a ""b""" */
            char *w = start = ++s;
            while (*s) {
                if (*s == '"') {
                    if (s[1] != '"') {
                        s++;
                        break;
                    }
                    s++;
                }
                *w++ = *s++;
            }
            while (*s && *s != sep)
                s++;
            char c = *s;
            *w = 0;
            if (n < max)
                cells[n++] = start;
            if (!c)
                return n;
            s++;
            continue;
        }
        while (*s && *s != sep)
            s++;
        char c = *s;
        *s = 0;
        char *e = s - 1; /* trim */
        while (e >= start && *e == ' ')
            *e-- = 0;
        if (n < max)
            cells[n++] = start;
        if (!c)
            return n;
        s++;
    }
}

/* A whole cell as a number (trailing spaces allowed). dec_comma: ',' is the
 * decimal separator ("3,5"), possible only when the field separator is ';'
 * or TAB. false for text, so the caller can recognise a header row. */
static bool parse_num(const char *s, bool dec_comma, double *v)
{
    char tmp[48];
    snprintf(tmp, sizeof tmp, "%s", s);
    if (dec_comma)
        for (char *p = tmp; *p; p++)
            if (*p == ',')
                *p = '.';
    char *end;
    *v = strtod(tmp, &end);
    while (*end == ' ')
        end++;
    return end != tmp && !*end;
}

/* Read the CSV file `path` into lists (format in the file header).
 * Steps:
 *   1. read the first line; pick the separator: TAB if any, else ';' if at
 *      least as frequent as ',', else ',' (European files use ';' with
 *      decimal commas);
 *   2. the first line is a header if any non-empty cell is not a number;
 *   3. map each column to a list: a header naming an existing list, else a
 *      valid new name (named list created), else (no header, or a header
 *      that is not a usable name) the next of L1..L6 not taken by any column;
 *      a list named by two headers gets the first column, the other is
 *      skipped, as are columns left without a list; every target list is
 *      emptied and loses its formula;
 *   4. read the rows (the first line too when it is not a header); blank
 *      lines are skipped; cells beyond a list's capacity are dropped;
 *   5. add imported named lists to the editor columns and call sl_changed().
 * Returns FS_OK, or an FS_E* code with msg set (a read error mid-file
 * returns FS_EIO, after the rows read so far were stored); msg also gets the
 * summary. */
int stat_csv_import(const char *path, char *msg, size_t msgn)
{
    reader_t rd;
    memset(&rd, 0, sizeof rd);
    int r = fs_open(&rd.f, path, FS_READ);
    if (r) {
        snprintf(msg, msgn, "%s: %s.", path, fs_strerror(r));
        card_done();
        return r;
    }
    char line[LINE], *cells[SL_MAX];
    if (!next_line(&rd, line, sizeof line)) {
        fs_close(&rd.f);
        card_done();
        snprintf(msg, msgn, "%s is empty.", path);
        return FS_EINVAL;
    }
    int ncomma = 0, nsemi = 0, ntab = 0;
    for (char *p = line; *p; p++)
        ncomma += *p == ',', nsemi += *p == ';', ntab += *p == '\t';
    char sep = ntab ? '\t' : nsemi && nsemi >= ncomma ? ';' : ',';
    bool dec_comma = sep != ',';
    char first[LINE];
    memcpy(first, line, sizeof line);
    int nc = split(first, sep, cells, SL_MAX);
    bool header = false;
    for (int c = 0; c < nc; c++) {
        double v;
        if (cells[c][0] && !parse_num(cells[c], dec_comma, &v))
            header = true;
    }
    /* target lists: first the columns whose header names a list, then the
     * others by position, skipping every list a named column took */
    slist_t *dst[SL_MAX];
    bool named[SL_MAX];
    for (int c = 0; c < nc; c++) {
        slist_t *l = header && cells[c][0] ? sl_find(cells[c]) : NULL;
        if (!l && header && sl_valid_name(cells[c]))
            l = sl_create(cells[c]);
        named[c] = l != NULL;
        for (int k = 0; k < c && l; k++)
            if (dst[k] == l) /* the same name twice: only the first column */
                l = NULL;
        dst[c] = l;
    }
    int next_l = 0;
    for (int c = 0; c < nc; c++) {
        if (named[c])
            continue;
        slist_t *l = NULL;
        while (next_l < 6 && !l) { /* by position: L1, L2, ... */
            l = sl_at(next_l++);
            for (int k = 0; k < nc && l; k++)
                if (dst[k] == l)
                    l = NULL;
        }
        dst[c] = l;
    }
    for (int c = 0; c < nc; c++)
        if (dst[c]) {
            dst[c]->len = 0;
            dst[c]->formula[0] = 0;
        }
    /* used[c]: cells written to column c's list so far (including NaN cells);
     * len only follows the last numeric cell, which cuts trailing empties */
    int used[SL_MAX] = {0}, rows = 0;
    bool have = header ? next_line(&rd, line, sizeof line) : true;
    while (have) {
        int n = split(line, sep, cells, SL_MAX);
        if (n > 1 || (n == 1 && cells[0][0])) {
            for (int c = 0; c < nc && c < n; c++) {
                slist_t *l = dst[c];
                double v;
                if (!l || used[c] >= sl_cap())
                    continue;
                if (!cells[c][0])
                    v = DNAN;
                else if (!parse_num(cells[c], dec_comma, &v))
                    v = DNAN;
                l->v[used[c]++] = v;
                if (!isnan(v))
                    l->len = used[c]; /* trailing empties are cut */
            }
            rows++;
        }
        have = next_line(&rd, line, sizeof line);
    }
    fs_close(&rd.f);
    card_done();
    int nl = 0;
    char names[64] = "";
    first_slot = -1;
    for (int c = 0; c < nc; c++)
        if (dst[c]) {
            nl++;
            if (strlen(names) + strlen(dst[c]->name) + 2 < sizeof names)
                snprintf(names + strlen(names), sizeof names - strlen(names), "%s%s", nl > 1 ? "," : "", dst[c]->name);
            /* show imported named lists in the editor */
            int slot = -1;
            for (int s = 0; s < SL_MAX; s++)
                if (sl_at(s) == dst[c])
                    slot = s;
            if (first_slot < 0)
                first_slot = slot;
            bool shown = false;
            for (int k = 0; k < sl_ncols; k++)
                shown |= sl_cols[k] == slot;
            if (!shown && slot > SL_RESID && sl_ncols < SL_MAX)
                sl_cols[sl_ncols++] = slot;
            sl_changed(dst[c]);
        }
    if (rd.err) {
        snprintf(msg, msgn, "%s: %s after %d rows.", path, fs_strerror(rd.err), rows);
        return FS_EIO;
    }
    snprintf(msg, msgn, "%d rows -> %s.", rows, names);
    return FS_OK;
}

/* ------------------------------------------------------------ UI */
/* Export form: field 0 = list names ("L1,L2"), field 1 = 8.3 file name in the
 * root directory; action 1 = Export. */
static void csv_action(int a);
static const sfield_t f_export[] = {
    {"Lists:", SF_TEXT, 0, 0, 0, "L1,L2"},
    {"File:", SF_TEXT, 0, 0, 0, "LISTS.CSV"},
    {"Export", SF_ACTION, 0, 0, 1, NULL},
};
static const sform_t form_export = {"Export lists to CSV", f_export, 3, csv_action, "stat-csv", 0};

/* Export from the form: errors stay on the form, success shows a message. */
static void csv_action(int a)
{
    int slots[SL_MAX];
    char msg[96];
    int n = sl_parse(sf_text(0), slots, SL_MAX, msg, sizeof msg);
    if (n < 0) {
        sf_error(msg);
        return;
    }
    if (!n) {
        sf_error("no lists");
        return;
    }
    char path[48];
    snprintf(path, sizeof path, "/%s", sf_text(1));
    int r = stat_csv_export(path, slots, n, msg, sizeof msg);
    if (r) {
        sf_error(r == FS_ENAME ? "File: use a name like DATA.CSV (8.3)" : msg);
        return;
    }
    ui_message("EXPORTED", msg);
}

/* file chooser: *.CSV / *.TXT in the root directory */
#define NFILES 24
static char files[NFILES][40];
static int nfiles;

/* fs_list callback: collect up to NFILES data files of the root directory.
 * The extension is tested on the 8.3 short name (always upper case); the
 * long name is shown and used to open the file when it fits. */
static int file_cb(const fs_dirent_t *e, void *user)
{
    const char *dot = strrchr(e->short_name, '.');
    if (!e->is_dir && dot && (!strcmp(dot, ".CSV") || !strcmp(dot, ".TXT")) && nfiles < NFILES)
        strcpy(files[nfiles++], strlen(e->name) < sizeof files[0] ? e->name : e->short_name);
    return 0;
}

static int fl_count(void) { return nfiles; }
static void fl_get(int i, menu_item_t *out) { *out = (menu_item_t){files[i], NULL, NULL, i, "stat-csv"}; }

/* Import the chosen file, then show the list editor with a summary, on the
 * column of the first imported list (named lists are added after L6, where
 * the user would not see them otherwise). */
static void fl_select(const menu_item_t *it)
{
    char path[72], msg[96];
    snprintf(path, sizeof path, "/%s", files[it->action]);
    int r = stat_csv_import(path, msg, sizeof msg);
    if (r) {
        ui_message("IMPORT FAILED", msg);
        return;
    }
    int col = 0;
    for (int k = 0; k < sl_ncols; k++)
        if (sl_cols[k] == first_slot)
            col = k;
    stat_edit_open(col);
    ui_message("IMPORTED", msg);
}

static const menu_def_t menu_files = {"IMPORT CSV", NULL, 0, fl_select, false, "stat-csv", fl_count, fl_get};

enum { C_EXPORT = 1, C_EXPORT_ALL, C_IMPORT, C_INFO };
static const menu_item_t csv_items[] = {
    {"Export lists...", NULL, NULL, C_EXPORT, "stat-csv"},
    {"Export all lists (ALL.CSV)", NULL, NULL, C_EXPORT_ALL, "stat-csv"},
    {"Import CSV...", NULL, NULL, C_IMPORT, "stat-csv"},
    {"Card info", NULL, NULL, C_INFO, "stat-csv"},
};

/* CSV menu actions. Import lists the root directory first (unmounting the
 * card again) and opens the file menu; Card info reports the FAT type, size
 * (blocks are 512 bytes: blocks / 2048 = MB) and free space. */
static void csv_select(const menu_item_t *it)
{
    char msg[96];
    switch (it->action) {
    case C_EXPORT: sf_open(&form_export); break;
    case C_EXPORT_ALL: {
        int slots[SL_MAX], n = 0;
        for (int i = 0; i < SL_MAX; i++)
            if (sl_at(i) && sl_at(i)->len)
                slots[n++] = i;
        if (!n) {
            ui_message("EXPORT", "All lists are empty.");
            return;
        }
        int r = stat_csv_export("/ALL.CSV", slots, n, msg, sizeof msg);
        ui_message(r ? "EXPORT FAILED" : "EXPORTED", msg);
        break;
    }
    case C_IMPORT: {
        nfiles = 0;
        int r = fs_list("/", file_cb, NULL);
        card_done();
        if (r) {
            sdbk_card_error(r, msg, sizeof msg);
            ui_message("IMPORT", msg);
            return;
        }
        if (!nfiles) {
            snprintf(msg, sizeof msg, "No .CSV or .TXT files in the card's root folder.");
            ui_message("IMPORT", msg);
            return;
        }
        menu_open(&menu_files);
        break;
    }
    case C_INFO: {
        uint32_t blocks, free_kb;
        int type;
        int r = fs_info(&blocks, &free_kb, &type);
        card_done();
        if (r)
            sdbk_card_error(r, msg, sizeof msg);
        else
            snprintf(msg, sizeof msg, "FAT%d, %lu MB, %lu MB free.", type, (unsigned long)(blocks / 2048u),
                     (unsigned long)(free_kb / 1024u));
        ui_message("MICROSD", msg);
        break;
    }
    }
}

static const menu_def_t menu_csv = {"CSV ON MICROSD", csv_items, sizeof csv_items / sizeof csv_items[0], csv_select,
                                    false, "stat-csv"};

void stat_csv_menu_open(void) { menu_open(&menu_csv); }
