/*
 * The Python console screen (APPS > Python; help page "python").
 *
 * Layout: the output (scrollback of wrapped rows: input echoed after its
 * prompt, output, errors in red) fills the body above the entry line; the
 * entry line at the bottom is the core's editor (app/py_core.c), drawn after
 * the prompt ">>>" or "..." (inside a block).
 *
 * Keys (the module sees every key first; what it does not use goes to the
 * entry line):
 *   ENTER      run the line; after a line ending in ':' (or an open bracket)
 *              the block continues on the next line, indented; an empty
 *              line ends the block and runs it
 *   F1 run     run the block now (the line too; a block may then hold
 *              several statements)
 *   F2 tab     indent (4 spaces); SHIFT F1 dedent
 *   F3 chars   characters and words the keyboard lacks (' # % ! & | ...)
 *   F4 break   forget the block being typed
 *   F5 clear   clear the output
 *   SHIFT F2   type "calc."; SHIFT F3 list the variables
 *   UP / DOWN  earlier entries; CLEAR clears the line, then the block, then
 *              leaves; ON stops a running program (KeyboardInterrupt)
 *   touch      drag the output to scroll it
 * The entry is calculator text: √( π ² ^ ≤ and the like become Python
 * (sqrt( pi **2 ** <=) before it runs.
 */
#include <string.h>
#include "py_mod.h"
#include "gen/keymap_gen.h"
#include "touch.h"
#include "genhdr/mpversion.h"
#include "py/repl.h"
#include "py/runtime.h"

#define W 320
#define TOP 18          /* below the status bar */
#define BOT 224         /* above the soft-key bar */
#define ROWW 45         /* output columns (7 px font) */
#define ROWH 14
#define NROWS 240       /* scrollback */
#define NHIST 32
#define HISTW 160
#define FG 0x0000
#define BG 0xFFFF
#define C_IN 0x02BB     /* input echo: blue */
#define C_ERR 0xD8E3    /* errors: red */
#define C_DIM 0x8410

enum { K_OUT, K_IN, K_ERR };
static struct {
    char row[NROWS][ROWW + 1];
    uint8_t kind[NROWS];
    int head, count;         /* ring of rows: oldest at head */
    int col;                 /* column of the last row (output being written) */
    uint8_t cur_kind;
    int view;                /* rows scrolled back from the bottom */
    char block[4096];        /* the block being typed (Python) */
    size_t blen;
    char hist[NHIST][HISTW];
    int nhist, hpos;         /* entries; recall position (nhist: the new line) */
    bool picker;             /* chars picker open */
    int psel;
    bool dirty;              /* output since the last progress redraw */
    uint32_t last_draw;
    int drag_y, drag_acc;
} U;

static const modx_api_t *A(void) { return py_api; }

/* ------------------------------------------------------------ scrollback */
static char *new_row(int kind)
{
    int i;
    if (U.count < NROWS)
        i = (U.head + U.count++) % NROWS;
    else
        i = U.head, U.head = (U.head + 1) % NROWS;
    U.row[i][0] = 0;
    U.kind[i] = (uint8_t)kind;
    U.col = 0;
    return U.row[i];
}
static char *last_row(void) { return U.count ? U.row[(U.head + U.count - 1) % NROWS] : NULL; }

/* append text to the scrollback, wrapping at ROWW columns (UTF-8 aware) */
static void put(const char *s, size_t n, int kind)
{
    char *r = last_row();
    if (!r || U.col < 0 || U.kind[(U.head + U.count - 1) % NROWS] != kind)
        r = new_row(kind);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            U.col = -1; /* the next text starts a new row */
            r = NULL;
            continue;
        }
        if (c == '\r')
            continue;
        if (!r || U.col < 0)
            r = new_row(kind);
        bool cont = (c & 0xC0) == 0x80;
        if (!cont && U.col >= ROWW)
            r = new_row(kind);
        size_t l = strlen(r);
        if (l < ROWW * 3 / 2 && l < sizeof U.row[0] - 1) {
            r[l] = (char)c, r[l + 1] = 0;
        }
        if (!cont)
            U.col++;
    }
    U.view = 0;
    U.dirty = true;
}

void pyui_out(const char *s, size_t n)
{
    /* tracebacks: the lines from "Traceback" on are red */
    if (n >= 9 && !memcmp(s, "Traceback", 9))
        U.cur_kind = K_ERR;
    put(s, n, U.cur_kind);
}

static void echo(const char *prompt, const char *text)
{
    U.col = -1;
    put(prompt, strlen(prompt), K_IN);
    put(text, strlen(text), K_IN);
    put("\n", 1, K_IN);
}

