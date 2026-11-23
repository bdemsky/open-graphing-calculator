/* Code modules (app/modx.c): the API table and the registration / binding
 * path, over the simulator's emulated QSPI module store (the same singleton
 * the app uses). Test modules are built here: a .MOD whose payload is a
 * modx descriptor (interface "modx", kind code-xip), and a host entry point
 * registered with modx_host_register (the simulators' stand-in for the
 * image's entry address).
 *   - the API table: ABI, size, entries present;
 *   - not installed / switched off (`modx off`) / incompatible store ABI /
 *     wrong descriptor (magic, ABI, name, kind) / init refusing / RW larger
 *     than the pool: no binding, and the sentence the user sees;
 *   - a good module: init runs once with the API table and a zeroed RW
 *     block, the trampolines reach its ops and count it busy, the store
 *     changing unbinds it and the next use binds it again with fresh state.
 * Built and run by `make test`. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modstore.h"
#include "modx.h"
#include "rle.h"
#include "session.h"

static int fails, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if (!(c)) {                                                               \
            fails++;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while (0)

static const mod_iface_t ifs[] = {{"help", 1}, {MODX_IFACE, MODX_ABI}};

/* A module file: header + 0xFF up to the payload + the descriptor (+ pad). */
static uint8_t *make_code_mod(const char *name, int store_abi, uint16_t kind, const modx_desc_t *d, size_t *size)
{
    uint32_t len = 256;
    *size = MOD_PAYLOAD_OFF + len;
    uint8_t *m = calloc(1, *size);
    memset(m, 0xFF, MOD_PAYLOAD_OFF);
    memcpy(m + MOD_PAYLOAD_OFF, d, sizeof *d);
    mod_hdr_t h;
    memset(&h, 0, sizeof h);
    h.magic = MOD_MAGIC, h.fmt = MOD_FMT, h.hdr_bytes = MOD_HDR_BYTES;
    strncpy(h.name, name, sizeof h.name);
    h.ver[1] = 1;
    h.kind = kind;
    strncpy(h.iface, MODX_IFACE, sizeof h.iface);
    h.abi_min = h.abi_max = (uint16_t)store_abi;
    h.tier = MOD_TIER_ADVANCED;
    h.payload_len = len;
    h.payload_crc = crc32_update(0, m + MOD_PAYLOAD_OFF, len);
    h.hcrc = crc32_update(0, &h, offsetof(mod_hdr_t, hcrc));
    memcpy(m, &h, sizeof h);
    return m;
}

static int install(const uint8_t *m, size_t size, bool force)
{
    static mod_inst_t in;
    int r = mod_inst_begin(&in, mod_store(MODSTORE_QSPI), ifs, 2, force);
    if (!r)
        r = mod_inst_write(&in, m, size);
    return r ? r : mod_inst_finish(&in);
}

static modx_desc_t desc(const char *name, uint32_t rw)
{
    modx_desc_t d;
    memset(&d, 0, sizeof d);
    d.magic = MODX_MAGIC, d.abi = MODX_ABI, d.desc_size = sizeof d;
    strncpy(d.name, name, sizeof d.name);
    d.rw_size = rw, d.init = 1, d.image_size = sizeof d, d.api_size = sizeof(modx_api_t);
    return d;
}

/* ------------------------------------------------------------ a test module */
typedef struct {
    const modx_api_t *api;
    int ticks, events, last_ev;
    bool busy_seen;
    uint8_t rest[200];
} tm_rw_t;
static int inits, init_result;
static bool init_saw_zero, init_busy;

