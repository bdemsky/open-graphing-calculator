/* NOTEBOOK.HTML on the CALC drive (app/usb_app.c; help page "usb"): the open notebook as one
 * self-contained HTML page that any phone or computer shows without running anything: no script,
 * nothing loaded from elsewhere, the CSS inline. Text cells become HTML (the Markdown the
 * calculator knows: headings, paragraphs, bold, italic, code, lists, quotes, code blocks; $math$
 * as text), math cells their input as text and their result as a PNG of the calculator's own
 * textbook drawing, graph cells a PNG of the plot, Python cells their code and output as text.
 *
 * Built into an SDRAM buffer when the drive is generated (every media change: connecting, after a
 * notebook is saved or imported), so its size is known for the directory and the computer reads it
 * like any file. The pictures are drawn offscreen (gfx_target) in the light theme and encoded by the
 * notebook's PNG writer (app/png.c, which the Python module's image compiles too: the home screen
 * must not need the Python module). From inside a long CAS evaluation (the USB stack runs there) a
 * graph cell that is not in the image cache is left out (plotting needs the CAS). */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "gfx.h"
#include "m2d.h"
#include "nbdoc.h"
#include "png.h"
#include "py_core.h"
#include "rle.h"
#include "theme.h"
#include "ui.h"
#include "version.h"


#define HTML_MAX (1536u << 10)
static char *html;
static size_t hn;
static bool full;
static uint8_t *idx; /* indexed pixels of a picture (LCD_WIDTH x LCD_HEIGHT, after the page) */
static uint16_t pal[256];

size_t nbhtml_mem_init(void *mem, bool big)
{
    html = big ? mem : NULL;
    idx = big ? (uint8_t *)mem + HTML_MAX : NULL;
    hn = 0;
    return big ? HTML_MAX + LCD_WIDTH * LCD_HEIGHT : 0;
}

static void put(const char *s, size_t n)
{
    if (!html || hn + n >= HTML_MAX - 64) {
        full = true;
        return;
    }
    memcpy(html + hn, s, n);
    hn += n;
}
static void puts_(const char *s) { put(s, strlen(s)); }
static void sink(void *u, const void *s, size_t n) { (void)u, put(s, n); }

/* text with < > & " escaped */
static void esc(const char *s, size_t n)
{
    for (size_t i = 0; i < n && s[i]; i++) {
        const char *e = s[i] == '<' ? "&lt;" : s[i] == '>' ? "&gt;" : s[i] == '&' ? "&amp;" : s[i] == '"' ? "&quot;" : NULL;
        if (e)
            puts_(e);
        else
            put(s + i, 1);
    }
}
static void esc_s(const char *s) { esc(s, strlen(s)); }

/* ------------------------------------------------------------ pictures */
/* the w x h image at rgb (row stride 320) as an <img> with a PNG data URL */
static void img(const uint16_t *rgb, int w, int h, const char *alt, const char *cls)
{
    int np = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint16_t c = rgb[y * LCD_WIDTH + x];
            int k = 0, best = 0;
            unsigned bd = ~0u;
            for (; k < np && pal[k] != c; k++) {
                int dr = (c >> 11) - (pal[k] >> 11), dg = ((c >> 5) & 63) - ((pal[k] >> 5) & 63), db = (c & 31) - (pal[k] & 31);
                unsigned d = (unsigned)(dr * dr * 4 + dg * dg + db * db * 4);
                if (d < bd)
                    bd = d, best = k;
            }
            if (k == np && np < 256)
                pal[np++] = c;
            else if (k == np)
                k = best;
            idx[y * w + x] = (uint8_t)k;
        }
    puts_("<img class=\"");
    puts_(cls);
    puts_("\" alt=\"");
    esc_s(alt);
    char b[96];
    snprintf(b, sizeof b, "\" width=\"%d\" height=\"%d\" src=\"data:image/png;base64,", w, h);
    puts_(b);
    b64_t e;
    b64_init(&e, sink, NULL);
    png_indexed(idx, w, h, pal, np, b64_put, &e);
    b64_end(&e);
    puts_("\">");
}

/* a result in textbook form as a picture; false: shown as text instead */
static bool math_img(const char *disp)
{
    uint16_t *fb = gcell_scratch();
    if (!fb)
        return false;
    gfx_target(fb);
    gfx_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, C_WHITE);
    nbio_math_t m = {disp, 4, 4, LCD_WIDTH - 8, C_BLACK, true, 0, 0};
    int r = m2d_math_box(&m);
    gfx_target(NULL);
    if (r || m.w <= 0 || m.h <= 0)
        return false;
    int w = m.w + 8 > LCD_WIDTH ? LCD_WIDTH : m.w + 8, h = m.h + 8 > LCD_HEIGHT ? LCD_HEIGHT : m.h + 8;
    img(fb, w, h, disp, "r");
    return true;
}

