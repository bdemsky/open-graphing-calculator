/*
 * The Animation code module ("animation", interface modx ABI 1; mechanism in
 * app/modx.h, the core's side in app/anim_core.c, design in
 * docs/QSPI_CONTENT.md section 12).
 *
 * What it does: plays the value the shown graph can animate (anim_target in
 * the core: a 2D slider parameter such as t in y = sin(x - t), the comet
 * drawing of PAR/POL/SEQ curves, the free variable of the 3D entries, t of an animated graph cell of the home screen) from
 * start to end with the frame timer, in loop, bounce or once mode at a speed
 * factor; pauses on any key or touch, when the screen is left, before the
 * calculator sleeps, and after PLAY_TIMEOUT_MS of playing without input;
 * measures the frame rate; in 3D keeps each frame within FRAME_BUDGET_US by
 * re-sampling the surfaces on a coarser grid; draws the play bar; records the
 * animation as an animated GIF on the microSD card (/CALC/ANIMnnn.GIF,
 * gif.c).
 *
 * Image rules (it runs in place from the QSPI on the hardware): no static RW
 * data (everything is in anim_rw_t, which the core allocates and zeroes), no
 * library calls (only the API table: rw->api), no pointers in constant tables
 * (the image is position-independent; tables hold characters, not char *).
 *
 * Timing. tick() runs every main-loop pass while a graph screen is shown; a
 * frame is drawn when FRAME_MS have passed since the last one. The phase
 * (0..1 along one sweep) advances by the real time elapsed (at most 4 frame
 * periods per frame, so a slow frame does not jump) x speed / seconds per
 * sweep. `anim step N` (console, tests) advances exactly N frames of
 * FRAME_MS each, independent of the clock; GIF frames are 1/GIF_FPS s apart.
 *
 * Frame rate: frames drawn in the last full second (anim_fps), plus the last
 * frame's drawing time (anim_frame_us) and the 3D grid used (anim_grid).
 */
#include <stdint.h>
#include "anim.h"
#include "gif.h"
#include "modx.h"

#define FRAME_MS 40                 /* frame timer: at most 25 frames per second */
#define PLAY_TIMEOUT_MS 120000u     /* stop playing after 2 minutes without input (battery) */
#define FRAME_BUDGET_US 80000u      /* 3D: aim for >= ~12 frames per second */
#define GIF_FPS 10                  /* recorded frames per second (delay 10 cs) */
#define GIF_MAX_FRAMES 120          /* bound on a recording (also the card space) */

/* colours (RGB565; the core's gfx maps them for the dark theme) */
#define COL_BG 0xFFFF
#define COL_FG 0x0000
#define COL_FRAME 0x8410
#define COL_PLAY 0x0400
#define COL_REC 0xD000

typedef struct {
    const modx_api_t *api;
    anim_target_t tgt;      /* what is being animated (kind NONE: idle) */
    bool active;            /* tgt is set (playing or paused mid-way) */
    bool playing;
    bool recording;         /* GIF frames being written (the play bar is hidden) */
    int8_t dir;             /* bounce: +1 forwards, -1 backwards */
    double phase;           /* 0..1 along the sweep */
    double speed;           /* factor (1 = one sweep in tgt.secs) */
    uint32_t last_frame;    /* ms of the last frame */
    uint32_t play_since;    /* ms of the last play / input (time-out) */
    /* statistics */
    uint32_t frames, frame_us, sec_start, sec_frames;
    uint32_t fps10;         /* frames per second x 10 over the last full second */
    int grid;               /* 3D grid of the last frame (0 = the setting) */
    uint32_t budget_us;     /* 3D frame time budget (FRAME_BUDGET_US; console `anim budget`) */
    /* last recording */
    char gif_path[24];
    uint32_t gif_bytes;
    int gif_frames;
    gif_t gif;
} anim_rw_t;

/* the size the core allocates (host builds; the image declares it in its descriptor) */
const uint32_t anim_module_rw_size = sizeof(anim_rw_t);

/* ------------------------------------------------------------ helpers */
static double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static double value_at(const anim_rw_t *a, double phase) { return a->tgt.start + phase * (a->tgt.end - a->tgt.start); }

/* Set up the target of the shown screen; false (with a message unless quiet)
 * if there is nothing to animate. The phase starts at the current value. */
