/*
 * Console commands of the statistics app (rows in app/app_console.c):
 *   stat lists | list NAME | set NAME [v1,v2,...] | expect NAME v1,v2,... |
 *        formula NAME EXPR | last | import FILE | export FILE L1,L2
 *   fs insert MB | eject | format | info | ls [DIR] | cat FILE | put FILE TEXT |
 *      rm FILE | mkdir DIR | expect FILE SUBSTRING
 *
 * Used by the UI test scripts (tests/ui) and for debugging over the console
 * (simulators and hardware). `stat set` / `formula` create a named list
 * when it does not exist; `expect` compares with a relative tolerance of
 * 1e-9 and counts a failure in console_expect_failures. `fs insert` / `eject`
 * attach a RAM card in the simulators only (hal_sd_sim_insert). Every `fs`
 * subcommand unmounts the card afterwards, like the app does. Buffers:
 * values and file contents go through st_scratch (the statistics scratch
 * area), so these commands must not run while a statistics computation is
 * using it (they run from the console, between key events).
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os") /* flash budget: size over speed (not time-critical) */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "fat.h"
#include "hal.h"
#include "stat.h"

/* "NAME=v1,v2,..." with 15 significant digits */
static void print_list(const slist_t *l)
{
    con_printf("%s=", l->name);
    for (int i = 0; i < l->len; i++)
        con_printf("%s%.15g", i ? "," : "", l->v[i]);
    con_printf("\n");
}

/* parse "1,2,3.5" (commas and/or spaces) into v; returns the count (input
 * beyond `max` values is ignored), or -1 on a token that is not a number
 * ("nan" and "inf" are accepted by strtod) */
static int parse_values(const char *s, double *v, int max)
{
    int n = 0;
    while (*s && n < max) {
        char *end;
        double d = strtod(s, &end);
        if (end == s)
            return -1;
        v[n++] = d;
        s = end;
        while (*s == ',' || *s == ' ')
            s++;
    }
    return n;
}

/* a equals the expected b within 1e-9 relative (absolute below |b| = 1);
 * NaN matches NaN (empty cells) */
static bool close_enough(double a, double b)
{
    return a == b || fabs(a - b) <= 1e-9 * fmax(1, fabs(b)) || (isnan(a) && isnan(b));
}

/* `stat` console command (see the file header). Returns 0, or the result of
 * con_error() on errors and failed expectations. */
int stat_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "lists";
    if (!strcmp(sub, "lists")) {
        for (int i = 0; i < SL_MAX; i++)
            if (sl_at(i))
                con_printf("%s\t%d%s%s\n", sl_at(i)->name, sl_at(i)->len, sl_at(i)->formula[0] ? "\t=" : "",
                           sl_at(i)->formula);
        return 0;
    }
    if (!strcmp(sub, "last")) {
        con_printf("title=%s\n", stat_last.title ? stat_last.title : "");
        for (int i = 0; i < stat_last.n; i++)
            con_printf("%s=%.15g\n", stat_last.lab[i], stat_last.val[i]);
        if (stat_last.eq[0])
            con_printf("RegEQ=%s\n", stat_last.eq);
        return 0;
    }
    if (argc < 3)
        return con_error("usage: stat lists|last|list N|set N V,..|expect N V,..|formula N E|import F|export F L,..");
    if (!strcmp(sub, "import") || !strcmp(sub, "export")) {
        char msg[96];
        int r;
        if (sub[0] == 'i') {
            r = stat_csv_import(argv[2], msg, sizeof msg);
        } else {
            int slots[SL_MAX], n = sl_parse(argc > 3 ? argv[3] : "L1", slots, SL_MAX, msg, sizeof msg);
            if (n < 0)
                return con_error("%s", msg);
            r = n ? stat_csv_export(argv[2], slots, n, msg, sizeof msg) : (snprintf(msg, sizeof msg, "no lists"), -1);
        }
        con_printf("%s\n", msg);
        return r ? con_error("%s", msg) : 0;
    }
    slist_t *l = !strcmp(sub, "set") || !strcmp(sub, "formula") ? sl_create(argv[2]) : sl_find(argv[2]);
    if (!l)
        return con_error("no list '%s'", argv[2]);
    if (!strcmp(sub, "list")) {
        print_list(l);
        return 0;
    }
    if (!strcmp(sub, "set")) {
        int n = argc > 3 ? parse_values(argv[3], st_scratch, st_scratch_cap) : 0;
        if (n < 0)
            return con_error("bad values");
        l->formula[0] = 0;
        sl_set(l, st_scratch, n);
        return 0;
    }
    if (!strcmp(sub, "formula")) {
        char err[64];
        const char *e = argc > 3 ? argv[3] : "";
        if (*e && sl_eval_into(l, e, err, sizeof err))
            return con_error("%s", err);
        snprintf(l->formula, sizeof l->formula, "%s", e);
        sl_changed(l);
        return 0;
    }
    if (!strcmp(sub, "expect")) {
        /* at most 64 expected values: longer lists cannot be checked */
        double want[64];
        int n = argc > 3 ? parse_values(argv[3], want, 64) : 0;
        bool ok = n == l->len;
        for (int i = 0; ok && i < n; i++)
            ok = close_enough(l->v[i], want[i]);
        if (!ok) {
            console_expect_failures++;
            print_list(l);
            return con_error("list %s differs", l->name);
        }
        return 0;
    }
    return con_error("unknown subcommand '%s'", sub);
}

