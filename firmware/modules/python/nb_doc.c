/*
 * The open notebook (nb.h): cells in memory, .ipynb (nbformat 4.5) reading
 * and writing, running cells, the QSPI store and the microSD card, the
 * console `nb`, and the USB drive's NOTEBOOKS folder and imports.
 *
 * The document is the core's (app/nbdoc.h): the home screen's notebook, which
 * this module shows (APPS > Notebook), saves, opens and runs. Its cells and
 * their text live in the core's arena (nb_core: alloc / free); a notebook is
 * read into it afresh each time one is opened. NB.open: a named notebook; the
 * unsaved one (the home screen's calculations) has no name and is kept in the
 * session by the core.
 *
 * The .ipynb mapping (docs/NOTEBOOK.md):
 *   Text cell   -> markdown cell (an imported raw cell stays raw)
 *   Python cell -> code cell, source verbatim; outputs: stream stdout, an
 *                  image of a calc.plot(), execute_result (text/plain, and
 *                  text/latex for a calc.Expr) or error (ename, evalue,
 *                  traceback)
 *   Math cell   -> code cell `import calc` / `calc.eval(r"CAS input")`, with
 *                  metadata {"calculator": {"kind": "math", "input": "<as typed>"}}
 *                  (the calculator restores the line as typed from it);
 *                  execute_result text/plain (the CAS's linear result) and
 *                  text/latex
 *   Graph cell  -> code cell `import calc` / `calc.plot(r"f(x)", xmin, xmax)`,
 *                  metadata kind "graph", input, xmin, xmax; display_data
 *                  image/png (drawn here) and text/plain
 * Reading takes any nbformat 4 file: unknown cell types become text cells,
 * image outputs that were not drawn here are left out (and the cell says so),
 * ANSI colour codes are removed from tracebacks; a file that is not JSON or
 * not nbformat 4 is refused with the reason.
 *
 * Execution: Python cells share the module's interpreter (restarted when a
 * notebook is opened); Math cells use the calculator's CAS session (the home
 * screen's variables and settings; `ans` is not changed; Ans and Out[n] are the notebook's results, below); graph cells
 * evaluate their function with the CAS. One execution counter for all.
 * Every change (an edit, a run, a cell added, moved or deleted) is noted
 * (nb_note: dirty, the browser page told); nb_sync.c decides when the
 * notebook is saved where it came from.
 *
 * Example notebooks (nb_examples.c, docs/NOTEBOOK.md): nb_list_all() adds them
 * to the list (where NB_EXAMPLE) unless the store has a notebook of the name;
 * nb_load() reads one from memory as a store notebook not yet written, so the
 * first change saves it to the store. Rename and delete refuse them.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "py_mod.h"
#include "nb.h"
#include "py/compile.h"
#include "py/formatfloat.h"
#include "py/runtime.h"

const nbdoc_api_t *nb_core;
nb_t *nb_docp;

static void *nb_alloc(size_t n) { return nb_core->alloc(n); }
static void nb_free(void *p) { nb_core->free(p); }

char *nb_strdup(const char *s, size_t n)
{
    char *d = nb_alloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}
void nb_set(nb_cell_t *c, int f, const char *s)
{
    char *old = c->f[f];
    c->f[f] = s && *s ? nb_strdup(s, strlen(s)) : NULL;
    nb_free(old);
    c->h = c->hh[0] = c->hh[1] = -1;
}
const char *nb_get(const nb_cell_t *c, int f) { return c->f[f] ? c->f[f] : ""; }
static void nb_append(nb_cell_t *c, int f, const char *s, size_t n)
{
    size_t k = c->f[f] ? strlen(c->f[f]) : 0;
    char *d = nb_alloc(k + n + 1);
    if (!d)
        return;
    if (k)
        memcpy(d, c->f[f], k);
    memcpy(d + k, s, n);
    d[k + n] = 0;
    nb_free(c->f[f]);
    c->f[f] = d;
}

/* ---------------------------------------------------------------- cells */
static const char *const kind_names[NBK_N] = {"text", "math", "python", "graph"};
const char *nb_kind_name(int k) { return k >= 0 && k < NBK_N ? kind_names[k] : "?"; }
int nb_kind_of(const char *s)
{
    for (int k = 0; k < NBK_N; k++)
        if (!strcmp(s, kind_names[k]) || (k == NBK_PY && !strcmp(s, "py")) || (k == NBK_TEXT && !strcmp(s, "markdown")))
            return k;
    return -1;
}

static void new_id(char *id)
{
    static uint32_t seed;
    for (;;) {
        seed = seed * 1664525u + 1013904223u + py_api->millis();
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 8; i++)
            id[i] = hex[(seed >> (4 * i)) & 15];
        id[8] = 0;
        bool dup = false;
        for (int i = 0; i < NB.ncells && !dup; i++)
            dup = NB.cell[i].id != id && !strcmp(NB.cell[i].id, id);
        if (!dup)
            return;
    }
}

nb_cell_t *nb_insert(int at, int kind)
{
    if (NB.ncells >= NB_MAX_CELLS)
        return NULL;
    if (at < 0 || at > NB.ncells)
        at = NB.ncells;
    memmove(&NB.cell[at + 1], &NB.cell[at], (size_t)(NB.ncells - at) * sizeof NB.cell[0]);
    NB.ncells++;
    nb_cell_t *c = &NB.cell[at];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    c->h = c->hh[0] = c->hh[1] = -1;
    c->xmin = -10, c->xmax = 10;
    new_id(c->id);
    return c;
}

void nb_clear_out(nb_cell_t *c)
{
    for (int f = NBF_OUT; f < NBF_NOUT; f++)
        nb_set(c, f, NULL);
    c->flags &= (uint8_t) ~(NBC_RAN | NBC_IMAGE);
    c->count = 0;
    c->h = -1;
}

void nb_delete_cell(int i)
{
    if (i < 0 || i >= NB.ncells)
        return;
    for (int f = 0; f < NBF_N; f++)
        nb_set(&NB.cell[i], f, NULL);
    memmove(&NB.cell[i], &NB.cell[i + 1], (size_t)(NB.ncells - i - 1) * sizeof NB.cell[0]);
    NB.ncells--;
}

void nb_move(int i, int dir)
{
    int j = i + dir;
    if (i < 0 || j < 0 || i >= NB.ncells || j >= NB.ncells)
        return;
    nb_cell_t t = NB.cell[i];
    NB.cell[i] = NB.cell[j], NB.cell[j] = t;
}

void nb_new_doc(const char *name, int where)
{
    nb_core->reset();
    NB.dirty = false;
    py_api->snprintf(NB.name, sizeof NB.name, "%s", name);
    NB.where = where;
    NB.open = true;
}

void nb_close_doc(void)
{
    nb_core->reset();
    NB.name[0] = 0;
    NB.where = 0;
    NB.open = NB.dirty = false;
}

int nb_keep_scratch(void)
{
    if (NB.open || !NB.ncells)
        return 0;
    static char msg[120];
    char name[NBS_NAME];
    nb_default_name(name, sizeof name);
    py_api->snprintf(NB.name, sizeof NB.name, "%s", name);
    NB.where = 0;
    NB.open = true;
    NB.dirty = true;
    if (nb_save(msg, sizeof msg)) {
        NB.open = false, NB.name[0] = 0;
        return -1;
    }
    static char t[64];
    py_api->snprintf(t, sizeof t, "saved as %.28s", name);
    py_api->toast(t);
    return 0;
}

/* "sin(x)" or "sin(x), -5, 5": the expression and the range */
void nb_graph_parse(const char *src, char *expr, size_t n, double *xmin, double *xmax)
{
    *xmin = -10, *xmax = 10;
    int depth = 0;
    const char *cut[2] = {NULL, NULL};
    int k = 0;
    for (const char *p = src; *p; p++) {
        if (*p == '(' || *p == '[' || *p == '{')
            depth++;
        else if (*p == ')' || *p == ']' || *p == '}')
            depth--;
        else if (*p == ',' && !depth && k < 2)
            cut[k++] = p;
    }
    size_t len = k == 2 ? (size_t)(cut[0] - src) : strlen(src);
    if (k == 2) {
        char a[40], b[40];
        py_api->snprintf(a, sizeof a, "%.*s", (int)(cut[1] - cut[0] - 1), cut[0] + 1);
        py_api->snprintf(b, sizeof b, "%s", cut[1] + 1);
        /* the calculator's minus sign as typed: ⁻ */
        for (char *s = a; s; s = s == a ? b : NULL)
            for (char *q; (q = strstr(s, "\xE2\x81\xBB")) != NULL;)
                q[0] = '-', memmove(q + 1, q + 3, strlen(q + 3) + 1);
        const char *e1, *e2;
        double lo = nb_strtod(a, &e1), hi = nb_strtod(b, &e2);
        if (e1 != a && e2 != b && lo < hi)
            *xmin = lo, *xmax = hi;
        else
            len = strlen(src);
    }
    while (len && src[len - 1] == ' ')
        len--;
    py_api->snprintf(expr, n, "%.*s", (int)len, src);
}

/* ---------------------------------------------------------------- core services */
static void to_cas(const char *disp, char *out, size_t n)
{
    nbio_text_t t = {disp, out, n};
    if (py_pc->nb_io(NBIO_TO_CAS, 0, &t, 0))
        py_api->snprintf(out, n, "%s", disp);
}
void nb_to_cas(const char *disp, char *out, size_t n) { to_cas(disp, out, n); }

/* ---------------------------------------------------------------- writing .ipynb */
static void sink_count(void *u, const void *s, size_t n)
{
    (void)s;
    *(size_t *)u += n;
}
static void b64_to_json(void *u, const void *s, size_t n) { jw_str_piece(u, s, n); }

/* plots: the last two drawn are kept (the page redraws them while it scrolls) */
static struct {
    char key[200], err[80];
    double a, b;
    bool ok;
    uint8_t pix[NB_PLOT_W * NB_PLOT_H];
} plots[2];
static int plot_next;
const uint8_t *nb_plot_get(const char *cas, double a, double b, char *err, size_t n)
{
    int i = 0;
    while (i < 2 && !(plots[i].key[0] && !strcmp(plots[i].key, cas) && plots[i].a == a && plots[i].b == b))
        i++;
    if (i == 2) {
        i = plot_next, plot_next = (plot_next + 1) % 2;
        py_api->snprintf(plots[i].key, sizeof plots[i].key, "%s", cas);
        plots[i].a = a, plots[i].b = b;
        plots[i].ok = !nb_plot(cas, a, b, plots[i].pix, NB_PLOT_W, NB_PLOT_H, plots[i].err, sizeof plots[i].err);
        if (strlen(cas) >= sizeof plots[i].key)
            plots[i].key[0] = 0; /* (not found again) */
    }
    if (!plots[i].ok && err)
        py_api->snprintf(err, n, "%s", plots[i].err);
    return plots[i].ok ? plots[i].pix : NULL;
}

/* source of a Python string literal for s (r"..." when it can be) */
static void py_literal(const char *s, char *out, size_t n)
{
    size_t l = strlen(s);
    if (!strpbrk(s, "\"\n") && (!l || s[l - 1] != '\\')) {
        py_api->snprintf(out, n, "r\"%s\"", s);
        return;
    }
    size_t k = 0;
    out[k++] = '"';
    for (; *s && k + 4 < n; s++) {
        if (*s == '"' || *s == '\\')
            out[k++] = '\\', out[k++] = *s;
        else if (*s == '\n')
            out[k++] = '\\', out[k++] = 'n';
        else
            out[k++] = *s;
    }
    out[k++] = '"';
    out[k] = 0;
}

