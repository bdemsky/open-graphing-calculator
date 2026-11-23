/*
 * fat.h -- small FAT16/FAT32 file layer on the microSD card (core/fat.c),
 * over the platform block device (hal.h: hal_sd_*). Portable C11, no malloc,
 * one file open at a time (plus directory listing between operations).
 *
 * Names: paths separated by '/', components in 8.3 form ("DATA.CSV",
 * matched case-insensitively; long names written by a PC are matched too and
 * shown by fs_list). A new file or directory whose name fits 8.3 gets just
 * that entry ("L1L2.CSV", "CALC/SHOT001.PNG"); any other name (up to 63 ASCII
 * characters: "CALC/NOTEBOOKS/Plan for May.ipynb") gets long-name entries
 * before a short alias ("PLANFO~1.IPY").
 *
 * Robustness: a file written with fs_open(FS_WRITE) is built in newly
 * allocated clusters; only fs_close() publishes it by rewriting its directory
 * entry (one 512-byte sector write) and then frees the clusters of the
 * previous version. Pulling the card or losing power before that leaves the
 * old file intact (at worst a few lost clusters that chkdsk can reclaim);
 * FAT copies are updated together, the FSInfo free count is invalidated.
 *
 * Errors are negative FS_E* codes; fs_strerror() gives a message.
 *
 * Users: the statistics list CSV import/export (app/stat_csv.c) and its
 * console commands (app/stat_console.c). On hardware the block device is the SDMMC
 * driver (platform/stm32h743/sd.c); the simulators use an image file
 * (--sd FILE) or a RAM card.
 */
#ifndef FAT_H
#define FAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FS_OK = 0,
    FS_ENOCARD = -1,   /* no card / card does not answer */
    FS_EIO = -2,       /* read/write error */
    FS_ENOFS = -3,     /* no FAT16/FAT32 file system */
    FS_ENOENT = -4,    /* file or directory not found */
    FS_ENAME = -5,     /* invalid 8.3 name */
    FS_EFULL = -6,     /* card or directory full */
    FS_EBUSY = -7,     /* a file is already open */
    FS_EISDIR = -8,    /* is a directory / not a directory */
    FS_EINVAL = -9,
    FS_EUSB = -10,     /* the card is connected to a computer (fs_lock) */
};

enum { FS_READ = 1, FS_WRITE = 2 /* create or replace */ };

/* Open-file state; owned by the caller, contents private to fat.c. */
typedef struct {
    bool open, writing;
    uint32_t first, cur, size, pos; /* first / current cluster, size, position */
    uint32_t old_first;             /* writing: previous version's chain (freed on close) */
    uint32_t dir_lba;               /* directory entry location (0 = new entry not yet placed) */
    uint16_t dir_off;
    uint32_t parent;                /* parent directory cluster (0 = FAT16 root) */
    char name83[11];
} fs_file_t;

typedef struct {
    char name[64];  /* long name if present, else "NAME.EXT" */
    char short_name[13];
    uint32_t size;
    bool is_dir;
} fs_dirent_t;

int fs_mount(void);      /* power up and mount (also done implicitly) */
void fs_unmount(void);   /* power the card down */
bool fs_mounted(void);
int fs_open(fs_file_t *f, const char *path, int mode);
int fs_read(fs_file_t *f, void *buf, size_t n);         /* bytes read (0 at end) or <0 */
int fs_write(fs_file_t *f, const void *buf, size_t n);  /* n or <0 */
int fs_close(fs_file_t *f);                             /* publishes a written file */
void fs_abort(fs_file_t *f);                            /* drop a file being written */
/* list a directory ("" or "/" = root); the callback returns nonzero to stop */
int fs_list(const char *dir, int (*cb)(const fs_dirent_t *e, void *user), void *user);
int fs_remove(const char *path);
int fs_mkdir(const char *path);  /* ok if it exists */
/* write a fresh FAT16 (< 64 MB) or FAT32 file system over the whole card
 * (no partition table). Destroys all data. Simulators only (RAM cards for
 * tests); FS_EINVAL on the hardware. */
int fs_mkfs(void);
const char *fs_strerror(int err);
/* Give the card to a computer (USB, app/usb_app.c): unmount it and refuse
 * every operation with FS_EUSB until fs_lock(false); the next use mounts it
 * afresh, so nothing cached from before is trusted. */
void fs_lock(bool on);
/* card size in 512-byte blocks and free clusters (slow: scans the FAT) */
int fs_info(uint32_t *blocks, uint32_t *free_kb, int *fat_type);

#endif
