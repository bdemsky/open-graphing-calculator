/*
 * App-level debug console commands (the `app_console_cmds[]` table that
 * core/console.c searches after its own commands and before the platform's): state, expect, type, eval,
 * casmem, redraw, session, reboot, manual, app, casreset, setmode, msg, plus
 * the statistics (`stat`, app/stat_console.c) and microSD (`fs`, core) commands.
 *
 * These are what the UI test scripts (tests/ui/NN_name.txt, README "Console
 * protocol") are written in: `type` and `key` drive the calculator, `state` /
 * `expect` observe it through app_state_foreach() and the graphics text log.
 * Each command returns 0 (console prints OK) or con_error() (ERR message);
 * a failed `expect` also counts in console_expect_failures, which becomes the
 * simulator's exit status. New commands: append rows at the end of the table.
 */
#include "anim.h"
int mq_console(int argc, char **argv); /* app/mquest_core.c */
#include "modx.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "modules_ui.h"
#include "app.h"
#include "console.h"
#include "evlog.h"
#include "gfx.h"
#include "help_data.h"
#include "keyq.h"
#include "registry.h"
#include "session.h"
#include "ui.h"
#include "stat.h"
#include "sheet.h"
#include "sdbk.h"
#include "g3d.h"
#include "geo_core.h"
#include "py_core.h"

/* app_kv_fn printing "key=value"; `user` is an optional key filter. */
static void print_kv(const char *k, const char *v, void *user)
{
    const char *filter = user;
    if (filter && strcmp(filter, k))
        return;
    con_printf("%s=%s\n", k, v);
}

static int cmd_state(int argc, char **argv)
{
    app_state_foreach(print_kv, argc > 1 ? argv[1] : NULL);
    return 0;
}

/* One state lookup: the first value reported for `key` (truncated to 255
 * bytes). */
typedef struct {
    const char *key;
    char value[256];
    bool found;
} lookup_t;

static void find_kv(const char *k, const char *v, void *user)
{
    lookup_t *l = user;
    if (!l->found && !strcmp(k, l->key)) {
        snprintf(l->value, sizeof l->value, "%s", v);
        l->found = true;
    }
}

/* Report a failed expectation (counted for the exit status). */
static int fail(const char *fmt, const char *a, const char *b)
{
    console_expect_failures++;
    return con_error(fmt, a, b);
}

/* expect screen NAME | text SUBSTR | notext SUBSTR | state KEY VALUE |
 *        statecontains KEY SUBSTR | pixel X Y RGB565 | color RGB565 [MIN] |
 *        eval EXPR RESULT
 * `text` searches the log of text runs currently visible on the screen
 * (gfx_textlog: what was drawn where), not the pixels; `state` compares exactly (a missing VALUE
 * means ""). */
void mquest_scroll(int dy, int x, int y); /* app/mquest_core.c */
static int cmd_scroll(int argc, char **argv)
{
    long dy, x = 160, y = 120;
    if (argc < 2 || !con_parse_int(argv[1], &dy) || (argc > 3 && (!con_parse_int(argv[2], &x) || !con_parse_int(argv[3], &y))))
        return con_error("usage: scroll DY [X Y]");
    mquest_scroll((int)dy, (int)x, (int)y);
    return 0;
}

#include <stdbool.h>
bool mquest_text(int c); /* app/mquest_core.c */
static int cmd_kbd(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: kbd TEXT (_ for a space)");
    for (int i = 1; i < argc; i++)
        for (const char *s = argv[i]; *s; s++)
            if (!mquest_text(*s == '_' ? ' ' : *s))
                return con_error("not taken: %c", *s);
    return 0;
}

