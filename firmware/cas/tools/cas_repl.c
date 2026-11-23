/* cas_repl.c -- host command-line front end for the CAS.
 * Interactive: prints "> " prompts.  Batch (-b): one result line per input
 * line, errors as "ERROR <code>: <message>".  -t <ms> sets a per-line timeout
 * implemented with cas_interrupt from a SIGALRM handler.
 *
 * This is the only part of the CAS that uses the C library's stdio, malloc and
 * signals; the engine itself (libcas.a) sees nothing but the arena and the
 * public API of include/cas.h.  The test harnesses drive this program in batch
 * mode: tests/difftest.py (SymPy differential tests) and tests/bench.py
 * (benchmarks, with -T timings).
 *
 * Options:
 *   -b       batch mode (no prompt; empty lines are evaluated too, so output
 *            lines stay aligned with input lines)
 *   -a       start in approximate mode (settings.exact = 0)
 *   -d       start in degree mode
 *   -r       start in real mode (settings.real = 1: real odd roots of negatives)
 *   -T       print the wall-clock time of every evaluation to stderr
 *   -t <ms>  interrupt any evaluation that runs longer than <ms> milliseconds
 *   -m <MB>  arena size in MiB (default 24)
 *   -k <KB>  arena size in KiB instead (stress tests: small arenas)
 *   -s <KB>  C stack limit of the engine (cas_set_stack_limit; default the library's)
 * The exact lines :mem, :approx, :exact, :deg, :rad, :real, :complex, :reset and :latex are
 * REPL commands; every other line goes to cas_eval. */
#include "../include/cas.h"
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static cas_ctx *ctx;
/* SIGALRM handler for -t: cas_interrupt only sets a volatile flag, which is
 * async-signal-safe; the engine notices it at its next poll and cas_eval
 * returns CAS_ERR_INTERRUPTED. */
static void on_alarm(int s) { (void)s; cas_interrupt(ctx); }

/* Parse options, create the context in a malloc'ed arena, then read lines from
 * stdin until EOF: REPL commands are handled here, all other lines are
 * evaluated and printed (result or "ERROR <status>: <message>"). */
static int batch, timing;
static long timeout_ms;
static cas_settings s;
static void *repl_loop(void *arg);

int main(int argc, char **argv) {
    int approx = 0, deg = 0, real = 0;
    size_t arena_mb = 24, arena_kb = 0, stack_kb = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-b")) batch = 1;
        else if (!strcmp(argv[i], "-a")) approx = 1;
        else if (!strcmp(argv[i], "-d")) deg = 1;
        else if (!strcmp(argv[i], "-r")) real = 1;
        else if (!strcmp(argv[i], "-T")) timing = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) timeout_ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) arena_mb = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "-k") && i + 1 < argc) arena_kb = (size_t)atol(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) stack_kb = (size_t)atol(argv[++i]);
    }
    size_t sz = arena_kb ? arena_kb << 10 : arena_mb << 20;
    void *arena = malloc(sz);
    ctx = cas_init(arena, sz);
    if (!ctx) { fprintf(stderr, "cas_init failed\n"); return 1; }
    if (stack_kb) cas_set_stack_limit(ctx, stack_kb << 10);
    cas_get_settings(ctx, &s);
    s.exact = !approx;
    s.angle = deg ? CAS_ANGLE_DEG : CAS_ANGLE_RAD;
    s.real = real;
    cas_set_settings(ctx, &s);
    signal(SIGALRM, on_alarm);
    if (stack_kb) {
        /* -s: the engine runs on a thread whose stack is only the limit plus a margin
         * (512 KB: sanitizer frames are large), so an unchecked deep recursion
         * really overflows instead of passing unnoticed on the 8 MB main stack */
        pthread_attr_t at;
        pthread_t th;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, (stack_kb << 10) + (512u << 10));
        if (pthread_create(&th, &at, repl_loop, NULL)) { fprintf(stderr, "pthread_create failed\n"); return 1; }
        pthread_join(th, NULL);
        return 0;
    }
    repl_loop(NULL);
    return 0;
}

static void *repl_loop(void *arg) {
    (void)arg;
    /* static: 1 MB of output does not belong on the stack */
    static char line[1 << 16];
    static char out[1 << 20];
    for (;;) {
        if (!batch) { printf("> "); fflush(stdout); }
        if (!fgets(line, sizeof line, stdin)) break;
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!batch && n == 0) continue;
        if (!strcmp(line, ":mem")) {
            cas_mem_stats m; cas_mem(ctx, &m);
            printf("arena %zu used %zu peak %zu\n", m.arena_size, m.used, m.peak);
            continue;
        }
        if (!strcmp(line, ":approx")) { s.exact = 0; cas_set_settings(ctx, &s); printf("approx mode\n"); continue; }
        if (!strcmp(line, ":exact")) { s.exact = 1; cas_set_settings(ctx, &s); printf("exact mode\n"); continue; }
        if (!strcmp(line, ":deg")) { s.angle = CAS_ANGLE_DEG; cas_set_settings(ctx, &s); printf("degrees\n"); continue; }
        if (!strcmp(line, ":rad")) { s.angle = CAS_ANGLE_RAD; cas_set_settings(ctx, &s); printf("radians\n"); continue; }
        if (!strcmp(line, ":real")) { s.real = 1; cas_set_settings(ctx, &s); printf("real mode\n"); continue; }
        if (!strcmp(line, ":complex")) { s.real = 0; cas_set_settings(ctx, &s); printf("complex mode\n"); continue; }
        if (!strcmp(line, ":reset")) { cas_reset(ctx); printf("reset\n"); continue; }
        if (!strcmp(line, ":latex")) { cas_latex(ctx, out, sizeof out); printf("%s\n", out); continue; }
        /* one-shot timer (interval 0) armed only around the evaluation */
        if (timeout_ms) {
            struct itimerval it = {{0, 0}, {timeout_ms / 1000, (timeout_ms % 1000) * 1000}};
            setitimer(ITIMER_REAL, &it, NULL);
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        cas_status st = cas_eval(ctx, line, out, sizeof out);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (timeout_ms) {
            struct itimerval it = {{0, 0}, {0, 0}};
            setitimer(ITIMER_REAL, &it, NULL);
        }
        if (st == CAS_OK) printf("%s\n", out);
        else printf("ERROR %d: %s\n", (int)st, cas_last_error(ctx));
        if (timing)
            fprintf(stderr, "[%.3f ms]\n", (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6);
        fflush(stdout);
    }
    return NULL;
}
