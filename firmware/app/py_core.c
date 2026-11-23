/*
 * Python, core side (app/py_core.h): the screens that host the "python" code
 * module (modules/python), their entry line, APPS > Python and APPS >
 * Notebook, the console `py` / `pyexec`, the break key and the services of
 * py_core_t (the CAS with a nested stack limit, the graph, and nb_io for the
 * Notebook app: its QSPI store region, textbook output, the card, the USB
 * drive's NOTEBOOKS folder).
 *
 * The module draws the Python console and the Notebook (modules/python
 * py_ui.c, nb_ui.c) and decides what keys mean; a key it does not use goes
 * to the entry line here, a core editor, so typing, the CAS / MATH / catalog
 * menus and the clipboard work as on the home screen. The Notebook's soft-key
 * labels come from the module (NBIO_SOFTKEY); labelled keys reach it as
 * KEY_F1..KEY_F5. Screen switches the module asks for (plot -> GRAPH, leave)
 * happen after the module call returned (never from inside module code).
 *
 * Without the module (not installed, `modx off python`, the core tier, the
 * browser simulator) APPS > Python / Notebook and the console say why.
 *
 * State: the entry line, a pending screen switch, the ON count, the
 * Notebook's soft-key labels. Main loop only, except py_break_isr (key
 * interrupt).
 */
#include "py_core.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "console.h"
#include "editor.h"
#include "gfx.h"
#include "graph.h"
#include "hal.h"
#include "m2d.h"
#include "modx.h"
#include "nbdoc.h"
#include "registry.h"
#include "sections.h"
#include "theme.h"
#include "ui.h"
#include "version.h"

#if defined(CONFIG_PYTHON) && !defined(PLATFORM_WEB)
#define HAVE_PY 1
#else
#define HAVE_PY 0
#endif

static volatile uint32_t breaks; /* ON presses (the key interrupt counts them) */
void py_break_isr(void) { breaks++; }

#if HAVE_PY
extern const screen_t scr_python, scr_notebook;
DTCM_BSS static editor_t ed; /* (AXI SRAM is full) */
static uint8_t pending; /* PY_SHOW_* after the current module call */
DTCM_BSS static char sk_lab[10][10]; /* the Notebook's soft keys (F1..F5, SHIFT F1..F5) */
DTCM_BSS static softkeys_t sk_bar;
static bool nb_edit; /* the Notebook edits a cell in the entry line */

static modx_mod_t *py_mod(void) { return modx_get(PY_MODULE); }
bool py_available(void) { return py_mod() != NULL; }
static bool ours(const screen_t *s) { return s == &scr_python || s == &scr_notebook; }
bool py_nb_shown(void) { return ui_current() == &scr_notebook; }
bool py_nb_following(void) { return py_nb_call("following", NULL) == 1; }

/* ------------------------------------------------------------ services */
/* The CAS from inside Python: the stack Python has used is taken off the
 * CAS's limit (on the calculator both share the 96 KB main stack). */
static cas_status pc_eval(const char *in, char *out, size_t n, size_t used)
{
    size_t lim = hal_cas_stack_limit();
    if (lim)
        cas_set_stack_limit(g.cas, lim > used + (16u << 10) ? lim - used : 16u << 10);
    cas_status s = cas_eval_quiet(g.cas, in, out, n);
    if (lim)
        cas_set_stack_limit(g.cas, lim);
    return s;
}
static const char *pc_error(void) { return cas_last_error(g.cas); }
static void pc_poll(void) { app_cas_poll(NULL); }
static uint32_t pc_breaks(void) { return breaks; }
static void pc_brk(void) { breaks++, cas_interrupt(g.cas); }
static uint32_t pc_stack_limit(void)
{
#if defined(PLATFORM_STM32)
    return CONFIG_PYTHON_STACK_KB << 10;
#else
    return 1u << 20; /* simulators: an 8 MB main thread; the sanitizers make frames large */
#endif
}
static void pc_line_get(char *out, size_t n) { ed_display(&ed, out, n); }
static void pc_line_set(const char *t) { ed_set_text(&ed, t); }
static void pc_line_insert(const char *t) { ed_insert_text(&ed, t, NULL); }
static void pc_line_draw(int x, int y, int cols, bool focus) { ui_draw_editor_line(&ed, x, y, cols, TH_FG, TH_BG, focus); }
static int pc_line_height(void) { return CH; }