static void lines_of(jw_t *w, const char *text, bool keep_nl)
{
    jw_arr(w);
    while (text && *text) {
        const char *nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) + (keep_nl ? 1 : 0) : strlen(text);
        jw_str_begin(w);
        jw_str_piece(w, text, len);
        jw_str_end(w);
        text = nl ? nl + 1 : text + len;
    }
    jw_arr_end(w);
}

static void w_plot(jw_t *w, const char *cas, double xmin, double xmax)
{
    char err[80], disp[256], plain[320];
    nbio_text_t t = {cas, disp, sizeof disp};
    if (py_pc->nb_io(NBIO_PRETTY, 0, &t, 0))
        py_api->snprintf(disp, sizeof disp, "%s", cas);
    jw_obj(w);
    jw_key(w, "data");
    jw_obj(w);
    const uint8_t *pix = nb_plot_get(cas, xmin, xmax, err, sizeof err);
    if (pix) {
        jw_key(w, "image/png");
        jw_str_begin(w);
        b64_t b;
        b64_init(&b, b64_to_json, w);
        png_indexed(pix, NB_PLOT_W, NB_PLOT_H, nb_plot_pal, NBP_N, b64_put, &b);
        b64_end(&b);
        jw_str_end(w);
    }
    jw_key(w, "text/plain");
    char a[24], z[24];
    py_api->fmt_num(xmin, 10, a, sizeof a);
    py_api->fmt_num(xmax, 10, z, sizeof z);
    py_api->snprintf(plain, sizeof plain, "<plot of %s for %s \xE2\x89\xA4 x \xE2\x89\xA4 %s>", disp, a, z);
    jw_str(w, plain);
    jw_obj_end(w);
    jw_key(w, "metadata");
    jw_obj(w);
    jw_key(w, "calculator");
    jw_obj(w);
    jw_key(w, "plot"), jw_str(w, cas);
    jw_key(w, "xmax"), jw_num(w, xmax);
    jw_key(w, "xmin"), jw_num(w, xmin);
    jw_obj_end(w);
    jw_obj_end(w);
    jw_key(w, "output_type");
    jw_str(w, "display_data");
    jw_obj_end(w);
}

/* A graph cell of the home screen (spec in NBF_GRAPH, app/gcell.c): its plot as the calculator draws
 * it (the core renders it, NB_PLOT_W x NB_PLOT_H), the colours indexed for the PNG. */
static uint8_t gpix[NB_PLOT_W * NB_PLOT_H];
static uint16_t gpal[256];
/* graph cell i's picture into gpix / gpal (np colours); NULL: none */
static const uint16_t *g_index(int i, int *npal)
{
    const uint16_t *rgb = nb_core->graph_image(i, NB_PLOT_W, NB_PLOT_H);
    int np = 0;
    for (int y = 0; rgb && y < NB_PLOT_H; y++)
        for (int x = 0; x < NB_PLOT_W; x++) {
            uint16_t c = rgb[y * 320 + x];
            int k = 0, best = 0;
            unsigned bd = ~0u;
            for (; k < np && gpal[k] != c; k++) {
                int dr = (c >> 11) - (gpal[k] >> 11), dg = ((c >> 5) & 63) - ((gpal[k] >> 5) & 63),
                    db = (c & 31) - (gpal[k] & 31);
                unsigned d = (unsigned)(dr * dr * 4 + dg * dg + db * db * 4);
                if (d < bd)
                    bd = d, best = k;
            }
            if (k == np && np < 256)
                gpal[np++] = c;
            else if (k == np)
                k = best;
            gpix[y * NB_PLOT_W + x] = (uint8_t)k;
        }
    *npal = np;
    return rgb;
}

/* `nb png ID`: graph cell ID's picture as the calculator draws it, a PNG in base64, png= lines of
 * up to 192 characters (the browser page shows 3D graph cells this way) */
static char png_buf[196];
static void png_line(void *u, const void *s, size_t n)
{
    size_t *col = u;
    const char *p = s;
    for (size_t k = 0; k < n; k++) {
        png_buf[(*col)++] = p[k];
        if (*col == 192)
            py_api->con_printf("png=%.*s\n", 192, png_buf), *col = 0;
    }
}
int nb_png_cell(int i)
{
    int np;
    if (i < 0 || i >= NB.ncells || NB.cell[i].kind != NBK_GRAPH || !NB.cell[i].f[NBF_GRAPH] || !g_index(i, &np))
        return py_api->con_error("no picture of cell %d", i + 1);
    size_t col = 0;
    b64_t b;
    b64_init(&b, png_line, &col);
    png_indexed(gpix, NB_PLOT_W, NB_PLOT_H, gpal, np, b64_put, &b);
    b64_end(&b);
    if (col)
        py_api->con_printf("png=%.*s\n", (int)col, png_buf);
    return 0;
}

static void w_plot_cell(jw_t *w, int i, const char *cas, double xmin, double xmax)
{
    int np;
    const uint16_t *rgb = g_index(i, &np);
    jw_obj(w);
    jw_key(w, "data");
    jw_obj(w);
    if (rgb) {
        jw_key(w, "image/png");
        jw_str_begin(w);
        b64_t b;
        b64_init(&b, b64_to_json, w);
        png_indexed(gpix, NB_PLOT_W, NB_PLOT_H, gpal, np, b64_put, &b);
        b64_end(&b);
        jw_str_end(w);
    }
    jw_key(w, "text/plain");
    jw_str(w, "<graph>");
    jw_obj_end(w);
    jw_key(w, "metadata");
    jw_obj(w);
    jw_key(w, "calculator");
    jw_obj(w);
    jw_key(w, "plot"), jw_str(w, cas);
    jw_key(w, "xmax"), jw_num(w, xmax);
    jw_key(w, "xmin"), jw_num(w, xmin);
    jw_obj_end(w);
    jw_obj_end(w);
    jw_key(w, "output_type");
    jw_str(w, "display_data");
    jw_obj_end(w);
}

/* the spec's functions and window into the cell's metadata (sorted keys: functions, height, ...) */
static void w_spec_meta(jw_t *w, const char *spec, int part)
{
    if (part == 0) { /* animation (an animated cell: its "a" line), functions, height */
        const char *a = spec[0] == 'a' && spec[1] == ' ' ? spec : strstr(spec, "\na ");
        if (a) {
            static const char *const modes[3] = {"loop", "bounce", "once"};
            const char *q = a + (a[0] == '\n' ? 3 : 2);
            double v[4];
            for (int k = 0; k < 4; k++)
                v[k] = nb_strtod(q, &q);
            int m = (int)v[3];
            jw_key(w, "animation");
            jw_obj(w);
            jw_key(w, "mode"), jw_str(w, modes[m >= 0 && m < 3 ? m : 0]);
            jw_key(w, "seconds"), jw_num(w, v[2]);
            jw_key(w, "tmax"), jw_num(w, v[1]);
            jw_key(w, "tmin"), jw_num(w, v[0]);
            jw_key(w, "variable"), jw_str(w, "t");
            jw_obj_end(w);
        }
        jw_key(w, "functions");
        jw_arr(w);
        for (const char *t = spec; t && *t;) {
            const char *e = strchr(t, '\n');
            if (t[0] == 'f' && t[1] == ' ') {
                const char *q = t + 2;
                for (int k = 0; k < 3 && q; k++) {
                    q = strchr(q, ' ');
                    q = q ? q + 1 : NULL;
                }
                if (q) {
                    jw_str_begin(w);
                    jw_str_piece(w, q, e ? (size_t)(e - q) : strlen(q));
                    jw_str_end(w);
                }
            }
            t = e ? e + 1 : NULL;
        }
        jw_arr_end(w);
        const char *o = strstr(spec, "\no ");
        if (o) {
            const char *q = o + 3;
            for (int k = 0; k < 2 && q; k++) {
                q = strchr(q + 1, ' ');
            }
            if (q)
                jw_key(w, "height"), jw_int(w, atoi(q + 1));
        }
    } else { /* xmax xmin ymax ymin from "w xmin xmax ymin ymax ..." */
        double v[4] = {-10, 10, -10, 10};
        const char *q = spec[0] == 'w' ? spec + 1 : "";
        for (int k = 0; k < 4 && *q; k++) {
            const char *e;
            v[k] = nb_strtod(q, &e);
            if (e == q)
                break;
            q = e;
        }
        jw_key(w, "xmax"), jw_num(w, v[1]);
        jw_key(w, "xmin"), jw_num(w, v[0]);
        jw_key(w, "ymax"), jw_num(w, v[3]);
        jw_key(w, "ymin"), jw_num(w, v[2]);
    }
}

static void w_cell(jw_t *w, nb_cell_t *c)
{
    static char src[2048], lit[1200], cas[1024], expr[512];
    bool code = c->kind != NBK_TEXT;
    jw_obj(w);
    jw_key(w, "cell_type");
    jw_str(w, code ? "code" : c->flags & NBC_RAW ? "raw" : "markdown");
    if (code) {
        jw_key(w, "execution_count");
        if (c->count)
            jw_int(w, c->count);
        else
            jw_null(w);
    }
    jw_key(w, "id");
    jw_str(w, c->id);
    jw_key(w, "metadata");
    jw_obj(w);
    double xmin = c->xmin, xmax = c->xmax;
    const char *spec = c->kind == NBK_GRAPH ? c->f[NBF_GRAPH] : NULL;
    if (c->kind == NBK_MATH || c->kind == NBK_GRAPH) {
        jw_key(w, "calculator");
        jw_obj(w);
        if (spec)
            w_spec_meta(w, spec, 0);
        jw_key(w, "input");
        jw_str(w, nb_get(c, NBF_SRC));
        jw_key(w, "kind");
        jw_str(w, nb_kind_name(c->kind));
        if (spec) {
            jw_key(w, "spec"), jw_str(w, spec);
            nb_graph_parse(nb_get(c, NBF_SRC), expr, sizeof expr, &xmin, &xmax);
            w_spec_meta(w, spec, 1);
        } else if (c->kind == NBK_GRAPH) {
            nb_graph_parse(nb_get(c, NBF_SRC), expr, sizeof expr, &xmin, &xmax);
            jw_key(w, "xmax"), jw_num(w, xmax);
            jw_key(w, "xmin"), jw_num(w, xmin);
        }
        jw_obj_end(w);
    }
    jw_obj_end(w);
    const char *source = nb_get(c, NBF_SRC);
    if (c->kind == NBK_MATH) {
        to_cas(source, cas, sizeof cas);
        py_literal(cas, lit, sizeof lit);
        py_api->snprintf(src, sizeof src, "import calc\ncalc.eval(%s)", lit);
        source = src;
    } else if (c->kind == NBK_GRAPH) {
        to_cas(expr, cas, sizeof cas);
        py_literal(cas, lit, sizeof lit);
        char a[40], z[40];
        mp_format_float(xmin, a, sizeof a, 'g', 15, '\0');
        mp_format_float(xmax, z, sizeof z, 'g', 15, '\0');
        py_api->snprintf(src, sizeof src, "import calc\ncalc.plot(%s, %s, %s)", lit, a, z);
        source = src;
    }
    if (code) {
        jw_key(w, "outputs");
        jw_arr(w);
        if (c->f[NBF_OUT]) {
            jw_obj(w);
            jw_key(w, "name"), jw_str(w, "stdout");
            jw_key(w, "output_type"), jw_str(w, "stream");
            jw_key(w, "text"), lines_of(w, c->f[NBF_OUT], true);
            jw_obj_end(w);
        }
        if (spec)
            w_plot_cell(w, (int)(c - NB.cell), cas, xmin, xmax);
        else if (c->kind == NBK_GRAPH && (c->flags & NBC_RAN) && !c->f[NBF_ENAME])
            w_plot(w, cas, xmin, xmax);
        else if (c->f[NBF_PLOT])
            w_plot(w, c->f[NBF_PLOT], c->xmin, c->xmax);
        if (c->f[NBF_ENAME]) {
            jw_obj(w);
            jw_key(w, "ename"), jw_str(w, nb_get(c, NBF_ENAME));
            jw_key(w, "evalue"), jw_str(w, nb_get(c, NBF_EVALUE));
            jw_key(w, "output_type"), jw_str(w, "error");
            jw_key(w, "traceback"), lines_of(w, nb_get(c, NBF_TB), false);
            jw_obj_end(w);
        } else if (c->f[NBF_VAL]) {
            jw_obj(w);
            jw_key(w, "data");
            jw_obj(w);
            if (c->f[NBF_TEX]) {
                jw_key(w, "text/latex");
                jw_str_begin(w);
                jw_str_piece(w, "$", 1);
                jw_str_piece(w, c->f[NBF_TEX], strlen(c->f[NBF_TEX]));
                jw_str_piece(w, "$", 1);
                jw_str_end(w);
            }
            jw_key(w, "text/plain"), lines_of(w, c->f[NBF_VAL], true);
            jw_obj_end(w);
            jw_key(w, "execution_count"), jw_int(w, c->count ? c->count : 1);
            jw_key(w, "metadata"), jw_obj(w), jw_obj_end(w);
            jw_key(w, "output_type"), jw_str(w, "execute_result");
            jw_obj_end(w);
        }
        jw_arr_end(w);
    }
    jw_key(w, "source");
    lines_of(w, source, true);
    jw_obj_end(w);
}