static bool setup(anim_rw_t *a, bool quiet)
{
    const modx_api_t *api = a->api;
    anim_target_t t;
    if (!api->anim_target(&t)) {
        if (!quiet)
            api->message("NOTHING TO ANIMATE",
                         t.scr == ANIM_SCR_3D ? "Use a free variable in z=, e.g. z=sin(x-t): t is animated."
                                              : "Use a free variable in y=, e.g. y=sin(x-t): its slider is "
                                                "animated. PAR, POL and SEQ can also draw as a comet (F5).");
        return false;
    }
    bool same = a->active && t.kind == a->tgt.kind && t.name[0] == a->tgt.name[0];
    a->tgt = t;
    if (!same) {
        double span = t.end - t.start;
        a->phase = span != 0 ? clamp01((t.value - t.start) / span) : 0;
        if (t.kind == ANIM_T_COMET && t.value >= 1)
            a->phase = 0; /* a whole curve: start drawing from the beginning */
        a->dir = 1;
        a->grid = 0;
    }
    a->active = true;
    return true;
}

/* Advance the phase by dt seconds of play; false when "once" reached the end. */
static bool advance(anim_rw_t *a, double dt)
{
    double secs = a->tgt.secs > 0 ? a->tgt.secs : 4;
    double d = dt * (a->speed > 0 ? a->speed : 1) / secs;
    switch (a->tgt.mode) {
    case ANIM_BOUNCE:
        a->phase += a->dir * d;
        while (a->phase > 1 || a->phase < 0) { /* reflect at the ends */
            if (a->phase > 1)
                a->phase = 2 - a->phase, a->dir = -1;
            else
                a->phase = -a->phase, a->dir = 1;
        }
        return true;
    case ANIM_ONCE:
        a->phase += d;
        if (a->phase >= 1) {
            a->phase = 1;
            return false;
        }
        return true;
    default: /* loop */
        a->phase += d;
        a->phase -= a->api->floor(a->phase);
        return true;
    }
}

/* Draw one frame at the current phase; 3D frames adapt their grid to the
 * time budget (the grid shrinks by a quarter while frames are too slow and
 * grows back while they take less than a third of it). */
static void do_frame(anim_rw_t *a)
{
    const modx_api_t *api = a->api;
    int grid = 0;
    if (a->tgt.kind == ANIM_T_G3D && a->playing) {
        int full = api->g3_grid();
        grid = a->grid ? a->grid : full;
        if (a->frames && a->frame_us >= a->budget_us && grid > 8)
            grid = grid * 3 / 4;
        else if (a->frames && a->frame_us < a->budget_us / 3 && grid < full)
            grid = grid + (grid + 3) / 4 > full ? full : grid + (grid + 3) / 4;
        a->grid = grid >= full ? 0 : grid;
        grid = a->grid;
    }
    a->frame_us = api->anim_apply(a->tgt.kind, value_at(a, a->phase), grid);
    a->frames++;
    uint32_t now = api->millis();
    if (now - a->sec_start >= 1000) {
        a->fps10 = a->sec_frames * 10000u / (now - a->sec_start); /* 32-bit: no library division */
        a->sec_start = now, a->sec_frames = 0;
    }
    a->sec_frames++;
}

static void do_pause(anim_rw_t *a)
{
    if (!a->playing)
        return;
    a->playing = false;
    if (a->grid && a->tgt.kind == ANIM_T_G3D) {
        a->grid = 0;
        a->api->anim_apply(a->tgt.kind, value_at(a, a->phase), 0); /* the paused frame at full detail */
    }
    a->api->anim_finish(a->tgt.kind, false);
    a->api->softkeys_changed();
}

static void do_play(anim_rw_t *a)
{
    if (!setup(a, false))
        return;
    if (a->tgt.mode == ANIM_ONCE && a->phase >= 1)
        a->phase = 0; /* played to the end: start again */
    a->playing = true;
    a->last_frame = a->play_since = a->sec_start = a->api->millis();
    a->sec_frames = 0;
    a->api->softkeys_changed();
    do_frame(a);
}

/* Back to the plain graph: the comet shows whole curves again, 3D its full
 * grid; a parameter keeps its value. */
static void do_stop(anim_rw_t *a)
{
    bool was = a->active;
    a->playing = false;
    a->active = false;
    if (was)
        a->api->anim_finish(a->tgt.kind, true);
    a->grid = 0;
    a->api->softkeys_changed();
}

/* ------------------------------------------------------------ GIF */
static int gif_out(void *user, const void *buf, size_t n)
{
    anim_rw_t *a = user;
    return a->api->file_write(buf, n);
}