static int pc_plot(const char *expr, double xmin, double xmax, char *err, size_t n)
{
    if (!(xmin < xmax)) {
        snprintf(err, n, "xmin must be less than xmax");
        return -1;
    }
    char disp[ED_MAX];
    pretty_from_cas(expr, disp, sizeof disp);
    int slot = -1;
    char cur[ED_MAX * 4];
    for (int i = 0; i < NFUNC && slot < 0; i++) {
        ed_display(&g.f[i].ed, cur, sizeof cur);
        if (!strcmp(cur, disp))
            slot = i;
    }
    for (int i = 0; i < NFUNC && slot < 0; i++)
        if (ed_empty(&g.f[i].ed))
            slot = i;
    if (slot < 0)
        slot = NFUNC - 1;
    gx_set_mode(GM_FUNC);
    ed_set_text(&g.f[slot].ed, disp);
    g.f[slot].enabled = true;
    g.xmin = xmin, g.xmax = xmax;
    funcs_invalidate();
    graph_zoom(7); /* ZoomFit: the y range of the functions */
    pending = PY_SHOW_GRAPH;
    return 0;
}
static void pc_show(int what) { pending = (uint8_t)what; }
static bool pc_shown(void) { return ours(ui_current()); }

/* Everything the Notebook needs from the core (NBIO_* in py_core.h). */
static int pc_nb_io(int op, uint32_t a, void *p, size_t n)
{
    switch (op) {
    case NBIO_STORE_SIZE:
        return hal_storage_size() >= NB_STORE_BASE + NB_STORE_SIZE ? (int)NB_STORE_SIZE : 0;
    case NBIO_READ:
        return a + n <= NB_STORE_SIZE && !hal_storage_read(NB_STORE_BASE + a, p, n) ? 0 : -1;
    case NBIO_ERASE:
    case NBIO_WRITE:
        /* never while code runs in place from the QSPI (a CAS extension module: app/modx.h) */
        if (a + n > NB_STORE_SIZE || cas_ext_busy())
            return -1;
        return (op == NBIO_ERASE ? hal_storage_erase(NB_STORE_BASE + a, n)
                                 : hal_storage_write(NB_STORE_BASE + a, p, n)) ? -1 : 0;
    case NBIO_SCREEN:
        return ui_current() == &scr_notebook ? 1 : ui_current() == &scr_python ? 2 : 0;
    case NBIO_SOFTKEY:
        if (a < 10) {
            snprintf(sk_lab[a], sizeof sk_lab[a], "%s", p ? (const char *)p : "");
            ui_softkeys_invalidate();
        }
        return 0;
    case NBIO_EDITING:
        nb_edit = a != 0;
        return 0;
    case NBIO_TO_CAS: { /* tokenised as typed text, without the undo log seeing the scratch line */
        nbio_text_t *t = p;
        editor_t tmp;
        void (*hook)(editor_t *) = ed_change_hook;
        memset(&tmp, 0, sizeof tmp);
        ed_change_hook = NULL;
        ed_set_text(&tmp, t->in);
        ed_cas(&tmp, t->out, t->n);
        ed_change_hook = hook;
        return 0;
    }
    case NBIO_PRETTY: {
        nbio_text_t *t = p;
        pretty_from_cas(t->in, t->out, t->n);
        return 0;
    }
    case NBIO_MATH:
        return m2d_math_box(p);
    case NBIO_SD_CREATE:
        return modx_nb_sd(0, p);
    case NBIO_SD_REMOVE:
        return modx_nb_sd(1, p);
    case NBIO_SD_LIST:
        return modx_nb_sd(2, p);
    case NBIO_LINE_CURSOR:
        if (a == (uint32_t)-1)
            return ed_cursor_col(&ed);
        ed.cur = 0;
        while (ed.cur < ed.len && ed_col_of(&ed, ed.cur + 1) <= (int)a)
            ed.cur++;
        return 0;
    case NBIO_REFS: {
        nbio_text_t *t = p;
        static char msg[80];
        if (nbd_refs(t->in, t->out, t->n, msg, sizeof msg))
            return 0;
        snprintf(t->out, t->n, "%s", msg);
        return 1;
    }
    case NBIO_VALUE_CAS: {
        nbio_text_t *t = p;
        if (a < (uint32_t)nbd->ncells && nbd_value((int)a)[0] && nbd_to_cas(nbd_value((int)a), t->out, t->n))
            return 0;
        return 1;
    }
    case NBIO_FW_VERSION:
        snprintf(p, n, "%s", fw_version);
        return 0;
    default:
        return usb_nb_io(op, a, p, n);
    }
}

