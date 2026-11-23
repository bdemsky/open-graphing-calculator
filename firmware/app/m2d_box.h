/*
 * 2D math box layout (internal to the m2d_* files): a tree of boxes in a
 * caller-provided pool (no malloc; pools live on the stack while drawing).
 *
 * Coordinates: every box has a width and an ascent/descent around its
 * baseline. After bx_place(), (x, y) is the absolute left edge / baseline.
 *
 * Usage: build a tree (m2d_parse() or m2d_layout_editor(), or bx_new/bx_add
 * by hand), then bx_measure(root), bx_place(root, x, baseline) once, and
 * bx_draw(root, colour). Check pool.overflow (or a -1 root) and fall back
 * to linear text when the pool was too small.
 */
#ifndef M2D_BOX_H
#define M2D_BOX_H
#include <stdbool.h>
#include <stdint.h>
#include "editor.h"
#include "font.h"

enum {
    BX_ROW,    /* children left to right on a common baseline */
    BX_TEXT,   /* s[0..len) in the level's font */
    BX_FRAC,   /* children: numerator, denominator */
    BX_SUP,    /* child: exponent, raised after the previous box of the row */
    BX_SUB,    /* child: subscript, lowered after the previous box */
    BX_ROOT,   /* children: radicand [, index] */
    BX_FENCE,  /* child: content; delimiters aux = left, aux2 = right ('(' '[' '{' '|' or 0) */
    BX_BIGOP,  /* aux = BIG_SUM/PROD/INT/LIM; children: lower, upper (rows, may be empty) */
    BX_MATRIX, /* children: cells row-major, aux = columns */
    BX_GAP,    /* aux = width in pixels */
    BX_LINES,  /* children: rows stacked as lines, all but the first indented aux px (bx_break) */
};
enum { BIG_SUM = 1, BIG_PROD, BIG_INT, BIG_LIM };

/* flags */
#define BXF_OVERLINE 0x01 /* text: bar over it (repeating decimals) */
#define BXF_SLOT 0x02     /* row: an editor slot (placeholder when empty) */
#define BXF_PAD 0x04      /* text: binary operator, 2 px each side */
#define BXF_WIDEPAD 0x08  /* text: relation (=, <, →), 4 px each side */
#define BXF_DIM 0x10      /* text: dimmed (secondary) */
#define BXF_BOLD 0x20     /* text: drawn twice, 1 px apart (the O of an order term, unlike a 0) */

/* One box (~40 bytes). Children are a linked list of pool indices. */
typedef struct {
    uint8_t kind, lvl, flags, aux, aux2; /* BX_*, nesting level 0..3 (font), BXF_*, kind-specific */
    int16_t w, asc, desc;      /* size: width, extent above / below the baseline (pixels) */
    int16_t x, y;              /* relative to the parent after bx_measure, absolute after bx_place */
    int16_t child, next, last; /* first child, next sibling, last child (-1: none) */
    const char *s; /* text */
    uint16_t len;  /* text bytes */
    int16_t tok;   /* editor: token index whose right edge this box marks (-1) */
    int16_t tb, te; /* editor slot rows: token range [tb, te) */
} bx_t;

/* Box storage provided by the caller (usually a stack array). */
typedef struct {
    bx_t *b;       /* the boxes */
    int n, cap;    /* used, capacity */
    bool overflow; /* a bx_new failed: the tree is incomplete, do not draw it */
} bxpool_t;

void bx_init(bxpool_t *p, bx_t *store, int cap);
int bx_new(bxpool_t *p, int kind, int lvl);
void bx_add(bxpool_t *p, int parent, int child);
int bx_text(bxpool_t *p, int lvl, const char *s, int len, int flags);
int bx_wrap(bxpool_t *p, int kind, int lvl, int child); /* new box with one child */
int bx_nchildren(const bxpool_t *p, int i);

/* font metrics of a nesting level (theme.h fonts, tight ink extents) */
const font_t *bx_font(int lvl);
int bx_asc(int lvl);
int bx_desc(int lvl);
int bx_axis(int lvl); /* height of the fraction bar / minus sign above the baseline */

void bx_measure(bxpool_t *p, int i);
void bx_place(bxpool_t *p, int i, int x, int y); /* absolute coordinates for the subtree */
/* A measured layout wider than `avail` broken into lines at the + and − of
 * its top-level sum (a BX_LINES box, measured), or i itself. */
int bx_break(bxpool_t *p, int i, int avail);
/* Draw a placed subtree (text log disabled by the caller). */
void bx_draw(const bxpool_t *p, int i, uint16_t fg);

/* Linear math text -> boxes (m2d_parse.c): root row or -1 if not understood. */
int m2d_parse(bxpool_t *p, const char *s, int lvl);

/* Textbook editor -> boxes (m2d_edit.c): root row. tokbox[i] (ED_MAX
 * entries) = box whose right edge is the caret position after token i (-1:
 * none); rows[] = the slot rows (BXF_SLOT, token range tb..te). */
int m2d_layout_editor(bxpool_t *p, const editor_t *e, int lvl, int16_t *tokbox, int16_t *rows, int *nrows,
                      int maxrows);

#endif
