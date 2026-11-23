/*
 * Debug console: a line-oriented text protocol over USART1 (hardware) or
 * stdin/stdout (simulator), designed to be driven by a program/agent.
 * See README.md "Console protocol".
 *
 * Every command produces zero or more output lines followed by exactly one
 * terminator line: "OK" or "ERR <message>". Asynchronous log lines start "# ".
 *
 * Structure: console.c owns line assembly, tokenizing, dispatch and the core
 * commands (keys, screen dump, touch injection, power, wait, ...). Commands are
 * looked up in three NULL-terminated tables in order: core, app
 * (app/app_console.c) and platform (hal_console_cmds, one per platform), so a
 * name in an earlier table shadows a later one.
 *
 * The main loop (core/calc.c) calls console_poll() only when the key and
 * touch queues are empty, so events injected by one command are fully
 * processed by the app before the next command is read. Main context only.
 *
 * Output goes through hal_console_write(), which blocks; on hardware that is
 * USART1 at 115200 baud (~11.5 KB/s), so large dumps (`screen`) take seconds.
 */
#ifndef CONSOLE_H
#define CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One command. Tables end with an all-zero entry. */
typedef struct console_cmd {
    const char *name;
    const char *usage; /* argument synopsis, may be "" */
    const char *help;  /* one line */
    /* argv[0] is the command name; arguments are already unquoted.
     * Return 0 for OK, else call con_error() (returns -1) for ERR. Returning
     * 1 means the command printed its own terminator or will print it later
     * (`wait`, `reset`), and console_exec prints nothing. */
    int (*fn)(int argc, char **argv);
} console_cmd;

/* Reset the line buffer and turn echo off. */
void console_init(void);
/* true while a `wait` command is pending (the main loop must keep running). */
bool console_waiting(void);
/* Read available input bytes (non-blocking) until one complete line has been
 * executed or input runs dry; process at most one command per call. While a
 * `wait` is pending, reads nothing and prints its OK once the time is up.
 * Sets console_quit when the input reports end-of-file (simulator stdin).
 * Returns true if a command ran (or an over-long line was rejected). */
bool console_poll(void);
/* Run one command line directly (used by tests and the simulator) and print
 * its terminator. Blank lines and lines starting with '#' are ignored (no
 * output). The line is copied and truncated to the line-buffer size (383
 * bytes). Returns 0 on OK (or deferred terminator), -1 on ERR. */
int console_exec(const char *line);

void con_write(const char *s, size_t n);
void con_puts(const char *s); /* writes s and "\n" */
/* Formatted output; one call produces at most 255 bytes (longer output is
 * truncated). No newline is added. */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Log line: "# ..." (newline added). Clients skip these, so they may be
 * printed at any time, even between a command's data lines. */
void con_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Set the error message for the current command (max 159 chars); returns -1
 * so a command can `return con_error(...)`. console_exec prints it as
 * "ERR <message>". If called more than once, the last message wins. */
int con_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Base64-encode and print binary data in lines of 76 chars (57 input bytes
 * per line, standard alphabet, '=' padding on the last line only). */
void con_base64(const void *data, size_t n);

/* Parse helpers: return true on success (the whole string must be consumed).
 * Decimal, or hex with a 0x prefix (a leading 0 is still decimal: "010" = 10).
 * Out-of-range values (and a sign for con_parse_u32) are rejected. */
bool con_parse_u32(const char *s, unsigned long *out); /* decimal or 0x hex */
bool con_parse_int(const char *s, long *out);

/* Keys held with the console `press` (hal_keypad_raw() bit layout), and the
 * raw-state bit of a key code: the safe-mode check at boot (core/safemode.h)
 * sees console-held keys like physically held ones. */
uint64_t console_held_keys(void);
uint64_t console_key_raw_mask(int key);
/* true if the Kconfig bool option CONFIG_<name> is enabled in this build. */
bool build_has_config(const char *name);

/* Number of failed `expect` commands so far (simulator exit status). */
extern int console_expect_failures;
/* Set when the console asked the main loop to exit (sim `quit` or EOF). */
extern bool console_quit;

/* Command tables: core (console.c), app (app/app_console.c), platform (hal). */
extern const console_cmd app_console_cmds[];
extern const console_cmd hal_console_cmds[];

#endif