static const py_core_t py_core = {
    PY_CORE_ABI, sizeof(py_core_t),
    pc_eval, pc_error,
    pc_poll, pc_breaks, pc_brk, hal_delay_ms, pc_stack_limit,
    pc_line_get, pc_line_set, pc_line_insert, pc_line_draw, pc_line_height,
    pc_plot, pc_show, pc_shown,
    pc_nb_io,
    &nbdoc_api,
};
const py_core_t *py_core_table(void) { return &py_core; }

/* ------------------------------------------------------------ screens */
/* After every call into the module: the screen switch it asked for. */
static void after(void)
{
    uint8_t p = pending;
    pending = PY_SHOW_NONE;
    if (p == PY_SHOW_GRAPH)
        ui_switch(&scr_graph);
    else if (p == PY_SHOW_HOME && ours(ui_current()))
        ui_switch(ui_home());
}

/* MODX_EV_ENTER: bit 0 returning from a message, bit 1 the Notebook (else the console) */
static void sc_enter(void)
{
    modx_mod_t *m = py_mod();
    if (m)
        modx_event(m, MODX_EV_ENTER, (ui_returning ? 1 : 0) | (ui_current() == &scr_notebook ? 2 : 0));
    after();
}
static void sc_draw(void)
{
    modx_mod_t *m = py_mod();
    int rect[4];
    ui_cursor_hide(); /* (drawing the entry line shows it again) */
    if (!m || !modx_draw(m, 0, rect)) {
        ui_cursor_hide();
        gfx_fill_rect(0, BODY_Y, LCD_WIDTH, BODY_H, TH_BG);
        gfx_text(&font_small, 8, BODY_Y + 8, modx_why(PY_MODULE), TH_FG, -1);
    }
}
static void sc_key(int key, int mod)
{
    modx_mod_t *m = py_mod();
    if (!m) {
        ui_switch(ui_home());
        return;
    }
    bool used = modx_event(m, MODX_EV_KEY, key | mod << 8) || ui_editor_key(&ed, key, mod);
    after();
    if (used && ours(ui_current()))
        sc_draw();
}
static void sc_touch(const touch_event_t *ev)
{
    modx_mod_t *m = py_mod();
    if (m && modx_event(m, MODX_EV_TOUCH, MODX_TOUCH(ev->type, ev->x < 0 ? 0 : ev->type == TEV_TAP ? ev->x0 : ev->x,
                                                     ev->type == TEV_TAP ? ev->y0 : ev->y < 0 ? 0 : ev->y)) == 2)
        sc_draw(); /* the view changed */
    after();
}
static editor_t *sc_editor(void) { return &ed; }
static editor_t *nb_editor(void) { return nb_edit ? &ed : NULL; }
static void sc_help(void) { help_open("python"); }

static const softkeys_t py_soft = {
    {{"run"}, {"tab"}, {"chars"}, {"break"}, {"clear"}},
    {{"dedent"}, {"calc"}, {"vars"}, {NULL}, {"help", sc_help}},
};
const screen_t scr_python = {"python", "PYTHON", sc_enter, sc_draw, sc_key, NULL, sc_editor, sc_touch, "python",
                             &py_soft};