static int cmd_expect(int argc, char **argv)
{
    if (argc < 3)
        return con_error("usage: expect screen|text|notext|state|statecontains|pixel ...");
    const char *what = argv[1];
    if (!strcmp(what, "screen")) {
        if (strcmp(app_screen_name(), argv[2]))
            return fail("expected screen %s, got %s", argv[2], app_screen_name());
    } else if (!strcmp(what, "text")) {
        if (!gfx_textlog_contains(argv[2]))
            return fail("text '%s' not on screen%s", argv[2], "");
    } else if (!strcmp(what, "notext")) {
        if (gfx_textlog_contains(argv[2]))
            return fail("text '%s' unexpectedly on screen%s", argv[2], "");
    } else if (!strcmp(what, "state") || !strcmp(what, "statecontains")) {
        const char *want = argc > 3 ? argv[3] : "";
        lookup_t l = {argv[2], "", false};
        app_state_foreach(find_kv, &l);
        if (!l.found)
            return fail("no state key '%s'%s", argv[2], "");
        bool ok = !strcmp(what, "state") ? !strcmp(l.value, want) : strstr(l.value, want) != NULL;
        if (!ok) {
            console_expect_failures++;
            return con_error("state %s='%s', expected %s'%s'", argv[2], l.value,
                             !strcmp(what, "state") ? "" : "to contain ", want);
        }
    } else if (!strcmp(what, "pixel")) {
        long x, y;
        unsigned long c;
        if (argc < 5 || !con_parse_int(argv[2], &x) || !con_parse_int(argv[3], &y) ||
            !con_parse_u32(argv[4], &c))
            return con_error("usage: expect pixel X Y RGB565");
        uint16_t p = gfx_get_pixel((int)x, (int)y);
        if (p != c) {
            console_expect_failures++;
            return con_error("pixel %ld,%ld = 0x%04x, expected 0x%04lx", x, y, p, c);
        }
    } else if (!strcmp(what, "eval")) {
        /* expect eval "EXPR" "RESULT": cas_eval output must match exactly */
        if (argc < 4)
            return con_error("usage: expect eval EXPR RESULT");
        static char out[2048];
        cas_status st = cas_eval(app_cas(), argv[2], out, sizeof out);
        if (st != CAS_OK)
            snprintf(out, sizeof out, "ERROR %d: %s", st, cas_last_error(app_cas()));
        if (strcmp(out, argv[3])) {
            console_expect_failures++;
            return con_error("eval %s = '%s', expected '%s'", argv[2], out, argv[3]);
        }
    } else if (!strcmp(what, "color")) {
        /* expect color RGB565 [MINCOUNT]: at least MINCOUNT pixels of this colour on screen */
        unsigned long c, min = 1;
        if (!con_parse_u32(argv[2], &c) || (argc > 3 && !con_parse_u32(argv[3], &min)))
            return con_error("usage: expect color RGB565 [MINCOUNT]");
        const uint16_t *fb = gfx_framebuffer();
        unsigned long n = 0;
        for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
            n += fb[i] == c;
        if (n < min) {
            console_expect_failures++;
            return con_error("colour 0x%04lx: %lu pixels, expected >= %lu", c, n, min);
        }
        con_printf("count=%lu\n", n);
    } else {
        return con_error("unknown expectation '%s'", what);
    }
    return 0;
}

/* type TEXT: map ASCII to key presses with the keyboard layout (keys/layout.py):
 * a key that types the character, else its ALPHA letter, else a SHIFT function. */
/* Queue a press and release of key k, as the keyboard driver would. */
static void tap(int k)
{
    keyq_push((uint8_t)k, true);
    keyq_push((uint8_t)k, false);
}

/* The first key (lowest code) whose text in `layer` (0 main, 1 ALPHA, 2 SHIFT)
 * is exactly s; 0 if none. */
static int find_key(const char *s, int layer)
{
    for (int k = 1; k < KEY_COUNT; k++) {
        const key_info_t *ki = &key_info[k];
        const char *t = layer == 0 ? ki->text : layer == 1 ? ki->alpha : ki->shift_text;
        if (t && !strcmp(t, s))
            return k;
    }
    return 0;
}

/* Queue the keys that type ASCII character c: '\n' is ENTER, '~' the (−)
 * negation key; otherwise a key that types c directly, else its ALPHA letter
 * (ALPHA once for lowercase, twice for uppercase), else a SHIFT function.
 * Space is looked up only among the ALPHA/SHIFT layers. Returns -1 if no key
 * types c. The keys are only queued; the main loop processes them later. */