/* Record one cycle (loop: a sweep; bounce: there and back; once: to the end)
 * at GIF_FPS frames per second, at most max_frames (<= GIF_MAX_FRAMES), as
 * /CALC/ANIMnnn.GIF; a key press ends it early (the file stays valid). The
 * plot area (below the status bar, above the soft keys) is recorded, without
 * the play bar. Reports with a message box; returns 0 or an error. */
static int record(anim_rw_t *a, int max_frames)
{
    const modx_api_t *api = a->api;
    do_pause(a);
    if (!setup(a, false))
        return -1;
    double secs = a->tgt.secs > 0 ? a->tgt.secs : 4;
    int per_sweep = (int)(secs * GIF_FPS / (a->speed > 0 ? a->speed : 1) + 0.5);
    if (per_sweep < 2)
        per_sweep = 2;
    int n = a->tgt.mode == ANIM_BOUNCE ? 2 * per_sweep : a->tgt.mode == ANIM_ONCE ? per_sweep + 1 : per_sweep;
    if (max_frames <= 0 || max_frames > GIF_MAX_FRAMES)
        max_frames = GIF_MAX_FRAMES;
    if (n > max_frames)
        n = max_frames;
    char msg[96];
    int r = api->file_create("ANIM", "GIF", a->gif_path, sizeof a->gif_path);
    if (r) {
        api->card_error(r, msg, sizeof msg);
        api->message("RECORDING FAILED", msg);
        a->gif_path[0] = 0;
        return r;
    }
    a->recording = true;
    a->dir = 1;
    const int x0 = 0, y0 = STATUS_H, w = LCD_WIDTH, h = LCD_HEIGHT - STATUS_H - SOFT_H;
    int k = 0;
    for (; k < n && !r; k++) {
        double ph = a->tgt.mode == ANIM_BOUNCE ? (k < per_sweep ? (double)k / per_sweep : 2.0 - (double)k / per_sweep)
                                                : (double)k / per_sweep;
        a->phase = clamp01(ph);
        a->frame_us = api->anim_apply(a->tgt.kind, value_at(a, a->phase), 0);
        api->flush();
        const uint16_t *fb = api->framebuffer() + y0 * LCD_WIDTH + x0;
        r = k == 0 ? gif_begin(&a->gif, w, h, fb, LCD_WIDTH, gif_out, a) : 0;
        if (!r)
            r = gif_frame(&a->gif, fb, LCD_WIDTH, 100 / GIF_FPS);
        api->watchdog_kick();
        if (k > 0 && api->input_pending())
            k = n; /* stopped by a key: keep what is there */
    }
    if (!r)
        r = gif_end(&a->gif);
    a->recording = false;
    int c = api->file_close(r == 0);
    if (!r)
        r = c;
    a->gif_frames = a->gif.frames;
    a->gif_bytes = a->gif.bytes;
    if (r) {
        api->card_error(r, msg, sizeof msg);
        api->message("RECORDING FAILED", msg);
        a->gif_path[0] = 0;
        return r;
    }
    api->snprintf(msg, sizeof msg, "%s: %d frames, %lu KB.", a->gif_path, a->gif_frames,
                  (unsigned long)((a->gif_bytes + 1023) / 1024));
    api->anim_apply(a->tgt.kind, value_at(a, a->phase), 0); /* the plain frame again */
    api->message("RECORDED", msg);
    return 0;
}

/* ------------------------------------------------------------ ops */
static void op_tick(void *rw, uint32_t now, int scr)
{
    anim_rw_t *a = rw;
    if (!a->playing)
        return;
    if (scr != a->tgt.scr) { /* another graph screen: stop there */
        do_pause(a);
        return;
    }
    if (now - a->play_since > PLAY_TIMEOUT_MS) {
        do_pause(a);
        a->api->toast("animation paused after 2 minutes");
        return;
    }
    uint32_t dt = now - a->last_frame;
    if (dt < FRAME_MS)
        return;
    a->last_frame = now;
    if (dt > 4 * FRAME_MS)
        dt = 4 * FRAME_MS;
    bool more = advance(a, dt / 1000.0);
    do_frame(a);
    if (!more)
        do_pause(a);
}

