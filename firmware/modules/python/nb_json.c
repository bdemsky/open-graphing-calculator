/*
 * JSON for the Notebook (nb.h): a writer in the layout Jupyter uses (so a
 * notebook saved on the calculator diffs cleanly against one saved by
 * Jupyter) and a streaming parser for .ipynb files of any size: events with
 * the path of member names and indices, strings handed over in pieces, a
 * nesting limit, and errors with a line number.
 */
#include <string.h>
#include "nb.h"
#include "py/formatfloat.h"

/* A decimal number ("-1.5e3", leading spaces allowed); *end after it (== s: none). Plain C:
 * the module's C library would pull in malloc for strtod. */
double nb_strtod(const char *s, const char **end)
{
    const char *p = s;
    while (*p == ' ')
        p++;
    double sign = 1, v = 0;
    if (*p == '-' || *p == '+')
        sign = *p++ == '-' ? -1 : 1;
    const char *d0 = p;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    if (*p == '.') {
        double f = 0.1;
        for (p++; *p >= '0' && *p <= '9'; p++, f /= 10)
            v += (*p - '0') * f;
    }
    if (p == d0 || (p == d0 + 1 && *d0 == '.')) {
        if (end)
            *end = s;
        return 0;
    }
    if (*p == 'e' || *p == 'E') {
        const char *q = p + 1;
        int es = 1, e = 0;
        if (*q == '-' || *q == '+')
            es = *q++ == '-' ? -1 : 1;
        if (*q >= '0' && *q <= '9') {
            while (*q >= '0' && *q <= '9')
                e = e < 400 ? e * 10 + (*q++ - '0') : (q++, e);
            for (; e > 0; e--)
                v = es > 0 ? v * 10 : v / 10;
            p = q;
        }
    }
    if (end)
        *end = p;
    return sign * v;
}

/* ---------------------------------------------------------------- writer */
static void out(jw_t *w, const char *s, size_t n)
{
    w->put(w->u, s, n);
    w->len += n;
}
static void outs(jw_t *w, const char *s) { out(w, s, strlen(s)); }

void jw_init(jw_t *w, nb_sink_fn put, void *u)
{
    memset(w, 0, sizeof *w);
    w->put = put, w->u = u;
}

/* before a value or a key: the comma, a new line and the indent (not after a key) */
static void sep(jw_t *w)
{
    if (w->after_key) {
        w->after_key = false;
        return;
    }
    if (!w->depth)
        return;
    uint32_t bit = 1u << (w->depth & 31);
    if (!(w->first & bit))
        out(w, ",", 1);
    w->first &= ~bit;
    out(w, "\n", 1);
    for (int i = 0; i < w->depth; i++)
        out(w, " ", 1);
}

static void open_c(jw_t *w, char c)
{
    sep(w);
    out(w, &c, 1);
    if (w->depth < JW_DEPTH - 1)
        w->depth++;
    w->first |= 1u << (w->depth & 31);
}
static void close_c(jw_t *w, char c)
{
    bool empty = w->first & (1u << (w->depth & 31));
    if (w->depth)
        w->depth--;
    if (!empty) {
        out(w, "\n", 1);
        for (int i = 0; i < w->depth; i++)
            out(w, " ", 1);
    }
    out(w, &c, 1);
    if (!w->depth)
        out(w, "\n", 1); /* (the file ends with a new line) */
}
void jw_obj(jw_t *w) { open_c(w, '{'); }
void jw_obj_end(jw_t *w) { close_c(w, '}'); }
void jw_arr(jw_t *w) { open_c(w, '['); }
void jw_arr_end(jw_t *w) { close_c(w, ']'); }