/* ------------------------------------------------------------ Markdown */
/* inline: **bold**, *italic*, `code`, $math$ (as text) */
static void inl(const char *s, size_t n)
{
    bool b = false, it = false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '`' || s[i] == '$') {
            const char *e = memchr(s + i + 1, s[i], n - i - 1);
            if (e) {
                puts_(s[i] == '`' ? "<code>" : "<span class=\"m\">");
                esc(s + i + 1, (size_t)(e - s - i - 1));
                puts_(s[i] == '`' ? "</code>" : "</span>");
                i = (size_t)(e - s);
                continue;
            }
        }
        if (s[i] == '*' && i + 1 < n && s[i + 1] == '*') {
            puts_(b ? "</b>" : "<b>");
            b = !b;
            i++;
            continue;
        }
        if (s[i] == '*' && (it || (i + 1 < n && s[i + 1] != ' '))) {
            puts_(it ? "</i>" : "<i>");
            it = !it;
            continue;
        }
        esc(s + i, 1);
    }
    if (b)
        puts_("</b>");
    if (it)
        puts_("</i>");
}

static void markdown(const char *s)
{
    enum { NONE, PARA, UL, OL, QUOTE, CODE } blk = NONE;
    static const char *const close[] = {"", "</p>\n", "</ul>\n", "</ol>\n", "</blockquote>\n", "</pre>\n"};
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (blk == CODE) {
            if (n >= 3 && !strncmp(s, "```", 3)) {
                puts_(close[blk]);
                blk = NONE;
            } else {
                esc(s, n);
                puts_("\n");
            }
        } else if (n >= 3 && !strncmp(s, "```", 3)) {
            puts_(close[blk]);
            puts_("<pre>");
            blk = CODE;
        } else if (!n || (n == strspn(s, " \t"))) {
            puts_(close[blk]);
            blk = NONE;
        } else if (s[0] == '#') {
            size_t k = 0;
            while (k < n && s[k] == '#')
                k++;
            puts_(close[blk]);
            blk = NONE;
            char t[8];
            snprintf(t, sizeof t, "<h%d>", k > 3 ? 5 : (int)k + 1);
            puts_(t);
            inl(s + k + (k < n && s[k] == ' '), n - k - (k < n && s[k] == ' '));
            t[1] = '/';
            snprintf(t, sizeof t, "</h%d>\n", k > 3 ? 5 : (int)k + 1);
            puts_(t);
        } else {
            int want = PARA;
            size_t skip = 0;
            if (n > 1 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ')
                want = UL, skip = 2;
            else if (n > 2 && s[0] >= '0' && s[0] <= '9' && (s[1] == '.' || (s[1] >= '0' && s[1] <= '9' && s[2] == '.'))) {
                want = OL;
                skip = (size_t)((const char *)memchr(s, '.', n) - s) + 1;
                while (skip < n && s[skip] == ' ')
                    skip++;
            } else if (s[0] == '>')
                want = QUOTE, skip = n > 1 && s[1] == ' ' ? 2 : 1;
            if ((int)blk != want) {
                puts_(close[blk]);
                puts_(want == UL ? "<ul>" : want == OL ? "<ol>" : want == QUOTE ? "<blockquote>" : "<p>");
                blk = (int)want;
            } else if (want == PARA || want == QUOTE) {
                puts_("<br>");
            }
            if (want == UL || want == OL)
                puts_("<li>");
            inl(s + skip, n - skip);
            if (want == UL || want == OL)
                puts_("</li>");
            puts_("\n");
        }
        s = e ? e + 1 : s + n;
    }
    puts_(close[blk]);
}

/* ------------------------------------------------------------ the page */
static const char css[] =
    "body{font:16px/1.45 -apple-system,system-ui,'Segoe UI',Roboto,sans-serif;margin:0 auto;max-width:44em;"
    "padding:12px 16px;color:#1b1b1f;background:#fff}"
    "h1{font-size:1.5em;margin:.2em 0 .6em}h2,h3,h4,h5{margin:.8em 0 .3em}"
    ".c{border-bottom:1px solid #e3e3e8;padding:.45em 0}"
    ".in{font-family:ui-monospace,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-word}"
    ".math .out{text-align:right}.r{max-width:100%;height:auto;vertical-align:middle}"
    ".g{display:block;max-width:100%;height:auto;border:1px solid #c8c8d0;border-radius:4px;margin:.3em 0}"
    ".err{color:#c0201c}.off{color:#999}pre{background:#f5f5f8;border-radius:4px;padding:.5em .7em;overflow-x:auto;"
    "white-space:pre-wrap;margin:.3em 0}.py .in{color:#0a4cc2}.m{font-style:italic}"
    "blockquote{border-left:3px solid #c8c8d0;margin:.4em 0;padding-left:.8em;color:#555}"
    "footer{color:#888;font-size:.8em;margin-top:1.5em}";

