/*
 * microSD on SDMMC1: power switch, card detect, identification
 * (CMD0/8/ACMD41/CMD2/CMD3) at 400 kHz, then the block device of hal.h
 * (hal_sd_*): CSD capacity, select, 4-bit bus at 25 MHz, single-block
 * CMD17/CMD24 transfers through the FIFO in polled mode with hardware flow
 * control (no DMA, no interrupts). The file system is core/fat.c.
 *
 * Hardware: SDMMC1 on PC8-PC12 (D0-D3, CK) and PD2 (CMD), AF12; the card is
 * powered from its own load-switched 3V3_SD rail (SD_EN, PG10) and the
 * card-detect switch is on PG7. The SDMMC kernel clock is PLL1Q = 100 MHz
 * (system.c); CK = kernel / (2 x CLKDIV). Protocol references: SD Physical
 * Layer Simplified Specification (command set, R1 card status, CSD layouts)
 * and RM0433 "SDMMC" (CPSM/DPSM, status flags).
 *
 * Because the FIFO is serviced by polling, an interrupt that holds the CPU
 * long enough can overrun/underrun it; HWFC_EN stops CK when the FIFO is
 * full (read) or empty (write), so that shows up as a slower transfer rather
 * than an error. Buffers may be anywhere (no DMA, no alignment needs).
 */
#include <string.h>
#include "hal.h"
#include "platform.h"

static const uint8_t g_sd_pins[] = {P_SD_D0, P_SD_D1, P_SD_D2, P_SD_D3, P_SD_CK, P_SD_CMD};
static bool g_sd_on;

/* Card detect has only the internal pull-up, which leaks ~80 uA through the
 * closed switch while a card sits in the slot. It is on while 3V3_SD is on
 * (the card draws far more) and only for the read in sd_card_present()
 * otherwise; in between the pin is analog. */
static void detect_pullup(bool on)
{
    if (on)
        gpio_config(P_SD_DETECT, PIN_IN, PULL_UP);
    else
        gpio_set_analog(P_SD_DETECT);
}

/* Boot state: card detect and bus pins analog, 3V3_SD off. */
void sd_init(void)
{
    detect_pullup(false);
    for (unsigned i = 0; i < sizeof g_sd_pins; i++) gpio_set_analog(g_sd_pins[i]);
    gpio_write(P_SD_EN, 0);
}

bool sd_card_present(void)
{
    if (!g_sd_on) {
        detect_pullup(true);
        delay_us(5); /* ~40k pull-up into ~10 pF: settled in well under 1 us */
    }
    int v = gpio_read(P_SD_DETECT);
    if (!g_sd_on) detect_pullup(false);
    return SD_DETECT_ACTIVE_LOW ? v == 0 : v == 1;
}

/* Switch the card rail and the SDMMC1 peripheral.
 * On: rail first, 10 ms for it and the card to come up, only then the pins
 * (never drive a card's inputs while it is unpowered), peripheral reset,
 * 400 kHz identification clock, POWER = on, then >= 74 clocks of CK before
 * the first command (SD spec power-up). Off: the reverse, pins analog before
 * SD_EN goes low. Blocking: ~11 ms on power-up. */
void sd_power(bool on)
{
    if (on == g_sd_on) return;
    if (on) {
        detect_pullup(true);
        gpio_write(P_SD_EN, 1);
        delay_us(10000);
        for (unsigned i = 0; i < sizeof g_sd_pins; i++) gpio_config(g_sd_pins[i], 12, PULL_NONE);
        RCC->AHB3ENR |= RCC_AHB3ENR_SDMMC1EN;
        (void)RCC->AHB3ENR;
        RCC->AHB3RSTR |= RCC_AHB3RSTR_SDMMC1RST;
        RCC->AHB3RSTR &= ~RCC_AHB3RSTR_SDMMC1RST;
        /* SDMMC kernel = PLL1Q 100 MHz; CK = 100 MHz / (2*125) = 400 kHz */
        SDMMC1->CLKCR = 125u << SDMMC_CLKCR_CLKDIV_Pos;
        SDMMC1->POWER = 3u << SDMMC_POWER_PWRCTRL_Pos;
        delay_us(1000); /* >= 74 clocks (1 ms at 400 kHz = 400) */
    } else {
        SDMMC1->POWER = 0;
        RCC->AHB3ENR &= ~RCC_AHB3ENR_SDMMC1EN;
        for (unsigned i = 0; i < sizeof g_sd_pins; i++) gpio_set_analog(g_sd_pins[i]);
        gpio_write(P_SD_EN, 0);
        detect_pullup(false);
    }
    g_sd_on = on;
}

