/*
 * Animation, core side (see anim.h): what the shown graph animates, drawing
 * one frame of it, the settings (saved), the F5 "anim" soft key and the
 * ANIMATION menu of the GRAPH and 3D GRAPH screens, the console `anim`, and
 * the fallback when the "animation" code module is not available.
 *
 * Everything that runs per frame or per tick lives in the module
 * (modules/animation/anim_mod.c), reached through app/modx.h; this file only
 * forwards events to it (tick, input, sleep, overlay) and answers its calls
 * through the API table (anim_target, anim_apply, anim_finish). Without the
 * module: F5 "anim" shows "Install the Animation module (Memory > Modules)."
 * and nothing else changes.
 *
 * State: the settings table cfg[] and anim_comet (blob "anim", persist_anim);
 * gr_comet (gr_plot.c) and the 3D time value (g3d_model.c) are set while a
 * module plays. Main loop only.
 */
#include "anim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "g3d.h"
#include "graph.h"
#include "hal.h"
#include "modx.h"
#include "nbdoc.h"
#include "registry.h"
#include "session.h"

#if defined(CONFIG_CONTENT_QSPI)
#define HAVE_MODULES 1
#else
#define HAVE_MODULES 0
#endif

bool anim_comet;
static anim_cfg_t cfg[ANIM_NCFG];

/* ------------------------------------------------------------ settings */
anim_cfg_t *anim_cfg(char name, bool create, double lo, double hi)
{
    for (int i = 0; i < ANIM_NCFG; i++)
        if (cfg[i].name == name)
            return &cfg[i];
    if (!create)
        return NULL;
    int k = 0;
    while (k < ANIM_NCFG - 1 && cfg[k].name) /* full: the last slot is reused */
        k++;
    cfg[k] = (anim_cfg_t){name, lo, hi, 4, ANIM_LOOP};
    return &cfg[k];
}

static int cfg_mode(const anim_cfg_t *c)
{
    int m = (int)lround(c->mode);
    return m >= 0 && m < ANIM_NMODES ? m : ANIM_LOOP;
}

static int cur_scr(void)
{
    const screen_t *s = ui_current();
    return s == &scr_graph ? ANIM_SCR_2D : s == &scr_g3d ? ANIM_SCR_3D
           : s == &scr_home && gcell_anim_cell() >= 0 ? ANIM_SCR_CELL : ANIM_SCR_NONE;
}

static void redraw(void)
{
    const screen_t *s = ui_current();
    if (s && s->draw)
        s->draw();
}

static bool comet_mode(void) { return gx.mode == GM_PAR || gx.mode == GM_POL || (gx.mode == GM_SEQ); }

/* ------------------------------------------------------------ API: target, frame */
/* What the shown screen would animate (anim.h); kind ANIM_T_NONE if nothing. */
int anim_target(anim_target_t *t)
{
    memset(t, 0, sizeof *t);
    t->scr = (uint8_t)cur_scr();
    const anim_cfg_t *c = NULL;
    if (t->scr == ANIM_SCR_2D) {
        gx_compile();
        int k = gx.sel_param;
        if (anim_comet && comet_mode()) {
            t->kind = ANIM_T_COMET;
            c = anim_cfg('@', true, 0, 1);
            t->start = 0, t->end = 1;
            t->value = isnan(gr_comet) ? 1 : gr_comet;
        } else if (k < GNPARAM && gx.prm[k].used && gx.prm[k].shown) {
            const gparam_t *p = &gx.prm[k];
            t->kind = ANIM_T_PARAM;
            t->name[0] = p->name[0];
            c = anim_cfg(p->name[0], true, p->min, p->max);
            t->start = c->start, t->end = c->end;
            t->value = p->val;
        }
    } else if (t->scr == ANIM_SCR_3D) {
        char v = g3_anim_var();
        if (v) {
            t->kind = ANIM_T_G3D;
            t->name[0] = v;
            c = anim_cfg(v, true, 0, 2 * M_PI);
            t->start = c->start, t->end = c->end;
            t->value = g3_anim_value();
        }
    } else if (t->scr == ANIM_SCR_CELL) { /* a graph cell of the home screen: its own settings */
        double an[4];
        gcell_anim_take(gcell_anim_cell(), an);
        t->kind = ANIM_T_CELL;
        t->name[0] = 't';
        t->start = an[0], t->end = an[1], t->secs = an[2], t->mode = (uint8_t)an[3];
        t->value = gcell_anim_t();
    }
    if (c) {
        t->mode = (uint8_t)cfg_mode(c);
        t->secs = c->secs > 0.05 ? c->secs : 4;
    }
    return t->kind;
}