/* ------------------------------------------------------------ calculator text -> Python */
void py_translate(const char *in, char *out, size_t n)
{
    static const struct {
        char from[12], to[8];
    } tr[] = {
        {"sin\xE2\x81\xBB\xC2\xB9", "asin"}, {"cos\xE2\x81\xBB\xC2\xB9", "acos"}, {"tan\xE2\x81\xBB\xC2\xB9", "atan"},
        {"\xE2\x81\xBB\xC2\xB9", "**-1"}, /* ⁻¹ */
        {"\xE2\x88\x9A", "sqrt"},         /* √ */
        {"\xCF\x80", "pi"},               /* π */
        {"\xC2\xB2", "**2"}, {"\xC2\xB3", "**3"},
        {"^", "**"},
        {"\xE2\x81\xBB", "-"},            /* ⁻ (negative) */
        {"\xE2\x89\xA4", "<="}, {"\xE2\x89\xA5", ">="}, {"\xE2\x89\xA0", "!="},
        {"\xE2\x86\x92", "="},            /* → */
        {"\xE1\xB4\x87", "e"},            /* ᴇ */
        {"\xE2\x88\x9E", "inf"},          /* ∞ */
        {"\xCE\xB8", "theta"},            /* θ */
        {"\xC3\x97", "*"}, {"\xC3\xB7", "/"},
    };
    size_t o = 0;
    char q = 0;
    while (*in && o + 8 < n) {
        if (q) { /* inside a string literal: as typed */
            if (*in == '\\' && in[1]) {
                out[o++] = *in++;
            } else if (*in == q) {
                q = 0;
            }
            out[o++] = *in++;
            continue;
        }
        if (*in == '\'' || *in == '"') {
            q = *in;
            out[o++] = *in++;
            continue;
        }
        if (*in == '#') { /* a comment: the rest as typed */
            while (*in && o + 1 < n)
                out[o++] = *in++;
            break;
        }
        size_t k = 0;
        for (; k < sizeof tr / sizeof tr[0]; k++) {
            size_t l = strlen(tr[k].from);
            if (!strncmp(in, tr[k].from, l)) {
                size_t t = strlen(tr[k].to);
                memcpy(out + o, tr[k].to, t);
                o += t;
                in += l;
                break;
            }
        }
        if (k == sizeof tr / sizeof tr[0])
            out[o++] = *in++;
    }
    out[o] = 0;
}

/* ------------------------------------------------------------ running */
static void hist_add(const char *line)
{
    if (!line[0] || (U.nhist && !strcmp(U.hist[(U.nhist - 1) % NHIST], line)))
        return;
    A()->snprintf(U.hist[U.nhist % NHIST], HISTW, "%s", line);
    U.nhist++;
}

static void redraw_now(void);

static void run_block(int mode)
{
    U.cur_kind = K_OUT;
    U.col = -1;
    int sinks = py_out_get();
    py_out_set(PY_OUT_SCREEN);
    py_pc->line_set("");
    if (py_pc->shown())
        redraw_now(); /* "running" */
    if (mode == PY_RUN_CELL) {
        mp_obj_t v;
        if (py_run(U.block, PY_RUN_CELL, &v) == PY_OK && v != MP_OBJ_NULL && v != mp_const_none) {
            static char t[512];
            py_value_text(v, t, sizeof t);
            U.col = -1;
            put(t, strlen(t), K_OUT);
            put("\n", 1, K_OUT);
        }
    } else {
        py_run(U.block, PY_RUN_REPL, NULL);
    }
    py_out_set(sinks);
    U.blen = 0, U.block[0] = 0;
    U.cur_kind = K_OUT;
}

static void block_add(const char *py)
{
    size_t l = strlen(py);
    if (U.blen + l + 2 >= sizeof U.block) {
        U.col = -1;
        put("block too long\n", 15, K_ERR);
        U.blen = 0, U.block[0] = 0;
        return;
    }
    if (U.blen)
        U.block[U.blen++] = '\n';
    memcpy(U.block + U.blen, py, l + 1);
    U.blen += l;
}

