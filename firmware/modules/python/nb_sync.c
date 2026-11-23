/*
 * The browser page as a second view of the open notebook (nb.h; docs/NOTEBOOK.md
 * "The browser page"). The notebook lives here, in the calculator; the page
 * (web/notebook) shows it and edits it through console commands, and learns of
 * every change from event lines:
 *
 *   # nb1 REV DIRTY TYPE [ARGS]
 *
 * "# " makes them log lines, which the console protocol keeps out of command
 * replies; "nb1" is the version of this protocol; REV the document revision
 * (one more for every event, so a page that sees a gap reads everything
 * again); DIRTY 1 while there are unsaved changes. Types: doc (another
 * notebook: read it all), cell ID (that cell changed), order (cells added,
 * deleted, moved), run ID | run - (a cell runs / none), meta (name, counter,
 * saved), list (the list of notebooks), lock ID calc|page, unlock ID, draft ID
 * (the cell the calculator edits, as typed so far). Events go out only while a
 * page follows (`nb sync on`; it lapses 15 s after the page's last command).
 *
 * Locks: a cell edited on one side cannot be edited on the other. The
 * calculator's lock lasts while its editor is open on the cell; the page's
 * (`nb lock ID`) until `nb unlock`, 30 s without a command from the page, or
 * the page going away.
 *
 * Saving: changes are kept in memory (NB.dirty) and written to the store or
 * the card at power-off (`nb flush` from the core), when the Notebook screen
 * (list, editor) is left, when another notebook is opened, before an export or the USB drive
 * lists the notebooks, and NB_IDLE_SAVE_MS after the last change. Not per
 * keystroke or per command: the store is flash, a ring of 4 KB sectors.
 */
#include <stdlib.h>
#include <string.h>
#include "py_mod.h"
#include "nb.h"

static struct {
    bool on;            /* a page follows (nb sync on) */
    uint32_t seen;      /* the page's last command (ms) */
    char lock[24];      /* the cell the page edits ("" none) */
    uint32_t lock_t;
    char calc[24];      /* the cell the calculator edits */
    uint32_t draft;     /* hash of the calculator's edit last announced */
    bool shown;         /* the Notebook screen was shown at the last tick */
    uint32_t changed;   /* the last change (ms) */
    bool refresh;       /* the USB drive is generated again once the page is gone */
    bool up;            /* an upload (nb put) is open */
    uint32_t up_t;
    char up_name[NBS_NAME];
    uint32_t b64;       /* base64 decoding: bits, count */
    int b64n;
} S;
static uint32_t rev = 1;
static uint32_t idle_ms = NB_IDLE_SAVE_MS; /* (`nb idle MS`: tests) */
static nbs_wr_t uwr;

#define UPLOAD "~upload"
static uint32_t now(void) { return py_api->millis(); }
bool nb_synced(void) { return S.on; }

static void event(const char *type, const char *a, const char *b)
{
    rev++;
    if (S.on)
        py_api->con_printf("# nb1 %lu %d %s%s%s%s%s\n", (unsigned long)rev, NB.dirty ? 1 : 0, type, a ? " " : "",
                           a ? a : "", b ? " " : "", b ? b : "");
}

static const char *id_of(int i) { return i >= 0 && i < NB.ncells ? NB.cell[i].id : NULL; }

void nb_note(int what, int i)
{
    if (what == NBN_CELL || what == NBN_ORDER || what == NBN_ALL) {
        NB.dirty = true;
        S.changed = now();
    }
    nb_core->touched(); /* (the home screen shows the same document) */
    switch (what) {
    case NBN_CELL:
        if (id_of(i))
            event("cell", id_of(i), NULL);
        break;
    case NBN_ORDER: event("order", NULL, NULL); break;
    case NBN_RUN: event("run", id_of(i) ? id_of(i) : "-", NULL); break;
    case NBN_META: event("meta", NULL, NULL); break;
    case NBN_LIST: event("list", NULL, NULL); break;
    default: /* NBN_DOC, NBN_ALL */
        if (what == NBN_DOC)
            S.lock[0] = 0; /* (a lock belongs to a cell of the notebook before) */
        event("doc", NULL, NULL);
        break;
    }
}

void nb_calc_edit(int i)
{
    if (i >= 0 && id_of(i)) {
        py_api->snprintf(S.calc, sizeof S.calc, "%s", id_of(i));
        S.draft = nbui_draft(NULL);
        event("lock", S.calc, "calc");
    } else if (S.calc[0]) {
        event("unlock", S.calc, NULL);
        S.calc[0] = 0;
    }
}

bool nb_page_edits(int i) { return S.lock[0] && id_of(i) && !strcmp(id_of(i), S.lock); }
static bool calc_edits(int i) { return S.calc[0] && id_of(i) && !strcmp(id_of(i), S.calc); }

static void page_unlock(void)
{
    if (S.lock[0]) {
        event("unlock", S.lock, NULL);
        S.lock[0] = 0;
    }
}