/* Set the animated value and draw one frame (API: anim_apply). grid: 3D grid
 * cells per side for this frame (0 = the setting). Returns the frame's
 * drawing time in microseconds. */
uint32_t anim_apply(int kind, double value, int grid)
{
    uint32_t t0 = hal_cycles();
    switch (kind) {
    case ANIM_T_PARAM: {
        if (cur_scr() != ANIM_SCR_2D)
            return 0;
        int k = gx.sel_param;
        if (k >= GNPARAM || !gx.prm[k].used || !isfinite(value))
            return 0;
        gx.prm[k].val = value; /* the animation range may exceed the slider's */
        gx_params_changed();
        graph_anim_frame();
        break;
    }
    case ANIM_T_COMET:
        if (cur_scr() != ANIM_SCR_2D)
            return 0;
        gr_comet = value < 0 ? 0 : value > 1 ? 1 : value;
        graph_anim_frame();
        break;
    case ANIM_T_G3D: {
        if (cur_scr() != ANIM_SCR_3D)
            return 0;
        char nm[2] = {g3_anim_var(), 0};
        g3_anim_set(nm, value, grid);
        g3d_anim_frame(grid);
        break;
    }
    case ANIM_T_CELL:
        if (cur_scr() != ANIM_SCR_CELL || !isfinite(value))
            return 0;
        gcell_anim_set_t(value);
        home_anim_frame();
        break;
    default:
        return 0;
    }
    return (hal_cycles() - t0) / hal_cycles_per_us();
}

/* Playing paused or stopped (API: anim_finish): a parameter's value goes to
 * its CAS variable (a:=v, like the end of a slider drag); reset = the target
 * goes back to its plain state (comet: whole curves; 3D: full grid). */
void anim_finish(int kind, bool reset)
{
    switch (kind) {
    case ANIM_T_PARAM:
        if (gx.sel_param < GNPARAM && gx.prm[gx.sel_param].used)
            gx_param_commit(gx.sel_param);
        break;
    case ANIM_T_COMET:
        if (reset && !isnan(gr_comet)) {
            gr_comet = NAN;
            g.graph_valid = false;
            if (cur_scr() == ANIM_SCR_2D)
                graph_anim_frame();
        }
        break;
    case ANIM_T_G3D: {
        char nm[2] = {g3_anim_var(), 0}, cmd[48], out[32];
        g3_anim_set(nm, g3_anim_value(), 0); /* full grid again */
        if (nm[0]) { /* the value goes to the variable, as for a 2D slider */
            snprintf(cmd, sizeof cmd, "%s:=%.15g", nm, g3_anim_value());
            cas_eval_quiet(g.cas, cmd, out, sizeof out);
        }
        g3d_redraw();
        if (cur_scr() == ANIM_SCR_3D)
            g3d_anim_frame(0);
        break;
    }
    case ANIM_T_CELL: /* (the cell keeps its frame; its caption no longer says ▶) */
        if (cur_scr() == ANIM_SCR_CELL)
            home_anim_frame();
        break;
    }
}

/* ------------------------------------------------------------ hooks */
static modx_mod_t *bound(void) { return HAVE_MODULES ? modx_peek(ANIM_MODULE) : NULL; }

void anim_tick(uint32_t now, int scr)
{
    if (!HAVE_MODULES)
        return;
    modx_mod_t *m = modx_get(ANIM_MODULE);
    if (m)
        modx_tick(m, now, scr);
}

bool anim_input(void)
{
    modx_mod_t *m = bound();
    return m && modx_event(m, MODX_EV_INPUT, 0) == 1;
}

void anim_sleep(void)
{
    modx_mod_t *m = bound();
    if (m)
        modx_event(m, MODX_EV_SLEEP, 0);
}

bool anim_overlay(int scr, int rect[4])
{
    modx_mod_t *m = bound();
    return m && modx_draw(m, scr, rect);
}

bool anim_playing(void)
{
    modx_mod_t *m = bound();
    return m && (modx_event(m, MODX_EV_QUERY, 0) & MODX_ST_PLAYING);
}

void anim_module_changed(void)
{
    if (!isnan(gr_comet)) {
        gr_comet = NAN;
        g.graph_valid = false;
    }
    ui_softkeys_invalidate();
}

/* ------------------------------------------------------------ soft key + menu */
static int menu_scr; /* screen the ANIMATION menu was opened from */
enum { A_PLAY = 1, A_MODE, A_COMET, A_SET, A_REC, A_STOP };

/* fixed item numbers (1 play ... 6 stop), so a number key always means the same */
static int am_count(void) { return 6; }

