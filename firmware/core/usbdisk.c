/* USB mass-storage logical units: the CALC drive and the microSD card. Design in usbdisk.h. */
#include "usbdisk.h"
#include <string.h>
#include "hal.h"

/* SCSI sense keys and codes (SPC-4 section 4.5.6, Annex F) */
#define SK_NOT_READY 0x02
#define SK_MEDIUM_ERROR 0x03
#define SK_ILLEGAL_REQUEST 0x05
#define SK_UNIT_ATTENTION 0x06
#define SK_DATA_PROTECT 0x07
#define ASC_NOT_PRESENT 0x3A     /* medium not present */
#define ASC_MAY_HAVE_CHANGED 0x28 /* not ready to ready change, medium may have changed */
#define ASC_LBA_RANGE 0x21       /* logical block address out of range */
#define ASC_WRITE_PROTECTED 0x27
#define ASC_UNRECOVERED_READ 0x11
#define ASC_WRITE_ERROR 0x0C

typedef struct {
    bool gone;           /* in a media change: "not present" until gone_until */
    uint32_t gone_until;
    bool attention;      /* report one unit attention next */
} unit_t;

static vdrive_t vd;
static bool vd_ready;    /* usbdisk_init ran with memory */
static void (*gen_cb)(vdrive_t *vd);
static unit_t units[USBDISK_LUNS];
static bool host;
static bool card_on, card_ejected;
static uint32_t card_blocks;

static void sense(usbdisk_sense_t *s, uint8_t key, uint8_t asc, uint8_t ascq)
{
    if (s)
        s->key = key, s->asc = asc, s->ascq = ascq;
}

static void regenerate(void)
{
    if (!vd_ready || !gen_cb)
        return;
    gen_cb(&vd);
    vd_build(&vd);
}

void usbdisk_init(uint8_t *mem, uint8_t *stage, uint32_t stage_bytes, void (*generate)(vdrive_t *d))
{
    memset(units, 0, sizeof units);
    gen_cb = generate;
    vd_ready = mem != NULL;
    if (vd_ready) {
        vd_init(&vd, mem, stage, stage_bytes);
        regenerate();
    }
}

vdrive_t *usbdisk_calc(void) { return vd_ready ? &vd : NULL; }

void usbdisk_media_changed(int lun)
{
    if (lun < 0 || lun >= USBDISK_LUNS)
        return;
    if (lun == USBDISK_CALC) {
        regenerate();
        if (hal_usb_reconnect(USBDISK_RECONNECT_MS))
            return; /* the computer mounts the new drive from scratch */
    }
    units[lun].gone = true;
    units[lun].gone_until = hal_millis() + USBDISK_GONE_MS;
    units[lun].attention = false;
}

bool usbdisk_changing(int lun) { return lun >= 0 && lun < USBDISK_LUNS && (units[lun].gone || units[lun].attention); }

/* The end of a media change's "not present" time, by the clock: it must not
 * wait for a TEST UNIT READY, which a host may never send (then every read
 * and write would fail for good). */
static void expire(unit_t *u)
{
    if (u->gone && (int32_t)(hal_millis() - u->gone_until) >= 0) {
        u->gone = false;
        u->attention = true;
    }
}

/* The media change state machine of one unit: false with the sense to report. */
static bool unit_ready(int lun, usbdisk_sense_t *s)
{
    unit_t *u = &units[lun];
    expire(u);
    if (u->gone) {
        sense(s, SK_NOT_READY, ASC_NOT_PRESENT, 0);
        return false;
    }
    if (u->attention) {
        u->attention = false;
        sense(s, SK_UNIT_ATTENTION, ASC_MAY_HAVE_CHANGED, 0);
        return false;
    }
    return true;
}

static bool present(int lun) { return lun == USBDISK_CALC ? vd_ready : card_on && !card_ejected; }

bool usbdisk_ready(int lun, usbdisk_sense_t *s)
{
    if (lun < 0 || lun >= USBDISK_LUNS || !present(lun)) {
        sense(s, SK_NOT_READY, ASC_NOT_PRESENT, 0);
        return false;
    }
    return unit_ready(lun, s);
}