int nb_flush(void)
{
    static char msg[160];
    if (!NB.open || !NB.dirty || S.up)
        return 0;
    if (nb_save(msg, sizeof msg)) {
        py_api->toast(msg);
        S.changed = now(); /* (tried again after the idle time) */
        return -1;
    }
    event("meta", NULL, NULL);
    return 0;
}

static void sync_end(void)
{
    page_unlock();
    S.on = false;
    if (S.refresh) { /* the drive lists the notebooks again (a reconnection the page no longer minds) */
        S.refresh = false;
        py_pc->nb_io(NBIO_USB_REFRESH, 0, NULL, 0);
    }
}

/* nb_save's refresh of the USB drive waits while a page follows: generating the drive
 * again makes the computer see the device leave and come back (the page reconnects). */
bool nb_sync_defer_refresh(void)
{
    if (!S.on)
        return false;
    S.refresh = true;
    return true;
}

static void upload_abort(void)
{
    if (S.up) {
        uwr.err = uwr.err ? uwr.err : NBS_E_SIZE;
        nbs_close(&uwr); /* (its sectors are free again) */
        S.up = false;
    }
}

/* every 100 ms from the core's main loop (`nb tick`): -> 1 when the Notebook screen should be drawn again */
static int tick(void)
{
    uint32_t t = now();
    int redraw = 0;
    if (S.on && t - S.seen > 15000) {
        sync_end();
        redraw = 1;
    }
    if (S.lock[0] && t - S.lock_t > 30000) {
        page_unlock();
        redraw = 1;
    }
    if (S.up && t - S.up_t > 10000)
        upload_abort();
    bool shown = py_pc->nb_io(NBIO_SCREEN, 0, NULL, 0) == 1;
    if (S.shown && !shown && NB.dirty) /* the Notebook screen (list, editor) was left */
        nb_flush();
    S.shown = shown;
    if (NB.open && NB.dirty && !nb_running() && t - S.changed >= idle_ms) {
        nb_flush();
        redraw = 1;
    }
    if (S.on && S.calc[0]) {
        uint32_t h = nbui_draft(NULL);
        if (h != S.draft) {
            S.draft = h;
            event("draft", S.calc, NULL);
        }
    }
    return redraw && shown;
}

/* ---------------------------------------------------------------- output */
/* JSON string contents (no quotes) through con_printf in pieces */
static void jput(const char *s, size_t n)
{
    char b[200];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (k > sizeof b - 8) {
            py_api->con_printf("%.*s", (int)k, b);
            k = 0;
        }
        if (c == '"' || c == '\\')
            b[k++] = '\\', b[k++] = (char)c;
        else if (c == '\n')
            b[k++] = '\\', b[k++] = 'n';
        else if (c == '\t')
            b[k++] = '\\', b[k++] = 't';
        else if (c < 0x20 || c == 0x7F)
            k += (size_t)py_api->snprintf(b + k, 8, "\\u%04x", c);
        else
            b[k++] = (char)c;
    }
    if (k)
        py_api->con_printf("%.*s", (int)k, b);
}
static void jkey(const char *k, const char *v, bool first)
{
    py_api->con_printf("%s\"%s\":\"", first ? "" : ",", k);
    if (v)
        jput(v, strlen(v));
    py_api->con_printf("\"");
}
static void jnum(const char *k, double v) { py_api->con_printf(",\"%s\":%.10g", k, v); }

static void head(void)
{
    py_api->con_printf("head={");
    jkey("name", NB.open ? NB.name : "", true);
    jkey("where", NB.where ? "card" : "flash", false);
    jkey("running", id_of(nb_running_cell()), false);
    jkey("note", NB.note, false);
    py_api->con_printf(",\"open\":%d,\"rev\":%lu,\"dirty\":%d,\"counter\":%d,\"cells\":%d}\n", NB.open ? 1 : 0,
                       (unsigned long)rev, NB.dirty ? 1 : 0, NB.counter, NB.ncells);
}

static void cell_line(int i)
{
    static const char *const fields[NBF_N] = {"src", "out", "val", "tex", "ename", "evalue", "tb", "plot", "spec"};
    nb_cell_t *c = &NB.cell[i];
    py_api->con_printf("cell={");
    jkey("id", c->id, true);
    jkey("kind", nb_kind_name(c->kind), false);
    jkey("lock", calc_edits(i) ? "calc" : nb_page_edits(i) ? "page" : "", false);
    for (int f = 0; f < NBF_N; f++)
        jkey(fields[f], c->f[f], false);
    jnum("xmin", c->xmin);
    jnum("xmax", c->xmax);
    py_api->con_printf(",\"count\":%d,\"ran\":%d}\n", c->count, (c->flags & NBC_RAN) ? 1 : 0);
}

static void draft_put(const char *s, size_t n) { jput(s, n); }