/* string contents: escapes, and only valid UTF-8 (anything else becomes U+FFFD) */
static void esc(jw_t *w, const uint8_t *s, size_t n)
{
    static const char hex[] = "0123456789abcdef";
    size_t i = 0, run = 0;
    while (i < n) {
        uint8_t c = s[i];
        int k = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (k == 1 && c >= 0x20 && c != '"' && c != '\\') {
            i++, run++;
            continue;
        }
        bool ok = k > 1 && i + (size_t)k <= n && (k != 2 || c >= 0xC2);
        for (int j = 1; ok && j < k; j++)
            ok = (s[i + (size_t)j] & 0xC0) == 0x80;
        if (ok) { /* a whole multi-byte character */
            i += (size_t)k, run += (size_t)k;
            continue;
        }
        out(w, (const char *)s + i - run, run);
        run = 0;
        if (k == 1) {
            char e[7] = {'\\', 0};
            switch (c) {
            case '"': e[1] = '"'; break;
            case '\\': e[1] = '\\'; break;
            case '\n': e[1] = 'n'; break;
            case '\r': e[1] = 'r'; break;
            case '\t': e[1] = 't'; break;
            case '\b': e[1] = 'b'; break;
            case '\f': e[1] = 'f'; break;
            default: e[1] = 'u', e[2] = '0', e[3] = '0', e[4] = hex[c >> 4], e[5] = hex[c & 15];
            }
            outs(w, e);
        } else {
            out(w, "\xEF\xBF\xBD", 3);
        }
        i++;
    }
    out(w, (const char *)s + n - run, run);
}

void jw_key(jw_t *w, const char *k)
{
    sep(w);
    out(w, "\"", 1);
    esc(w, (const uint8_t *)k, strlen(k));
    out(w, "\": ", 3);
    w->after_key = true;
}

void jw_str_begin(jw_t *w)
{
    sep(w);
    out(w, "\"", 1);
    w->un = 0;
}
/* A piece of a string; a UTF-8 character cut at the end of a piece is kept for the next one. */
void jw_str_piece(jw_t *w, const char *s, size_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    while (w->un && n) { /* finish the character left over */
        w->ub[w->un++] = *p++, n--;
        uint8_t c = w->ub[0];
        int k = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        if (w->un >= k || (w->ub[w->un - 1] & 0xC0) != 0x80) {
            esc(w, w->ub, (size_t)w->un);
            w->un = 0;
        }
    }
    /* a trailing lead byte (and its continuations) waits for the next piece */
    size_t keep = 0;
    for (size_t j = 1; j <= 3 && j <= n; j++) {
        uint8_t c = p[n - j];
        if ((c & 0xC0) == 0x80)
            continue;
        int k = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        if (k > (int)j)
            keep = j;
        break;
    }
    esc(w, p, n - keep);
    memcpy(w->ub, p + n - keep, keep);
    w->un = (int)keep;
}
void jw_str_end(jw_t *w)
{
    if (w->un)
        esc(w, w->ub, (size_t)w->un); /* (a cut character: U+FFFD) */
    w->un = 0;
    out(w, "\"", 1);
}
void jw_str(jw_t *w, const char *s)
{
    jw_str_begin(w);
    if (s)
        esc(w, (const uint8_t *)s, strlen(s));
    out(w, "\"", 1);
}
void jw_raw(jw_t *w, const char *s)
{
    sep(w);
    outs(w, s);
}
void jw_int(jw_t *w, long v)
{
    char b[24], *e = b + sizeof b - 1;
    unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
    *e = 0;
    do
        *--e = (char)('0' + u % 10);
    while (u /= 10);
    if (v < 0)
        *--e = '-';
    jw_raw(w, e);
}
void jw_num(jw_t *w, double v)
{
    if (v > -1e15 && v < 1e15 && v == (double)(long)v) { /* (NaN fails the range test first) */
        jw_int(w, (long)v);
        return;
    }
    if (!(v == v) || v > 1e308 || v < -1e308) {
        jw_null(w); /* (JSON has no NaN or infinity) */
        return;
    }
    char b[40];
    mp_format_float(v, b, sizeof b, 'g', 15, '\0');
    jw_raw(w, b);
}
void jw_null(jw_t *w) { jw_raw(w, "null"); }
void jw_bool(jw_t *w, bool v) { jw_raw(w, v ? "true" : "false"); }

void jw_lines(jw_t *w, const char *s)
{
    jw_arr(w);
    while (s && *s) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) + 1 : strlen(s);
        jw_str_begin(w);
        esc(w, (const uint8_t *)s, n);
        out(w, "\"", 1);
        s += n;
    }
    jw_arr_end(w);
}