static const softkeys_t *nb_softkeys(void)
{
    for (int i = 0; i < 5; i++) {
        sk_bar.key[i] = (softkey_t){sk_lab[i][0] ? sk_lab[i] : NULL};
        sk_bar.shift[i] = (softkey_t){sk_lab[5 + i][0] ? sk_lab[5 + i] : NULL};
    }
    return &sk_bar;
}
const screen_t scr_notebook = {"notebook", "NOTEBOOK", sc_enter, sc_draw, sc_key, NULL, nb_editor, sc_touch,
                               "notebook", NULL, nb_softkeys};

/* A notebook command from the core (the USB drive): through the module's console op */
int py_nb_call(const char *cmd, const char *arg)
{
    if (modx_busy() || app_in_cas_poll()) /* (module code or a CAS evaluation below us) */
        return -2;
    modx_mod_t *m = py_mod();
    if (!m)
        return -1;
    char *argv[4] = {"nb", (char *)cmd, (char *)arg, NULL};
    int r = modx_console_mod(m, arg ? 3 : 2, argv);
    after();
    return r;
}

/* The Notebook's housekeeping (`nb tick`: saving after a while, the browser page's locks and
 * events) every 100 ms, and its save before the power goes (`nb flush`); only while the module
 * is bound (an unbound module has no notebook open) and not in the middle of module code. */
static int nb_quiet(const char *cmd)
{
    modx_mod_t *m = modx_peek(PY_MODULE);
    if (!m || modx_busy() || app_in_cas_poll())
        return 0;
    char *argv[3] = {"nb", (char *)cmd, NULL};
    int r = modx_console_mod(m, 2, argv);
    after();
    return r;
}
void py_tick(uint32_t now)
{
    static uint32_t last;
    if (now - last < 100)
        return;
    last = now;
    if (nb_quiet("tick") == 1 && ui_current() == &scr_notebook)
        sc_draw();
}
void py_nb_flush(void) { nb_quiet("flush"); }
#else
bool py_available(void) { return false; }
void py_tick(uint32_t now) { (void)now; }
void py_nb_flush(void) {}
const py_core_t *py_core_table(void) { return NULL; }
bool py_nb_shown(void) { return false; }
bool py_nb_following(void) { return false; }
int py_nb_call(const char *cmd, const char *arg)
{
    (void)cmd, (void)arg;
    return -1;
}
#endif

/* APPS > Python: the console, or why there is none. APPS > Notebook: the same module. */
static void open_screen(bool notebook)
{
#if HAVE_PY
    if (py_mod()) {
        ui_switch(notebook ? &scr_notebook : &scr_python);
        return;
    }
    ui_message(notebook ? "NOTEBOOK NOT AVAILABLE" : "PYTHON NOT INSTALLED", modx_why(PY_MODULE));
#else
    (void)notebook;
    ui_message(notebook ? "NOTEBOOK NOT AVAILABLE" : "PYTHON NOT AVAILABLE", "Python is not part of this firmware.");
#endif
}
static void py_open(void) { open_screen(false); }
static void nb_open(void) { open_screen(true); }
void app_open_notebook(void) { open_screen(true); }
const app_def_t app_python = {"python", "Python", py_open, {{0}}, "python"};
const app_def_t app_notebook = {"notebook", "Notebook", nb_open, {{0}}, "notebook"};

/* console `py ...`, `pyexec CODE` and `nb ...`: everything goes to the module */
int py_console(int argc, char **argv)
{
#if HAVE_PY
    modx_mod_t *m = py_mod();
    if (!m)
        return con_error("%s", modx_why(PY_MODULE));
    int r = modx_console_mod(m, argc, argv);
    after();
    if (ours(ui_current()))
        sc_draw();
    return r;
#else
    (void)argc, (void)argv;
    return con_error("Python is not part of this firmware.");
#endif
}

/* `state` keys: py_module (ok, or why not) and the module's own (py_*, nb_*) */
void py_state(void (*kv)(const char *, const char *, void *), void *user)
{
#if HAVE_PY
    modx_mod_t *m = modx_peek(PY_MODULE);
    kv("py_module", m ? "ok" : "unbound", user); /* (asking why would bind it) */
    if (m)
        modx_state(m, kv, user);
#else
    kv("py_module", "not part of this firmware", user);
#endif
}