int nb_write_json(nb_sink_fn put, void *u)
{
    jw_t w;
    jw_init(&w, put, u);
    jw_obj(&w);
    jw_key(&w, "cells");
    jw_arr(&w);
    for (int i = 0; i < NB.ncells; i++)
        w_cell(&w, &NB.cell[i]);
    jw_arr_end(&w);
    jw_key(&w, "metadata");
    jw_obj(&w);
    jw_key(&w, "calculator");
    jw_obj(&w);
    static char fw[48]; /* the running firmware's version (a core before NBIO_FW_VERSION: none) */
    if (py_pc->nb_io(NBIO_FW_VERSION, 0, fw, sizeof fw) < 0)
        fw[0] = 0;
    jw_key(&w, "firmware"), jw_str(&w, fw);
    jw_key(&w, "format"), jw_int(&w, 1);
    jw_obj_end(&w);
    jw_key(&w, "kernelspec");
    jw_obj(&w);
    jw_key(&w, "display_name"), jw_str(&w, "Python 3");
    jw_key(&w, "language"), jw_str(&w, "python");
    jw_key(&w, "name"), jw_str(&w, "python3");
    jw_obj_end(&w);
    jw_key(&w, "language_info");
    jw_obj(&w);
    jw_key(&w, "name"), jw_str(&w, "python");
    jw_obj_end(&w);
    jw_obj_end(&w);
    jw_key(&w, "nbformat"), jw_int(&w, 4);
    jw_key(&w, "nbformat_minor"), jw_int(&w, 5);
    jw_obj_end(&w);
    return (int)w.len;
}

/* ---------------------------------------------------------------- reading .ipynb */
enum { T_NONE = -1, T_CTYPE, T_ID, T_SRC, T_KIND, T_INPUT, T_OTYPE, T_ONAME, T_OTEXT, T_PLAIN, T_LATEX, T_ENAME,
       T_EVALUE, T_TB, T_OPLOT, T_IMAGE, T_SPEC };
typedef struct {
    int cell, tgt, tgt_d;
    bool tgt_arr, tgt_join;
    char ctype[16], kind[16], otype[24], oname[16];
    char *input, *spec;
    double xmin, xmax;
    bool have_x;
    char *o[T_IMAGE + 1]; /* an output's strings (T_OTEXT .. T_OPLOT) */
    double oxmin, oxmax;
    bool o_image;
    int nbformat, dropped, images;
    bool out_of_memory, truncated;
} bld_t;
#define SB_MAX (64u << 10)
static char sb[SB_MAX + 1];
static size_t sbn;

static void sb_add(bld_t *b, const char *s, size_t n)
{
    if (b->tgt == T_IMAGE)
        return; /* (an image: not kept) */
    if (sbn + n > SB_MAX) {
        n = SB_MAX - sbn;
        b->truncated = true;
    }
    memcpy(sb + sbn, s, n);
    sbn += n;
}

static void small(char *dst, size_t n)
{
    size_t k = sbn < n - 1 ? sbn : n - 1;
    memcpy(dst, sb, k);
    dst[k] = 0;
}

/* ANSI escape sequences (Jupyter's coloured tracebacks) removed in place */
static size_t strip_ansi(char *s, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == 0x1B && i + 1 < n && s[i + 1] == '[') {
            i += 2;
            while (i < n && !(s[i] >= 0x40 && s[i] <= 0x7E))
                i++;
            continue;
        }
        s[o++] = s[i];
    }
    return o;
}

static void commit(bld_t *b)
{
    int t = b->tgt;
    b->tgt = T_NONE;
    if (b->tgt_join && sbn && sb[sbn - 1] == '\n')
        sbn--;
    if (t == T_TB || t == T_EVALUE)
        sbn = strip_ansi(sb, sbn);
    sb[sbn] = 0;
    nb_cell_t *c = b->cell >= 0 ? &NB.cell[b->cell] : NULL;
    switch (t) {
    case T_CTYPE: small(b->ctype, sizeof b->ctype); break;
    case T_KIND: small(b->kind, sizeof b->kind); break;
    case T_OTYPE: small(b->otype, sizeof b->otype); break;
    case T_ONAME: small(b->oname, sizeof b->oname); break;
    case T_ID: {
        bool ok = sbn >= 1 && sbn < sizeof c->id;
        for (size_t i = 0; i < sbn && ok; i++)
            ok = (sb[i] >= '0' && sb[i] <= '9') || (sb[i] >= 'a' && sb[i] <= 'z') || (sb[i] >= 'A' && sb[i] <= 'Z') ||
                 sb[i] == '-' || sb[i] == '_';
        for (int i = 0; i < NB.ncells && ok; i++)
            ok = &NB.cell[i] == c || strcmp(NB.cell[i].id, sb);
        if (ok && c)
            memcpy(c->id, sb, sbn + 1);
        break;
    }
    case T_SRC:
        if (c) {
            nb_set(c, NBF_SRC, sb);
            if (sbn && !c->f[NBF_SRC])
                b->out_of_memory = true;
        }
        break;
    case T_INPUT:
        nb_free(b->input);
        b->input = nb_strdup(sb, sbn);
        break;
    case T_SPEC:
        nb_free(b->spec);
        b->spec = nb_strdup(sb, sbn);
        break;
    case T_IMAGE: b->o_image = true; break;
    default:
        if (t >= T_OTEXT && t <= T_OPLOT) {
            if (t == T_OTEXT && b->o[t]) { /* (text arrives once; keep the first) */
                break;
            }
            nb_free(b->o[t]);
            b->o[t] = nb_strdup(sb, sbn);
        }
    }
    sbn = 0;
}

static void want(bld_t *b, int ev, int t, int d)
{
    if (ev != JE_STR && ev != JE_ARR)
        return;
    b->tgt = t, b->tgt_d = d, b->tgt_arr = ev == JE_ARR, b->tgt_join = t == T_TB;
    sbn = 0;
}

static void begin_cell(bld_t *b)
{
    nb_cell_t *c = nb_insert(NB.ncells, NBK_PY);
    b->cell = c ? NB.ncells - 1 : -1;
    if (!c)
        b->dropped++;
    else
        c->id[0] = 0; /* (an id of its own, or a new one at the end) */
    b->ctype[0] = b->kind[0] = 0;
    nb_free(b->input);
    nb_free(b->spec);
    b->input = b->spec = NULL;
    b->have_x = false;
}

static void end_cell(bld_t *b)
{
    if (b->cell < 0)
        return;
    nb_cell_t *c = &NB.cell[b->cell];
    if (!c->id[0])
        new_id(c->id);
    if (!strcmp(b->ctype, "markdown") || !strcmp(b->ctype, "raw")) {
        c->kind = NBK_TEXT;
        if (b->ctype[0] == 'r')
            c->flags |= NBC_RAW;
        nb_clear_out(c);
    } else if (strcmp(b->ctype, "code")) {
        c->kind = NBK_TEXT; /* an unknown cell type: kept as text */
        nb_clear_out(c);
    } else if (b->input && (!strcmp(b->kind, "math") || !strcmp(b->kind, "graph"))) {
        c->kind = b->kind[0] == 'm' ? NBK_MATH : NBK_GRAPH;
        nb_set(c, NBF_SRC, b->input); /* the line as typed */
        if (b->kind[0] == 'g') {
            /* (the plot: its function and range from the output's metadata, drawn again here) */
            if (b->have_x)
                c->xmin = b->xmin, c->xmax = b->xmax;
            if (b->spec) /* the home screen's graph cell: its functions, window and style */
                nb_set(c, NBF_GRAPH, b->spec);
            else /* (another graph cell: one made of its functions and range) */
                nb_core->graph_set((int)(c - NB.cell), NULL);
        }
    }
    if (c->count || c->f[NBF_OUT] || c->f[NBF_VAL] || c->f[NBF_ENAME] || c->f[NBF_PLOT])
        c->flags |= NBC_RAN;
    c->h = -1;
    b->cell = -1;
}

static void end_output(bld_t *b)
{
    if (b->cell < 0)
        return;
    nb_cell_t *c = &NB.cell[b->cell];
    const char *ot = b->otype;
    if (!strcmp(ot, "stream") && b->o[T_OTEXT]) {
        nb_append(c, NBF_OUT, b->o[T_OTEXT], strlen(b->o[T_OTEXT]));
    } else if (!strcmp(ot, "execute_result")) {
        nb_set(c, NBF_VAL, b->o[T_PLAIN]);
        if (b->o[T_LATEX]) { /* without the $ or $$ around it */
            char *s = b->o[T_LATEX];
            size_t n = strlen(s), a = 0;
            while (a < n && a < 2 && s[a] == '$')
                a++;
            while (n > a && s[n - 1] == '$')
                n--;
            s[n] = 0;
            nb_set(c, NBF_TEX, s + a);
        }
    } else if (!strcmp(ot, "display_data")) {
        if (b->o[T_OPLOT]) { /* a plot drawn here: drawn again from its function */
            nb_set(c, NBF_PLOT, b->o[T_OPLOT]);
            c->xmin = b->oxmin, c->xmax = b->oxmax;
        } else if (b->o_image) {
            c->flags |= NBC_IMAGE;
            b->images++;
        } else if (b->o[T_PLAIN]) {
            nb_append(c, NBF_OUT, b->o[T_PLAIN], strlen(b->o[T_PLAIN]));
            nb_append(c, NBF_OUT, "\n", 1);
        }
    } else if (!strcmp(ot, "error")) {
        nb_set(c, NBF_ENAME, b->o[T_ENAME]);
        nb_set(c, NBF_EVALUE, b->o[T_EVALUE]);
        nb_set(c, NBF_TB, b->o[T_TB]);
    }
    for (int t = T_OTEXT; t <= T_OPLOT; t++) {
        nb_free(b->o[t]);
        b->o[t] = NULL;
    }
    b->o_image = false;
    b->oxmin = -10, b->oxmax = 10;
    b->otype[0] = b->oname[0] = 0;
}