static int op_event(void *rw, int ev, int arg)
{
    anim_rw_t *a = rw;
    (void)arg;
    switch (ev) {
    case MODX_EV_QUERY: return (a->playing ? MODX_ST_PLAYING : 0) | (a->recording ? MODX_ST_RECORDING : 0);
    case MODX_EV_INPUT:
        if (!a->playing)
            return 0;
        do_pause(a);
        return 1;
    case MODX_EV_LEAVE:
    case MODX_EV_SLEEP:
    case MODX_EV_PAUSE: do_pause(a); return 0;
    case MODX_EV_PLAY: do_play(a); return 0;
    case MODX_EV_TOGGLE:
        if (a->playing)
            do_pause(a);
        else
            do_play(a);
        return 0;
    case MODX_EV_STOP: do_stop(a); return 0;
    case MODX_EV_RECORD: return record(a, 0);
    }
    return 0;
}

/* The play bar at the top right of the plot: ▶ (playing) or two bars
 * (paused), the variable and its value ("t=1.25", comet: the percentage),
 * the speed if not 1. */
static bool op_draw(void *rw, int scr, int rect[4])
{
    anim_rw_t *a = rw;
    const modx_api_t *api = a->api;
    if (!a->active || a->recording || scr != a->tgt.scr)
        return false;
    char v[24], t[48];
    double val = value_at(a, a->phase);
    if (a->tgt.kind == ANIM_T_COMET) {
        api->snprintf(t, sizeof t, "%d%%", (int)(a->phase * 100 + 0.5));
    } else {
        api->fmt_num(val, 7, v, sizeof v);
        api->snprintf(t, sizeof t, "%s=%s", a->tgt.name, v);
    }
    if (a->speed > 0 && a->speed != 1) {
        size_t n = 0;
        while (t[n])
            n++;
        api->snprintf(t + n, sizeof t - n, " x%g", a->speed);
    }
    const font_t *f = api->font_small;
    int tw = api->text_width(f, t), w = tw + 22, h = 14;
    int x = LCD_WIDTH - w - 2, y = STATUS_H + 18;
    api->fill_rect(x, y, w, h, COL_BG);
    api->rect(x, y, w, h, COL_FRAME);
    if (a->playing) {
        api->text(f, x + 3, y + 1, "▶", COL_PLAY, -1);
    } else {
        api->fill_rect(x + 4, y + 3, 3, 8, COL_FG);
        api->fill_rect(x + 9, y + 3, 3, 8, COL_FG);
    }
    api->text(f, x + 17, y + 1, t, COL_FG, -1);
    rect[0] = x, rect[1] = y, rect[2] = w, rect[3] = h;
    return true;
}

/* A non-negative decimal number (no library: strtod is not in the API);
 * -1 if s is not one. */
static double parse_dec(const char *s)
{
    double v = 0, scale = 0;
    if (!*s)
        return -1;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (scale > 0)
                v += (*s - '0') * scale, scale /= 10;
            else
                v = v * 10 + (*s - '0');
        } else if (*s == '.' && scale == 0) {
            scale = 0.1;
        } else {
            return -1;
        }
    }
    return v;
}