static void cell(int i, bool can_plot)
{
    const nb_cell_t *c = &nbd->cell[i];
    const char *src = nbd_get(c, NBF_SRC);
    bool err = c->f[NBF_ENAME] != NULL;
    switch (c->kind) {
    case NBK_TEXT:
        puts_("<div class=\"c text\">\n");
        markdown(src);
        break;
    case NBK_MATH:
        puts_("<div class=\"c math\"><div class=\"in\">");
        esc_s(src);
        puts_("</div><div class=\"out");
        puts_(err ? " err\">" : "\">");
        if (err)
            esc_s(nbd_get(c, NBF_EVALUE));
        else if (c->f[NBF_VAL] && !math_img(c->f[NBF_VAL]))
            esc_s(c->f[NBF_VAL]);
        puts_("</div>");
        break;
    case NBK_PY:
        puts_("<div class=\"c py\"><pre class=\"in\">");
        esc_s(src);
        puts_("</pre>");
        if (c->f[NBF_OUT] || c->f[NBF_VAL] || err) {
            puts_(err ? "<pre class=\"err\">" : "<pre>");
            esc_s(nbd_get(c, NBF_OUT));
            if (err)
                esc_s(c->f[NBF_TB] ? c->f[NBF_TB] : nbd_get(c, NBF_EVALUE));
            else
                esc_s(nbd_get(c, NBF_VAL));
            puts_("</pre>");
        }
        break;
    case NBK_GRAPH: {
        puts_("<div class=\"c graph\"><div class=\"in\">");
        int w, h;
        gcell_plot_size(i, &w, &h);
        bool empty = gcell_empty(i);
        const uint16_t *pix = empty ? NULL : gcell_cached(i, w, h);
        if (!pix && can_plot && !empty)
            pix = gcell_image(i, w, h);
        char fns[256], b[ED_MAX * 2 + 16];
        uint16_t col;
        bool on;
        for (int k = 0; gcell_fn(i, k, b, sizeof b, &col, &on); k++) { /* the caption, in the curves' colours */
            char st[48];
            snprintf(st, sizeof st, "<span style=\"color:#%02x%02x%02x\">", (col >> 8) & 0xF8, (col >> 3) & 0xFC,
                     (col << 3) & 0xF8);
            puts_(k ? "&nbsp; " : "");
            puts_(on ? st : "<span class=\"off\">");
            esc_s(b);
            puts_("</span>");
        }
        if (empty)
            puts_("<i>graph: no function</i>");
        double an[4];
        if (gcell_anim_cfg(i, an)) { /* an animated cell: a still frame here */
            snprintf(b, sizeof b, " &nbsp;<i>animated (t from %.4g to %.4g)</i>", an[0], an[1]);
            puts_(b);
        }
        gcell_functions(i, fns, sizeof fns);
        puts_("</div>");
        if (pix)
            img(pix, w, h, fns, "g");
        else if (!empty)
            puts_("<p><i>(the plot is drawn when the drive is opened again)</i></p>");
        break;
    }
    }
    puts_("</div>\n");
}

/* Build the page from the open notebook; -> its size in bytes (0: no memory). */
uint32_t nbhtml_build(void)
{
    if (!html || !nbd)
        return 0;
    hn = 0;
    full = false;
    int theme = theme_get();
    if (theme_is_dark())
        theme_set(THEME_LIGHT); /* (the pictures in the light theme) */
    const char *name = nbd->open ? nbd->name : "Calculations";
    puts_("<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>");
    esc_s(name);
    puts_("</title><style>");
    puts_(css);
    puts_("</style></head><body>\n<h1>");
    esc_s(name);
    puts_("</h1>\n");
    bool can_plot = !app_in_cas_poll();
    for (int i = 0; i < nbd->ncells && !full; i++)
        cell(i, can_plot);
    if (full)
        puts_("<p><i>(the rest of the notebook did not fit)</i></p>");
    char f[160];
    snprintf(f, sizeof f, "<footer>%d cells. Made by the calculator, firmware %s.</footer>\n",
             nbd->ncells, fw_version);
    puts_(f);
    puts_("</body></html>\n");
    if (theme != theme_get())
        theme_set(theme);
    return (uint32_t)hn;
}

void nbhtml_read(void *user, uint32_t off, uint8_t *buf, uint32_t n)
{
    (void)user;
    for (uint32_t i = 0; i < n; i++)
        buf[i] = html && off + i < hn ? (uint8_t)html[off + i] : 0;
}