/* ---------------------------------------------------------------- parser */
static int peekc(jp_t *p)
{
    if (p->pos >= p->len) {
        if (p->eof)
            return -1;
        int r = p->read(p->ru, p->buf, sizeof p->buf);
        if (r <= 0) {
            p->eof = true;
            if (r < 0 && !p->stop) {
                memcpy(p->err, "the file could not be read", 27);
                p->stop = true;
            }
            return -1;
        }
        p->len = (size_t)r, p->pos = 0;
    }
    return p->buf[p->pos];
}
static int getc_(jp_t *p)
{
    int c = peekc(p);
    if (c >= 0) {
        p->pos++, p->off++;
        if (c == '\n')
            p->line++;
        if (p->max && p->off > p->max && !p->stop) {
            memcpy(p->err, "the file is too large", 22);
            p->stop = true;
        }
    }
    return c;
}

static void fail(jp_t *p, const char *what)
{
    if (p->stop)
        return;
    p->stop = true;
    /* "bad JSON (line 12): expected ':'" */
    char ln[12], *e = ln + sizeof ln - 1;
    uint32_t v = p->line;
    *e = 0;
    do
        *--e = (char)('0' + v % 10);
    while (v /= 10);
    size_t k = 0;
    const char *parts[] = {"bad JSON (line ", e, "): ", what};
    for (int i = 0; i < 4; i++)
        for (const char *s = parts[i]; *s && k < sizeof p->err - 1; s++)
            p->err[k++] = *s;
    p->err[k] = 0;
}

static void ws(jp_t *p)
{
    for (int c; (c = peekc(p)) == ' ' || c == '\t' || c == '\n' || c == '\r';)
        getc_(p);
}

static int hexv(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static int u4(jp_t *p)
{
    int v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexv(getc_(p));
        if (h < 0)
            return -1;
        v = v << 4 | h;
    }
    return v;
}

/* a string after its opening quote: as a key (into key) or as pieces */
static void string(jp_t *p, char *key, size_t keyn)
{
    char piece[260];
    size_t n = 0, kn = 0;
    if (!key)
        p->ev(p, JE_STR, NULL, 0);
    for (;;) {
        if (p->stop)
            return;
        int c = getc_(p);
        if (c < 0) {
            fail(p, "unterminated string");
            return;
        }
        if (c == '"')
            break;
        char u[4];
        int k = 1;
        u[0] = (char)c;
        if (c < 0x20) {
            fail(p, "control character in a string");
            return;
        }
        if (c == '\\') {
            c = getc_(p);
            switch (c) {
            case '"': case '\\': case '/': u[0] = (char)c; break;
            case 'n': u[0] = '\n'; break;
            case 't': u[0] = '\t'; break;
            case 'r': u[0] = '\r'; break;
            case 'b': u[0] = '\b'; break;
            case 'f': u[0] = '\f'; break;
            case 'u': {
                long cp = u4(p);
                if (cp >= 0xD800 && cp < 0xDC00) { /* a surrogate pair */
                    long lo = getc_(p) == '\\' && getc_(p) == 'u' ? u4(p) : -1;
                    cp = lo >= 0xDC00 && lo < 0xE000 ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
                } else if (cp >= 0xDC00 && cp < 0xE000) {
                    cp = 0xFFFD;
                }
                if (cp < 0) {
                    fail(p, "bad \\u escape");
                    return;
                }
                if (cp < 0x80)
                    u[0] = (char)cp;
                else if (cp < 0x800)
                    u[0] = (char)(0xC0 | cp >> 6), u[1] = (char)(0x80 | (cp & 63)), k = 2;
                else if (cp < 0x10000)
                    u[0] = (char)(0xE0 | cp >> 12), u[1] = (char)(0x80 | ((cp >> 6) & 63)), u[2] = (char)(0x80 | (cp & 63)), k = 3;
                else
                    u[0] = (char)(0xF0 | cp >> 18), u[1] = (char)(0x80 | ((cp >> 12) & 63)),
                    u[2] = (char)(0x80 | ((cp >> 6) & 63)), u[3] = (char)(0x80 | (cp & 63)), k = 4;
                break;
            }
            default:
                fail(p, "bad escape in a string");
                return;
            }
        }
        if (key) {
            for (int j = 0; j < k && kn + 1 < keyn; j++)
                key[kn++] = u[j];
            continue;
        }
        memcpy(piece + n, u, (size_t)k);
        n += (size_t)k;
        if (n >= 256) {
            p->ev(p, JE_STR_PIECE, piece, n);
            n = 0;
        }
    }
    if (key) {
        key[kn] = 0;
        return;
    }
    if (n)
        p->ev(p, JE_STR_PIECE, piece, n);
    p->ev(p, JE_STR_END, NULL, 0);
}