static double num_of(const char *s) { return nb_strtod(s, NULL); }

static void bev(jp_t *p, int e, const char *s, size_t n)
{
    bld_t *b = p->u;
    int d = p->depth;
    if (b->tgt != T_NONE) {
        if (e == JE_STR_PIECE) {
            sb_add(b, s, n);
            return;
        }
        if (e == JE_STR)
            return;
        if (e == JE_STR_END) {
            if (!b->tgt_arr)
                commit(b);
            else if (b->tgt_join)
                sb_add(b, "\n", 1);
            return;
        }
        if (b->tgt_arr) {
            if (e == JE_ARR_END && d == b->tgt_d)
                commit(b);
            return;
        }
    }
    if (d == 1 && e == JE_NUM && !strcmp(p->key[0], "nbformat"))
        b->nbformat = atoi(s);
    if (d < 2 || strcmp(p->key[0], "cells"))
        return;
    if (d == 2) {
        if (e == JE_OBJ)
            begin_cell(b);
        else if (e == JE_OBJ_END)
            end_cell(b);
        return;
    }
    if (b->cell < 0)
        return;
    const char *k2 = p->key[2];
    if (d == 3) {
        if (!strcmp(k2, "cell_type"))
            want(b, e, T_CTYPE, d);
        else if (!strcmp(k2, "id"))
            want(b, e, T_ID, d);
        else if (!strcmp(k2, "source"))
            want(b, e, T_SRC, d);
        else if (!strcmp(k2, "execution_count") && e == JE_NUM)
            NB.cell[b->cell].count = (int16_t)atoi(s);
        return;
    }
    if (!strcmp(k2, "metadata")) {
        if (d == 5 && !strcmp(p->key[3], "calculator")) {
            const char *k = p->key[4];
            if (!strcmp(k, "kind"))
                want(b, e, T_KIND, d);
            else if (!strcmp(k, "input"))
                want(b, e, T_INPUT, d);
            else if (!strcmp(k, "spec"))
                want(b, e, T_SPEC, d);
            else if (e == JE_NUM && !strcmp(k, "xmin"))
                b->xmin = num_of(s), b->have_x = true;
            else if (e == JE_NUM && !strcmp(k, "xmax"))
                b->xmax = num_of(s);
        }
        return;
    }
    if (strcmp(k2, "outputs"))
        return;
    if (d == 4) {
        if (e == JE_OBJ_END)
            end_output(b);
        return;
    }
    const char *k4 = p->key[4];
    if (d == 5) {
        static const struct {
            char key[16];
            int t;
        } f[] = {{"output_type", T_OTYPE}, {"name", T_ONAME}, {"text", T_OTEXT}, {"ename", T_ENAME},
                 {"evalue", T_EVALUE}, {"traceback", T_TB}};
        for (size_t i = 0; i < sizeof f / sizeof f[0]; i++)
            if (!strcmp(k4, f[i].key))
                want(b, e, f[i].t, d);
        return;
    }
    if (d == 6 && !strcmp(k4, "data")) {
        const char *mime = p->key[5];
        if (!strcmp(mime, "text/plain"))
            want(b, e, T_PLAIN, d);
        else if (!strcmp(mime, "text/latex"))
            want(b, e, T_LATEX, d);
        else if (!strncmp(mime, "image/", 6))
            want(b, e, T_IMAGE, d);
        return;
    }
    if (d == 7 && !strcmp(k4, "metadata") && !strcmp(p->key[5], "calculator")) {
        const char *k = p->key[6];
        if (!strcmp(k, "plot"))
            want(b, e, T_OPLOT, d);
        else if (e == JE_NUM && !strcmp(k, "xmin"))
            b->oxmin = num_of(s);
        else if (e == JE_NUM && !strcmp(k, "xmax"))
            b->oxmax = num_of(s);
    }
}

/* Read a notebook into NB (named `name`) from a byte source; 0, or <0 with the reason. */
static int read_doc(int (*rd)(void *, uint8_t *, size_t), void *ru, const char *name, int where, uint32_t max, char *msg,
                    size_t n)
{
    nb_new_doc(name, where);
    static bld_t b;
    memset(&b, 0, sizeof b);
    b.cell = -1, b.tgt = T_NONE, b.oxmin = -10, b.oxmax = 10;
    static jp_t p;
    memset(&p, 0, sizeof p);
    p.read = rd, p.ru = ru, p.ev = bev, p.u = &b, p.max = max;
    sbn = 0;
    int r = jp_parse(&p);
    if (r) {
        py_api->snprintf(msg, n, "%s", p.err);
    } else if (b.nbformat != 4) {
        py_api->snprintf(msg, n, b.nbformat ? "nbformat %d is not supported (only nbformat 4)"
                                            : "not a notebook (no nbformat)", b.nbformat);
        r = -1;
    }
    if (r) {
        nb_close_doc();
        return -1;
    }
    NB.note[0] = 0;
    if (b.dropped || b.images || b.truncated || b.out_of_memory)
        py_api->snprintf(NB.note, sizeof NB.note, "%s%s%s%s", b.dropped ? "too many cells: some were left out. " : "",
                         b.images ? "images are not shown. " : "", b.truncated ? "very long texts were cut. " : "",
                         b.out_of_memory ? "out of memory: some text was lost." : "");
    for (int i = 0; i < NB.ncells; i++)
        if (NB.cell[i].count > NB.counter)
            NB.counter = NB.cell[i].count;
    msg[0] = 0;
    return 0;
}
int nb_read_doc(int (*rd)(void *, uint8_t *, size_t), void *ru, const char *name, int where, uint32_t max, char *msg,
                size_t n)
{
    return read_doc(rd, ru, name, where, max, msg, n);
}

/* ---------------------------------------------------------------- the store */
static int q_read(void *u, uint32_t off, void *buf, size_t n)
{
    (void)u;
    return py_pc->nb_io(NBIO_READ, off, buf, n);
}
static int q_erase(void *u, uint32_t off, size_t n)
{
    (void)u;
    return py_pc->nb_io(NBIO_ERASE, off, NULL, n);
}
static int q_write(void *u, uint32_t off, const void *buf, size_t n)
{
    (void)u;
    return py_pc->nb_io(NBIO_WRITE, off, (void *)buf, n);
}
static nbs_dev_t qdev = {q_read, q_erase, q_write, NULL, 0};
static nbs_t store;
static bool store_tried;
static nbs_wr_t wr;

int nb_store_mount(void)
{
    if (store_tried && store.mounted)
        return 0;
    store_tried = true;
    qdev.size = (uint32_t)py_pc->nb_io(NBIO_STORE_SIZE, 0, NULL, 0);
    return nbs_mount(&store, qdev.size ? &qdev : NULL);
}
nbs_t *nb_store(void) { return &store; }

static const char *store_error(int e)
{
    switch (e) {
    case NBS_E_FULL: return "the notebook store is full: delete a notebook";
    case NBS_E_SIZE: return "too large (the limit is 256 KB): clear some outputs";
    case NBS_E_NOTFOUND: return "no such notebook";
    case NBS_E_CRC: return "the stored copy is damaged";
    case NBS_E_NODEV: return "the QSPI flash is not available";
    case NBS_E_NAME: return "a name of 1 to 35 letters, digits and spaces";
    default: return "the flash could not be written";
    }
}

static void card_path(const char *name, char *out, size_t n) { py_api->snprintf(out, n, "/CALC/NOTEBOOKS/%s.ipynb", name); }

/* the card: a file written through the module API's one-file slot */
typedef struct {
    uint8_t b[512];
    size_t n;
    int err;
} cardw_t;
static void card_put(void *u, const void *s, size_t n)
{
    cardw_t *c = u;
    const uint8_t *p = s;
    while (n && !c->err) {
        size_t k = sizeof c->b - c->n < n ? sizeof c->b - c->n : n;
        memcpy(c->b + c->n, p, k);
        c->n += k, p += k, n -= k;
        if (c->n == sizeof c->b) {
            c->err = py_api->file_write(c->b, c->n);
            c->n = 0;
        }
    }
}
static int card_read(void *u, uint8_t *buf, size_t n)
{
    (void)u;
    return py_api->file_read(buf, n);
}

int nb_save(char *msg, size_t n)
{
    msg[0] = 0;
    if (!NB.open)
        return 0;
    size_t size = 0;
    nb_write_json(sink_count, &size);
    if (size > NBS_MAX_BYTES) {
        py_api->snprintf(msg, n, "Not saved: %s", store_error(NBS_E_SIZE));
        return -1;
    }
    if (NB.where == 1) {
        char path[80];
        card_path(NB.name, path, sizeof path);
        int r = py_pc->nb_io(NBIO_SD_CREATE, 0, path, 0);
        if (r) {
            char e[64];
            py_api->card_error(r, e, sizeof e);
            py_api->snprintf(msg, n, "Not saved: %s", e);
            return -1;
        }
        static cardw_t cw;
        cw.n = 0, cw.err = 0;
        nb_write_json(card_put, &cw);
        if (!cw.err && cw.n)
            cw.err = py_api->file_write(cw.b, cw.n);
        r = py_api->file_close(!cw.err);
        if (cw.err || r) {
            char e[64];
            py_api->card_error(cw.err ? cw.err : r, e, sizeof e);
            py_api->snprintf(msg, n, "Not saved: %s", e);
            return -1;
        }
    } else {
        int r = nb_store_mount();
        if (!r)
            r = nbs_create(&store, NB.name, (uint32_t)size, &wr);
        if (!r) {
            nb_write_json(nbs_put, &wr);
            r = nbs_close(&wr);
        } else {
            nbs_close(&wr);
        }
        if (r) {
            py_api->snprintf(msg, n, "Not saved: %s", store_error(r));
            return -1;
        }
    }
    NB.dirty = false;
    if (!nb_sync_defer_refresh())
        py_pc->nb_io(NBIO_USB_REFRESH, 0, NULL, 0);
    return 0;
}

/* an example notebook (nb_examples.c) read from memory */
typedef struct {
    const char *p;
    uint32_t left;
} mem_rd_t;
static int mem_read(void *u, uint8_t *buf, size_t n)
{
    mem_rd_t *m = u;
    size_t k = n < m->left ? n : m->left;
    memcpy(buf, m->p, k);
    m->p += k, m->left -= (uint32_t)k;
    return (int)k;
}
static const nb_example_t *example_named(const char *name)
{
    for (int i = 0; i < nb_nexamples; i++)
        if (nbs_name_same(nb_examples[i].name, name))
            return &nb_examples[i];
    return NULL;
}

int nb_load(const char *name, int where, char *msg, size_t n)
{
    char note[96] = "";
    if (where == NB_EXAMPLE) { /* a notebook of the store from now on: saved there at the first change */
        const nb_example_t *ex = example_named(name);
        if (!ex) {
            py_api->snprintf(msg, n, "there is no example named %s", name);
            return -1;
        }
        mem_rd_t m = {ex->json, ex->len};
        return read_doc(mem_read, &m, ex->name, 0, ex->len + 1, msg, n);
    }
    if (where == 1) {
        char path[80];
        uint32_t size;
        card_path(name, path, sizeof path);
        int r = py_api->file_open(path, &size);
        if (r) {
            char e[64];
            py_api->card_error(r, e, sizeof e);
            py_api->snprintf(msg, n, "%s", e);
            return -1;
        }
        r = read_doc(card_read, NULL, name, 1, 4u << 20, msg, n);
        py_api->file_close(false);
        return r;
    }
    int r = nb_store_mount();
    nbs_rd_t rd;
    for (int which = 0;; which++) {
        if (!r)
            r = nbs_open(&store, name, which, &rd);
        if (r) {
            py_api->snprintf(msg, n, "%s", store_error(which ? NBS_E_CRC : r));
            return -1;
        }
        if (!read_doc(nbs_read, &rd, name, 0, NBS_MAX_BYTES, msg, n))
            break;
        if (!rd.err)
            return -1; /* (bad JSON: not a storage fault) */
        py_api->snprintf(note, sizeof note, "the newest copy was damaged: its previous version was opened");
    }
    if (note[0])
        py_api->snprintf(NB.note, sizeof NB.note, "%s", note);
    return 0;
}