static int type_char(char c)
{
    char s[2] = {c, 0};
    int k;
    if (c == '\n') {
        tap(KEY_ENTER);
        return 0;
    }
    if (c == '~') {
        tap(KEY_NEG);
        return 0;
    }
    if (c != ' ' && (k = find_key(s, 0))) {
        tap(k);
        return 0;
    }
    s[0] = (char)tolower((unsigned char)c);
    if ((k = find_key(s, 1))) {
        tap(KEY_ALPHA);
        if (isupper((unsigned char)c))
            tap(KEY_ALPHA);
        tap(k);
        return 0;
    }
    s[0] = c;
    if ((k = find_key(s, 2))) {
        tap(KEY_SHIFT);
        tap(k);
        return 0;
    }
    return -1;
}

/* type TEXT...: several arguments are typed separated by one space. */
static int cmd_type(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: type TEXT (quote it; \\n = ENTER)");
    for (int a = 1; a < argc; a++) {
        if (a > 1 && type_char(' '))
            return -1;
        for (const char *s = argv[a]; *s; s++)
            if (type_char(*s))
                return con_error("cannot type '%c'", *s);
    }
    return 0;
}

/* eval EXPR...: arguments joined with spaces (up to 319 bytes) and given to
 * cas_eval_quiet() directly: raw CAS output, no MODE number format, and `ans`
 * is left alone (a look from the console must not change what the home
 * screen's next `ans` means). */
static int cmd_eval(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: eval \"EXPR\"");
    char expr[320] = "";
    for (int i = 1; i < argc; i++) {
        if (i > 1)
            strncat(expr, " ", sizeof expr - strlen(expr) - 1);
        strncat(expr, argv[i], sizeof expr - strlen(expr) - 1);
    }
    char out[512];
    cas_status st = cas_eval_quiet(app_cas(), expr, out, sizeof out);
    if (st != CAS_OK)
        return con_error("cas status %d: %s", st, cas_last_error(app_cas()));
    con_printf("result=%s\n", out);
    return 0;
}

static int cmd_casmem(int argc, char **argv)
{
    cas_mem_stats s;
    cas_mem(app_cas(), &s);
    con_printf("arena=%lu\nused=%lu\npeak=%lu\nimpl=%s\n", (unsigned long)s.arena_size, (unsigned long)s.used,
               (unsigned long)s.peak, CAS_IMPL);
    return 0;
}

static int cmd_redraw(int argc, char **argv)
{
    app_redraw();
    return 0;
}

/* session [info|save|clear|fail N|limit BYTES]: `save` forces a write even
 * if unchanged; `fail` and `limit` are test hooks (the next N flash writes
 * fail; the serialization buffer is cut to BYTES, 0 = full size); every form
 * ends with the session_* status lines. */
static int cmd_session(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    long n = 0;
    if ((!strcmp(sub, "fail") || !strcmp(sub, "limit")) && (argc < 3 || !con_parse_int(argv[2], &n) || n < 0))
        return con_error("usage: session %s N", sub);
    if (!strcmp(sub, "fail")) {
        session_test_fail_writes((int)n);
    } else if (!strcmp(sub, "limit")) {
        session_test_limit((size_t)n);
    } else if (!strcmp(sub, "save")) {
        int rc = session_save(true);
        if (rc)
            return con_error("save failed (%d)", rc);
    } else if (!strcmp(sub, "clear")) {
        if (session_clear())
            return con_error("clear failed");
    } else if (strcmp(sub, "info")) {
        return con_error("usage: session [info|save|clear|fail N|limit BYTES]");
    }
    session_state(print_kv, NULL);
    return 0;
}

/* evlog: the event log (core/evlog.h), newest first; evlog clear: forget it
 * (and save, so the saved copy does not come back); evlog add TEXT...: record
 * an event (tests). */
