/*
 * unit_robust.inc -- running out of memory, one exhaustion after another, leaves the
 * engine fully usable: scratch released, variables and ans intact, no stuck interrupt
 * flag, the reserve kept for printing, sessions still saved; included by unit_tests.c.
 */
static int robust_sink(void *user, const char *s, size_t n) {
    (void)s;
    *(size_t *)user += n;
    return 0;
}

static void test_robust(void) {
    size_t sz = 600u << 10;
    void *mem = malloc(sz);
    cas_ctx *c = cas_init(mem, sz);
    CHECK(c != NULL, "cas_init 600 KB");
    if (!c) return;
    char out[8192];
    CHECK(cas_eval(c, "v:=7", out, sizeof out) == CAS_OK, "v:=7");
    cas_mem_stats m0, m1;
    cas_mem(c, &m0);
    static const char *ooms[] = {"expand((x+y+z)^60)", "factorial(19999)*factorial(19998)", "factor(x^4096-1)",
                                 "seq(k^k,k,1,2000)", "partition(5000)", "expand((x+1)^3000)"};
    for (int round = 0; round < 3; round++)
        for (size_t i = 0; i < sizeof ooms / sizeof ooms[0]; i++) {
            cas_status st = cas_eval(c, ooms[i], out, sizeof out);
            CHECK(st == CAS_ERR_MEMORY, "%s: status %d (%s), want CAS_ERR_MEMORY", ooms[i], (int)st, cas_last_error(c));
            CHECK(out[0] == 0, "%s: no text after an error", ooms[i]);
            cas_mem(c, &m1);
            CHECK(m1.used == m0.used, "%s: %zu bytes still in use after the error (was %zu)", ooms[i], m1.used, m0.used);
        }
    /* normal work afterwards */
    CHECK(cas_eval(c, "v+1", out, sizeof out) == CAS_OK && !strcmp(out, "8"), "v+1 -> %s", out);
    CHECK(cas_eval(c, "factor(x^2-1)", out, sizeof out) == CAS_OK && !strcmp(out, "(x-1)*(x+1)"), "factor -> %s", out);
    CHECK(cas_eval(c, "integrate(x*sin(x),x)", out, sizeof out) == CAS_OK, "integrate after OOM: %s", cas_last_error(c));
    /* an interrupt requested between evaluations does not stick */
    cas_interrupt(c);
    CHECK(cas_eval(c, "1+1", out, sizeof out) == CAS_OK && !strcmp(out, "2"), "1+1 after cas_interrupt -> %s", out);
    /* a stack limit hit leaves it usable too */
    cas_set_stack_limit(c, 16u << 10);
    static char deep[4200];
    for (int i = 0; i < 1400; i++) deep[i] = '(';
    deep[1400] = 'x';
    for (int i = 0; i < 1400; i++) deep[1401 + i] = ')';
    deep[2801] = 0;
    cas_status st = cas_eval(c, deep, out, sizeof out);
    CHECK(st == CAS_ERR_MEMORY || st == CAS_ERR_SYNTAX, "deep nesting: status %d", (int)st);
    cas_set_stack_limit(c, 256u << 10);
    CHECK(cas_eval(c, "v*2", out, sizeof out) == CAS_OK && !strcmp(out, "14"), "v*2 -> %s", out);
    /* the session still saves */
    size_t bytes = 0;
    CHECK(cas_save(c, robust_sink, &bytes) == CAS_OK && bytes > 0, "cas_save after OOM");
    free(mem);
    cas_reset(ctx); /* back to the shared test context */
}