/* ---------------------------------------------------------------- the list */
typedef struct {
    nb_item_t *out;
    int n, max;
} list_ctx_t;
static int card_cb(const char *name, uint32_t size, void *u)
{
    list_ctx_t *c = u;
    size_t k = strlen(name);
    if (k <= 6 || k - 6 >= NBS_NAME || strcmp(name + k - 6, ".ipynb") || name[0] == '.' || c->n >= c->max)
        return 0;
    nb_item_t *it = &c->out[c->n++];
    memset(it, 0, sizeof *it);
    memcpy(it->name, name, k - 6);
    it->size = size, it->where = 1, it->versions = 1;
    return 0;
}

int nb_list(nb_item_t *out, int max)
{
    int n = 0;
    if (!nb_store_mount()) {
        static nbs_entry_t e[64];
        int k = nbs_list(&store, e, max < 64 ? max : 64);
        for (int i = 0; i < k; i++, n++) {
            memset(&out[n], 0, sizeof out[n]);
            memcpy(out[n].name, e[i].name, NBS_NAME);
            out[n].size = e[i].size, out[n].where = 0, out[n].versions = (uint8_t)e[i].versions;
        }
    }
    list_ctx_t c = {out, n, max};
    nbio_list_t l = {"/CALC/NOTEBOOKS", card_cb, &c};
    py_pc->nb_io(NBIO_SD_LIST, 0, &l, 0);
    return c.n;
}

int nb_list_all(nb_item_t *out, int max)
{
    int n = nb_list(out, max), k = n;
    for (int e = 0; e < nb_nexamples && n < max; e++) {
        bool saved = false; /* (changed and saved: the store's copy is listed instead) */
        for (int j = 0; j < k && !saved; j++)
            saved = out[j].where == 0 && nbs_name_same(out[j].name, nb_examples[e].name);
        if (saved)
            continue;
        memset(&out[n], 0, sizeof out[n]);
        py_api->snprintf(out[n].name, sizeof out[n].name, "%s", nb_examples[e].name);
        out[n].size = nb_examples[e].len, out[n].where = NB_EXAMPLE, out[n].versions = 1;
        n++;
    }
    return n;
}

void nb_default_name(char *out, size_t n)
{
    static nb_item_t items[80];
    int k = nb_list(items, 80);
    for (int i = 1;; i++) {
        py_api->snprintf(out, n, "Notebook %d", i);
        bool used = false;
        for (int j = 0; j < k && !used; j++) {
            const char *a = items[j].name, *b = out;
            while (*a && (*a | 32) == (*b | 32))
                a++, b++;
            used = !*a && !*b;
        }
        if (!used)
            return;
    }
}

/* a notebook of this name (any case) in the store (where 0) or on the card (1) */
static bool exists(const char *name, int where)
{
    static nb_item_t items[80];
    int k = nb_list(items, 80);
    for (int j = 0; j < k; j++) {
        const char *a = items[j].name, *b = name;
        while (*a && (*a | 32) == (*b | 32))
            a++, b++;
        if (!*a && !*b && items[j].where == where)
            return true;
    }
    return false;
}

int nb_remove(const char *name, int where, char *msg, size_t n)
{
    int r;
    if (where == NB_EXAMPLE) {
        py_api->snprintf(msg, n, "%s is a built-in example: it cannot be deleted", name);
        return -1;
    }
    if (where == 1) {
        char path[80];
        card_path(name, path, sizeof path);
        r = py_pc->nb_io(NBIO_SD_REMOVE, 0, path, 0);
        if (r)
            py_api->card_error(r, msg, n);
    } else {
        r = nb_store_mount();
        if (!r)
            r = nbs_delete(&store, name);
        if (r)
            py_api->snprintf(msg, n, "%s", store_error(r));
        else
            py_pc->nb_io(NBIO_USB_REFRESH, 0, NULL, 0);
    }
    if (!r && NB.open && NB.where == where && !strcmp(NB.name, name))
        nb_close_doc();
    return r ? -1 : 0;
}

int nb_rename(const char *from, int where, const char *to, char *msg, size_t n)
{
    if (where == NB_EXAMPLE) {
        py_api->snprintf(msg, n, "%s is a built-in example: open it, then save it under another name", from);
        return -1;
    }
    if (!nbs_name_ok(to)) {
        py_api->snprintf(msg, n, "%s", store_error(NBS_E_NAME));
        return -1;
    }
    bool was_open = NB.open && NB.where == where && !strcmp(NB.name, from);
    if (exists(to, where) && !nbs_name_same(from, to)) {
        py_api->snprintf(msg, n, "there is a notebook named %s already", to);
        return -1;
    }
    if (where == 1 || was_open) { /* through memory: load, save under the new name, remove */
        if (!was_open) {
            nb_core->stash(); /* (the open notebook comes back afterwards) */
            if (nb_load(from, where, msg, n)) {
                nb_core->unstash();
                return -1;
            }
        }
        py_api->snprintf(NB.name, sizeof NB.name, "%s", to);
        int r = nb_save(msg, n);
        if (!r) {
            char m2[80];
            NB.open = false; /* (nb_remove closes the notebook of that name) */
            nb_remove(from, where, m2, sizeof m2);
            NB.open = true;
        }
        if (!was_open)
            nb_core->unstash();
        return r ? -1 : 0;
    }
    int r = nb_store_mount();
    if (!r)
        r = nbs_copy(&store, from, 0, to, &wr);
    if (!r)
        r = nbs_delete(&store, from);
    if (r) {
        py_api->snprintf(msg, n, "%s", store_error(r));
        return -1;
    }
    py_pc->nb_io(NBIO_USB_REFRESH, 0, NULL, 0);
    return 0;
}

int nb_revert(const char *name, char *msg, size_t n)
{
    int r = nb_store_mount();
    if (!r)
        r = nbs_copy(&store, name, 1, name, &wr);
    if (r) {
        py_api->snprintf(msg, n, "%s", r == NBS_E_NOTFOUND ? "there is no previous version" : store_error(r));
        return -1;
    }
    py_pc->nb_io(NBIO_USB_REFRESH, 0, NULL, 0);
    return 0;
}

int nb_copy_to(const char *name, int where, int to, char *msg, size_t n)
{
    if (NB.open && NB.dirty)
        nb_save(msg, n);
    nb_core->stash(); /* the open notebook (or the home screen's unsaved one) comes back afterwards */
    int r = nb_load(name, where, msg, n);
    if (!r) {
        NB.where = to;
        r = nb_save(msg, n);
    }
    nb_core->unstash();
    return r;
}

/* ---------------------------------------------------------------- running */
static int running = -1; /* the cell running (-1 none) */
static int plot_cell = -1; /* calc.plot() goes to this cell */
bool nb_running(void) { return running >= 0; }
int nb_running_cell(void) { return running; }

bool nb_plot_hook(const char *expr, double xmin, double xmax)
{
    if (plot_cell < 0 || plot_cell >= NB.ncells)
        return false;
    nb_cell_t *c = &NB.cell[plot_cell];
    nb_set(c, NBF_PLOT, expr);
    c->xmin = xmin, c->xmax = xmax;
    return true;
}

static char outbuf[16 << 10];

/* the last line of a traceback: "ValueError: math domain error" -> its message */
static void evalue_of(const char *tb, const char *ename, char *out, size_t n)
{
    const char *last = tb, *p = tb;
    for (; *p; p++)
        if (*p == '\n' && p[1])
            last = p + 1;
    size_t k = strlen(ename);
    if (!strncmp(last, ename, k) && last[k] == ':')
        last += k + 1 + (last[k + 1] == ' ');
    else if (!strncmp(last, ename, k))
        last += k;
    py_api->snprintf(out, n, "%s", last);
    size_t l = strlen(out);
    while (l && (out[l - 1] == '\n' || out[l - 1] == ' '))
        out[--l] = 0;
}

/* ---- Ans and Out[n] (app/nbdoc.h; docs/NOTEBOOK.md) ----
 * Math cells: the core replaces them in the CAS text (NBIO_REFS), before the cell's own result is
 * cleared, as on the home screen. Python cells: the globals Ans (the latest result) and Out (a
 * read-only mapping, execution count -> value) are set before each cell runs. A math result is a
 * calc.Expr of its CAS text; a Python value is the object itself for a cell run since the
 * interpreter started (Out keeps it), else rebuilt from its text: a calc.Expr when it was one, a
 * literal (numbers, strings, lists, tuples, dicts, True, False, None); otherwise it is not there. */
static char math_cas[4096]; /* the math cell about to run: its CAS text, or why it cannot run */
static int math_err;

static void math_prepare(nb_cell_t *c)
{
    static char cas[2048];
    to_cas(nb_get(c, NBF_SRC), cas, sizeof cas);
    nbio_text_t t = {cas, math_cas, sizeof math_cas};
    int r = py_pc->nb_io(NBIO_REFS, 0, &t, 0);
    math_err = r == 1;
    if (r && r != 1)
        py_api->snprintf(math_cas, sizeof math_cas, "%s", cas);
}

static bool has_value(const nb_cell_t *c)
{
    return (c->kind == NBK_MATH || c->kind == NBK_PY) && c->count > 0 && !c->f[NBF_ENAME] && c->f[NBF_VAL];
}
/* the cell of that execution count with a value (count 0: the latest result), -1 */
static int result_cell(int count)
{
    int best = -1;
    for (int i = 0; i < NB.ncells; i++) {
        const nb_cell_t *c = &NB.cell[i];
        if (has_value(c) && (count > 0 ? c->count == count : best < 0 || c->count >= NB.cell[best].count))
            best = i;
    }
    return best;
}

static bool idc(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9'); }
/* a Python literal (no names but True False None, so nothing runs): its value, else NULL */
static mp_obj_t literal(const char *s)
{
    for (const char *p = s; *p;) {
        if (*p == '\'' || *p == '"') {
            char q = *p++;
            while (*p && *p != q)
                p += p[0] == '\\' && p[1] ? 2 : 1;
            p += *p != 0;
        } else if (idc(*p) && !(*p >= '0' && *p <= '9')) {
            const char *w = p;
            bool num = w > s && ((w[-1] >= '0' && w[-1] <= '9') || w[-1] == '.'); /* (1e5, 2j, 0x1f) */
            while (idc(*p))
                p++;
            size_t l = (size_t)(p - w);
            if (!num && !(l == 4 && (!strncmp(w, "True", 4) || !strncmp(w, "None", 4))) &&
                !(l == 5 && !strncmp(w, "False", 5)))
                return MP_OBJ_NULL;
        } else {
            p++;
        }
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_string_gt_, s, strlen(s), 0);
        mp_obj_dict_t *g = mp_obj_new_dict(0);
        mp_obj_t v = mp_parse_compile_execute(lex, MP_PARSE_EVAL_INPUT, g, g);
        nlr_pop();
        return v;
    }
    return MP_OBJ_NULL;
}