static int cmd_evlog(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "clear")) {
        evlog_clear();
        session_save(false);
    } else if (argc > 2 && !strcmp(argv[1], "add")) {
        char t[EVLOG_TEXT * 2] = "";
        for (int i = 2; i < argc; i++) {
            size_t l = strlen(t);
            snprintf(t + l, sizeof t - l, "%s%s", i > 2 ? " " : "", argv[i]);
        }
        evlog_add("%s", t);
    } else if (argc > 1) {
        return con_error("usage: evlog [clear|add TEXT...]");
    }
    int n = evlog_count();
    con_printf("boot=%lu\nevents=%d\n", (unsigned long)evlog_boots(), n);
    for (int i = n - 1; i >= 0; i--) {
        char line[112];
        evlog_format(evlog_get(i), line, sizeof line);
        con_printf("%s\n", line);
    }
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    /* what a reset does: the app restarts from the last saved session;
     * `reboot crash` as after a crash (counts towards safe mode) */
    if (argc > 1 && !strcmp(argv[1], "crash"))
        app_crash_restart(HAL_RESET_FAULT);
    else
        app_restart(HAL_RESET_SOFTWARE);
    gfx_flush();
    return 0;
}

/* manual list: every help topic (id, title) and the help data sizes;
 * manual ID: open that topic in the viewer. */
static int cmd_manual(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: manual TOPIC | manual list");
    if (!strcmp(argv[1], "list")) {
        for (int i = 0; i < help_ntopics; i++)
            con_printf("%s\t%s\n", help_strings + help_topics[i].id, help_strings + help_topics[i].title);
        con_printf("topics=%d catalog=%d compressed=%lu raw=%lu\n", help_ntopics, help_ncatalog,
                   (unsigned long)help_blob_size, (unsigned long)help_raw_size);
        return 0;
    }
    if (!help_exists(argv[1]))
        return con_error("no help topic '%s'", argv[1]);
    help_open(argv[1]);
    return 0;
}

/* app [list]: the registered apps; app ID: launch one like the APPS menu. */
static int cmd_app(int argc, char **argv)
{
    if (argc < 2 || !strcmp(argv[1], "list")) {
        for (int i = 0; i < app_registry_count; i++)
            con_printf("%s\t%s\n", app_registry[i]->id, app_registry[i]->name);
        return 0;
    }
    const app_def_t *a = app_find(argv[1]);
    if (!a)
        return con_error("no app '%s'", argv[1]);
    a->open();
    return 0;
}

/* setmode ROW OPTION: select a MODE option by label (case-insensitive), so scripted tests
 * don't depend on the order of the rows other features register. */
static int cmd_setmode(int argc, char **argv)
{
    if (argc < 3)
        return con_error("usage: setmode ROW OPTION");
    for (int i = 0; i < mode_registry_count; i++) {
        const mode_row_t *r = mode_registry[i];
        if (strcasecmp(r->label, argv[1]))
            continue;
        for (int o = 0; o < r->nopts; o++)
            if (!strcasecmp(r->opts[o], argv[2])) {
                r->set(o);
                app_apply_settings();
                return 0;
            }
        return con_error("MODE row '%s' has no option '%s'", argv[1], argv[2]);
    }
    return con_error("no MODE row '%s'", argv[1]);
}

/* casreset: an empty CAS (as MEM > Clear CAS variables, without the message);
 * the help-example tests run it before each command page. */
static int cmd_casreset(int argc, char **argv)
{
    funcs_invalidate(); /* frees the compiled graph functions first */
    cas_reset(app_cas());
    yvars_invalidate();
    return 0;
}

/* msg TITLE TEXT: show a message box (ui_message keeps the pointers, hence
 * the static copies). */
static int cmd_msg(int argc, char **argv)
{
    static char title[32], text[128];
    if (argc < 3)
        return con_error("usage: msg TITLE TEXT");
    snprintf(title, sizeof title, "%s", argv[1]);
    snprintf(text, sizeof text, "%s", argv[2]);
    ui_message(title, text);
    return 0;
}

