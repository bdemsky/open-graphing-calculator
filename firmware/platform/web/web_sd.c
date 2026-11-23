/* Web simulator microSD (hal.h hal_sd_*): a RAM card created by the console
 * (`sd insert MB`, blank until `sd format`); lost when the page is closed.
 * The browser cannot hand the wasm module a disk image, so unlike
 * platform/sim/sim_sd.c there is no image file and no write-failure injection;
 * the size limit is lower (256 MB), since the card lives in the growable wasm
 * heap (48 MB initially, -sALLOW_MEMORY_GROWTH; 32-bit address space). */
#include <stdlib.h>
#include <string.h>
#include "hal.h"
#include "hostdev.h"

static uint8_t *ram;
static uint32_t ram_blocks;

bool hal_sd_present(void) { return ram != NULL; }

int hal_sd_open(uint32_t *nblocks)
{
    if (!ram || hostdev_sd_fail())
        return -1;
    *nblocks = ram_blocks;
    return 0;
}

int hal_sd_read(uint32_t lba, void *buf, uint32_t count)
{
    if (!ram || hostdev_sd_fail() || lba >= ram_blocks || count > ram_blocks - lba)
        return -1;
    memcpy(buf, ram + (size_t)lba * HAL_SD_BLOCK, (size_t)count * HAL_SD_BLOCK);
    return 0;
}

int hal_sd_write(uint32_t lba, const void *buf, uint32_t count)
{
    if (!ram || hostdev_sd_fail() || lba >= ram_blocks || count > ram_blocks - lba)
        return -1;
    memcpy(ram + (size_t)lba * HAL_SD_BLOCK, buf, (size_t)count * HAL_SD_BLOCK);
    return 0;
}

void hal_sd_close(void) {}

/* Replace the card with a zeroed one of `mb` MB (2048 blocks per MB), or
 * remove it for mb = 0. -1 if too large or out of memory. */
int hal_sd_sim_insert(unsigned mb)
{
    free(ram);
    ram = NULL;
    ram_blocks = 0;
    if (!mb)
        return 0;
    if (mb > 256)
        return -1;
    ram_blocks = mb * 2048u;
    ram = calloc(ram_blocks, HAL_SD_BLOCK);
    return ram ? 0 : -1;
}
