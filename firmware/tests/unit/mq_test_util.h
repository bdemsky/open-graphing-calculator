/* Sparkle Town (modules/mathquest) unit tests: the shared harness -- a bound module with a fixed seed,
 * the console, CHECK. Included by the test files that exercise one part each (test_mq_*.c); the big
 * one, test_mathquest.c, has its own copy. */
#ifndef MQ_TEST_UTIL_H
#define MQ_TEST_UTIL_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../modules/mathquest/mq.h"
#include "gfx.h"
#include "modx.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

extern int mq_module_init(const modx_api_t *api, void *rw, modx_ops_t *ops);
extern const uint32_t mq_module_rw_size;

static mq_t *q;
static modx_ops_t ops;
static uint16_t fb[320 * 240];
/* the api the module is bound to: the host's (its millis the wall clock); a test whose checks hang on the
 * module's clock points this at a copy with a fixed millis, so that time is `mq step`'s only */
static const modx_api_t *mq_test_api = &modx_api;

static void fresh_module(void)
{
    memset(q, 0, mq_module_rw_size);
    memset(&ops, 0, sizeof ops);
    CHECK(mq_module_init(mq_test_api, q, &ops) == 0);
    q->fun_ms = 0;
    q->rng = 0x2545F491u;
    q->scenes_off = true;
}

static int con(const char *line)
{
    char buf[160], *argv[10];
    int argc = 0;
    snprintf(buf, sizeof buf, "mq %s", line);
    for (char *t = strtok(buf, " "); t && argc < 10; t = strtok(NULL, " "))
        argv[argc++] = t;
    return ops.console(q, argc, argv);
}

static void harness_init(void)
{
    gfx_init(fb);
    static uint8_t arena[MODX_POOL + MODX_SAVE_MAX + 16];
    modx_mem_init(arena, true);
    q = calloc(1, mq_module_rw_size);
    fresh_module();
}
#endif