/* resp: 0 none, 1 short, 2 short without CRC check (R3), 3 long; extra: CMD flags (CMDTRANS)
 * Send one command through the command path state machine (CPSM) and poll
 * for its end: CMDSENT (no response), CMDREND (response with good CRC),
 * CCRCFAIL (accepted for R3, whose CRC field is all ones), CTIMEOUT (no
 * response within 64 CK cycles). Returns 0, -1 timeout, -2 CRC error, -3
 * software timeout (100 ms). The response is left in RESP1..4. */
static int sd_cmdx(uint32_t idx, uint32_t arg, int resp, uint32_t extra)
{
    SDMMC1->ICR = 0xFFFFFFFFu;
    SDMMC1->ARG = arg;
    uint32_t wr = resp == 0 ? 0u : resp == 3 ? 3u : 1u;
    SDMMC1->CMD = idx | (wr << SDMMC_CMD_WAITRESP_Pos) | SDMMC_CMD_CPSMEN | extra;
    uint32_t t0 = g_ticks_ms;
    for (;;) {
        uint32_t sta = SDMMC1->STA;
        if (resp == 0 && (sta & SDMMC_STA_CMDSENT)) return 0;
        if (sta & SDMMC_STA_CMDREND) return 0;
        if (sta & SDMMC_STA_CCRCFAIL) return resp == 2 ? 0 : -2;
        if (sta & SDMMC_STA_CTIMEOUT) return -1;
        if (g_ticks_ms - t0 > 100) return -3;
    }
}

static int sd_cmd(uint32_t idx, uint32_t arg, int resp) { return sd_cmdx(idx, arg, resp, 0); }

/* Card identification (SD spec "Card Identification Mode"), at 400 kHz:
 *   CMD0  -> idle;
 *   CMD8  0x1AA (2.7-3.6 V, check pattern AA): an echo means a v2.00+ card,
 *         which may be high capacity; no response means v1.x (SDSC);
 *   ACMD41 (CMD55 + CMD41) with the 2.7-3.6 V window (OCR bits 23-15) and
 *         HCS (bit 30) for v2 cards, repeated until the busy bit (OCR bit 31)
 *         says initialisation finished (max 1 s, the spec's limit);
 *   CMD2  -> CID (long response), CMD3 -> the card publishes its RCA.
 * Leaves the card in stand-by state and the rail on. Outputs the OCR (bit 30
 * CCS = SDHC/SDXC block addressing), the CID words and the RCA. Returns 0,
 * -10 no card, -11..-15 the step that failed. Also used by console `sd`. */
int sd_identify(uint32_t *ocr, uint32_t cid[4], uint32_t *rca)
{
    if (!sd_card_present()) return -10;
    sd_power(true);
    sd_cmd(0, 0, 0);                                 /* GO_IDLE */
    bool v2 = sd_cmd(8, 0x1AA, 1) == 0 && (SDMMC1->RESP1 & 0xFFF) == 0x1AA;
    uint32_t t0 = g_ticks_ms;
    uint32_t r = 0;
    do {
        if (sd_cmd(55, 0, 1)) return -11;            /* APP_CMD */
        if (sd_cmd(41, 0x00FF8000u | (v2 ? 0x40000000u : 0), 2)) return -12; /* SD_SEND_OP_COND */
        r = SDMMC1->RESP1;
        iwdg_kick(); /* up to 1 s */
        if (g_ticks_ms - t0 > 1000) return -13;
    } while (!(r & 0x80000000u));
    *ocr = r;
    if (sd_cmd(2, 0, 3)) return -14;                 /* ALL_SEND_CID */
    cid[0] = SDMMC1->RESP1;
    cid[1] = SDMMC1->RESP2;
    cid[2] = SDMMC1->RESP3;
    cid[3] = SDMMC1->RESP4;
    if (sd_cmd(3, 0, 1)) return -15;                 /* SEND_RELATIVE_ADDR */
    *rca = SDMMC1->RESP1 >> 16;
    return 0;
}