uint32_t usbdisk_blocks(int lun)
{
    if (!present(lun))
        return 0;
    return lun == USBDISK_CALC ? vd_sectors(&vd) : card_blocks;
}

bool usbdisk_writable(int lun) { return lun == USBDISK_CALC ? vd_ready && vd_writable(&vd) : present(lun); }

/* READ10 / WRITE10 check: present, not in a media change, in range. */
static bool io_ok(int lun, uint32_t lba, uint32_t count, usbdisk_sense_t *s)
{
    if (lun >= 0 && lun < USBDISK_LUNS)
        expire(&units[lun]);
    if (lun < 0 || lun >= USBDISK_LUNS || !present(lun) || units[lun].gone) {
        sense(s, SK_NOT_READY, ASC_NOT_PRESENT, 0);
        return false;
    }
    uint32_t n = usbdisk_blocks(lun);
    if (lba >= n || count > n - lba) {
        sense(s, SK_ILLEGAL_REQUEST, ASC_LBA_RANGE, 0);
        return false;
    }
    return true;
}

static uint32_t last_io; /* the host's last read or write of the CALC drive (ms) */
uint32_t usbdisk_last_io(void) { return last_io; }

int usbdisk_read(int lun, uint32_t lba, void *buf, uint32_t count, usbdisk_sense_t *s)
{
    if (!io_ok(lun, lba, count, s))
        return -1;
    if (lun == USBDISK_CALC) {
        last_io = hal_millis();
        for (uint32_t i = 0; i < count; i++)
            vd_read(&vd, lba + i, (uint8_t *)buf + i * VD_SECTOR);
        return 0;
    }
    if (hal_sd_read(lba, buf, count)) {
        sense(s, SK_MEDIUM_ERROR, ASC_UNRECOVERED_READ, 0);
        return -1;
    }
    return 0;
}

int usbdisk_write(int lun, uint32_t lba, const void *buf, uint32_t count, usbdisk_sense_t *s)
{
    if (!io_ok(lun, lba, count, s))
        return -1;
    if (!usbdisk_writable(lun)) {
        sense(s, SK_DATA_PROTECT, ASC_WRITE_PROTECTED, 0);
        return -1;
    }
    if (lun == USBDISK_CALC) {
        uint32_t now = last_io = hal_millis();
        for (uint32_t i = 0; i < count; i++)
            vd_write(&vd, lba + i, (const uint8_t *)buf + i * VD_SECTOR, now);
        return 0;
    }
    if (hal_sd_write(lba, buf, count)) {
        sense(s, SK_MEDIUM_ERROR, ASC_WRITE_ERROR, 0);
        return -1;
    }
    return 0;
}

void usbdisk_eject(int lun)
{
    if (lun == USBDISK_CARD && card_on)
        card_ejected = true; /* the computer is done with the card: the app may take it back */
}

void usbdisk_inquiry(int lun, char vendor[8], char product[16], char rev[4])
{
    memcpy(vendor, "Calc    ", 8);
    memcpy(product, lun == USBDISK_CALC ? "CALC drive      " : "SD card reader  ", 16);
    memcpy(rev, "0.1 ", 4);
}

void usbdisk_host(bool p)
{
    bool was = host;
    host = p; /* (first: the generator asks whether a computer is there) */
    if (p && !was)
        regenerate(); /* a new connection sees a fresh drive */
}

bool usbdisk_host_present(void) { return host; }
uint32_t usbdisk_host_writes(void) { return vd_ready ? vd.writes : 0; }

int usbdisk_card_connect(bool on)
{
    if (on == card_on)
        return 0;
    if (on) {
        if (!hal_sd_present() || hal_sd_open(&card_blocks))
            return -1;
        card_on = true;
        card_ejected = false;
        usbdisk_media_changed(USBDISK_CARD); /* a host that saw "no medium" looks again */
        return 0;
    }
    card_on = false;
    card_ejected = false;
    card_blocks = 0;
    hal_sd_close();
    usbdisk_media_changed(USBDISK_CARD);
    return 0;
}

bool usbdisk_card_connected(void) { return card_on; }
bool usbdisk_card_ejected(void) { return card_ejected; }
