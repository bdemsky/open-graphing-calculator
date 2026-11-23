/* Simulator entry point. The debug console is on stdin/stdout (or --script).
 *
 *   calcsim [--script FILE] [--shots DIR] [--live FILE.png] [--flash FILE] [--sd FILE] [--realtime]
 *
 * Exit status: 0, or 1 if any `expect` command failed.
 *
 *   --script FILE   read console commands from FILE instead of stdin
 *   --shots DIR     base directory for relative `screenshot` paths
 *   --live FILE.png keep FILE.png updated with the panel (at most ~10x/s), for
 *                   watching an interactive session in an image viewer
 *   --flash FILE    back the 16 MB "QSPI NOR" with FILE (persists sessions)
 *   --sd FILE       raw microSD card image (sim_sd.c)
 *   --no-content    start with an empty QSPI content region (no manual
 *                   beyond the built-in essentials; docs/QSPI_CONTENT.md)
 *   --realtime      use the wall clock even when input is not a terminal
 *
 * When input is a script or a pipe, time is virtual (deterministic tests; see
 * hal_idle in sim_hal.c) unless --realtime is given.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "calc.h"
#include "hal.h"
#include "hostdev.h"
#include "sim.h"

static void usage(void)
{
    fprintf(stderr,
            "usage: calcsim [--script FILE] [--shots DIR] [--live FILE.png] [--flash FILE] [--sd FILE] [--no-content] [--realtime]\n"
            "  Console commands are read from stdin (or FILE); try 'help'.\n");
    exit(2);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--script") && v) {
            sim_opt.input_fd = open(v, O_RDONLY);
            if (sim_opt.input_fd < 0) {
                perror(v);
                return 2;
            }
            i++;
        } else if (!strcmp(a, "--shots") && v) {
            sim_opt.shot_dir = v;
            i++;
        } else if (!strcmp(a, "--live") && v) {
            sim_opt.live_png = v;
            i++;
        } else if (!strcmp(a, "--flash") && v) {
            sim_opt.flash_file = v;
            i++;
        } else if (!strcmp(a, "--sd") && v) { /* microSD card image */
            sim_opt.sd_image = v;
            i++;
        } else if (!strcmp(a, "--no-content")) {
            hostdev_no_content = true;
        } else if (!strcmp(a, "--realtime")) {
            sim_opt.realtime = true;
        } else {
            usage();
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0); /* line-buffered even into a pipe: readers see each console line as it is printed */
    hal_init();
    return calc_main();
}
