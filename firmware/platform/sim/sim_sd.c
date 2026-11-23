/* Simulator microSD (hal.h hal_sd_*): a disk image file (--sd FILE, a raw
 * card image such as one made by `dd` from a real card) or a RAM card created
 * by the console (`sd insert MB`, for tests; starts blank: `sd format`).
 *
 * A RAM card, while inserted, takes precedence over the image file. There is no
 * power state: open/close only report the size. Tests simulate pulling the card
 * mid-write with sim_sd_writes_left (sim.h). */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "hal.h"
#include "hostdev.h"
#include "sim.h"

static uint8_t *ram;        /* RAM card (sd insert), NULL if none */
static uint32_t ram_blocks;
static int img_fd = -1;     /* --sd image, opened lazily on first use */
static uint32_t img_blocks; /* image size in whole 512-byte blocks */
static bool img_tried;      /* open attempted once; a missing image stays absent */
long sim_sd_writes_left = -1; /* tests: >= 0 makes writes fail after that many (card pulled) */

static void open_image(void)
{
    if (img_tried || !sim_opt.sd_image)
        return;
    img_tried = true;
    img_fd = open(sim_opt.sd_image, O_RDWR);
    struct stat st;
    if (img_fd >= 0 && fstat(img_fd, &st) == 0)
        img_blocks = (uint32_t)(st.st_size / HAL_SD_BLOCK);
}

bool hal_sd_present(void)
{
    open_image();
    return ram || (img_fd >= 0 && img_blocks);
}

int hal_sd_open(uint32_t *nblocks)
{
    if (!hal_sd_present() || hostdev_sd_fail())
        return -1;
    *nblocks = ram ? ram_blocks : img_blocks;
    return 0;
}

/* 0 if blocks [lba, lba+count) lie on the current card (overflow-safe), else -1. */
static int range(uint32_t lba, uint32_t count)
{
    uint32_t n = ram ? ram_blocks : img_blocks;
    return hal_sd_present() && lba < n && count <= n - lba ? 0 : -1;
}

int hal_sd_read(uint32_t lba, void *buf, uint32_t count)
{
    if (range(lba, count) || hostdev_sd_fail())
        return -1;
    size_t len = (size_t)count * HAL_SD_BLOCK;
    if (ram) {
        memcpy(buf, ram + (size_t)lba * HAL_SD_BLOCK, len);
        return 0;
    }
    return pread(img_fd, buf, len, (off_t)lba * HAL_SD_BLOCK) == (ssize_t)len ? 0 : -1;
}

int hal_sd_write(uint32_t lba, const void *buf, uint32_t count)
{
    if (range(lba, count) || hostdev_sd_fail())
        return -1;
    if (sim_sd_writes_left == 0) /* injected failure: the card "was pulled" */
        return -1;
    if (sim_sd_writes_left > 0)
        sim_sd_writes_left--;
    size_t len = (size_t)count * HAL_SD_BLOCK;
    if (ram) {
        memcpy(ram + (size_t)lba * HAL_SD_BLOCK, buf, len);
        return 0;
    }
    return pwrite(img_fd, buf, len, (off_t)lba * HAL_SD_BLOCK) == (ssize_t)len ? 0 : -1;
}

void hal_sd_close(void) {}

/* Replace the RAM card with a zeroed one of `mb` MB (2048 blocks per MB, at
 * most 512 MB), or remove it for mb = 0. -1 if too large or out of memory. */
int hal_sd_sim_insert(unsigned mb)
{
    free(ram);
    ram = NULL;
    ram_blocks = 0;
    if (!mb)
        return 0;
    if (mb > 512)
        return -1;
    ram_blocks = mb * 2048u;
    ram = calloc(ram_blocks, HAL_SD_BLOCK);
    return ram ? 0 : -1;
}