typedef struct {
    mp_obj_base_t base;
    mp_obj_t kept; /* dict: count -> (cell id, value) of the Python cells run since the interpreter started */
} outmap_t;
extern const mp_obj_type_t nb_type_out;

/* the value of cell i, or NULL (*why: why not) */
static mp_obj_t cell_value(outmap_t *m, int i, const char **why)
{
    const nb_cell_t *c = &NB.cell[i];
    *why = "no such result";
    if (c->kind == NBK_MATH) {
        static char cas[4096];
        nbio_text_t t = {NULL, cas, sizeof cas};
        if (py_pc->nb_io(NBIO_VALUE_CAS, (uint32_t)i, &t, 0)) {
            *why = "the result is too long";
            return MP_OBJ_NULL;
        }
        return py_expr_new(cas);
    }
    mp_map_elem_t *e =
        m ? mp_map_lookup(mp_obj_dict_get_map(m->kept), MP_OBJ_NEW_SMALL_INT(c->count), MP_MAP_LOOKUP) : NULL;
    if (e) {
        size_t n;
        mp_obj_t *kv;
        mp_obj_tuple_get(e->value, &n, &kv);
        if (!strcmp(mp_obj_str_get_str(kv[0]), c->id))
            return kv[1];
    }
    if (c->f[NBF_TEX]) /* (a calc.Expr: its text is CAS text) */
        return py_expr_new(nb_get(c, NBF_VAL));
    mp_obj_t v = literal(nb_get(c, NBF_VAL));
    if (v == MP_OBJ_NULL)
        *why = "its value is not kept: run that cell again";
    return v;
}

static mp_obj_t out_get(mp_obj_t self, mp_obj_t key, bool raise)
{
    const char *why = "no such result";
    int k = mp_obj_is_int(key) ? mp_obj_get_int(key) : 0;
    int i = k > 0 ? result_cell(k) : -1;
    if (i >= 0) {
        mp_obj_t v = cell_value(MP_OBJ_TO_PTR(self), i, &why);
        if (v != MP_OBJ_NULL)
            return v;
    }
    if (!raise)
        return MP_OBJ_NULL;
    mp_raise_msg_varg(&mp_type_KeyError, MP_ERROR_TEXT("Out[%d]: %s"), k, why);
}

/* the results there are, as a dict (printing, len, keys, values, items, iterating) */
static mp_obj_t out_dict(mp_obj_t self)
{
    mp_obj_t d = mp_obj_new_dict(0);
    for (int i = 0; i < NB.ncells; i++)
        if (has_value(&NB.cell[i]) && result_cell(NB.cell[i].count) == i) {
            const char *why;
            mp_obj_t v = cell_value(MP_OBJ_TO_PTR(self), i, &why);
            if (v != MP_OBJ_NULL)
                mp_obj_dict_store(d, MP_OBJ_NEW_SMALL_INT(NB.cell[i].count), v);
        }
    return d;
}

static mp_obj_t out_subscr(mp_obj_t self, mp_obj_t index, mp_obj_t value)
{
    if (value != MP_OBJ_SENTINEL)
        return MP_OBJ_NULL; /* read-only */
    return out_get(self, index, true);
}
static mp_obj_t out_binary_op(mp_binary_op_t op, mp_obj_t lhs, mp_obj_t rhs)
{
    if (op == MP_BINARY_OP_CONTAINS)
        return mp_obj_new_bool(out_get(lhs, rhs, false) != MP_OBJ_NULL);
    return MP_OBJ_NULL;
}
static mp_obj_t out_unary_op(mp_unary_op_t op, mp_obj_t self)
{
    if (op == MP_UNARY_OP_LEN)
        return mp_obj_len(out_dict(self));
    if (op == MP_UNARY_OP_BOOL)
        return mp_obj_new_bool(result_cell(0) >= 0);
    return MP_OBJ_NULL;
}
static void out_print(const mp_print_t *print, mp_obj_t self, mp_print_kind_t kind)
{
    mp_obj_print_helper(print, out_dict(self), kind);
}
static mp_obj_t out_getiter(mp_obj_t self, mp_obj_iter_buf_t *iter_buf)
{
    (void)iter_buf;
    return mp_getiter(out_dict(self), NULL);
}
static mp_obj_t out_method(mp_obj_t self, qstr name) { return mp_call_function_0(mp_load_attr(out_dict(self), name)); }
static mp_obj_t out_keys(mp_obj_t self) { return out_method(self, MP_QSTR_keys); }
static mp_obj_t out_values(mp_obj_t self) { return out_method(self, MP_QSTR_values); }
static mp_obj_t out_items(mp_obj_t self) { return out_method(self, MP_QSTR_items); }
static mp_obj_t out_getm(size_t n_args, const mp_obj_t *args)
{
    mp_obj_t v = out_get(args[0], args[1], false);
    return v != MP_OBJ_NULL ? v : n_args > 2 ? args[2] : mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(out_keys_obj, out_keys);
static MP_DEFINE_CONST_FUN_OBJ_1(out_values_obj, out_values);
static MP_DEFINE_CONST_FUN_OBJ_1(out_items_obj, out_items);
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(out_getm_obj, 2, 3, out_getm);
static const mp_rom_map_elem_t out_locals_table[] = {
    {MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&out_getm_obj)},
    {MP_ROM_QSTR(MP_QSTR_keys), MP_ROM_PTR(&out_keys_obj)},
    {MP_ROM_QSTR(MP_QSTR_values), MP_ROM_PTR(&out_values_obj)},
    {MP_ROM_QSTR(MP_QSTR_items), MP_ROM_PTR(&out_items_obj)},
};
static MP_DEFINE_CONST_DICT(out_locals, out_locals_table);
MP_DEFINE_CONST_OBJ_TYPE(nb_type_out, MP_QSTR_Out, MP_TYPE_FLAG_ITER_IS_GETITER, print, out_print, unary_op,
                         out_unary_op, binary_op, out_binary_op, subscr, out_subscr, iter, out_getiter, locals_dict,
                         &out_locals);

/* the interpreter's Out (made when there is none, or the name was given to something else) */
static outmap_t *out_map(void)
{
    mp_map_elem_t *e = mp_map_lookup(&mp_globals_get()->map, MP_OBJ_NEW_QSTR(MP_QSTR_Out), MP_MAP_LOOKUP);
    if (e && mp_obj_is_type(e->value, &nb_type_out))
        return MP_OBJ_TO_PTR(e->value);
    outmap_t *m = mp_obj_malloc(outmap_t, &nb_type_out);
    m->kept = mp_obj_new_dict(0);
    mp_store_global(MP_QSTR_Out, MP_OBJ_FROM_PTR(m));
    return m;
}

/* before a Python cell runs: Out there, Ans the latest result (None: none) */
static void py_refs(void)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        outmap_t *m = out_map();
        int i = result_cell(0);
        const char *why;
        mp_obj_t v = i >= 0 ? cell_value(m, i, &why) : MP_OBJ_NULL;
        mp_store_global(MP_QSTR_Ans, v != MP_OBJ_NULL ? v : mp_const_none);
        nlr_pop();
    }
}

/* after: the value of Python cell c kept for Out (the object itself, not its text) */
static void py_keep(const nb_cell_t *c, mp_obj_t v)
{
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t kv[2] = {mp_obj_new_str(c->id, strlen(c->id)), v};
        mp_obj_dict_store(out_map()->kept, MP_OBJ_NEW_SMALL_INT(c->count), mp_obj_new_tuple(2, kv));
        nlr_pop();
    }
}

static int run_math(nb_cell_t *c)
{
    const char *cas = math_cas;
    if (math_err) {
        nb_set(c, NBF_ENAME, "CASError");
        nb_set(c, NBF_EVALUE, math_cas);
        py_api->snprintf(outbuf, sizeof outbuf, "CASError: %s", math_cas);
        nb_set(c, NBF_TB, outbuf);
        return 1;
    }
    if (!cas[strspn(cas, " ")])
        return 0;
    cas_status s = py_pc->eval(cas, outbuf, sizeof outbuf, py_stack_used());
    if (s == CAS_OK) {
        nb_set(c, NBF_VAL, outbuf);
        static char tex[4096];
        py_latex_text(outbuf, tex, sizeof tex);
        nb_set(c, NBF_TEX, tex);
        return 0;
    }
    bool intr = s == CAS_ERR_INTERRUPTED;
    if (intr)
        py_break_pending(); /* (the press that stopped it) */
    const char *msg = intr ? "" : py_pc->error();
    nb_set(c, NBF_ENAME, intr ? "KeyboardInterrupt" : "CASError");
    nb_set(c, NBF_EVALUE, msg);
    py_api->snprintf(outbuf, sizeof outbuf, "%s%s%s", intr ? "KeyboardInterrupt" : "CASError", msg[0] ? ": " : "", msg);
    nb_set(c, NBF_TB, outbuf);
    return 1;
}

static mp_obj_t py_val; /* (nb_run: the value of the Python cell just run, kept for Out) */
static int run_py(nb_cell_t *c, int i)
{
    const char *code = nb_get(c, NBF_SRC);
    int sinks = py_out_get();
    py_out_set(PY_OUT_CAPTURE);
    py_capture_reset();
    plot_cell = i;
    mp_obj_t v;
    int r = py_run(code, PY_RUN_CELL, &v);
    plot_cell = -1;
    py_out_set(sinks);
    size_t len, tb = py_capture_tb();
    const char *cap = py_capture(&len);
    if (r == PY_OK) {
        nb_set(c, NBF_OUT, len ? cap : NULL);
        if (v != MP_OBJ_NULL && v != mp_const_none) {
            py_val = v;
            py_value_text(v, outbuf, sizeof outbuf);
            nb_set(c, NBF_VAL, outbuf);
            py_value_latex(v, outbuf, sizeof outbuf);
            nb_set(c, NBF_TEX, outbuf);
        }
        return 0;
    }
    if (tb > len)
        tb = len;
    if (tb) {
        char *o = nb_strdup(cap, tb);
        nb_set(c, NBF_OUT, o);
        nb_free(o);
    }
    nb_set(c, NBF_TB, cap + tb);
    nb_set(c, NBF_ENAME, py_last_exc());
    evalue_of(cap + tb, py_last_exc(), outbuf, sizeof outbuf);
    nb_set(c, NBF_EVALUE, outbuf);
    return 1;
}

static int run_graph(nb_cell_t *c)
{
    static char expr[512], cas[1024], err[96];
    nb_graph_parse(nb_get(c, NBF_SRC), expr, sizeof expr, &c->xmin, &c->xmax);
    to_cas(expr, cas, sizeof cas);
    if (!cas[strspn(cas, " ")])
        return 0;
    nb_set(c, NBF_PLOT, cas);
    if (!nb_plot_get(cas, c->xmin, c->xmax, err, sizeof err)) {
        nb_set(c, NBF_PLOT, NULL);
        nb_set(c, NBF_ENAME, "GraphError");
        nb_set(c, NBF_EVALUE, err);
        py_api->snprintf(outbuf, sizeof outbuf, "GraphError: %s", err);
        nb_set(c, NBF_TB, outbuf);
        return 1;
    }
    return 0;
}

