/*
 * sheet.h -- internal interface of the spreadsheet app (app/sheet_*.c).
 *
 *   sheet_core.c   cell storage, A1 references, formula rewriting for the CAS,
 *                  dependency-ordered recalculation with cycle detection,
 *                  fill/copy with relative references, column links to the
 *                  statistics lists, CSV import/export, persistence
 *   sheet_ui.c     the grid screen (scr_sheet), its soft keys and menus
 *   sheet_console.c console commands `sheet ...` for the UI tests
 *
 * Model. The sheet has SH_COLS columns A..Z and SH_ROWS rows 1..999. Only
 * non-empty cells exist: a fixed pool of scell_t carved from the arena
 * (sheet_mem_init, called by app_init), found by linear search. A cell holds
 * what the user typed (display text, as the token editor shows it) and the
 * cached result. Every cell is a CAS expression ("=" in front is optional):
 * 3.5, 1/3, 2.0_m, 9.81±0.02, =sum(A1:A10), =B$1*x; text in double quotes
 * ("mass") is a label. References: A1 (relative), $A$1, $A1, A$1 (absolute
 * parts, kept by fill/copy), ranges A1:B3 (a list, row by row, of the
 * non-empty cells, so sum(A1:A10), mean(...), max(...) work); lowercase a1
 * is the same cell.
 *
 * Evaluation. Each cell's value lives in the CAS as the hidden variable
 * _sA1 (names starting with '_' are not saved by cas_save and do not show in
 * VARS), assigned with cas_eval_quiet("_sA1:=..."), so exact values, units
 * and uncertainty sources (correlations: =A1-A1 is 0±0) flow between cells.
 * The variables are rebuilt after a boot or a CAS reset: sheet_ensure()
 * checks the sentinel variable _shok.
 */
#ifndef SHEET_H
#define SHEET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "stat.h"

#define SH_COLS 26
#define SH_ROWS 999
#define SH_TEXT 72 /* cell input (display text, UTF-8) */
#define SH_RES 56  /* cached CAS result text */

/* cell flags */
enum {
    SC_USED = 1,   /* slot in use */
    SC_ERR = 2,    /* evaluation failed (res holds "ERR:...") */
    SC_DIRTY = 4,  /* needs re-evaluation (recalc in progress) */
    SC_DONE = 8,   /* evaluated in the current recalc */
    SC_STACK = 16, /* on the DFS stack (cycle detection) */
    SC_LABEL = 32, /* "text" label */
};

typedef struct {
    uint16_t row;          /* 0-based */
    uint8_t col;           /* 0..SH_COLS-1 */
    uint8_t flags;         /* SC_* */
    double val;            /* numeric value (nominal value for x±s), NAN if none */
    char text[SH_TEXT];    /* input as typed */
    char res[SH_RES];      /* CAS output ("ERR:..." on errors) */
} scell_t;

/* A rectangle of cells (inclusive, 0-based) named by a reference or range. */
typedef struct {
    int c0, r0, c1, r1;
} srect_t;

/* ---- storage ---- */
size_t sheet_mem_init(void *mem, bool big); /* carve the pool; returns bytes used */
int sheet_cap(void);                        /* pool capacity in cells */
scell_t *sheet_at(int i);                   /* pool slot i (NULL if unused or out of range) */
scell_t *sheet_get(int col, int row);       /* the cell, NULL if empty */
int sheet_count(void);                      /* cells in use */
/* Set a cell's input text ("" deletes it); recalculates what depends on it.
 * Returns 0, or -1 when the pool is full. */
int sheet_set(int col, int row, const char *text);
void sheet_clear_all(void);
/* Parse "A1" / "$B$12" into col/row (0-based); returns the length used, 0 if
 * not a cell name. */
int sheet_parse_ref(const char *s, int *col, int *row);
void sheet_ref_name(int col, int row, char *out, size_t n); /* "A1" */
/* References in a formula, ranges as rectangles; returns the count (<= max). */
int sheet_refs(const char *text, srect_t *out, int max);
/* text with relative references moved by (dc, dr) into out (references that
 * leave the sheet become #REF). */
void sheet_shift_refs(const char *text, int dc, int dr, char *out, size_t n);

/* ---- recalculation ---- */
void sheet_recalc_all(void);
void sheet_ensure(void);       /* recalc everything if the CAS lost the cell variables */
void sheet_invalidate(void);   /* force the next sheet_ensure() to recalc */
/* Fill: copy (col,r0) into rows r0+1..r1 with relative row references moved. */
int sheet_fill_down(int col, int r0, int r1);
/* Copy one cell's input to another cell, moving relative references. */
int sheet_copy(int c0, int r0, int c1, int r1);

/* Display text of a cell's value with the MODE number format, fitted to
 * `width` characters (out gets "" for an empty cell). */
void sheet_display(const scell_t *c, int width, char *out, size_t n);

/* ---- links to the statistics lists ---- */
/* Column col feeds the list `name` ("" = no link): after every recalculation
 * the column's numeric cells (in row order; labels and empty cells skipped)
 * become the list's values. */
int sheet_link(int col, const char *name);
const char *sheet_link_name(int col);
void sheet_push_links(void);
/* Copy a list into a column (from row 1), replacing the column's cells. */
int sheet_from_list(int col, const char *name);

/* ---- CSV on the microSD card ---- */
/* formulas: write the inputs (re-importable) instead of the values */
int sheet_csv_export(const char *path, bool formulas, char *msg, size_t msgn);
int sheet_csv_import(const char *path, char *msg, size_t msgn);

/* ---- UI / console ---- */
extern const screen_t scr_sheet;
void sheet_open(void);
void sheet_state(void (*kv)(const char *, const char *, void *), void *user);
int sheet_console(int argc, char **argv);
/* selection of the sheet screen (for the link / csv menus) */
int sheet_sel_col(void);
int sheet_sel_row(void);
void sheet_select(int col, int row);
/* Copy hook of the edit bar (app/input_clip.c): the entry line while editing,
 * else the selected cell's value as displayed (unabridged; a label's text).
 * false if there is nothing to copy (empty cell, error). */
bool sheet_clip_text(char *out, size_t n);
#endif