static void am_get(int i, menu_item_t *out)
{
    static char b[40];
    static const char *const modes[] = {"loop", "bounce", "once"};
    int a = i + 1;
    anim_target_t t;
    anim_target(&t);
    const char *lbl = b;
    switch (a) {
    case A_PLAY: lbl = "▶ Play"; break;
    case A_MODE: snprintf(b, sizeof b, "Mode: %s", t.kind ? modes[t.mode % 3] : "-"); break;
    case A_COMET:
        if (menu_scr == ANIM_SCR_2D && comet_mode())
            snprintf(b, sizeof b, "Comet (draw along %s): %s", gx_indep_name(), anim_comet ? "on" : "off");
        else
            lbl = "Comet (PAR, POL, SEQ graphs)";
        break;
    case A_SET: lbl = "Settings: range, speed"; break;
    case A_REC: lbl = "Record GIF to microSD"; break;
    default: lbl = "Stop (back to the start)"; break;
    }
    *out = (menu_item_t){lbl, NULL, NULL, a, "animation"};
}

static void am_select(const menu_item_t *it)
{
    menu_close();
    modx_mod_t *m = modx_get(ANIM_MODULE);
    switch (it->action) {
    case A_PLAY: modx_event(m, MODX_EV_PLAY, 0); break;
    case A_MODE: {
        anim_target_t t;
        if (anim_target(&t)) {
            anim_cfg_t *c = anim_cfg(t.kind == ANIM_T_COMET ? '@' : t.name[0], true, t.start, t.end);
            c->mode = (cfg_mode(c) + 1) % ANIM_NMODES;
            static const char *const modes[] = {"mode: loop", "mode: bounce", "mode: once"};
            ui_toast(modes[cfg_mode(c)]);
        }
        break;
    }
    case A_COMET:
        if (!(menu_scr == ANIM_SCR_2D && comet_mode())) {
            ui_toast("comet: PAR, POL and SEQ graphs only");
            break;
        }
        modx_event(m, MODX_EV_STOP, 0);
        anim_comet = !anim_comet;
        gr_comet = NAN;
        g.graph_valid = false;
        redraw();
        ui_toast(anim_comet ? "comet on: F5 1 plays" : "comet off");
        break;
    case A_SET: anim_settings_open(menu_scr); break;
    case A_REC: modx_event(m, MODX_EV_RECORD, 0); break;
    case A_STOP: modx_event(m, MODX_EV_STOP, 0); break;
    }
    ui_softkeys_invalidate();
}

static const menu_def_t menu_anim = {"ANIMATION", NULL, 0, am_select, false, "animation", am_count, am_get};

/* F5 on GRAPH / 3D GRAPH: pause if playing, else the ANIMATION menu; without
 * the module a message says how to get it. */
void anim_open(int scr)
{
    modx_mod_t *m = modx_get(ANIM_MODULE);
    if (!m) {
        ui_message("ANIMATION NOT INSTALLED", modx_why(ANIM_MODULE));
        return;
    }
    if (modx_event(m, MODX_EV_QUERY, 0) & MODX_ST_PLAYING) {
        modx_event(m, MODX_EV_PAUSE, 0);
        ui_softkeys_invalidate();
        return;
    }
    menu_scr = scr;
    menu_open(&menu_anim);
}

bool anim_available(void) { return HAVE_MODULES && modx_get(ANIM_MODULE); }

/* The home screen's play key on an animated graph cell: it plays from the t it shows (the module's
 * earlier target stopped first); without the module a message says how to get it. */
void anim_cell_play(int i)
{
    modx_mod_t *m = HAVE_MODULES ? modx_get(ANIM_MODULE) : NULL;
    if (!m) {
        ui_message("ANIMATION NOT INSTALLED", HAVE_MODULES ? modx_why(ANIM_MODULE)
                                                           : "Animation needs the Animation module (not in this firmware).");
        return;
    }
    double an[4];
    gcell_anim_take(i, an);
    modx_event(m, MODX_EV_STOP, 0);
    modx_event(m, MODX_EV_PLAY, 0);
    ui_softkeys_invalidate();
}

static void f5_2d(void) { anim_open(ANIM_SCR_2D); }
static void f5_3d(void) { anim_open(ANIM_SCR_3D); }

/* The graph screens' bar with F5 "anim" (or "pause" while playing) instead
 * of the "graph" key that would only re-show the screen. Core tier: the bar
 * as it is. */
static void back_home(void) { ui_switch(ui_home()); } /* (a bound graph cell is written back: home.c) */