/* ------------------------------------------------------------ block device */
/* g_ready: card identified, selected and switched to 4-bit/25 MHz.
 * g_sdhc: block-addressed card (SDHC/SDXC); SDSC cards take byte addresses.
 * g_rca: relative card address; g_blocks: capacity in 512-byte blocks. */
static bool g_ready, g_sdhc;
static uint32_t g_rca, g_blocks;

#define R1_ERRORS 0xFDFFE008u /* error bits of the R1 card status */

/* Command with an R1 response; also fails (-20) if the card status reports
 * any error bit. */
static int sd_r1(uint32_t idx, uint32_t arg, uint32_t extra)
{
    int r = sd_cmdx(idx, arg, 1, extra);
    if (r) return r;
    return (SDMMC1->RESP1 & R1_ERRORS) ? -20 : 0;
}

/* wait until the card is in the transfer state and ready for data (CMD13)
 * Card status: bit 8 READY_FOR_DATA, bits 12:9 CURRENT_STATE (4 = tran).
 * After a write the card sits in "prg" (7) while it programs its flash. */
static int sd_wait_ready(uint32_t ms)
{
    uint32_t t0 = g_ticks_ms;
    do {
        if (sd_cmd(13, g_rca << 16, 1) == 0) {
            uint32_t st = SDMMC1->RESP1;
            if ((st & (1u << 8)) && ((st >> 9) & 0xF) == 4) return 0;
        }
        iwdg_kick();
    } while (g_ticks_ms - t0 < ms);
    return -21;
}

bool hal_sd_present(void) { return sd_card_present(); }

void hal_sd_close(void)
{
    g_ready = false;
    sd_power(false);
}

/* hal.h block device open: identify the card, read its capacity from the
 * CSD, select it (CMD7), switch to a 4-bit bus (ACMD6 arg 2) and 25 MHz
 * (default-speed maximum), and on SDSC cards fix the block length at 512.
 * Cheap if the card is already open and still present. On any failure the
 * card is powered off again. Returns 0 with *nblocks = 512-byte blocks, or -1. */
int hal_sd_open(uint32_t *nblocks)
{
    if (g_ready && sd_card_present()) {
        *nblocks = g_blocks;
        return 0;
    }
    g_ready = false;
    uint32_t ocr, cid[4], rca;
    if (sd_identify(&ocr, cid, &rca)) goto fail;
    g_rca = rca;
    g_sdhc = (ocr & 0x40000000u) != 0;
    if (sd_cmd(9, rca << 16, 3)) goto fail; /* SEND_CSD */
    /* Long response: RESP1 = CSD bits 127:96, RESP2 = 95:64, RESP3 = 63:32.
     * CSD_STRUCTURE (bits 127:126) selects the layout. */
    uint32_t c0 = SDMMC1->RESP1, c1 = SDMMC1->RESP2, c2 = SDMMC1->RESP3;
    if ((c0 >> 30) == 1) { /* CSD 2.0: C_SIZE = bits 69:48; capacity = (C_SIZE + 1) x 512 KB */
        uint32_t csize = ((c1 & 0x3Fu) << 16) | (c2 >> 16);
        g_blocks = (csize + 1) * 1024u;
    } else { /* CSD 1.0: C_SIZE 73:62, C_SIZE_MULT 49:47, READ_BL_LEN 83:80
              * bytes = (C_SIZE + 1) x 2^(C_SIZE_MULT + 2) x 2^READ_BL_LEN; / 512 */
        uint32_t bl = (c1 >> 16) & 0xF, csize = ((c1 & 0x3FFu) << 2) | (c2 >> 30), mult = (c2 >> 15) & 7;
        g_blocks = (csize + 1) << (mult + 2 + bl - 9);
    }
    if (sd_cmd(7, rca << 16, 1) || sd_wait_ready(500)) goto fail; /* SELECT */
    if (sd_r1(55, rca << 16, 0) || sd_r1(6, 2, 0)) goto fail;    /* 4-bit bus */
    /* 100 MHz / (2*2) = 25 MHz, 4-bit, hardware flow control */
    SDMMC1->CLKCR = (2u << SDMMC_CLKCR_CLKDIV_Pos) | SDMMC_CLKCR_WIDBUS_0 | SDMMC_CLKCR_HWFC_EN;
    if (!g_sdhc && sd_r1(16, 512, 0)) goto fail; /* SET_BLOCKLEN */
    g_ready = true;
    *nblocks = g_blocks;
    return 0;
fail:
    hal_sd_close();
    return -1;
}