/* function k (1, 2, ...: its "f" line) of a graph cell's spec (app/gcell.c) as CAS text; false: none */
static bool spec_fn(const nb_cell_t *c, long k, char *out, size_t n)
{
    static char disp[512];
    const char *s = c->f[NBF_GRAPH];
    for (long j = 0; s && *s;) {
        const char *e = strchr(s, '\n');
        size_t len = e ? (size_t)(e - s) : strlen(s);
        if (len > 2 && s[0] == 'f' && s[1] == ' ' && ++j == k) {
            const char *p = s + 2, *end = s + len;
            for (int sp = 0; p < end && sp < 3; p++) /* (ON COLOR STYLE) */
                if (*p == ' ')
                    sp++;
            py_api->snprintf(disp, sizeof disp, "%.*s", (int)(end - p), p);
            nb_to_cas(disp, out, n);
            return out[0] != 0;
        }
        s = e ? e + 1 : NULL;
    }
    return false;
}

/* `nb sample ID N [XMIN XMAX [K]]`: the cell's function (a graph cell's first, or its function K of
 * the spec; a Python cell's calc.plot()) at N points: ys= lines of 12 values, n where there is no
 * real value */
static int sample(int i, long n, double a, double b, bool range, long fk)
{
    nb_cell_t *c = &NB.cell[i];
    static char fn[1024];
    const char *plot = c->f[NBF_PLOT];
    if (fk > 0) {
        if (!spec_fn(c, fk, fn, sizeof fn))
            return py_api->con_error("cell %d has no function %ld", i + 1, fk);
        plot = fn;
    }
    if (!plot)
        return py_api->con_error("cell %d has no plot (run it)", i + 1);
    if (!range)
        a = c->xmin, b = c->xmax;
    if (n < 2 || n > 2000 || !(a < b))
        return py_api->con_error("usage: nb sample ID N [XMIN XMAX [K]] (2 <= N <= 2000, XMIN < XMAX)");
    static const char *const vars[1] = {"x"};
    cas_fn *cf = NULL;
    if (py_api->cas_compile_vars(py_api->cas(), plot, vars, 1, &cf) != CAS_OK || !cf)
        return py_api->con_error("cannot graph %s", plot);
    py_api->con_printf("plot=%s\nrange=%.17g %.17g\n", plot, a, b);
    char line[220];
    size_t k = 0;
    for (long j = 0; j < n; j++) {
        double x = a + (b - a) * (double)j / (double)(n - 1);
        double y = py_api->cas_fn_eval_vars(cf, &x);
        if (!k)
            k = (size_t)py_api->snprintf(line, sizeof line, "ys=");
        if (y == y && y < 1e300 && y > -1e300)
            k += (size_t)py_api->snprintf(line + k, sizeof line - k, "%s%.10g", k > 3 ? " " : "", y);
        else
            k += (size_t)py_api->snprintf(line + k, sizeof line - k, "%sn", k > 3 ? " " : "");
        if ((j + 1) % 12 == 0 || j == n - 1) {
            py_api->con_printf("%s\n", line);
            k = 0;
        }
        if (!(j & 63))
            py_pc->poll();
    }
    py_api->cas_fn_free(py_api->cas(), cf);
    return 0;
}

void nb_graph_text(int i)
{
    if (i >= 0 && i < NB.ncells && NB.cell[i].kind == NBK_GRAPH) {
        nb_set(&NB.cell[i], NBF_GRAPH, NULL);
        nb_core->graph_set(i, NULL);
    }
}

/* `nb sample3d ID NX NY [K]`: the 3D cell's surface z(x,y) (its function K of the spec, else the
 * first one that is on) on a grid of NX x NY points over its x and y range, one round trip:
 * grid=K NX NY XMIN XMAX YMIN YMAX ZMIN ZMAX, then zs= lines of 16 values row by row (y outer),
 * n where there is no real value, then edge=INDEX X Y Z lines: nodes with no value moved onto the
 * edge of the domain (so sqrt(9-x^2-y^2) reaches z = 0). Only z = f(x,y) surfaces (parametric
 * ones: the picture, nb png). */