const softkeys_t *anim_softkeys(const softkeys_t *base, int scr)
{
    if (!HAVE_MODULES)
        return base;
    static softkeys_t sk[2];
    softkeys_t *s = &sk[scr == ANIM_SCR_3D];
    *s = *base;
    /* F5 on the graph screens goes back to the notebook; the animation is on SHIFT F5 (the 2D
     * graph's "table" there gives way: it stays on SHIFT F5 of y=, window and the other screens) */
    s->key[4] = (softkey_t){"back", back_home};
    s->shift[4] = (softkey_t){anim_playing() ? "pause" : "anim", scr == ANIM_SCR_3D ? f5_3d : f5_2d};
    return s;
}

/* APPS > Animation (also in CATALOG, Tools): the ANIMATION menu of the graph
 * screen that is shown, else of GRAPH. */
static void anim_app_open(void)
{
    int scr = cur_scr();
    if (scr == ANIM_SCR_NONE || scr == ANIM_SCR_CELL) {
        ui_switch(&scr_graph);
        scr = ANIM_SCR_2D;
    }
    anim_open(scr);
}
const app_def_t app_animation = {"animation", "Animation", anim_app_open, {{0}}, "animation"};

/* ------------------------------------------------------------ console, state */
/* anim comet on|off | cfg NAME START END SECS MODE (core); everything else
 * (play, pause, step N, stop, rec, ...) goes to the module. */
int anim_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "";
    if (!strcmp(sub, "comet") && argc > 2) {
        anim_comet = !strcmp(argv[2], "on");
        gr_comet = NAN;
        g.graph_valid = false;
        redraw();
        return 0;
    }
    if (!strcmp(sub, "cfg") && argc > 6) {
        double v[4];
        for (int i = 0; i < 4; i++) {
            char err[48];
            if (eval_number(argv[3 + i], &v[i], err, sizeof err))
                return con_error("%s: %s", argv[3 + i], err);
        }
        anim_cfg_t *c = anim_cfg(argv[2][0], true, v[0], v[1]);
        c->start = v[0], c->end = v[1], c->secs = v[2], c->mode = v[3];
        return 0;
    }
    modx_mod_t *m = HAVE_MODULES ? modx_get(ANIM_MODULE) : NULL;
    if (!m)
        return con_error("%s", modx_why(ANIM_MODULE));
    int r = modx_console_mod(m, argc, argv);
    ui_softkeys_invalidate();
    ui_draw_softkeys();
    return r;
}

/* `state` keys: anim_module (ok, or why not), anim_comet, the module's own
 * (anim_playing, anim_value, anim_fps, ...). */
void anim_state(void (*kv)(const char *, const char *, void *), void *user)
{
    modx_mod_t *m = bound();
    kv("anim_module", m ? "ok" : HAVE_MODULES ? modx_why(ANIM_MODULE) : "core tier", user);
    kv("anim_comet", anim_comet ? "1" : "0", user);
    if (m)
        modx_state(m, kv, user);
}

/* ------------------------------------------------------------ saved state */
/* Blob "anim" version 1: u8 comet, u8 count, per entry: u8 name, f64 start,
 * end, secs, mode. */
static void an_save(pw_t *w)
{
    int n = 0;
    for (int i = 0; i < ANIM_NCFG; i++)
        n += cfg[i].name != 0;
    pw_u8(w, anim_comet);
    pw_u8(w, (uint32_t)n);
    for (int i = 0; i < ANIM_NCFG; i++)
        if (cfg[i].name) {
            pw_u8(w, (uint8_t)cfg[i].name);
            pw_f64(w, cfg[i].start);
            pw_f64(w, cfg[i].end);
            pw_f64(w, cfg[i].secs);
            pw_f64(w, cfg[i].mode);
        }
}

static void an_reset(void)
{
    memset(cfg, 0, sizeof cfg);
    anim_comet = false;
    gr_comet = NAN;
}

static void an_load(pr_t *r, unsigned version)
{
    (void)version;
    an_reset();
    anim_comet = pr_u8(r) != 0;
    unsigned n = pr_u8(r);
    for (unsigned i = 0; i < n && !r->err; i++) {
        anim_cfg_t c;
        c.name = (char)pr_u8(r);
        c.start = pr_f64(r), c.end = pr_f64(r), c.secs = pr_f64(r), c.mode = pr_f64(r);
        if (!r->err && i < ANIM_NCFG && c.name && isfinite(c.start) && isfinite(c.end) && c.secs > 0)
            cfg[i] = c;
    }
}

const persist_def_t persist_anim = {"anim", 1, an_save, an_load, an_reset};