/* Transfer one 512-byte block (CMD17 READ_SINGLE_BLOCK / CMD24
 * WRITE_BLOCK). The data path (DPSM) is armed first (FIFO reset, timeout,
 * length, block size 2^9, direction) and started by the command itself
 * (CMDTRANS), as the H7 SDMMC requires. The FIFO is moved 8 words at a time
 * on the half-full / half-empty flags; after DATAEND the remaining read words
 * are drained. A write then waits (CMD13) until the card has finished
 * programming. Returns 0 or a negative step code. */
static int sd_xfer(uint32_t lba, uint8_t *buf, bool write)
{
    if (!g_ready || !sd_card_present()) return -1;
    SDMMC1->DCTRL = SDMMC_DCTRL_FIFORST;
    SDMMC1->DCTRL = 0;
    SDMMC1->DTIMER = 25000000u / 4; /* 250 ms, in CK periods at 25 MHz */
    SDMMC1->DLEN = 512;
    SDMMC1->DCTRL = (9u << SDMMC_DCTRL_DBLOCKSIZE_Pos) | (write ? 0 : SDMMC_DCTRL_DTDIR);
    uint32_t addr = g_sdhc ? lba : lba * 512u; /* SDSC: byte address (<= 2 GB, fits) */
    if (sd_r1(write ? 24 : 17, addr, SDMMC_CMD_CMDTRANS)) return -2;
    uint32_t n = 0, t0 = g_ticks_ms, word;
    const uint32_t errs = SDMMC_STA_DCRCFAIL | SDMMC_STA_DTIMEOUT | (write ? SDMMC_STA_TXUNDERR : SDMMC_STA_RXOVERR);
    for (;;) {
        uint32_t sta = SDMMC1->STA;
        if (sta & errs) {
            SDMMC1->ICR = 0xFFFFFFFFu;
            return -3;
        }
        if (write) {
            if ((sta & SDMMC_STA_TXFIFOHE) && n < 512)
                for (int k = 0; k < 8 && n < 512; k++, n += 4) {
                    /* memcpy: buf need not be word aligned */
                    memcpy(&word, buf + n, 4);
                    SDMMC1->FIFO = word;
                }
        } else if (sta & SDMMC_STA_RXFIFOHF) {
            for (int k = 0; k < 8 && n < 512; k++, n += 4) {
                word = SDMMC1->FIFO;
                memcpy(buf + n, &word, 4);
            }
        }
        if (sta & SDMMC_STA_DATAEND) break;
        if (g_ticks_ms - t0 > 500) return -4;
    }
    while (!write && n < 512 && !(SDMMC1->STA & SDMMC_STA_RXFIFOE)) {
        word = SDMMC1->FIFO;
        memcpy(buf + n, &word, 4);
        n += 4;
    }
    SDMMC1->ICR = 0xFFFFFFFFu;
    if (n < 512) return -5;
    return write ? sd_wait_ready(1000) : 0;
}

/* hal.h block read/write: `count` consecutive blocks, one single-block
 * command each (no CMD18/CMD25 multi-block). Stops at the first error and
 * returns -1; 0 on success. */
int hal_sd_read(uint32_t lba, void *buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (sd_xfer(lba + i, (uint8_t *)buf + i * 512u, false)) return -1;
    return 0;
}

int hal_sd_write(uint32_t lba, const void *buf, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        if (sd_xfer(lba + i, (uint8_t *)buf + i * 512u, true)) return -1;
    return 0;
}

/* Simulator-only hook (virtual card insertion); not supported on hardware. */
int hal_sd_sim_insert(unsigned mb) { return -1; }
