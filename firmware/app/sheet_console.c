/*
 * Console commands of the spreadsheet (one row `sheet` in app/app_console.c),
 * for the UI tests and debugging:
 *   sheet set REF TEXT...      store a cell's input (arguments joined by spaces;
 *                              no TEXT deletes the cell)
 *   sheet show REF             text, raw result, value and displayed text
 *   sheet expect REF RESULT    the raw CAS result must equal RESULT
 *   sheet input REF TEXT       the cell's input must equal TEXT
 *   sheet value REF NUMBER     the numeric value within 1e-9 (relative)
 *   sheet fill REF ROW         fill down to ROW;  sheet copy SRC DST
 *   sheet link COL [LIST]      column -> list;   sheet fromlist COL LIST
 *   sheet export FILE [f]      CSV values (f: formulas);  sheet import FILE
 *   sheet select REF | clear | recalc
 * Failed expectations count in console_expect_failures.
 */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "console.h"
#include "sheet.h"

/* REF argument -> col/row; false (error printed) if it is not a cell name. */
static bool ref_arg(const char *s, int *c, int *r)
{
    int n = sheet_parse_ref(s, c, r);
    if (!n || s[n] || *c >= SH_COLS || *r >= SH_ROWS) {
        con_error("bad cell '%s'", s);
        return false;
    }
    return true;
}

int sheet_console(int argc, char **argv)
{
    if (argc < 2)
        return con_error("usage: sheet set|show|expect|value|fill|copy|link|fromlist|export|import|select|clear|recalc");
    const char *sub = argv[1];
    int c, r, c2, r2;
    char msg[96];
    if (!strcmp(sub, "clear")) {
        sheet_clear_all();
        return 0;
    }
    if (!strcmp(sub, "recalc")) {
        sheet_recalc_all();
        return 0;
    }
    if (!strcmp(sub, "export") || !strcmp(sub, "import")) {
        if (argc < 3)
            return con_error("usage: sheet %s FILE", sub);
        int rc = sub[0] == 'e' ? sheet_csv_export(argv[2], argc > 3 && argv[3][0] == 'f', msg, sizeof msg)
                               : sheet_csv_import(argv[2], msg, sizeof msg);
        con_printf("%s\n", msg);
        return rc ? con_error("%s failed", sub) : 0;
    }
    if (!strcmp(sub, "link") || !strcmp(sub, "fromlist")) {
        if (argc < 3 || argv[2][0] < 'A' || argv[2][0] > 'Z')
            return con_error("usage: sheet %s COL [LIST]", sub);
        int col = argv[2][0] - 'A';
        int rc = sub[0] == 'l' ? sheet_link(col, argc > 3 ? argv[3] : "") : sheet_from_list(col, argc > 3 ? argv[3] : "");
        return rc ? con_error("%s failed", sub) : 0;
    }
    if (argc < 3 || !ref_arg(argv[2], &c, &r))
        return argc < 3 ? con_error("usage: sheet %s REF ...", sub) : -1;
    sheet_ensure();
    const scell_t *cell = sheet_get(c, r);
    if (!strcmp(sub, "set")) {
        char t[SH_TEXT] = "";
        for (int i = 3; i < argc; i++) {
            if (i > 3)
                strncat(t, " ", sizeof t - strlen(t) - 1);
            strncat(t, argv[i], sizeof t - strlen(t) - 1);
        }
        return sheet_set(c, r, t) ? con_error("sheet full") : 0;
    }
    if (!strcmp(sub, "select")) {
        sheet_select(c, r);
        return 0;
    }
    if (!strcmp(sub, "show")) {
        char d[64];
        sheet_display(cell, 20, d, sizeof d);
        con_printf("text=%s\nres=%s\nval=%.15g\ndisp=%s\n", cell ? cell->text : "", cell ? cell->res : "",
                   cell ? cell->val : (double)NAN, d);
        return 0;
    }
    if (!strcmp(sub, "expect") || !strcmp(sub, "input")) {
        const char *want = argc > 3 ? argv[3] : "";
        const char *got = cell ? (sub[0] == 'e' ? cell->res : cell->text) : "";
        if (strcmp(got, want)) {
            console_expect_failures++;
            return con_error("%s = '%s', expected '%s'", argv[2], got, want);
        }
        return 0;
    }
    if (!strcmp(sub, "value")) {
        double want = argc > 3 ? strtod(argv[3], NULL) : (double)NAN, got = cell ? cell->val : (double)NAN;
        if (!(fabs(got - want) <= 1e-9 * fmax(1, fabs(want)))) {
            console_expect_failures++;
            return con_error("%s value %.15g, expected %s", argv[2], got, argc > 3 ? argv[3] : "?");
        }
        return 0;
    }
    if (!strcmp(sub, "fill")) {
        long to = argc > 3 ? strtol(argv[3], NULL, 10) : 0;
        return sheet_fill_down(c, r, (int)to - 1) ? con_error("fill failed") : 0;
    }
    if (!strcmp(sub, "copy")) {
        if (argc < 4 || !ref_arg(argv[3], &c2, &r2))
            return con_error("usage: sheet copy SRC DST");
        return sheet_copy(c, r, c2, r2) ? con_error("copy failed") : 0;
    }
    return con_error("unknown sheet command '%s'", sub);
}