static int sample3d(int i, long nx, long ny, long fk)
{
    nb_cell_t *c = &NB.cell[i];
    const char *s = c->f[NBF_GRAPH];
    static char fn[1024];
    double w[6] = {-5, 5, -5, 5, -5, 5};
    if (!s || strncmp(s, "3d ", 3))
        return py_api->con_error("cell %d is not a 3D graph", i + 1);
    if (nx < 2 || ny < 2 || nx > 64 || ny > 64)
        return py_api->con_error("usage: nb sample3d ID NX NY [K] (2 <= NX, NY <= 64)");
    long k = 0;
    for (long j = 0; s && *s;) { /* the window, the function's f line (ON COLOR KIND EXPR) */
        const char *e = strchr(s, '\n');
        if (s[0] == 'w' && s[1] == ' ') {
            const char *q = s + 2;
            for (int m = 0; m < 6; m++)
                w[m] = nb_strtod(q, &q);
        } else if (s[0] == 'f' && s[1] == ' ' && !k) {
            j++;
            char *p;
            long on = strtol(s + 2, &p, 10);
            strtol(p, &p, 10);
            long kind = strtol(p, &p, 10);
            if ((fk > 0 && j == fk) || (fk <= 0 && on && kind == 0)) {
                if (kind != 0)
                    return py_api->con_error("function %ld is not z = f(x,y)", j);
                if (*p == ' ')
                    p++;
                size_t n = e ? (size_t)(e - p) : strlen(p);
                py_api->snprintf(fn, sizeof fn, "%.*s", (int)n, p);
                k = j;
            }
        }
        s = e ? e + 1 : NULL;
    }
    if (!k)
        return py_api->con_error("cell %d has no function z = f(x,y)", i + 1);
    static char cas[1024];
    nb_to_cas(fn, cas, sizeof cas);
    static const char *const vars[2] = {"x", "y"};
    cas_fn *cf = NULL;
    if (!cas[0] || py_api->cas_compile_vars(py_api->cas(), cas, vars, 2, &cf) != CAS_OK || !cf)
        return py_api->con_error("cannot graph %s", fn);
    py_api->con_printf("grid=%ld %ld %ld %.10g %.10g %.10g %.10g %.10g %.10g\n", k, nx, ny, w[0], w[1], w[2], w[3], w[4],
                       w[5]);
    char line[260];
    size_t o = 0;
    long cnt = 0;
    static unsigned char def3[64 * 64]; /* the nodes with a value (for the edge points) */
    for (long r = 0; r < ny; r++)
        for (long q = 0; q < nx; q++) {
            double v[2] = {w[0] + (w[1] - w[0]) * (double)q / (double)(nx - 1),
                           w[2] + (w[3] - w[2]) * (double)r / (double)(ny - 1)};
            double z = py_api->cas_fn_eval_vars(cf, v);
            if (!o)
                o = (size_t)py_api->snprintf(line, sizeof line, "zs=");
            if (z == z && z < 1e300 && z > -1e300)
                o += (size_t)py_api->snprintf(line + o, sizeof line - o, "%s%.6g", o > 3 ? " " : "", z);
            else
                o += (size_t)py_api->snprintf(line + o, sizeof line - o, "%sn", o > 3 ? " " : "");
            if (++cnt % 16 == 0 || cnt == nx * ny) {
                py_api->con_printf("%s\n", line);
                o = 0;
            }
            def3[r * nx + q] = z == z && z < 1e300 && z > -1e300;
            if (!(cnt & 63))
                py_pc->poll();
        }
    /* the edge of the domain (as g3_mesh_edges on the calculator): a node with no value next to
     * one with a value moves to the last point with a value found by bisecting from that
     * neighbour towards it, the nearest of its neighbours' edge points; one line each,
     * edge=INDEX X Y Z (pages that do not know these lines skip them) */
    static const signed char dir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (long r = 0; r < ny; r++)
        for (long q = 0; q < nx; q++) {
            if (def3[r * nx + q])
                continue;
            double x1 = w[0] + (w[1] - w[0]) * (double)q / (double)(nx - 1);
            double y1 = w[2] + (w[3] - w[2]) * (double)r / (double)(ny - 1);
            double best = 2, bp[3] = {0, 0, 0};
            for (int d = 0; d < 4; d++) {
                long q0 = q + dir[d][0], r0 = r + dir[d][1];
                if (q0 < 0 || q0 >= nx || r0 < 0 || r0 >= ny || !def3[r0 * nx + q0])
                    continue;
                double x0 = w[0] + (w[1] - w[0]) * (double)q0 / (double)(nx - 1);
                double y0 = w[2] + (w[3] - w[2]) * (double)r0 / (double)(ny - 1);
                double lo = 0, hi = 1, lp[3] = {x0, y0, 0};
                double v0[2] = {x0, y0};
                lp[2] = py_api->cas_fn_eval_vars(cf, v0);
                for (int it = 0; it < 12; it++) {
                    double t = (lo + hi) / 2, v[2] = {x0 + t * (x1 - x0), y0 + t * (y1 - y0)};
                    double z = py_api->cas_fn_eval_vars(cf, v);
                    if (z == z && z < 1e300 && z > -1e300) {
                        lo = t;
                        lp[0] = v[0], lp[1] = v[1], lp[2] = z;
                    } else {
                        hi = t;
                    }
                }
                if (lo > 0 && 1 - lo < best) {
                    best = 1 - lo;
                    bp[0] = lp[0], bp[1] = lp[1], bp[2] = lp[2];
                }
            }
            if (best <= 1)
                py_api->con_printf("edge=%ld %.6g %.6g %.6g\n", r * nx + q, bp[0], bp[1], bp[2]);
            py_pc->poll();
        }
    py_api->cas_fn_free(py_api->cas(), cf);
    return 0;
}

/* ------------------------------------------------------------ animated graph cells: frames
 * An animated cell's spec has the line "a TMIN TMAX SECS MODE" (app/gcell.c). Its frames are the
 * functions sampled at NF values of t from TMIN to TMAX (both ends), in one reply: the page plays
 * them. Values go out 16 a line (`%.6g`, n: no real value), frame after frame. */