static void enter_line(bool run_now)
{
    static char disp[512], py[1024];
    py_pc->line_get(disp, sizeof disp);
    echo(U.blen ? "... " : ">>> ", disp);
    hist_add(disp);
    U.hpos = U.nhist;
    py_translate(disp, py, sizeof py);
    bool empty = !py[strspn(py, " ")];
    if (run_now) {
        if (!empty)
            block_add(py);
        if (U.blen)
            run_block(PY_RUN_CELL);
        else
            py_pc->line_set("");
        return;
    }
    if (empty && !U.blen) {
        py_pc->line_set("");
        return;
    }
    if (!empty || U.blen)
        block_add(py);
    if (!empty && mp_repl_continue_with_input(U.block)) {
        /* the next line of the block, indented like this one (+4 after ':') */
        size_t ind = strspn(py, " ");
        size_t l = strlen(py);
        while (l && py[l - 1] == ' ')
            l--;
        if (l && py[l - 1] == ':')
            ind += 4;
        char sp[64];
        if (ind > sizeof sp - 1)
            ind = sizeof sp - 1;
        memset(sp, ' ', ind);
        sp[ind] = 0;
        py_pc->line_set(sp);
        return;
    }
    run_block(PY_RUN_REPL);
}

/* ------------------------------------------------------------ the chars picker */
static const char picks[][10] = {"'", "#", "%", "!", "&", "|", ";", "@", "~", "\\", "?", "$", "`", "//",
                                 "==", "**", "<", ">", "_", ":", "def ", "for ", " in ", "range(", "if ",
                                 "elif ", "else:", "while ", "return ", "print(", "import ", "lambda ",
                                 "True", "False", "None", "and ", "or ", "not "};
#define NPICK ((int)(sizeof picks / sizeof picks[0]))
#define PCOLS 6

static void draw_picker(void)
{
    const modx_api_t *a = A();
    int rows = (NPICK + PCOLS - 1) / PCOLS, cw = W / PCOLS, y0 = BOT - 22 - rows * 18;
    a->fill_rect(0, y0 - 4, W, rows * 18 + 6, 0xE71C);
    for (int i = 0; i < NPICK; i++) {
        int x = (i % PCOLS) * cw, y = y0 + (i / PCOLS) * 18;
        bool sel = i == U.psel;
        if (sel)
            a->fill_rect(x + 1, y - 1, cw - 2, 17, C_IN);
        char lab[12];
        const char *s = picks[i] + strspn(picks[i], " "); /* shown without the spaces it types */
        a->snprintf(lab, sizeof lab, "%s", s);
        size_t l = strlen(lab);
        while (l > 1 && lab[l - 1] == ' ')
            lab[--l] = 0;
        a->text(a->font_medium, x + 4, y, lab, sel ? BG : FG, -1);
    }
}

static int picker_key(int key)
{
    switch (key) {
    case KEY_LEFT: U.psel = (U.psel + NPICK - 1) % NPICK; break;
    case KEY_RIGHT: U.psel = (U.psel + 1) % NPICK; break;
    case KEY_UP: U.psel = (U.psel + NPICK - PCOLS) % NPICK; break;
    case KEY_DOWN: U.psel = (U.psel + PCOLS) % NPICK; break;
    case KEY_ENTER: py_pc->line_insert(picks[U.psel]); U.picker = false; break;
    default: U.picker = false; break; /* CLEAR, F3, any other key closes it */
    }
    return 1;
}

/* ------------------------------------------------------------ drawing */
static int out_rows(void) { return (BOT - TOP - py_pc->line_height() - 2) / ROWH; }

bool pyui_draw(void)
{
    const modx_api_t *a = A();
    int lh = py_pc->line_height(), ly = BOT - lh;
    int nr = out_rows();
    a->fill_rect(0, TOP, W, ly - TOP, BG);
    int first = U.count - nr - U.view;
    if (first < 0)
        first = 0;
    for (int r = 0; r < nr && first + r < U.count; r++) {
        int i = (U.head + first + r) % NROWS;
        uint16_t c = U.kind[i] == K_IN ? C_IN : U.kind[i] == K_ERR ? C_ERR : FG;
        a->text(a->font_medium, 2, TOP + 2 + r * ROWH, U.row[i], c, -1);
    }
    if (U.view)
        a->text(a->font_small, W - 60, TOP + 2, "(scrolled)", C_DIM, BG);
    a->fill_rect(0, ly - 2, W, 1, 0xC618);
    if (py_running()) {
        a->fill_rect(0, ly - 1, W, lh + 1, BG);
        a->text(a->font_medium, 4, ly + 3, "running ... ON stops it", C_DIM, -1);
    } else {
        a->fill_rect(0, ly - 1, 30, lh + 1, BG);
        a->text(a->font_medium, 2, ly + 3, U.blen ? "..." : ">>>", C_IN, -1);
        py_pc->line_draw(30, ly, (W - 30) / 10, !U.picker);
    }
    if (U.picker)
        draw_picker();
    U.dirty = false;
    return true;
}

static void redraw_now(void)
{
    pyui_draw();
    A()->flush();
    U.last_draw = A()->millis();
}

