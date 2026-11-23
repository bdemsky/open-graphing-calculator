/*
 * fuzz.c -- random-input robustness test.  Inputs are generated from a
 * token grammar (all builtin names, operators, brackets, numbers) with random
 * mutations and raw bytes.  Each evaluation runs under a timer that calls
 * cas_interrupt.  Built with AddressSanitizer/UBSan by `make fuzz`.
 * Also checks that successful exact outputs re-parse.  FUZZ_DUMP=1 prints the
 * generated inputs instead (the fuzz corpus of `make stress`).
 */
#include "cas_int.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static cas_ctx *ctx;
static void on_alarm(int s) { (void)s; cas_interrupt(ctx); }

static const char *names[] = {
#define X_NAME(id, name) name,
    BUILTIN_SYMS(X_NAME)
#undef X_NAME
};
static const char *ops[] = {"+", "-", "*", "/", "^", "=", ":=", "<", ">", "<=", "!", ",", "(", ")", "[", "]", ";", "'", "|", "=>"};
static const char *atoms[] = {"x", "y", "z", "a", "n", "k", "t", "0", "1", "2", "-1", "1/2", "0.5", "1e300", "pi", "e", "i", "inf", "3.75", "12345678901234567890", "f", "g"};

static unsigned rs = 1;
static unsigned rnd(void) { rs = rs * 1103515245u + 12345u; return (rs >> 16) & 0x7fff; }

static void gen_expr(char *b, int *n, int cap, int depth) {
    if (*n > cap - 64) return;
    int r = rnd() % 10;
    if (depth > 6 || r < 3) {
        const char *a = atoms[rnd() % (sizeof atoms / sizeof atoms[0])];
        *n += snprintf(b + *n, (size_t)(cap - *n), "%s", a);
        return;
    }
    if (r < 6) {
        gen_expr(b, n, cap, depth + 1);
        const char *o = ops[rnd() % 5];
        *n += snprintf(b + *n, (size_t)(cap - *n), "%s", o);
        gen_expr(b, n, cap, depth + 1);
        return;
    }
    if (r < 9) {
        const char *f = names[S_E + rnd() % (S__COUNT - 1)];
        if (!strcmp(f, "_cmd") || !strcmp(f, "restart") || !strcmp(f, "purge")) f = "sin";
        if (!(f[0] >= 'a' && f[0] <= 'z')) f = "cos";
        *n += snprintf(b + *n, (size_t)(cap - *n), "%s(", f);
        int na = rnd() % 4;
        for (int i = 0; i < na; i++) {
            if (i) b[(*n)++] = ',';
            gen_expr(b, n, cap, depth + 1);
        }
        b[(*n)++] = ')';
        b[*n] = 0;
        return;
    }
    b[(*n)++] = '[';
    int na = 1 + rnd() % 3;
    for (int i = 0; i < na; i++) {
        if (i) b[(*n)++] = ',';
        gen_expr(b, n, cap, depth + 1);
    }
    b[(*n)++] = ']';
    b[*n] = 0;
}

static void gen_math(char *b, int *n, int cap, int depth) {
    static const char *fa[] = {"x", "x", "x", "2", "3", "1/2", "-1", "pi", "y", "x^2", "e"};
    static const char *fn[] = {"sin", "cos", "tan", "exp", "ln", "sqrt", "atan", "asin", "sinh", "abs"};
    if (*n > cap - 64) return;
    int r = rnd() % 10;
    if (depth > 4 || r < 3) {
        *n += snprintf(b + *n, (size_t)(cap - *n), "%s", fa[rnd() % 11]);
        return;
    }
    if (r < 7) {
        b[(*n)++] = '(';
        gen_math(b, n, cap, depth + 1);
        const char *o = ops[rnd() % 5];
        if (o[0] == '^') {
            *n += snprintf(b + *n, (size_t)(cap - *n), ")^%d", (int)(rnd() % 4) - 1);
            return;
        }
        *n += snprintf(b + *n, (size_t)(cap - *n), "%s", o);
        gen_math(b, n, cap, depth + 1);
        b[(*n)++] = ')';
        b[*n] = 0;
        return;
    }
    *n += snprintf(b + *n, (size_t)(cap - *n), "%s(", fn[rnd() % 10]);
    gen_math(b, n, cap, depth + 1);
    b[(*n)++] = ')';
    b[*n] = 0;
}

