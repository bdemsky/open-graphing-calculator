/*
 * Animation: the core's side (app/anim_core.c) of the "animation" code
 * module (modules/animation/; mechanism: app/modx.h).
 *
 * The core knows WHAT can be animated on the shown graph and how to draw one
 * frame of it; the module decides WHEN (play/pause, loop/bounce/once, speed,
 * frame timer, time-outs, frame-rate measurement, the 3D time budget), draws
 * the play bar and records GIF files. Without the module the graph screens
 * work as before and F5 "anim" says how to get it.
 *
 * Targets (anim_target, for the screen that is shown):
 *   ANIM_T_PARAM  2D graph, any mode: the selected slider parameter (a free
 *                 letter of the entries, e.g. t in y = sin(x - t)) from
 *                 `start` to `end`;
 *   ANIM_T_COMET  2D PAR/POL/SEQ with comet mode on: the curves are drawn up
 *                 to a fraction 0..1 of their parameter range (t, θ or n)
 *                 with a dot at the head; sequences show their terms one by
 *                 one;
 *   ANIM_T_G3D    3D graph: the free letter of the entries (t preferred), e.g.
 *                 z = sin(√(x²+y²) − t); surfaces are re-sampled every frame.
 *   ANIM_T_CELL   home screen: the animated graph cell that is selected (its
 *                 functions use t; app/gcell.c), t over the range, seconds
 *                 and mode of its spec's "a" line; the cell's block is drawn
 *                 again each frame (no play bar: its caption shows t).
 * Settings per variable letter (anim_cfg: start, end, seconds per sweep,
 * mode), saved in the session (blob "anim"); a 2D parameter's start/end
 * default to its slider's min/max, the 3D variable's to 0..2π, the comet's
 * range is always 0..1 (entry '@').
 */
#ifndef ANIM_H
#define ANIM_H
#include <stdbool.h>
#include <stdint.h>
#include "ui.h"

#define ANIM_MODULE "animation"

enum { ANIM_SCR_NONE = 0, ANIM_SCR_2D, ANIM_SCR_3D, ANIM_SCR_CELL };
enum { ANIM_LOOP = 0, ANIM_BOUNCE, ANIM_ONCE, ANIM_NMODES };
enum { ANIM_T_NONE = 0, ANIM_T_PARAM, ANIM_T_COMET, ANIM_T_G3D, ANIM_T_CELL };

/* What the shown screen animates (filled by anim_target). Layout is part of
 * the modx ABI. */
struct anim_target {
    uint8_t kind;      /* ANIM_T_* (NONE: nothing to animate here; why in name/msg) */
    uint8_t mode;      /* ANIM_LOOP / BOUNCE / ONCE */
    uint8_t scr;       /* ANIM_SCR_* */
    uint8_t pad;
    char name[4];      /* variable ("t"); comet: "" */
    double start, end; /* value range of one sweep (comet: 0, 1) */
    double secs;       /* seconds per sweep at speed 1 */
    double value;      /* current value */
};
typedef struct anim_target anim_target_t;

/* Settings of one animated variable (saved). */
typedef struct {
    char name;              /* letter; '@' = comet; 0 = free slot */
    double start, end, secs, mode; /* doubles: edited in a form (form.c) */
} anim_cfg_t;
#define ANIM_NCFG 10
/* Settings of `name`, created with start/end lo/hi, 4 s, loop when new
 * (NULL if the table is full and create is false or impossible). */
anim_cfg_t *anim_cfg(char name, bool create, double lo, double hi);
extern bool anim_comet;            /* PAR/POL/SEQ: animate the drawing of the curves */

/* graphing hooks used by the module through the API table */
int anim_target(anim_target_t *t);
uint32_t anim_apply(int kind, double value, int grid);
void anim_finish(int kind, bool reset);

/* screen hooks */
void anim_tick(uint32_t now, int scr);    /* graph screens' tick */
bool anim_input(void);                    /* app.c: key/touch arrived; true = it paused (consume) */
void anim_sleep(void);                    /* before power-off */
bool anim_overlay(int scr, int rect[4]);  /* draw the play bar; rect drawn */
bool anim_playing(void);
const softkeys_t *anim_softkeys(const softkeys_t *base, int scr); /* F5 = anim / pause */
void anim_open(int scr);                  /* F5: the ANIMATION menu (or why it is unavailable) */
bool anim_available(void);                /* the module is there (bound or bindable) */
void anim_cell_play(int i);               /* home: play graph cell i from its t (or why not) */
void home_anim_frame(void);               /* home.c: the animated cell's block drawn again */
void anim_module_changed(void);           /* store changed / modules unbound */
int anim_console(int argc, char **argv);  /* console `anim ...` */
void anim_state(void (*kv)(const char *, const char *, void *), void *user);
extern const struct persist_def persist_anim;

/* settings form (form.c): the animation rows of the slider form, and the
 * form for the 3D variable / the comet */
void anim_settings_open(int scr);

/* graph screens (graph.c, g3d_screen.c) */
void graph_anim_frame(void);      /* re-plot, follow TRACE and CALC, decorations */
void g3d_anim_frame(int grid);    /* re-evaluate with the time value, render, present */
/* 3D model (g3d_model.c): the animated variable while playing ("" = none) */
void g3_anim_set(const char *name, double value, int grid);
void g3_anim_keep(bool restore);  /* its value kept aside (false) and back (true): a graph cell drawn */
char g3_anim_var(void);
double g3_anim_value(void);       /* its value now (the setting's start until played) */           /* free letter of the 3D entries ('t' preferred), 0 if none */
/* 2D plotting (gr_plot.c): comet fraction 0..1, NAN = whole curves */
extern double gr_comet;
/* 2D model (gr_model.c): single letters of CAS text */
void gx_scan_letters(const char *s, void (*cb)(char c, void *u), void *u);

#endif
