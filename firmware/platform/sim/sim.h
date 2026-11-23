/* Simulator-internal interface shared by sim_main.c (command line),
 * sim_hal.c (hal.h on POSIX) and sim_sd.c (microSD). Not used by portable code. */
#ifndef SIM_H
#define SIM_H
#include <stdbool.h>

/* Command-line options, filled by main() before hal_init(). */

typedef struct {
    int input_fd;          /* console input (stdin or --script file) */
    bool virtual_time;     /* deterministic clock (non-interactive runs) */
    bool realtime;         /* --realtime: force the wall clock */
    const char *shot_dir;  /* base directory for relative screenshot paths */
    const char *live_png;  /* --live FILE: keep a PNG of the panel updated */
    const char *flash_file;/* --flash FILE: persistent QSPI image */
    const char *sd_image;  /* --sd FILE: microSD card image (sim_sd.c) */
} sim_options_t;

extern sim_options_t sim_opt;
/* Write what the simulated panel shows (black while the display is off) as a
 * PNG. 0 = ok. */
int sim_write_lcd_png(const char *path);
/* microSD (sim_sd.c): tests set >= 0 to make writes fail after that many */
extern long sim_sd_writes_left;

#endif