static struct {
    char line[260];
    size_t o;
    long cnt;
} V;
static void v_put(const char *key, double z)
{
    if (!V.o)
        V.o = (size_t)py_api->snprintf(V.line, sizeof V.line, "%s=", key);
    bool first = V.line[V.o - 1] == '=';
    if (z == z && z < 1e300 && z > -1e300)
        V.o += (size_t)py_api->snprintf(V.line + V.o, sizeof V.line - V.o, "%s%.6g", first ? "" : " ", z);
    else
        V.o += (size_t)py_api->snprintf(V.line + V.o, sizeof V.line - V.o, "%sn", first ? "" : " ");
    if (++V.cnt % 16 == 0) {
        py_api->con_printf("%s\n", V.line);
        V.o = 0;
    }
    if (!(V.cnt & 63))
        py_pc->poll();
}
static void v_end(void)
{
    if (V.o)
        py_api->con_printf("%s\n", V.line);
    V.o = 0, V.cnt = 0;
}

/* the t range of an animated spec (its "a" line; 0..2π without one) */
static void spec_trange(const char *s, double *t0, double *t1)
{
    *t0 = 0, *t1 = 6.283185307179586;
    const char *a = s && s[0] == 'a' && s[1] == ' ' ? s : s ? strstr(s, "\na ") : NULL;
    if (!a)
        return;
    const char *q = a + (a[0] == '\n' ? 3 : 2);
    double v0 = nb_strtod(q, &q), v1 = nb_strtod(q, &q);
    if (v0 < v1)
        *t0 = v0, *t1 = v1;
}

/* `nb frames ID NF N [XMIN XMAX]`: the 2D cell's functions that are on, in x and t, at N points
 * of its x range (or XMIN..XMAX) for NF values of t: frames=NF N XMIN XMAX TMIN TMAX K..., then
 * ys= lines, frame by frame, in each frame function after function (K: their f lines, 1, 2, ...) */
static int frames2d(int i, long nf, long n, double a, double b, bool range)
{
    nb_cell_t *c = &NB.cell[i];
    const char *s = c->f[NBF_GRAPH];
    if (!s || !strncmp(s, "3d", 2))
        return py_api->con_error("cell %d is not a 2D graph", i + 1);
    if (!range)
        a = c->xmin, b = c->xmax;
    long k[6], nk = 0;
    static cas_fn *cf[6];
    static char cas[1024];
    for (long f = 1; s && *s && nk < 6;) { /* the functions that are on (f lines "f ON COLOR STYLE EXPR") */
        const char *e = strchr(s, '\n');
        if (s[0] == 'f' && s[1] == ' ') {
            if (s[2] == '1' && spec_fn(c, f, cas, sizeof cas))
                k[nk++] = f;
            f++;
        }
        s = e ? e + 1 : NULL;
    }
    if (nf < 2 || nf > 120 || n < 2 || n > 400 || !(a < b) || !nk || nf * n * nk > 48000)
        return py_api->con_error(nk ? "usage: nb frames ID NF N [XMIN XMAX] (2 <= NF <= 120, 2 <= N <= 400, "
                                      "NF x N x functions <= 48000)"
                                    : "cell %d has no function", i + 1);
    static const char *const vars[2] = {"x", "t"};
    for (long q = 0; q < nk; q++) {
        cf[q] = NULL;
        if (spec_fn(c, k[q], cas, sizeof cas) && py_api->cas_compile_vars(py_api->cas(), cas, vars, 2, &cf[q]) != CAS_OK)
            cf[q] = NULL;
    }
    double t0, t1;
    spec_trange(c->f[NBF_GRAPH], &t0, &t1);
    py_api->con_printf("frames=%ld %ld %.17g %.17g %.17g %.17g", nf, n, a, b, t0, t1);
    for (long q = 0; q < nk; q++)
        py_api->con_printf(" %ld", k[q]);
    py_api->con_printf("\n");
    for (long fr = 0; fr < nf; fr++)
        for (long q = 0; q < nk; q++)
            for (long j = 0; j < n; j++) {
                double v[2] = {a + (b - a) * (double)j / (double)(n - 1), t0 + (t1 - t0) * (double)fr / (double)(nf - 1)};
                v_put("ys", cf[q] ? py_api->cas_fn_eval_vars(cf[q], v) : __builtin_nan(""));
            }
    v_end();
    for (long q = 0; q < nk; q++)
        if (cf[q])
            py_api->cas_fn_free(py_api->cas(), cf[q]);
    return 0;
}

/* `nb frames3d ID NF NX NY [K]`: the 3D cell's surface z(x,y,t) (function K, else the first
 * z = f(x,y) that is on) on its NX x NY grid for NF values of t: grid=K NX NY XMIN XMAX YMIN YMAX
 * ZMIN ZMAX, frames=NF TMIN TMAX, then zs= lines, frame by frame (each row by row, y outer) */