/* `fs ls` line: name (with "/" for directories), size, 8.3 short name */
static int ls_cb(const fs_dirent_t *e, void *user)
{
    con_printf("%s%s\t%lu\t%s\n", e->name, e->is_dir ? "/" : "", (unsigned long)e->size, e->short_name);
    return 0;
}

/* `fs` console command (microSD card). `cat` / `expect` read the file with
 * one fs_read() into st_scratch (3 x 999 doubles, about 24 KB with the
 * normal arena), so longer files are cut; `put` joins its arguments with spaces and turns the two
 * characters "\n" into a line break. */
int sd_console(int argc, char **argv)
{
    const char *sub = argc > 1 ? argv[1] : "info";
    int r = 0;
    if (!strcmp(sub, "insert")) {
        long mb = argc > 2 ? atol(argv[2]) : 8;
        fs_unmount();
        if (hal_sd_sim_insert((unsigned)mb))
            return con_error("cannot insert a %ld MB RAM card (simulators only)", mb);
        return 0;
    }
    if (!strcmp(sub, "eject")) {
        fs_unmount();
        return hal_sd_sim_insert(0) ? con_error("simulators only") : 0;
    }
    if (!strcmp(sub, "format")) {
        r = fs_mkfs();
    } else if (!strcmp(sub, "info")) {
        uint32_t blocks, free_kb;
        int type;
        if (!(r = fs_info(&blocks, &free_kb, &type)))
            con_printf("fat=%d\nblocks=%lu\nfree_kb=%lu\n", type, (unsigned long)blocks, (unsigned long)free_kb);
    } else if (!strcmp(sub, "ls")) {
        r = fs_list(argc > 2 ? argv[2] : "/", ls_cb, NULL);
    } else if (argc < 3) {
        return con_error("usage: fs insert MB|eject|format|info|ls [D]|cat F|put F TEXT|rm F|mkdir D|expect F S");
    } else if (!strcmp(sub, "cat") || !strcmp(sub, "expect")) {
        fs_file_t f;
        char *buf = (char *)st_scratch;
        int n = 0;
        if (!(r = fs_open(&f, argv[2], FS_READ))) {
            n = fs_read(&f, buf, (size_t)st_scratch_cap * sizeof(double) - 1);
            fs_close(&f);
            if (n < 0)
                r = n;
        }
        if (!r) {
            buf[n] = 0;
            if (sub[0] == 'c') {
                con_write(buf, (size_t)n);
                if (n && buf[n - 1] != '\n')
                    con_printf("\n");
            } else if (!strstr(buf, argc > 3 ? argv[3] : "")) {
                console_expect_failures++;
                fs_unmount();
                return con_error("%s does not contain '%s'", argv[2], argv[3]);
            }
        }
    } else if (!strcmp(sub, "put")) {
        fs_file_t f;
        if (!(r = fs_open(&f, argv[2], FS_WRITE))) {
            for (int a = 3; a < argc && r >= 0; a++) {
                /* "\n" in the text is a line break */
                for (const char *s = argv[a]; *s && r >= 0; s++) {
                    char c = *s;
                    if (c == '\\' && s[1] == 'n')
                        c = '\n', s++;
                    r = fs_write(&f, &c, 1);
                }
                if (a + 1 < argc && r >= 0)
                    r = fs_write(&f, " ", 1);
            }
            r = r < 0 ? r : fs_close(&f);
        }
    } else if (!strcmp(sub, "rm")) {
        r = fs_remove(argv[2]);
    } else if (!strcmp(sub, "mkdir")) {
        r = fs_mkdir(argv[2]);
    } else {
        return con_error("unknown subcommand '%s'", sub);
    }
    fs_unmount();
    return r < 0 ? con_error("%s", fs_strerror(r)) : 0;
}