/* anim play|pause|toggle|stop | step [N] | seek PHASE | speed X | rec [N] | info */
static int op_console(void *rw, int argc, char **argv)
{
    anim_rw_t *a = rw;
    const modx_api_t *api = a->api;
    const char *sub = argc > 1 ? argv[1] : "info";
    long n = 1;
    if (!api->strcmp(sub, "play") || !api->strcmp(sub, "toggle")) {
        if (!api->strcmp(sub, "play") || !a->playing)
            do_play(a);
        else
            do_pause(a);
        return a->playing || !api->strcmp(sub, "toggle") ? 0 : api->con_error("nothing to animate here");
    }
    if (!api->strcmp(sub, "pause")) {
        do_pause(a);
        return 0;
    }
    if (!api->strcmp(sub, "stop")) {
        do_stop(a);
        return 0;
    }
    if (!api->strcmp(sub, "speed") && argc > 2) {
        double s = parse_dec(argv[2]);
        if (!(s >= 0.05 && s <= 20))
            return api->con_error("speed 0.05 to 20");
        a->speed = s;
        return 0;
    }
    if (!api->strcmp(sub, "budget") && argc > 2) { /* 3D frame budget in us (tests: 0 = every frame is over it) */
        if (!api->con_parse_int(argv[2], &n) || n < 0)
            return api->con_error("bad budget");
        a->budget_us = (uint32_t)n;
        return 0;
    }
    if (!api->strcmp(sub, "seek") && argc > 2) { /* jump to a phase 0..1 of the sweep and draw it */
        double ph = parse_dec(argv[2]);
        if (!(ph >= 0 && ph <= 1))
            return api->con_error("phase 0 to 1");
        if (!setup(a, true))
            return api->con_error("nothing to animate here");
        a->phase = ph;
        a->dir = 1;
        do_frame(a);
        if (!a->playing)
            a->api->anim_finish(a->tgt.kind, false);
        return 0;
    }
    if (!api->strcmp(sub, "step")) {
        if (argc > 2 && (!api->con_parse_int(argv[2], &n) || n < 1 || n > 100000))
            return api->con_error("bad frame count");
        if (!a->active && !setup(a, true))
            return api->con_error("nothing to animate here");
        bool was = a->playing, more = true;
        a->playing = true; /* frames as while playing (3D budget), then back */
        for (long k = 0; k < n && more; k++) {
            more = advance(a, FRAME_MS / 1000.0);
            do_frame(a);
            api->watchdog_kick();
        }
        if (was && more)
            return 0;
        do_pause(a); /* was paused, or "once" reached the end */
        return 0;
    }
    if (!api->strcmp(sub, "rec")) {
        if (argc > 2 && (!api->con_parse_int(argv[2], &n) || n < 1))
            return api->con_error("bad frame count");
        int r = record(a, argc > 2 ? (int)n : 0);
        if (r)
            return api->con_error("recording failed (%d)", r);
        api->con_printf("%s %d frames %lu bytes\n", a->gif_path, a->gif_frames, (unsigned long)a->gif_bytes);
        return 0;
    }
    if (!api->strcmp(sub, "info")) {
        char v[24];
        api->fmt_num(value_at(a, a->phase), 10, v, sizeof v);
        api->con_printf("%s %s=%s phase %d/1000 frames %lu fps %lu.%lu frame %lu us grid %d\n",
                        a->playing ? "playing" : a->active ? "paused" : "idle", a->tgt.name, v,
                        (int)(a->phase * 1000 + 0.5), (unsigned long)a->frames, (unsigned long)(a->fps10 / 10),
                        (unsigned long)(a->fps10 % 10), (unsigned long)a->frame_us, a->grid);
        return 0;
    }
    return api->con_error("usage: anim play|pause|toggle|stop|step [N]|seek PHASE|speed X|budget US|rec [N]|info|"
                          "comet on|off|"
                          "cfg NAME START END SECS MODE");
}

/* `state` keys of the module (UI tests, debugging). */
static void op_state(void *rw, void (*kv)(const char *, const char *, void *), void *user)
{
    anim_rw_t *a = rw;
    const modx_api_t *api = a->api;
    char b[32];
    static const char kinds[5][8] = {"none", "param", "comet", "g3d", "cell"};
    kv("anim_playing", a->playing ? "1" : "0", user);
    kv("anim_kind", a->active ? kinds[a->tgt.kind % 5] : "none", user);
    kv("anim_var", a->active ? a->tgt.name : "", user);
    api->snprintf(b, sizeof b, "%.6g", a->active ? value_at(a, a->phase) : 0.0);
    kv("anim_value", b, user);
    api->snprintf(b, sizeof b, "%.4f", a->phase);
    kv("anim_phase", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)a->frames);
    kv("anim_frames", b, user);
    api->snprintf(b, sizeof b, "%lu.%lu", (unsigned long)(a->fps10 / 10), (unsigned long)(a->fps10 % 10));
    kv("anim_fps", b, user);
    api->snprintf(b, sizeof b, "%lu", (unsigned long)a->frame_us);
    kv("anim_frame_us", b, user);
    api->snprintf(b, sizeof b, "%d", a->grid);
    kv("anim_grid", b, user);
    api->snprintf(b, sizeof b, "%g", a->speed);
    kv("anim_speed", b, user);
    kv("anim_gif", a->gif_path, user);
    api->snprintf(b, sizeof b, "%d", a->gif_frames);
    kv("anim_gif_frames", b, user);
}

/* ------------------------------------------------------------ entry */
/* Called by the core when it binds the module (rw zeroed). Refuses an API
 * table that is older than the one this module was built against. */
int anim_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops); /* modx.c (host), the descriptor (image) */
int anim_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    if (api->abi != MODX_ABI || api->size < sizeof(modx_api_t))
        return -1;
    anim_rw_t *a = rw;
    a->api = api;
    a->speed = 1;
    a->dir = 1;
    a->budget_us = FRAME_BUDGET_US;
    ops->size = sizeof *ops;
    ops->tick = op_tick;
    ops->event = op_event;
    ops->draw = op_draw;
    ops->console = op_console;
    ops->state = op_state;
    return 0;
}

MODX_DESCRIPTOR(ANIM_MODULE, sizeof(anim_rw_t), anim_module_init);