/* name, argument synopsis, one-line help (console `help`), handler; ends with
 * an all-zero row. */
const console_cmd app_console_cmds[] = {
    {"state", "[KEY]", "app state as key=value lines", cmd_state},
    {"expect", "WHAT ARGS", "assert: screen N | text S | notext S | state K V | statecontains K S | "
                            "pixel X Y C | color C [N] | eval EXPR RESULT", cmd_expect},
    {"type", "\"TEXT\"", "type ASCII text as key presses (\\n = ENTER)", cmd_type},
    {"eval", "EXPR", "evaluate with the CAS directly", cmd_eval},
    {"casmem", "", "CAS arena statistics", cmd_casmem},
    {"redraw", "", "redraw the current screen", cmd_redraw},
    {"session", "[info|save|clear|fail N|limit B]",
     "saved session (QSPI flash log): status, save now, forget; test hooks", cmd_session},
    {"reboot", "[crash]", "restart the app from the saved session (as after a reset / a crash)", cmd_reboot},
    {"module", "list|begin|put|end|install|remove|revert|verify|export|...", "content modules (app/modules_ui.c)",
     modules_console},
    {"safemode", "info|force|off", "boot recovery (core/safemode.h)", safemode_console},
    {"wdg", "info|hang", "watchdog: status and last reset; hang = stop feeding it (it resets)", watchdog_console},
    {"devices", "", "how the optional devices (QSPI, microSD, touch, ...) answered at boot", devices_console},
    {"manual", "TOPIC|list", "open a help topic on screen, or list the topics", cmd_manual},
    {"app", "ID|list", "open a registered app (APPS menu entry)", cmd_app},
    {"casreset", "", "delete all CAS variables and functions", cmd_casreset},
    {"setmode", "ROW OPTION", "select a settings option by label, e.g. setmode ENTRY RPN", cmd_setmode},
    {"stat", "lists|list|set|expect|formula|last|import|export ...", "statistics lists (app/stat_console.c)",
     stat_console},
    {"fs", "insert|eject|format|info|ls|cat|put|rm|mkdir|expect ...", "microSD files (core/fat.c)",
     sd_console},
    {"msg", "TITLE TEXT", "show a message box (any key or a tap closes it)", cmd_msg},
    {"kbd", "TEXT", "characters typed on a computer keyboard (_ a space; the web pages: Sparkle Town's name)", cmd_kbd},
    {"scroll", "DY X Y", "a scroll wheel over the game screen (the web pages' wheel; Sparkle Town)", cmd_scroll},
    {"g3d", "set|kind|view|bench ...", "3D graphing (app/g3d_screen.c)", g3d_console},
    {"anim", "play|pause|stop|step N|rec|speed X|comet on|off|cfg ...", "animation (app/anim_core.c + module)",
     anim_console},
    {"mq", "seed N|go SCENE|answer X|step N|...", "Math Quest (app/mquest_core.c + module)", mq_console},
    {"modx", "list|off N|on N|bind N|unbind", "code modules (app/modx.c)", modx_console},
    {"sheet", "set|show|expect|value|fill|copy|link|export|import ...", "spreadsheet (app/sheet_console.c)",
     sheet_console},
    {"sdtool", "shot|backup [F]|restore [F]", "screenshot / session backup on the microSD card (app/sdbk.c)",
     sdbk_console},
    {"geo", "state|list|expect|cursor|new|save|load ...", "geometry app (modules/geometry, app/geo_core.c)", geo_console},
    {"py", "STATEMENT|exec CODE|add LINE|run|buf TEXT|cell|expect CODE OUT|reset|breakafter N", "Python (app/py_core.c + module)", py_console},
    {"pyexec", "CODE", "run Python code (cell: output and the value of its last expression)", py_console},
    {"nb", "list|new N|open N|add KIND TEXT|run [I]|runall|...", "the notebook (modules/python nb_*.c)", py_console},
    {"evlog", "[clear|add TEXT...]", "the event log: last events, newest first (core/evlog.h)", cmd_evlog},
    {0, 0, 0, 0},
};
