/* Host test for the saved-state blobs of the solver and finance apps
 * (persist_solver in app/solver.c, persist_finance in app/finance.c) and of
 * the settings (persist_settings in app/mode.c).
 *
 * Each blob stores arrays with a count in front. A count too large for the
 * app's arrays (from a corrupt or foreign blob) must keep the defaults for
 * that array but still skip its entries, so the fields after it are read
 * from the right place. The test writes such blobs with the session writer
 * (pw_*), loads them through the persist_def_t callbacks, saves the state
 * again and reads the fields back with pr_*.
 * Built and run by `make test` (tests/unit/test_*.c, see Makefile). */
#include <stdio.h>
#include <string.h>
#include "session.h"
#include "ui.h"

extern const persist_def_t persist_solver, persist_finance, persist_settings;

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static uint8_t blob[8192], out[8192];

/* load blob[0..len) into d, save d again into out; returns the saved length */
static size_t round_trip(const persist_def_t *d, size_t len)
{
    pr_t r = {blob, len, 0, false};
    d->load(&r, d->version);
    pw_t w = {out, sizeof out, 0, false};
    d->save(&w);
    return w.overflow ? 0 : w.len;
}

/* Solver blob (version 1): str eq; u8 nv; nv x (str name, f64 value);
 * f64 lo, hi; u8 deg; (deg + 1) x f64; u8 m, n; m x (n + 1) x f64.
 * 10 variables (the solver keeps 8): the variables keep their defaults, but
 * the bounds, the polynomial and the linear system are still read. */
static void test_solver(void)
{
    pw_t w = {blob, sizeof blob, 0, false};
    pw_str(&w, "a+b+c+d+e+f+g+h+i+j=1");
    pw_u8(&w, 10);
    for (int i = 0; i < 10; i++) {
        char name[2] = {(char)('a' + i), 0};
        pw_str(&w, name);
        pw_f64(&w, i);
    }
    pw_f64(&w, -5);
    pw_f64(&w, 7);
    pw_u8(&w, 2);
    pw_f64(&w, 1), pw_f64(&w, 0), pw_f64(&w, -4);
    pw_u8(&w, 1), pw_u8(&w, 1);
    pw_f64(&w, 3), pw_f64(&w, 6);
    size_t n = round_trip(&persist_solver, w.len);
    CHECK(n > 0);
    pr_t r = {out, n, 0, false};
    char eq[160];
    pr_str(&r, eq, sizeof eq);
    CHECK(!strcmp(eq, "a+b+c+d+e+f+g+h+i+j=1"));
    CHECK(pr_u8(&r) == 0); /* too many variables: none kept */
    CHECK(pr_f64(&r) == -5 && pr_f64(&r) == 7);
    CHECK(pr_u8(&r) == 2);
    CHECK(pr_f64(&r) == 1 && pr_f64(&r) == 0 && pr_f64(&r) == -4);
    CHECK(pr_u8(&r) == 1 && pr_u8(&r) == 1 && pr_f64(&r) == 3 && pr_f64(&r) == 6);
    CHECK(!r.err);
}

/* Finance blob (version 1): 7 x f64 TVM; u8 begin; f64 Pmt1, Pmt2, I%, CF0;
 * u8 ncf; ncf x f64; f64 Nom%, Eff%, C/Y, date1, date2. 20 cash flows (the
 * app keeps 12): one cash flow is kept, the fields after them are read. */
static void test_finance(void)
{
    pw_t w = {blob, sizeof blob, 0, false};
    for (int i = 0; i < 7; i++)
        pw_f64(&w, i == 5 || i == 6 ? 12 : i);
    pw_u8(&w, 0);
    pw_f64(&w, 1), pw_f64(&w, 12), pw_f64(&w, 10), pw_f64(&w, -1000);
    pw_u8(&w, 20);
    for (int i = 0; i < 20; i++)
        pw_f64(&w, 100 + i);
    pw_f64(&w, 6), pw_f64(&w, 6.17), pw_f64(&w, 4), pw_f64(&w, 20240101), pw_f64(&w, 20241231);
    size_t n = round_trip(&persist_finance, w.len);
    CHECK(n > 0);
    pr_t r = {out, n, 0, false};
    for (int i = 0; i < 7; i++)
        pr_f64(&r);
    pr_u8(&r);
    CHECK(pr_f64(&r) == 1 && pr_f64(&r) == 12 && pr_f64(&r) == 10 && pr_f64(&r) == -1000);
    CHECK(pr_u8(&r) == 1); /* too many cash flows: the default count */
    pr_f64(&r);
    CHECK(pr_f64(&r) == 6 && pr_f64(&r) == 6.17 && pr_f64(&r) == 4);
    CHECK(pr_f64(&r) == 20240101 && pr_f64(&r) == 20241231);
    CHECK(!r.err);
}

/* Settings: version 1 had ten bytes; version 2 adds ON USB. The session pads
 * every blob to 4 bytes with zeros, so a version 1 blob as it was stored has an
 * eleventh byte, 0, which must not read as ON USB = SLEEP. */
static void test_settings(void)
{
    static const uint8_t v1[12] = {1, 0, 10, 0, 0xFF, 0, 1, 1, 60, 1, 0, 0}; /* (padding) */
    g.usb_awake = false;
    pr_t r = {v1, sizeof v1, 0, false};
    persist_settings.load(&r, 1);
    CHECK(g.usb_awake);  /* no ON USB byte: STAY ON */
    CHECK(g.cs.digits == 10 && g.grid && g.backlight == 60);
    for (int on = 0; on < 2; on++) { /* version 2 keeps the choice either way */
        g.usb_awake = !on;
        pw_t w = {blob, sizeof blob, 0, false};
        persist_settings.save(&w);
        while (w.len & 3) /* as the session pads it */
            pw_u8(&w, 0);
        g.usb_awake = on;
        pr_t r2 = {blob, w.len, 0, false};
        persist_settings.load(&r2, persist_settings.version);
        CHECK(g.usb_awake == !on);
    }
    CHECK(persist_settings.version >= 2);
}

int main(void)
{
    test_solver();
    test_finance();
    test_settings();
    printf("test_persist: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