/* while a program runs: new output on the screen every 100 ms */
void pyui_progress(void)
{
    if (!U.dirty || !py_pc->shown() || A()->millis() - U.last_draw < 100)
        return;
    redraw_now();
}

/* ------------------------------------------------------------ events */
static void list_vars(void)
{
    block_add("print(' '.join(sorted(k for k in globals() if k[0] != '_')))");
    run_block(PY_RUN_REPL);
}

static int key_event(int key, int mod)
{
    if (U.picker)
        return picker_key(key);
    char line[256];
    if (mod == 1 /* MOD_SHIFT */) {
        switch (key) {
        case KEY_F1: { /* dedent */
            py_pc->line_get(line, sizeof line);
            size_t k = strspn(line, " ");
            py_pc->line_set(line + (k > 4 ? 4 : k));
            return 1;
        }
        case KEY_F2: py_pc->line_insert("calc."); return 1;
        case KEY_F3: list_vars(); return 1;
        default: return 0;
        }
    }
    switch (key) {
    case KEY_ENTER:
        if (mod)
            return 0;
        enter_line(false);
        return 1;
    case KEY_F1: enter_line(true); return 1;
    case KEY_F2: py_pc->line_insert("    "); return 1;
    case KEY_F3: U.picker = true; return 1;
    case KEY_F4: /* forget the block */
        if (U.blen) {
            U.col = -1;
            put("(block dropped)\n", 16, K_ERR);
        }
        U.blen = 0, U.block[0] = 0;
        py_pc->line_set("");
        return 1;
    case KEY_F5:
        U.count = 0, U.head = 0, U.view = 0;
        return 1;
    case KEY_CLEAR:
        py_pc->line_get(line, sizeof line);
        if (line[0])
            py_pc->line_set("");
        else if (U.blen)
            U.blen = 0, U.block[0] = 0;
        else
            py_pc->show(PY_SHOW_HOME);
        return 1;
    case KEY_UP:
    case KEY_DOWN:
        if (!U.nhist)
            return 1;
        if (key == KEY_UP && U.hpos > 0 && U.hpos > U.nhist - NHIST)
            U.hpos--;
        else if (key == KEY_DOWN && U.hpos < U.nhist)
            U.hpos++;
        py_pc->line_set(U.hpos < U.nhist ? U.hist[U.hpos % NHIST] : "");
        return 1;
    default:
        return 0;
    }
}

int pyui_event(int ev, int arg)
{
    switch (ev) {
    case MODX_EV_ENTER:
        U.picker = false;
        return 0;
    case MODX_EV_KEY:
        return key_event(arg & 0xFF, arg >> 8);
    case MODX_EV_TOUCH: {
        int t = (int)MODX_TOUCH_TYPE(arg), y = MODX_TOUCH_Y(arg), view = U.view;
        if (t == TEV_DOWN) {
            U.drag_y = y, U.drag_acc = 0;
        } else if (t == TEV_DRAG) {
            U.drag_acc += y - U.drag_y;
            U.drag_y = y;
            int max = U.count - out_rows();
            while (U.drag_acc >= ROWH) /* finger down: earlier rows */
                U.drag_acc -= ROWH, U.view = U.view < max ? U.view + 1 : U.view;
            while (U.drag_acc <= -ROWH)
                U.drag_acc += ROWH, U.view = U.view > 0 ? U.view - 1 : 0;
        }
        return U.view != view ? 2 : 1; /* 2: scrolled, redraw */
    }
    default:
        return 0;
    }
}

void pyui_init(void)
{
    memset(&U, 0, sizeof U);
    static const char banner[] = "MicroPython " MICROPY_GIT_TAG " with calc\nhelp() for help; ON stops a program\n";
    put(banner, sizeof banner - 1, K_OUT);
    U.col = -1;
}

void pyui_state(void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16], line[256];
    kv("py_block", U.block, user);
    py_pc->line_get(line, sizeof line);
    kv("py_line", line, user);
    A()->snprintf(b, sizeof b, "%d", U.count);
    kv("py_rows", b, user);
    const char *r = last_row();
    /* the last non-empty output row */
    for (int k = U.count - 1; k >= 0; k--) {
        const char *s = U.row[(U.head + k) % NROWS];
        if (s[0]) {
            r = s;
            break;
        }
    }
    kv("py_last", r ? r : "", user);
}

/* test helpers: `py screen` prints the visible output rows */
int pyui_console(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "screen")) {
        int nr = out_rows(), first = U.count - nr - U.view;
        if (first < 0)
            first = 0;
        for (int r = 0; r < nr && first + r < U.count; r++)
            A()->con_printf("row=%s\n", U.row[(U.head + first + r) % NROWS]);
        return 0;
    }
    return -1;
}