static int frames3d(int i, long nf, long nx, long ny, long fk)
{
    nb_cell_t *c = &NB.cell[i];
    const char *s = c->f[NBF_GRAPH];
    static char fn[1024], cas[1024];
    double w[6] = {-5, 5, -5, 5, -5, 5};
    if (!s || strncmp(s, "3d ", 3))
        return py_api->con_error("cell %d is not a 3D graph", i + 1);
    if (nf < 2 || nf > 120 || nx < 2 || ny < 2 || nx > 64 || ny > 64 || nf * nx * ny > 48000)
        return py_api->con_error("usage: nb frames3d ID NF NX NY [K] (2 <= NF <= 120, 2 <= NX, NY <= 64, NF x NX x NY <= 48000)");
    long k = 0;
    for (long j = 0; s && *s;) {
        const char *e = strchr(s, '\n');
        if (s[0] == 'w' && s[1] == ' ') {
            const char *q = s + 2;
            for (int m = 0; m < 6; m++)
                w[m] = nb_strtod(q, &q);
        } else if (s[0] == 'f' && s[1] == ' ' && !k) {
            j++;
            char *p;
            long on = strtol(s + 2, &p, 10);
            strtol(p, &p, 10);
            long kind = strtol(p, &p, 10);
            if ((fk > 0 && j == fk) || (fk <= 0 && on && kind == 0)) {
                if (kind != 0)
                    return py_api->con_error("function %ld is not z = f(x,y)", j);
                if (*p == ' ')
                    p++;
                py_api->snprintf(fn, sizeof fn, "%.*s", (int)(e ? (size_t)(e - p) : strlen(p)), p);
                k = j;
            }
        }
        s = e ? e + 1 : NULL;
    }
    if (!k)
        return py_api->con_error("cell %d has no function z = f(x,y)", i + 1);
    nb_to_cas(fn, cas, sizeof cas);
    static const char *const vars[3] = {"x", "y", "t"};
    cas_fn *cf = NULL;
    if (!cas[0] || py_api->cas_compile_vars(py_api->cas(), cas, vars, 3, &cf) != CAS_OK || !cf)
        return py_api->con_error("cannot graph %s", fn);
    double t0, t1;
    spec_trange(c->f[NBF_GRAPH], &t0, &t1);
    py_api->con_printf("grid=%ld %ld %ld %.10g %.10g %.10g %.10g %.10g %.10g\nframes=%ld %.17g %.17g\n", k, nx, ny, w[0],
                       w[1], w[2], w[3], w[4], w[5], nf, t0, t1);
    for (long fr = 0; fr < nf; fr++)
        for (long r = 0; r < ny; r++)
            for (long q = 0; q < nx; q++) {
                double v[3] = {w[0] + (w[1] - w[0]) * (double)q / (double)(nx - 1),
                               w[2] + (w[3] - w[2]) * (double)r / (double)(ny - 1),
                               t0 + (t1 - t0) * (double)fr / (double)(nf - 1)};
                v_put("zs", py_api->cas_fn_eval_vars(cf, v));
            }
    v_end();
    py_api->cas_fn_free(py_api->cas(), cf);
    return 0;
}

/* `nb spec ID TEXT` (`nb specbuf ID`: the text from `py buf` pieces): a graph cell's functions,
 * window and style from the browser page (app/gcell.c normalises it; "3d ..." a 3D cell) */
static int set_spec(int i, const char *spec)
{
    if (NB.cell[i].kind != NBK_GRAPH)
        return py_api->con_error("cell %d is not a graph", i + 1);
    if (calc_edits(i))
        return py_api->con_error("editing on the calculator");
    nbui_mark();
    nb_core->graph_set(i, spec);
    nb_note(NBN_CELL, i);
    nbui_changed();
    return 0;
}

/* base64 text -> bytes into the upload */
static int up_b64_feed(const char *s)
{
    uint8_t out[240];
    size_t o = 0;
    for (; *s; s++) {
        char ch = *s;
        int v = ch >= 'A' && ch <= 'Z' ? ch - 'A' : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26 : ch >= '0' && ch <= '9' ? ch - '0' + 52
                : ch == '+' ? 62 : ch == '/' ? 63 : -1;
        if (v < 0)
            continue; /* (padding, spaces) */
        S.b64 = S.b64 << 6 | (uint32_t)v;
        if (++S.b64n == 4) {
            out[o++] = (uint8_t)(S.b64 >> 16), out[o++] = (uint8_t)(S.b64 >> 8), out[o++] = (uint8_t)S.b64;
            S.b64n = 0, S.b64 = 0;
        }
        if (o > sizeof out - 3) {
            nbs_put(&uwr, out, o);
            o = 0;
        }
    }
    if (o)
        nbs_put(&uwr, out, o);
    return uwr.err;
}
static void up_b64_end(void)
{
    uint8_t out[2];
    if (S.b64n == 2)
        out[0] = (uint8_t)(S.b64 >> 4), nbs_put(&uwr, out, 1);
    else if (S.b64n == 3)
        out[0] = (uint8_t)(S.b64 >> 10), out[1] = (uint8_t)(S.b64 >> 2), nbs_put(&uwr, out, 2);
    S.b64n = 0, S.b64 = 0;
}

