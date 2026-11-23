/*
 * Live preview: the result of the entry line, shown dimmed under it while
 * typing. Evaluated from the home screen's tick once the line has been idle
 * for PREVIEW_IDLE_MS, with cas_eval_quiet (ans and the history are not
 * touched), never for assignments or commands with side effects or that are
 * slow, and time-boxed: the CAS poll callback stops it after
 * PREVIEW_BUDGET_MS or as soon as a key is waiting, so typing is never held
 * up. In the simulators the clock is virtual (tests are deterministic).
 *
 * State machine (all in `pv`): every tick hashes the entry line together with
 * the settings that change the result. A different hash means the line was
 * edited: the old preview is dropped (redraw if one was shown) and an
 * evaluation becomes pending. Once the hash has been stable for
 * PREVIEW_IDLE_MS and no key is queued, the line is evaluated once; the
 * result stays valid until the hash changes again. A timed-out or failed
 * evaluation is not retried until the line changes.
 *
 * Runs in the main loop only (home screen tick); the poll callback runs
 * inside the CAS evaluation on the same thread and only reads the key queue
 * (filled by the key interrupt) and the clock.
 */
#include <stdio.h>
#include <string.h>
#include "app.h" /* app_cas_poll */
#include "hal.h"
#include "m2d.h"
#include "nbdoc.h"

#define PREVIEW_IDLE_MS 300  /* idle time after the last edit before evaluating */
#define PREVIEW_BUDGET_MS 150 /* longest evaluation allowed */

static struct {
    uint32_t hash, changed_at; /* hash of the line + settings; when it last changed */
    bool pending, valid;       /* evaluation due; text holds a result for `hash` */
    char text[192];            /* display text of the result */
} pv;

/* FNV-1a hash of the token ids, mixed with the settings that affect the
 * displayed result (angle, exact, digits, number format, complex mode,
 * repeating decimals) and the length. 0 is used as "nothing" by reset. */
static uint32_t hash_editor(const editor_t *e)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < e->len; i++)
        h = (h ^ e->t[i]) * 16777619u;
    const int s[] = {g.cs.angle, g.cs.exact, g.cs.digits, g.fmt, g.fix, g.cplx, m2d_repeat, autosimp_on(), e->len};
    for (size_t i = 0; i < sizeof s / sizeof s[0]; i++)
        h = (h ^ (uint32_t)s[i]) * 16777619u;
    return h;
}

/* Forget the preview (PREVIEW switched, MEM reset). */
void m2d_preview_reset(void)
{
    pv.valid = pv.pending = false;
    pv.hash = 0;
}

/* The preview for the editor's current contents, or NULL if the setting is
 * off, nothing has been evaluated yet, or the line has changed since. */
const char *m2d_preview_text(const editor_t *ed)
{
    if (!m2d_preview || !pv.valid || pv.hash != hash_editor(ed))
        return NULL;
    return pv.text;
}

/* substring test (deliberately loose: any name containing a listed word is
 * skipped too, which errs on the safe side) */
static bool word(const char *s, const char *w) { return strstr(s, w) != NULL; }

static bool ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

/* does the text contain the imaginary unit i as a separate word? */
static bool has_i(const char *s)
{
    for (const char *p = s; *p; p++)
        if (*p == 'i' && (p == s || !ident_char(p[-1])) && !ident_char(p[1]))
            return true;
    return false;
}

/* commands that change state, are random, or can take long ("\x01" is the
 * CAS text of the → store token, ":=" an assignment) */
static bool skip(const char *cas)
{
    static const char *const bad[] = {":=", "\x01", "assume", "purge", "delvar", "restart", "rand", "solve",
                                      "integrate", "limit", "series", "taylor", "desolve", "dsolve", "isprime",
                                      "ifactor", "nextprime", "Store", "store", "define", "kill"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        if (word(cas, bad[i]))
            return true;
    return false;
}

static uint32_t eval_start;
/* CAS poll callback (called during the evaluation): nonzero stops it with
 * CAS_ERR_INTERRUPTED when a key is waiting or the time budget is spent */
static int poll_cb(void *user)
{
    (void)user;
    hal_watchdog_kick();
    return !keyq_empty() || hal_millis() - eval_start > PREVIEW_BUDGET_MS;
}

/* Evaluate the line for the preview into pv.text; true if there is
 * something worth showing. Steps: CAS text of the line; refuse skipped
 * commands; auto-close unbalanced "(" (at most 16; unbalanced "[" or a
 * surplus ")" refuse); rewrite repeating decimals; define Y1..Y6; evaluate
 * quietly with the time box; refuse errors and non-real results in REAL mode
 * (unless the input itself uses i); format like the home screen (number
 * format, pretty printing); drop results that merely repeat the input or are
 * "undef". */
static bool evaluate(const editor_t *ed)
{
    char cas[ED_MAX * 4 + 64], disp[ED_MAX * 4 + 16];
    ed_cas(ed, cas, sizeof cas - 32);
    if (!cas[0] || skip(cas))
        return false;
    /* close what is still open: "2*(3+4" previews as 2*(3+4) */
    int depth = 0, sq = 0;
    for (const char *p = cas; *p; p++) {
        depth += *p == '(' ? 1 : *p == ')' ? -1 : 0;
        sq += *p == '[' ? 1 : *p == ']' ? -1 : 0;
    }
    if (depth < 0 || sq != 0 || depth > 16)
        return false;
    size_t l = strlen(cas);
    while (depth-- > 0)
        cas[l++] = ')';
    cas[l] = 0;
    static char ref[ED_MAX * 4 + 2048];
    char rw[ED_MAX * 4 + 64], msg[80];
    if (!nbd_refs(cas, ref, sizeof ref, msg, sizeof msg)) /* Ans and Out[n] of the notebook */
        return false;
    const char *in = repdec_rewrite(ref, rw, sizeof rw) ? rw : ref;
    yvars_sync();
    char raw[256], tmp[256];
    eval_start = hal_millis();
    cas_set_poll(g.cas, poll_cb, NULL);
    cas_status st = cas_eval_quiet(g.cas, in, raw, sizeof raw);
    cas_set_poll(g.cas, app_cas_poll, NULL); /* the app's default hook (watchdog) */
    if (st != CAS_OK || !raw[0])
        return false;
    autosimp_text(in, raw, sizeof raw); /* MODE SIMPLIFY AUTO: show what ENTER will give */
    if (numfmt_nonreal(raw) && g.cplx == CPLX_REAL && !has_i(cas))
        return false;
    numfmt_apply(raw, tmp, sizeof tmp);
    pretty_from_cas(tmp, pv.text, sizeof pv.text);
    ed_display(ed, disp, sizeof disp);
    if (!strcmp(disp, pv.text) || !strcmp(pv.text, "undef"))
        return false; /* nothing to tell */
    return true;
}

/* Advance the state machine (see the file header); true if the home screen
 * must be redrawn (a preview appeared or an old one became stale). */
bool m2d_preview_tick(const editor_t *ed, uint32_t now)
{
    if (!m2d_preview)
        return false;
    uint32_t h = hash_editor(ed);
    if (h != pv.hash) {
        bool had = pv.valid;
        pv.hash = h;
        pv.valid = false;
        pv.pending = !ed_empty(ed);
        pv.changed_at = now;
        return had;
    }
    if (pv.pending && now - pv.changed_at >= PREVIEW_IDLE_MS) {
        pv.pending = false;
        if (!keyq_empty())
            return false;
        pv.valid = evaluate(ed);
        return pv.valid;
    }
    return false;
}