static bool word(jp_t *p, const char *w)
{
    for (; *w; w++)
        if (getc_(p) != *w)
            return false;
    return true;
}

static void value(jp_t *p);

static void container(jp_t *p, bool obj)
{
    if (p->depth >= JP_DEPTH) {
        fail(p, "nested too deeply");
        return;
    }
    p->ev(p, obj ? JE_OBJ : JE_ARR, NULL, 0);
    int d = p->depth++;
    p->key[d][0] = 0;
    p->idx[d] = 0;
    getc_(p);
    ws(p);
    if (peekc(p) == (obj ? '}' : ']')) {
        getc_(p);
    } else {
        for (;;) {
            if (p->stop)
                return;
            if (obj) {
                ws(p);
                if (getc_(p) != '"') {
                    fail(p, "expected a member name");
                    return;
                }
                string(p, p->key[d], sizeof p->key[d]);
                p->ev(p, JE_KEY, p->key[d], strlen(p->key[d]));
                ws(p);
                if (getc_(p) != ':') {
                    fail(p, "expected ':'");
                    return;
                }
            }
            value(p);
            if (p->stop)
                return;
            ws(p);
            int c = getc_(p);
            if (c == ',') {
                p->idx[d]++;
                continue;
            }
            if (c == (obj ? '}' : ']'))
                break;
            fail(p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
            return;
        }
    }
    p->depth--;
    p->ev(p, obj ? JE_OBJ_END : JE_ARR_END, NULL, 0);
}

static void value(jp_t *p)
{
    ws(p);
    int c = peekc(p);
    switch (c) {
    case '{':
    case '[':
        container(p, c == '{');
        return;
    case '"':
        getc_(p);
        string(p, NULL, 0);
        return;
    case 't':
        if (word(p, "true"))
            p->ev(p, JE_TRUE, "true", 4);
        else
            fail(p, "bad literal");
        return;
    case 'f':
        if (word(p, "false"))
            p->ev(p, JE_FALSE, "false", 5);
        else
            fail(p, "bad literal");
        return;
    case 'n':
        if (word(p, "null"))
            p->ev(p, JE_NULL, "null", 4);
        else
            fail(p, "bad literal");
        return;
    default:
        if (c == '-' || (c >= '0' && c <= '9')) {
            char num[40];
            size_t n = 0;
            while ((c = peekc(p)) == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9')) {
                if (n < sizeof num - 1)
                    num[n++] = (char)c;
                getc_(p);
            }
            num[n] = 0;
            p->ev(p, JE_NUM, num, n);
            return;
        }
        fail(p, c < 0 ? "unexpected end of the file" : "unexpected character");
    }
}

int jp_parse(jp_t *p)
{
    p->pos = p->len = 0;
    p->eof = p->stop = false;
    p->off = 0, p->line = 1, p->depth = 0;
    p->err[0] = 0;
    ws(p);
    if (peekc(p) != '{') {
        if (!p->stop)
            fail(p, "not a JSON object");
        return -1;
    }
    value(p);
    ws(p);
    if (!p->stop && peekc(p) >= 0)
        fail(p, "text after the end");
    return p->stop ? -1 : 0;
}

bool jp_path(const jp_t *p, int n, const char *const *keys)
{
    if (p->depth != n)
        return false;
    for (int i = 0; i < n; i++)
        if (strcmp(keys[i], "*") && strcmp(keys[i], p->key[i]))
            return false;
    return true;
}