int nb_run(int i)
{
    if (i < 0 || i >= NB.ncells)
        return 0;
    nb_cell_t *c = &NB.cell[i];
    if (c->kind == NBK_TEXT)
        return 0;
    if (c->kind == NBK_GRAPH && c->f[NBF_GRAPH]) { /* the home screen's graph cell: the core draws it */
        c->count = (int16_t)++NB.counter;
        c->flags |= NBC_RAN;
        c->h = -1;
        nb_note(NBN_CELL, i);
        return 0;
    }
    if (c->kind == NBK_MATH) /* (Ans and Out[n] as they are before this cell's result is gone) */
        math_prepare(c);
    else if (c->kind == NBK_PY)
        py_refs();
    nb_clear_out(c);
    running = i;
    nb_note(NBN_RUN, i);
    nbui_progress(); /* "running" on the screen */
    py_val = MP_OBJ_NULL;
    int r = c->kind == NBK_MATH ? run_math(c) : c->kind == NBK_PY ? run_py(c, i) : run_graph(c);
    running = -1;
    c->count = (int16_t)++NB.counter;
    if (py_val != MP_OBJ_NULL)
        py_keep(c, py_val);
    py_val = MP_OBJ_NULL;
    c->flags |= NBC_RAN;
    c->h = -1;
    nb_note(NBN_CELL, i);
    nb_note(NBN_RUN, -1);
    return r;
}

int nb_run_all(void)
{
    py_break_pending(); /* (earlier presses do not count) */
    for (int i = 0; i < NB.ncells; i++) {
        if (nb_run(i))
            return 1;
        if (py_break_pending())
            return 1;
    }
    return 0;
}

/* A fresh interpreter. The numbering goes on from the notebook's highest execution count, so that
 * Out[n] keeps naming one cell (a notebook without results starts again at 1). */
void nb_restart_python(void)
{
    py_restart(false);
    NB.counter = 0;
    for (int i = 0; i < NB.ncells; i++)
        if (NB.cell[i].count > NB.counter)
            NB.counter = NB.cell[i].count;
    nb_note(NBN_META, -1);
}

/* ---------------------------------------------------------------- USB */
static uint32_t usb_off;
static int usb_read(void *u, uint8_t *buf, size_t n)
{
    (void)u;
    int r = py_pc->nb_io(NBIO_USB_READ, usb_off, buf, n);
    if (r > 0)
        usb_off += (uint32_t)r;
    return r;
}

static void usb_result(bool ok, const char *toast, const char *detail)
{
    static char t[400];
    py_api->snprintf(t, sizeof t, "%s\n%s", toast, detail);
    py_pc->nb_io(NBIO_USB_RESULT, ok, t, 0);
}

/* `nb usb`: every stored notebook for the drive's NOTEBOOKS folder */
static int usb_list(void)
{
    if (nb_store_mount())
        return 0;
    nbs_unpin(&store);
    static nbs_entry_t e[64];
    static uint32_t off[NBS_MAX_BYTES / NBS_DATA + 2];
    int k = nbs_list(&store, e, 64);
    for (int i = 0; i < k; i++) {
        uint32_t size;
        if (nbs_sectors(&store, e[i].name, off, (int)(sizeof off / sizeof off[0]), &size) < 0)
            continue;
        char fn[48];
        py_api->snprintf(fn, sizeof fn, "%s.ipynb", e[i].name);
        nbio_usb_file_t f = {fn, size, off};
        if (py_pc->nb_io(NBIO_USB_FILE, 0, &f, 0))
            break;
    }
    return 0;
}

/* `nb import FILE`: a notebook dropped on the drive */
static int usb_import(const char *file)
{
    char name[NBS_NAME], msg[160], detail[300];
    /* the name: the file's without .ipynb, made a valid notebook name */
    size_t k = strlen(file), o = 0;
    if (k > 6 && file[k - 6] == '.' && (file[k - 5] | 32) == 'i' && (file[k - 4] | 32) == 'p' &&
        (file[k - 3] | 32) == 'y' && (file[k - 2] | 32) == 'n' && (file[k - 1] | 32) == 'b')
        k -= 6;
    for (size_t i = 0; i < k && o < NBS_NAME - 1; i++) {
        char c = file[i];
        name[o++] = (c < 0x20 || c > 0x7E || strchr("/\\:*?\"<>|", c)) ? '_' : c;
    }
    name[o] = 0;
    while (o && (name[o - 1] == ' ' || name[o - 1] == '.'))
        name[--o] = 0;
    while (name[0] == ' ' || name[0] == '.')
        memmove(name, name + 1, strlen(name));
    if (!name[0])
        py_api->snprintf(name, sizeof name, "Imported");
    /* the open notebook is saved and comes back afterwards (the home screen's unsaved one too) */
    nbui_commit();
    if (NB.open && NB.dirty)
        nb_save(msg, sizeof msg);
    nb_core->stash();
    usb_off = 0;
    bool replaced = !nb_store_mount() && nbs_find(&store, name, 0) >= 0;
    int r = read_doc(usb_read, NULL, name, 0, 2u << 20, msg, sizeof msg);
    char toast[72];
    if (r) {
        py_api->snprintf(toast, sizeof toast, "Not a notebook: %.30s", file);
        py_api->snprintf(detail, sizeof detail, "The file could not be read as a Jupyter notebook: %s.", msg);
    } else {
        int ncells = NB.ncells;
        r = nb_save(msg, sizeof msg);
        if (r) {
            py_api->snprintf(toast, sizeof toast, "Notebook not imported");
            py_api->snprintf(detail, sizeof detail, "%s: %s", name, msg);
        } else {
            py_api->snprintf(toast, sizeof toast, "Imported %.40s", name);
            py_api->snprintf(detail, sizeof detail, "Notebook \"%s\": %d cells.%s%s%s%s", name, ncells,
                             replaced ? " It replaced the notebook of that name; Notebook > file > previous version "
                                        "brings that one back."
                                      : "",
                             NB.note[0] ? " Note: " : "", NB.note, "");
        }
    }
    nb_core->unstash();
    nbui_changed();
    nb_note(NBN_DOC, -1);
    nb_note(NBN_LIST, -1);
    usb_result(r == 0, toast, detail);
    return 0;
}

/* ---------------------------------------------------------------- console */
int nb_find_cell(const char *s)
{
    for (int i = 0; i < NB.ncells; i++)
        if (!strcmp(NB.cell[i].id, s))
            return i;
    char *e;
    long v = strtol(s, &e, 10);
    return !*e && v >= 1 && v <= NB.ncells ? (int)v - 1 : -1;
}
#define find_cell nb_find_cell

static void print_field(const char *k, const char *v)
{
    /* one line per line of the text: k=... */
    while (v && *v) {
        const char *nl = strchr(v, '\n');
        int n = nl ? (int)(nl - v) : (int)strlen(v);
        py_api->con_printf("%s=%.*s\n", k, n > 200 ? 200 : n, v);
        v = nl ? nl + 1 : v + n;
    }
}

static void con_sink(void *u, const void *s, size_t n)
{
    (void)u;
    const char *p = s;
    while (n) {
        int k = n > 200 ? 200 : (int)n;
        py_api->con_printf("%.*s", k, p);
        p += k, n -= (size_t)k;
    }
}

/* The home screen's notebook menu (app/home.c) through py_nb_call: `nb touched` (the core changed the
 * document), `runcell ID`, `saveas [NAME]`, `savenow`, `renameto NAME`, `export` (a copy on the card),
 * `listview` (the list of notebooks next). 0, or -1 with the reason in NB.msg; -2 not one of these. */
static int core_fail(const char *m)
{
    py_api->snprintf(NB.msg, sizeof NB.msg, "%s", m);
    return py_api->con_error("%s", m);
}
static int nb_core_cmd(const char *sub, const char *arg)
{
    static char msg[160];
    NB.msg[0] = 0;
    if (!strcmp(sub, "touched")) { /* (named or not: the browser page follows the home screen's notebook) */
        nb_note(NBN_ALL, -1);
        nbui_changed();
        return 0;
    }
    if (!strcmp(sub, "calcedit")) { /* the home screen edits cell ID in place (none: done) */
        int i = arg ? nb_find_cell(arg) : -1;
        if (i >= 0 && nb_page_edits(i))
            return core_fail("This cell is being edited in the browser.");
        nb_calc_edit(i);
        return 0;
    }
    if (!strcmp(sub, "editcell")) { /* ENTER on a text or Python cell of the home screen: the editor */
        int i = arg ? nb_find_cell(arg) : -1;
        if (i < 0)
            return core_fail("no such cell");
        if (nb_page_edits(i))
            return core_fail("This cell is being edited in the browser.");
        if (nbui_edit(i))
            return core_fail("This cell is too long to edit on the calculator.");
        return 0;
    }
    if (!strcmp(sub, "runcell")) {
        int i = arg ? nb_find_cell(arg) : -1;
        if (i < 0)
            return core_fail("no such cell");
        nb_run(i);
        nbui_changed();
        return 0;
    }
    if (!strcmp(sub, "saveas")) {
        char name[NBS_NAME];
        if (arg && arg[0])
            py_api->snprintf(name, sizeof name, "%s", arg);
        else
            nb_default_name(name, sizeof name);
        if (!nbs_name_ok(name))
            return core_fail(store_error(NBS_E_NAME));
        if (exists(name, 0) && !(NB.open && NB.where == 0 && nbs_name_same(NB.name, name))) {
            py_api->snprintf(msg, sizeof msg, "There is a notebook named %s already.", name);
            return core_fail(msg);
        }
        char old[NBS_NAME];
        bool was = NB.open;
        int wh = NB.where;
        py_api->snprintf(old, sizeof old, "%s", NB.name);
        py_api->snprintf(NB.name, sizeof NB.name, "%s", name);
        NB.where = 0, NB.open = true, NB.dirty = true;
        if (nb_save(msg, sizeof msg)) {
            py_api->snprintf(NB.name, sizeof NB.name, "%s", old);
            NB.open = was, NB.where = wh;
            return core_fail(msg);
        }
        nbui_changed();
        nb_note(NBN_META, -1);
        nb_note(NBN_LIST, -1);
        return 0;
    }
    if (!strcmp(sub, "savenow")) {
        if (!NB.open)
            return core_fail("The notebook has no name yet.");
        NB.dirty = true;
        if (nb_save(msg, sizeof msg))
            return core_fail(msg);
        nb_note(NBN_META, -1);
        return 0;
    }
    if (!strcmp(sub, "renameto")) {
        if (!NB.open || !arg)
            return core_fail("The notebook has no name yet.");
        if (nb_flush() || nb_rename(NB.name, NB.where, arg, msg, sizeof msg))
            return core_fail(msg[0] ? msg : "not renamed");
        nbui_changed();
        nb_note(NBN_META, -1);
        nb_note(NBN_LIST, -1);
        return 0;
    }
    if (!strcmp(sub, "export")) {
        if (!NB.open)
            return core_fail("Save the notebook first.");
        nb_flush();
        if (NB.where == 1)
            return 0; /* (it is on the card) */
        if (nb_copy_to(NB.name, 0, 1, msg, sizeof msg))
            return core_fail(msg);
        return 0;
    }
    if (!strcmp(sub, "listview")) {
        nbui_list();
        return 0;
    }
    return -2;
}