/* the uploaded file (stored as UPLOAD) becomes the notebook `name`, opened */
static int upload_finish(char *msg, size_t n)
{
    up_b64_end();
    int r = nbs_close(&uwr);
    S.up = false;
    if (r) {
        py_api->snprintf(msg, n, "the file could not be stored (%d)", r);
        return -1;
    }
    nb_keep_scratch(); /* (the home screen's calculations are not lost) */
    nb_core->stash();
    nbs_rd_t rd;
    r = nbs_open(nb_store(), UPLOAD, 0, &rd);
    if (!r)
        r = nb_read_doc(nbs_read, &rd, S.up_name, 0, NBS_MAX_BYTES, msg, n);
    else
        py_api->snprintf(msg, n, "the stored copy could not be read");
    if (!r)
        r = nb_save(msg, n);
    nbs_delete(nb_store(), UPLOAD);
    if (r) {
        nb_core->unstash();
        return -1;
    }
    nb_restart_python();
    return 0;
}

/* ---------------------------------------------------------------- commands */
int nb_sync_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    static char msg[200];
    if (!strcmp(sub, "tick"))
        return tick();
    if (!strcmp(sub, "following")) /* (the core asks: 1 while a page follows; the USB drive waits) */
        return S.on ? 1 : 0;
    static const char *const core[] = {"touched", "runcell", "saveas", "savenow", "renameto", "export",
                                       "listview", "editcell", "calcedit", "usb", "import"};
    for (size_t k = 0; k < sizeof core / sizeof core[0]; k++)
        if (!strcmp(sub, core[k]))
            return -2; /* (the core's commands, not the page's: they do not keep the page followed) */
    if (S.on)
        S.seen = now();
    if (S.lock[0] && S.on)
        S.lock_t = now(); /* (the page is there: its lock lasts) */
    if (!strcmp(sub, "sync") && argc > 2) {
        bool on = !strcmp(argv[2], "on");
        if (on) {
            S.on = true, S.seen = now();
        } else if (S.on) {
            sync_end();
        }
        py_api->con_printf("rev=%lu\nsync=%d\n", (unsigned long)rev, S.on ? 1 : 0);
        return 0;
    }
    if (!strcmp(sub, "rev")) {
        py_api->con_printf("rev=%lu\nsync=%d\ndirty=%d\n", (unsigned long)rev, S.on ? 1 : 0, NB.dirty ? 1 : 0);
        return 0;
    }
    if (!strcmp(sub, "idle") && argc > 2) { /* test hook: the idle time before a save */
        long ms = strtol(argv[2], NULL, 10);
        idle_ms = ms > 0 ? (uint32_t)ms : NB_IDLE_SAVE_MS;
        return 0;
    }
    if (!strcmp(sub, "flush"))
        return nb_flush() ? py_api->con_error("not saved") : 0;
    if (!strcmp(sub, "head")) {
        head();
        return 0;
    }
    if (!strcmp(sub, "put") && argc > 2) { /* nb put begin NAME SIZE | DATA | end | abort */
        if (!strcmp(argv[2], "begin") && argc > 4) {
            upload_abort();
            long size = strtol(argv[4], NULL, 10);
            if (!nbs_name_ok(argv[3]))
                return py_api->con_error("a name of 1 to 35 letters, digits and spaces");
            if (size <= 0 || size > (long)NBS_MAX_BYTES)
                return py_api->con_error("too large (the limit is 256 KB)");
            int r = nb_store_mount();
            if (!r)
                r = nbs_create(nb_store(), UPLOAD, (uint32_t)size, &uwr);
            if (r) {
                nbs_close(&uwr);
                return py_api->con_error("the notebook store has no room (%d)", r);
            }
            py_api->snprintf(S.up_name, sizeof S.up_name, "%s", argv[3]);
            S.up = true, S.up_t = now(), S.b64 = 0, S.b64n = 0;
            return 0;
        }
        if (!S.up)
            return py_api->con_error("no upload (nb put begin NAME SIZE)");
        S.up_t = now();
        if (!strcmp(argv[2], "abort")) {
            upload_abort();
            return 0;
        }
        if (!strcmp(argv[2], "end")) {
            nbui_commit();
            nb_flush();
            int r = upload_finish(msg, sizeof msg);
            nbui_changed();
            nb_note(NBN_DOC, -1);
            nb_note(NBN_LIST, -1);
            return r ? py_api->con_error("%s", msg) : 0;
        }
        return up_b64_feed(argv[2]) ? py_api->con_error("the upload failed") : 0;
    }
    if (!strcmp(sub, "doc")) {
        head();
        for (int i = 0; i < NB.ncells; i++)
            cell_line(i);
        return 0;
    }
    if (!strcmp(sub, "ids")) {
        py_api->con_printf("ids=");
        for (int i = 0; i < NB.ncells; i++)
            py_api->con_printf("%s%s", i ? " " : "", NB.cell[i].id);
        py_api->con_printf("\n");
        return 0;
    }
    if (!strcmp(sub, "draft")) {
        py_api->con_printf("draft=\"");
        if (S.calc[0])
            nbui_draft(draft_put);
        py_api->con_printf("\"\nid=%s\n", S.calc);
        return 0;
    }
    int i = argc > 2 ? nb_find_cell(argv[2]) : -1;
    if (!strcmp(sub, "get") && argc > 2) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        cell_line(i);
        return 0;
    }
    if (!strcmp(sub, "lock") && argc > 2) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        if (calc_edits(i))
            return py_api->con_error("editing on the calculator");
        if (!nb_page_edits(i)) {
            page_unlock();
            py_api->snprintf(S.lock, sizeof S.lock, "%s", NB.cell[i].id);
            event("lock", S.lock, "page");
        }
        S.lock_t = now();
        return 0;
    }
    if (!strcmp(sub, "unlock")) {
        if (argc < 3 || (i >= 0 && nb_page_edits(i)))
            page_unlock();
        return 0;
    }
    if (!strcmp(sub, "sample") && argc > 3) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        bool range = argc > 5;
        return sample(i, strtol(argv[3], NULL, 10), range ? nb_strtod(argv[4], NULL) : 0,
                      range ? nb_strtod(argv[5], NULL) : 0, range, argc > 6 ? strtol(argv[6], NULL, 10) : 0);
    }
    if (!strcmp(sub, "sample3d") && argc > 4) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        return sample3d(i, strtol(argv[3], NULL, 10), strtol(argv[4], NULL, 10), argc > 5 ? strtol(argv[5], NULL, 10) : 0);
    }
    if (!strcmp(sub, "frames") && argc > 4) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        bool range = argc > 6;
        return frames2d(i, strtol(argv[3], NULL, 10), strtol(argv[4], NULL, 10), range ? nb_strtod(argv[5], NULL) : 0,
                        range ? nb_strtod(argv[6], NULL) : 0, range);
    }
    if (!strcmp(sub, "frames3d") && argc > 5) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        return frames3d(i, strtol(argv[3], NULL, 10), strtol(argv[4], NULL, 10), strtol(argv[5], NULL, 10),
                        argc > 6 ? strtol(argv[6], NULL, 10) : 0);
    }
    if (!strcmp(sub, "spec") && argc > 3) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        return set_spec(i, argv[3]);
    }
    if (!strcmp(sub, "specbuf") && argc > 2) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        int r = set_spec(i, py_srcbuf());
        py_srcbuf_clear();
        return r;
    }
    if (!strcmp(sub, "png") && argc > 2) {
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        return nb_png_cell(i);
    }
    if (!strcmp(sub, "setbuf") && argc > 2) { /* the source from `py buf` pieces (longer than a console line) */
        if (i < 0)
            return py_api->con_error("no cell %s", argv[2]);
        if (calc_edits(i))
            return py_api->con_error("editing on the calculator");
        nbui_mark();
        nb_set(&NB.cell[i], NBF_SRC, py_srcbuf());
        py_srcbuf_clear();
        nb_graph_text(i);
        nb_note(NBN_CELL, i);
        nbui_changed();
        return 0;
    }
    if (!strcmp(sub, "insert") && argc > 3) { /* nb insert AT KIND: at 0-based position AT -> id= */
        int k = nb_kind_of(argv[3]);
        long at = strtol(argv[2], NULL, 10);
        if (k < 0)
            return py_api->con_error("kinds: text math python graph");
        nbui_mark();
        nb_cell_t *c = nb_insert(at < 0 ? 0 : (int)at, k);
        if (!c)
            return py_api->con_error("too many cells");
        if (k == NBK_GRAPH) /* (with a spec: no function yet) */
            nb_core->graph_set((int)(c - NB.cell), NULL);
        py_api->con_printf("id=%s\n", c->id);
        nb_note(NBN_ORDER, -1);
        nbui_changed();
        return 0;
    }
    if (!strcmp(sub, "move") && argc > 3) { /* nb move ID -1|1 */
        long d = strtol(argv[3], NULL, 10);
        if (i < 0 || (d != 1 && d != -1))
            return py_api->con_error("usage: nb move ID -1|1");
        if (i + d < 0 || i + d >= NB.ncells)
            return 0;
        nbui_mark();
        nb_move(i, (int)d);
        nb_note(NBN_ORDER, -1);
        nbui_changed();
        return 0;
    }
    return -2;
}

bool nb_calc_edits(int i) { return calc_edits(i); }

void nb_sync_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[24];
    kv("nb_sync", S.on ? "1" : "0", user);
    py_api->snprintf(b, sizeof b, "%lu", (unsigned long)rev);
    kv("nb_rev", b, user);
    kv("nb_dirty", NB.dirty ? "1" : "0", user);
    kv("nb_page_lock", S.lock, user);
    kv("nb_calc_lock", S.calc, user);
}