static void tm_tick(void *rw, uint32_t now, int scr)
{
    tm_rw_t *t = rw;
    t->ticks++;
    t->busy_seen = modx_busy();
    (void)now, (void)scr;
}
static int tm_event(void *rw, int ev, int arg)
{
    tm_rw_t *t = rw;
    t->events++, t->last_ev = ev;
    if (ev == 99) /* the write rule: no session save (QSPI write) while module code runs */
        return session_save(true);
    return ev * 10 + arg;
}
static void tm_state(void *rw, void (*kv)(const char *, const char *, void *), void *user)
{
    char b[16];
    snprintf(b, sizeof b, "%d", ((tm_rw_t *)rw)->ticks);
    kv("tm_ticks", b, user);
}
static int tm_init(const modx_api_t *api, void *rw, modx_ops_t *ops)
{
    inits++;
    init_busy = modx_busy();
    tm_rw_t *t = rw;
    init_saw_zero = true;
    for (size_t i = 0; i < sizeof *t; i++)
        if (((uint8_t *)rw)[i])
            init_saw_zero = false;
    if (init_result < 0)
        return init_result;
    t->api = api;
    ops->size = sizeof *ops;
    ops->tick = tm_tick;
    ops->event = tm_event;
    ops->state = tm_state;
    return 0;
}

static char seen_ticks[16], seen_sfx[1024];
static void kv_cb(const char *k, const char *v, void *u)
{
    (void)u;
    if (!strcmp(k, "tm_ticks"))
        snprintf(seen_ticks, sizeof seen_ticks, "%s", v);
    if (!strcmp(k, "modx_sfx"))
        snprintf(seen_sfx, sizeof seen_sfx, "%s", v);
}

/* the sound log in `state` holds every effect fired (a toy box mashed through all of them overran a 128-byte
 * buffer: UBSan "index 129 out of bounds for type char[128]") */
static void test_sfx_log(void)
{
    for (int i = 1; i < MODX_SFX_COUNT; i++)
        modx_api.sfx(i, 0);
    modx_status(kv_cb, NULL);
    CHECK(strlen(seen_sfx) > 128);
    CHECK(strstr(seen_sfx, modx_sfx_name(1)) && strstr(seen_sfx, modx_sfx_name(MODX_SFX_COUNT - 1)));
}

static uint8_t pool_mem[MODX_POOL + MODX_SAVE_MAX + 64]; /* the pool, then the saved bytes */

static void test_api(void)
{
    CHECK(modx_api.abi == MODX_ABI && modx_api.size == sizeof(modx_api_t));
    CHECK(modx_api.millis && modx_api.snprintf && modx_api.fill_rect && modx_api.text && modx_api.framebuffer);
    CHECK(modx_api.cas_compile_vars && modx_api.cas_fn_eval_vars && modx_api.cas_fn_free && modx_api.cas);
    CHECK(modx_api.anim_target && modx_api.anim_apply && modx_api.anim_finish);
    CHECK(modx_api.file_create && modx_api.file_write && modx_api.file_close && modx_api.font_small);
    CHECK(!strcmp(modx_title("animation"), "Animation"));
    char b[16];
    modx_api.snprintf(b, sizeof b, "%d-%s", 7, "x");
    CHECK(!strcmp(b, "7-x"));
}