int nb_console(int argc, char **argv)
{
    if (strcmp(argv[0], "nb"))
        return -1;
    const char *sub = argc > 1 ? argv[1] : "list";
    static char msg[200];
    static nb_item_t items[80];
    bool card = argc > 3 && !strcmp(argv[argc - 1], "card");
    int r = nb_sync_console(argc, argv);
    if (r != -2)
        return r;
    r = 0;
    if (!strcmp(sub, "usb")) {
        nb_flush(); /* (the drive lists what is stored) */
        return usb_list();
    }
    if (!strcmp(sub, "import") && argc > 2)
        return usb_import(argv[2]);
    r = nb_core_cmd(sub, argc > 2 ? argv[2] : NULL);
    if (r != -2)
        return r;
    r = 0;
    if (!strcmp(sub, "list")) {
        int n = nb_list(items, 80);
        for (int i = 0; i < n; i++)
            py_api->con_printf("nb=%s %lu %s %d\n", items[i].name, (unsigned long)items[i].size,
                               items[i].where ? "card" : "flash", items[i].versions);
        return 0;
    }
    if (!strcmp(sub, "examples")) { /* the built-in examples ("saved": the store has a changed copy) */
        int n = nb_list(items, 80);
        for (int e = 0; e < nb_nexamples; e++) {
            bool saved = false;
            for (int j = 0; j < n && !saved; j++)
                saved = items[j].where == 0 && nbs_name_same(items[j].name, nb_examples[e].name);
            py_api->con_printf("nbex=%s %lu%s\n", nb_examples[e].name, (unsigned long)nb_examples[e].len,
                               saved ? " saved" : "");
        }
        return 0;
    }
    if (!strcmp(sub, "new") || !strcmp(sub, "open")) {
        const char *name = argc > 2 && strcmp(argv[2], "card") ? argv[2] : NULL;
        int where = (argc > 2 && !strcmp(argv[argc - 1], "card")) ? 1 : 0;
        if (sub[0] == 'o' && argc > 3 && !strcmp(argv[argc - 1], "example"))
            where = NB_EXAMPLE; /* nb open NAME example */
        char def[NBS_NAME];
        if (!name && sub[0] == 'n') {
            nb_default_name(def, sizeof def);
            name = def;
        }
        if (!name)
            return py_api->con_error("usage: nb open NAME [card]");
        nbui_commit();
        nb_keep_scratch(); /* (the home screen's calculations are not lost) */
        if (NB.open && NB.dirty)
            nb_save(msg, sizeof msg);
        if (sub[0] == 'n') {
            if (!nbs_name_ok(name))
                return py_api->con_error("%s", store_error(NBS_E_NAME));
            if (exists(name, where))
                return py_api->con_error("there is a notebook named %s already", name);
            nb_new_doc(name, where);
            nb_insert(0, NBK_MATH); /* (a first cell to type in) */
            r = nb_save(msg, sizeof msg);
        } else {
            r = nb_load(name, where, msg, sizeof msg);
        }
        if (!r)
            nb_restart_python();
        nbui_changed();
        nb_note(NBN_DOC, -1);
        if (sub[0] == 'n')
            nb_note(NBN_LIST, -1);
        return r ? py_api->con_error("%s", msg) : 0;
    }
    if (!strcmp(sub, "close")) {
        nbui_commit();
        if (NB.open && NB.dirty)
            nb_save(msg, sizeof msg);
        nb_close_doc();
        nbui_changed();
        nb_note(NBN_DOC, -1);
        return 0;
    }
    if (!strcmp(sub, "delete") && argc > 2) {
        bool was = NB.open;
        if (NB.open && NB.where == card && !strcmp(NB.name, argv[2]))
            nbui_commit();
        r = nb_remove(argv[2], card, msg, sizeof msg);
        nbui_changed();
        nb_note(NBN_LIST, -1);
        if (was && !NB.open)
            nb_note(NBN_DOC, -1);
        return r ? py_api->con_error("%s", msg) : 0;
    }
    if (!strcmp(sub, "rename") && argc > 3) {
        r = nb_rename(argv[2], argc > 4 && card, argv[3], msg, sizeof msg);
        nbui_changed();
        nb_note(NBN_LIST, -1);
        nb_note(NBN_META, -1);
        return r ? py_api->con_error("%s", msg) : 0;
    }
    if (!strcmp(sub, "revert") && argc > 2) {
        r = nb_revert(argv[2], msg, sizeof msg);
        if (!r && NB.open && NB.where == 0 && !strcmp(NB.name, argv[2])) {
            nbui_commit();
            r = nb_load(argv[2], 0, msg, sizeof msg);
            nb_note(NBN_DOC, -1);
        }
        nbui_changed();
        nb_note(NBN_LIST, -1);
        return r ? py_api->con_error("%s", msg) : 0;
    }
    if (!strcmp(sub, "copy") && argc > 3) { /* nb copy NAME card|flash */
        int to = !strcmp(argv[3], "card");
        nbui_commit();
        r = nb_copy_to(argv[2], !to, to, msg, sizeof msg);
        nbui_changed();
        nb_note(NBN_DOC, -1);
        nb_note(NBN_LIST, -1);
        return r ? py_api->con_error("%s", msg) : 0;
    }
    if (!strcmp(sub, "store")) {
        if (argc > 3 && !strcmp(argv[2], "fail")) {
            nb_store_mount();
            store.fail_after = strtol(argv[3], NULL, 10);
            return 0;
        }
        if (argc > 2 && !strcmp(argv[2], "remount")) {
            store_tried = false;
            store.mounted = false;
            r = nb_store_mount();
            return r && r != NBS_E_NODEV ? py_api->con_error("mount: %d", r) : 0;
        }
        if (argc > 2 && !strcmp(argv[2], "wipe")) { /* every notebook gone (tests) */
            nb_store_mount();
            for (uint32_t i = 0; i < store.nsec; i++)
                if (store.owner[i] != -2)
                    py_pc->nb_io(NBIO_ERASE, i * NBS_SECTOR, NULL, NBS_SECTOR);
            store_tried = false;
            nb_close_doc();
            nb_store_mount();
            nbui_changed();
            nb_note(NBN_DOC, -1);
            nb_note(NBN_LIST, -1);
            return 0;
        }
        nb_store_mount();
        py_api->con_printf("mounted=%d versions=%d free=%lu writes=%lu erases=%lu\n", store.mounted, store.nver,
                           (unsigned long)nbs_free(&store), (unsigned long)store.writes, (unsigned long)store.erases);
        return 0;
    }
    int ci = argc > 2 ? find_cell(argv[2]) : -1;
    if ((!strcmp(sub, "set") || !strcmp(sub, "kind") || !strcmp(sub, "del")) && ci >= 0 && nb_calc_edits(ci))
        return py_api->con_error("editing on the calculator");
    int note = -1, at = -1; /* what changed */
    nbui_mark();
    if (!strcmp(sub, "add") && argc > 2) { /* nb add KIND [TEXT] [at N] */
        int k = nb_kind_of(argv[2]);
        if (k < 0)
            return py_api->con_error("kinds: text math python graph");
        nb_cell_t *c = nb_insert(NB.ncells, k);
        if (!c)
            return py_api->con_error("too many cells");
        if (argc > 3)
            nb_set(c, NBF_SRC, argv[3]);
        if (k == NBK_GRAPH) /* (a graph cell has a spec: its functions and window) */
            nb_core->graph_set(NB.ncells - 1, NULL);
        note = NBN_ORDER;
    } else if (!strcmp(sub, "set") && argc > 3) {
        int i = find_cell(argv[2]);
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        nb_set(&NB.cell[i], NBF_SRC, argv[3]);
        nb_graph_text(i);
        note = NBN_CELL, at = i;
    } else if (!strcmp(sub, "kind") && argc > 3) {
        int i = find_cell(argv[2]), k = nb_kind_of(argv[3]);
        if (i < 0 || k < 0)
            return py_api->con_error("usage: nb kind N text|math|python|graph");
        if (NB.cell[i].kind != k) {
            NB.cell[i].kind = (uint8_t)k;
            nb_clear_out(&NB.cell[i]);
            nb_set(&NB.cell[i], NBF_GRAPH, NULL);
            nb_graph_text(i);
            note = NBN_CELL, at = i;
        }
    } else if (!strcmp(sub, "del") && argc > 2) {
        int i = find_cell(argv[2]);
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        nb_delete_cell(i);
        note = NBN_ORDER;
    } else if (!strcmp(sub, "run")) {
        int i = argc > 2 ? find_cell(argv[2]) : NB.ncells - 1;
        if (argc > 2 && i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        nb_run(i);
    } else if (!strcmp(sub, "runall")) {
        nb_run_all();
    } else if (!strcmp(sub, "clear")) {
        if (argc > 2 && ci < 0)
            return py_api->con_error("no cell %s", argv[2]);
        for (int i = 0; i < NB.ncells; i++)
            if (argc < 3 || i == ci)
                nb_clear_out(&NB.cell[i]);
        if (argc < 3)
            NB.counter = 0;
        note = argc > 2 ? NBN_CELL : NBN_ALL, at = ci;
    } else if (!strcmp(sub, "restart")) {
        nb_restart_python();
    } else if (!strcmp(sub, "save")) {
        NB.dirty = true;
        nbui_changed();
        return nb_flush() ? py_api->con_error("not saved") : 0;
    } else if (!strcmp(sub, "cell") && argc > 2) {
        int i = find_cell(argv[2]);
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        nb_cell_t *c = &NB.cell[i];
        static const char *const names[NBF_N] = {"src", "out", "val", "tex", "ename", "evalue", "tb", "plot", "spec"};
        py_api->con_printf("kind=%s\ncount=%d\nid=%s\n", nb_kind_name(c->kind), c->count, c->id);
        for (int f = 0; f < NBF_N; f++)
            print_field(names[f], c->f[f]);
        return 0;
    } else if (!strcmp(sub, "expect") && argc > 4) { /* nb expect N FIELD TEXT: FIELD contains TEXT */
        int i = find_cell(argv[2]);
        static const char *const names[NBF_N] = {"src", "out", "val", "tex", "ename", "evalue", "tb", "plot", "spec"};
        int f = -1;
        for (int k = 0; k < NBF_N; k++)
            if (!strcmp(argv[3], names[k]))
                f = k;
        if (i < 0 || f < 0)
            return py_api->con_error("usage: nb expect N src|out|val|tex|ename|evalue|tb|plot TEXT");
        const char *v = nb_get(&NB.cell[i], f);
        if (!strstr(v, argv[4]) || (!argv[4][0] && v[0]))
            return py_api->con_error("cell %d %s is \"%s\", expected \"%s\"", i + 1, argv[3], v, argv[4]);
        return 0;
    } else if (!strcmp(sub, "json")) {
        nb_flush(); /* (an export: what the computer gets is stored too) */
        nb_write_json(con_sink, NULL);
        return 0;
    } else {
        return py_api->con_error("usage: nb list|new [NAME] [card]|open NAME [card]|close|delete NAME [card]|rename A B|"
                                 "revert NAME|copy NAME card|flash|add KIND [TEXT]|set N TEXT|kind N KIND|del N|run [N]|"
                                 "runall|clear [N]|restart|save|cell N|expect N FIELD TEXT|json|store [fail N|remount|wipe]|"
                                 "sync on|off|rev|doc|head|ids|get ID|lock ID|unlock|draft|setbuf ID|insert AT KIND|"
                                 "move ID -1|1|sample ID N [A B [K]]|png ID|put begin NAME SIZE|put B64|put end|flush");
    }
    if (note >= 0)
        nb_note(note, at);
    nbui_changed();
    return 0;
}

void nb_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[48];
    kv("nb_open", NB.open ? NB.name : "", user);
    py_api->snprintf(b, sizeof b, "%d", NB.ncells);
    kv("nb_cells", b, user);
    kv("nb_where", NB.where ? "card" : "flash", user);
    kv("nb_note", NB.note, user);
    py_api->snprintf(b, sizeof b, "%d", NB.counter);
    kv("nb_counter", b, user);
    nb_sync_state(kv, user);
    nbui_state(kv, user);
}