int main(int argc, char **argv) {
    long iters = argc > 1 ? atol(argv[1]) : 10000;
    if (argc > 2) rs = (unsigned)atol(argv[2]);
    int dump = getenv("FUZZ_DUMP") != NULL; /* print the generated inputs instead of running them */
    size_t sz = 8u << 20;
    void *arena = malloc(sz);
    ctx = cas_init(arena, sz);
    signal(SIGALRM, on_alarm);
    static char in[4096], out[1 << 16], out2[1 << 16];
    long ok = 0, err = 0, reparse_fail = 0, intr = 0;
    for (long it = 0; it < iters; it++) {
        int n = 0;
        int mode = rnd() % 10;
        static const char *tmpl[] = {"integrate(%s,x)", "diff(%s,x)", "limit(%s,x,0)", "limit(%s,x,inf)",
                                     "series(%s,x,0,4)", "solve(%s=0,x)", "factor(%s)", "simplify(%s)",
                                     "expand(%s)", "integrate(%s,x,0,1)", "sum(%s,x,1,n)", "partfrac(%s,x)"};
        if (mode >= 7) {
            char tmp[1100];
            int m = 0;
            gen_math(tmp, &m, 1000, 0);
            tmp[m] = 0;
            snprintf(in, sizeof in, tmpl[rnd() % (sizeof tmpl / sizeof tmpl[0])], tmp);
        } else if (mode == 0) {
            int len = rnd() % 64;
            for (int i = 0; i < len; i++) in[i] = (char)(1 + rnd() % 255);
            in[len] = 0;
        } else {
            gen_expr(in, &n, 1024, 0);
            in[n] = 0;
            if (mode == 1 && n > 2) { /* mutate */
                int k = rnd() % 3;
                for (int j = 0; j < k; j++) in[rnd() % n] = "()[],+-*/^=!x1 "[rnd() % 15];
            }
        }
        if (dump) { /* the corpus for `make stress` (one input per line) */
            for (char *c = in; *c; c++)
                if (*c == '\n' || *c == '\r') *c = ' ';
            puts(in);
            continue;
        }
        struct itimerval tv = {{0, 0}, {0, 300000}};
        setitimer(ITIMER_REAL, &tv, NULL);
        cas_status st = cas_eval(ctx, in, out, sizeof out);
        struct itimerval z = {{0, 0}, {0, 0}};
        setitimer(ITIMER_REAL, &z, NULL);
        if (st == CAS_OK) {
            ok++;
            if (!strchr(out, '.') && strlen(out) < 2000) {
                setitimer(ITIMER_REAL, &tv, NULL);
                cas_status st2 = cas_eval(ctx, out, out2, sizeof out2);
                setitimer(ITIMER_REAL, &z, NULL);
                if (st2 == CAS_ERR_SYNTAX) {
                    reparse_fail++;
                    if (reparse_fail < 20) printf("REPARSE FAIL: %s\n   -> %s\n", in, out);
                }
            }
        } else if (st == CAS_ERR_INTERRUPTED) {
            intr++;
            if (getenv("SHOWINTR")) printf("INTERRUPTED: %s\n", in);
        }
        else
            err++;
        if (it % 50000 == 0 && it) printf("%ld iterations...\n", it);
    }
    cas_mem_stats m;
    cas_mem(ctx, &m);
    printf("fuzz: %ld runs, %ld ok, %ld errors, %ld interrupted, %ld reparse failures, peak mem %zu\n", iters, ok, err,
           intr, reparse_fail, m.peak);
    return reparse_fail ? 1 : 0;
}