static void test_binding(void)
{
    mod_boot(ifs, 2, false);
    CHECK(modstore_format(mod_store(MODSTORE_QSPI)) == 0);
    mod_remount();
    CHECK(modx_mem_init(pool_mem, true) >= MODX_POOL);
    modx_host_register("testmod", tm_init, sizeof(tm_rw_t));
    modx_host_register("badinit", tm_init, sizeof(tm_rw_t));
    modx_host_register("huge", tm_init, MODX_POOL + 1);

    /* not installed */
    CHECK(modx_get("testmod") == NULL);
    CHECK(strstr(modx_why("testmod"), "Install the testmod module (Memory > Modules)") != NULL);
    CHECK(inits == 0);

    /* installed: binds on first use */
    size_t sz;
    modx_desc_t d = desc("testmod", sizeof(tm_rw_t));
    uint8_t *m = make_code_mod("testmod", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    modx_unbind_all();
    mod_remount();
    CHECK(mod_state("testmod") == MOD_OK);
    modx_mod_t *x = modx_get("testmod");
    CHECK(x != NULL);
    CHECK(inits == 1 && init_saw_zero && init_busy);
    CHECK(!modx_busy());
    CHECK(modx_get("testmod") == x && inits == 1); /* bound once */
    CHECK(modx_peek("testmod") == x && modx_peek("animation") == NULL);
    modx_tick(x, 5, 1);
    modx_tick(x, 6, 1);
    CHECK(modx_event(x, 3, 4) == 34);
    CHECK(modx_event(x, 99, 0) == -2); /* deferred */
    modx_state(x, kv_cb, NULL);
    CHECK(!strcmp(seen_ticks, "2"));
    CHECK(!modx_draw(x, 1, (int[4]){0}));   /* no draw op: nothing */

    /* a store change unbinds; the next use binds again with fresh state */
    modx_unbind_all();
    x = modx_get("testmod");
    CHECK(x != NULL && inits == 2 && init_saw_zero);
    modx_state(x, kv_cb, NULL);
    CHECK(!strcmp(seen_ticks, "0"));

    /* the test switch: not installed, then back */
    char *off[] = {"modx", "off", "testmod"}, *on[] = {"modx", "on", "testmod"};
    CHECK(modx_console(3, off) == 0);
    CHECK(modx_get("testmod") == NULL && strstr(modx_why("testmod"), "Install the testmod") != NULL);
    CHECK(modx_console(3, on) == 0);
    CHECK(modx_get("testmod") != NULL && inits == 3);

    /* removed from the store */
    CHECK(modstore_remove(mod_store(MODSTORE_QSPI), "testmod") == 0);
    modx_unbind_all();
    mod_remount();
    CHECK(modx_get("testmod") == NULL);
}

static void test_refusals(void)
{
    size_t sz;
    uint8_t *m;
    /* store ABI of another firmware: the store disables it */
    modx_desc_t d = desc("oldabi", 64);
    m = make_code_mod("oldabi", MODX_ABI + 1, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, true) == 0);
    free(m);
    /* descriptor problems: magic, ABI, name, kind */
    d = desc("badmagic", 64);
    d.magic = 0x12345678;
    m = make_code_mod("badmagic", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    d = desc("badabi", 64);
    d.abi = MODX_ABI + 7;
    m = make_code_mod("badabi", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    d = desc("othername", 64);
    m = make_code_mod("badname", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    d = desc("datakind", 64);
    m = make_code_mod("datakind", MODX_ABI, MOD_KIND_DATA, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    /* init refuses; RW too large for the pool */
    d = desc("badinit", 64);
    m = make_code_mod("badinit", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    d = desc("huge", 64);
    m = make_code_mod("huge", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    modx_unbind_all();
    mod_remount();

    CHECK(mod_state("oldabi") == MOD_ABI);
    CHECK(modx_get("oldabi") == NULL && strstr(modx_why("oldabi"), "another firmware") != NULL);
    CHECK(modx_get("badmagic") == NULL && strstr(modx_why("badmagic"), "another firmware") != NULL);
    CHECK(modx_get("badabi") == NULL);
    CHECK(modx_get("badname") == NULL);
    CHECK(modx_get("datakind") == NULL);
    int before = inits;
    init_result = -3;
    CHECK(modx_get("badinit") == NULL && strstr(modx_why("badinit"), "refused to start") != NULL);
    CHECK(inits == before + 1);
    init_result = 0;
    CHECK(modx_get("huge") == NULL && strstr(modx_why("huge"), "SDRAM") != NULL);
    CHECK(inits == before + 1); /* never called */
    /* a module the core has no host entry for (and no XIP in the simulators) */
    d = desc("nohost", 64);
    m = make_code_mod("nohost", MODX_ABI, MOD_KIND_CODE_XIP, &d, &sz);
    CHECK(install(m, sz, false) == 0);
    free(m);
    modx_unbind_all();
    mod_remount();
    CHECK(modx_get("nohost") == NULL);
    /* safe mode: nothing binds */
    mod_boot(ifs, 2, true);
    modx_unbind_all();
    CHECK(modx_get("badinit") == NULL && strstr(modx_why("badinit"), "safe mode") != NULL);
    mod_boot(ifs, 2, false);
    CHECK(modstore_format(mod_store(MODSTORE_QSPI)) == 0);
    mod_remount();
    modx_unbind_all();
}

/* Saved bytes (blob "modx.save"): a full save (MODX_SAVE_MAX) goes through the blob's save and
 * load intact; one byte more is refused; a version-1 blob (u16 length, cores up to 16 KB) still
 * loads; another module's name cannot take the slot. */
extern const persist_def_t persist_modx_save;
static void test_save(void)
{
    CHECK(modx_mem_init(pool_mem, true) >= MODX_POOL + MODX_SAVE_MAX);
    CHECK(modx_api.size >= offsetof(modx_api_t, save_size) + sizeof(void *) && modx_api.save_size);
    CHECK(modx_api.save_max() == MODX_SAVE_MAX && MODX_SAVE_MAX >= (96u << 10));
    static uint8_t full[MODX_SAVE_MAX + 1], back[MODX_SAVE_MAX + 1];
    for (size_t i = 0; i < sizeof full; i++)
        full[i] = (uint8_t)(i * 131u + (i >> 9));
    CHECK(modx_api.save_put("bigsave", full, MODX_SAVE_MAX + 1) == -1);
    CHECK(modx_api.save_put("bigsave", full, MODX_SAVE_MAX) == 0);
    CHECK(modx_api.save_size("bigsave") == MODX_SAVE_MAX && modx_api.save_size("other") == 0);
    CHECK(modx_api.save_put("other", full, 10) == -1); /* one module at a time */
    /* the blob itself (version 2: u32 length) */
    static uint8_t blob[MODX_SAVE_MAX + 64];
    pw_t w = {blob, sizeof blob, 0, false};
    persist_modx_save.save(&w);
    CHECK(!w.overflow && w.len == 2 + 7 + 4 + MODX_SAVE_MAX && persist_modx_save.version == 2);
    persist_modx_save.reset();
    CHECK(modx_api.save_size("bigsave") == 0);
    pr_t r = {blob, w.len, 0, false};
    persist_modx_save.load(&r, 2);
    memset(back, 0, sizeof back);
    CHECK(modx_api.save_get("bigsave", back, sizeof back) == MODX_SAVE_MAX && !memcmp(back, full, MODX_SAVE_MAX));
    /* (it fits a session payload with room to spare for everything else) */
    CHECK(w.len + 32 < SESSION_MAX / 2);
    /* version 1 (u16 length) */
    w = (pw_t){blob, sizeof blob, 0, false};
    pw_str(&w, "oldmod");
    pw_u16(&w, 300);
    pw_bytes(&w, full, 300);
    r = (pr_t){blob, w.len, 0, false};
    persist_modx_save.load(&r, 1);
    CHECK(modx_api.save_size("oldmod") == 300 && modx_api.save_get("oldmod", back, sizeof back) == 300 &&
          !memcmp(back, full, 300));
    /* a damaged length (more than is there): nothing kept */
    w = (pw_t){blob, sizeof blob, 0, false};
    pw_str(&w, "oldmod");
    pw_u32(&w, 5000);
    pw_bytes(&w, full, 100);
    r = (pr_t){blob, w.len, 0, false};
    persist_modx_save.load(&r, 2);
    CHECK(modx_api.save_size("oldmod") == 0);
    persist_modx_save.reset();
}

int main(void)
{
    test_api();
    test_binding();
    test_refusals();
    test_save();
    test_sfx_log();
    printf("test_modx: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
