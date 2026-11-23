/*
 * usb.c — the USB device: OTG_FS through TinyUSB 0.18.0 (third_party/tinyusb,
 * its synopsys/dwc2 driver). RM0433 section 57 "USB on-the-go full-speed/
 * high-speed (OTG_FS/OTG_HS)"; on the H743 the full-speed port is the OTG2
 * instance USB2_OTG_FS (0x40080000, interrupt OTG_FS_IRQn = 101) with its
 * embedded full-speed PHY on PA11 (DM) / PA12 (DP), alternate function 10
 * (DS12110 table 9). TinyUSB calls this controller root-hub port 0.
 *
 * Composite device (interface association): CDC-ACM, the debug console in
 * parallel with the UART (interfaces 0 and 1), and mass storage (interface 2)
 * with two logical units (core/usbdisk.h: the generated CALC drive and the
 * microSD card when the user connects it). VID:PID in include/usb_ids.h.
 *
 * Clocks and supply: the 48 MHz kernel clock is PLL3Q (RCC_D2CCIP2R.USBSEL =
 * PLL3Q, system.c); the bus clock is RCC_AHB1ENR.USB2OTGFSEN. VDD33USB comes
 * straight from 3V3 (PWR_CR3: USBREGEN = 0, USB33DEN = 1, system.c); start
 * waits for PWR_CR3.USB33RDY (RM0433 "Power control (PWR)", PWR_CR3: the
 * VDD33USB level detector saying the supply is valid).
 *
 * VBUS: the OTG_FS VBUS pin PA9 is the UART's TX here, so the core's own VBUS
 * detection stays off (OTG_GCCFG.VBDEN, bit 21, = 0) and TinyUSB's
 * dcd_init forces the B-session valid (OTG_GOTGCTL.BVALOEN = BVALOVAL = 1).
 * Whether a cable is in comes from the board's VBUS divider on PA2
 * (USB_VBUS_SENSE, hal_usb_present): usb_service() debounces it and starts
 * the device (clock, core reset, soft connect: the D+ pull-up) when VBUS has
 * been present USB_DEBOUNCE_MS, and stops it (soft disconnect, so D+ is never
 * pulled up towards an unpowered port, USB 2.0 section 7.1.5; clock off; PHY
 * powered down by the reset) when it has been gone as long. A charger alone
 * never configures the device, so nothing else changes then.
 *
 * Interrupt: OTG_FS_IRQHandler only lets TinyUSB queue events; every class
 * callback (CDC, MSC: core/usbdisk.c, core/vdrive.c) runs from tud_task(),
 * called by usb_service() in the main loop and by hal_usb_poll() from the CAS
 * poll hook, never from the interrupt. The handler, TinyUSB and everything
 * here are in internal flash (bank 2) and RAM (TinyUSB's statics in DTCM,
 * see the linker script), so a QSPI write never stalls them
 * (docs/QSPI_CONTENT.md 7.2).
 */
#include <stdio.h>
#include <string.h>
#include "console.h"
#include "evlog.h"
#include "hal.h"
#include "keyq.h"
#include "platform.h"
#include "usb_ids.h"
#include "usbdisk.h"
#if HAVE_USB
#include "tusb.h"
#include "device/dcd.h" /* dcd_event_t: the event hook below */
#endif

#if HAVE_USB

#define USB_DEBOUNCE_MS 30u  /* VBUS stable this long before start / stop */
#define USB_IRQ_PRIO 6       /* below the UART (2), SysTick (3), keys (4), touch (5) */
#define CDC_WRITE_WAIT_MS 50 /* console output waits this long for a slow host, then drops */

/* ------------------------------------------------------------ descriptors */
enum { ITF_CDC_COMM, ITF_CDC_DATA, ITF_MSC, ITF_TOTAL };
enum { STR_LANG, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_MSC };
#define EP_CDC_NOTIF 0x81
#define EP_CDC_OUT 0x02
#define EP_CDC_IN 0x82
#define EP_MSC_OUT 0x03
#define EP_MSC_IN 0x83
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)

static const tusb_desc_device_t dev_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    /* interface association: miscellaneous class, common class, IAD protocol */
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

/* Bus powered: the calculator runs from VBUS while plugged in (the battery
 * switch opens, ARCHITECTURE.md section 6); ~150 mA at 5 V computing with the
 * backlight, more with the microSD card writing. */
static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0x00, 250),
    TUD_CDC_DESCRIPTOR(ITF_CDC_COMM, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_MSC_DESCRIPTOR(ITF_MSC, STR_MSC, EP_MSC_OUT, EP_MSC_IN, 64),
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&dev_desc; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return config_desc;
}

/* String descriptors (UTF-16LE). The serial number is the MCU's 96-bit unique
 * ID (RM0433 "Device electronic signature", UID at 0x1FF1E800), so each calculator keeps
 * its own port name on the computer. */
static uint16_t str_buf[1 + 32];
const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    char s[32];
    switch (index) {
    case STR_LANG:
        str_buf[1] = 0x0409; /* English (United States) */
        str_buf[0] = (uint16_t)(TUSB_DESC_STRING << 8 | 4);
        return str_buf;
    case STR_MANUF: snprintf(s, sizeof s, "Open Graphing Calculator"); break;
    case STR_PRODUCT: snprintf(s, sizeof s, "Graphing Calculator"); break;
    case STR_SERIAL: {
        const volatile uint32_t *uid = (const volatile uint32_t *)UID_BASE;
        snprintf(s, sizeof s, "%08lX%08lX%08lX", (unsigned long)uid[0], (unsigned long)uid[1], (unsigned long)uid[2]);
        break;
    }
    case STR_CDC: snprintf(s, sizeof s, "Calculator console"); break;
    case STR_MSC: snprintf(s, sizeof s, "Calculator drives"); break;
    default: return NULL;
    }
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++)
        str_buf[1 + i] = (uint8_t)s[i];
    str_buf[0] = (uint16_t)(TUSB_DESC_STRING << 8 | (2 * n + 2));
    return str_buf;
}

/* ------------------------------------------------------------ state */
#define USB2_OTG_FS_DEVICE ((USB_OTG_DeviceTypeDef *)(USB2_OTG_FS_PERIPH_BASE + USB_OTG_DEVICE_BASE))
static void usb_kv_print(const char *k, const char *v, void *u)
{
    (void)u;
    con_printf("%-13s %s\n", k, v);
}
static bool g_running;         /* the stack is initialised (VBUS present) */
static bool g_vbus;            /* debounced VBUS */
static bool g_vbus_raw;
static uint32_t g_vbus_since;  /* ms the raw level has been stable since */
static bool g_in_task;         /* tud_task() is on the stack (no re-entry) */
static bool g_was_mounted;
static bool g_reconnecting;   /* off the bus until g_reconnect_at (hal_usb_reconnect) */
static uint32_t g_reconnect_at;
static bool g_gave_up;         /* a start failed: no retry until VBUS comes again (or `usb start`) */
static const char *g_fail = ""; /* why the last start failed */

/* Diagnostics (MEM > USB status, console `usb`): counted in the interrupt
 * handler and in the event hook below, read by the main loop. Word-sized
 * counters: a torn read is impossible, a missed increment harmless. */
static volatile struct {
    uint32_t irqs, bus_resets, setups, suspends, resumes, unplugs;
    uint32_t gintsts_seen;      /* every enabled OTG_GINTSTS bit seen in the handler */
    uint8_t last_setup[8];      /* bmRequestType, bRequest, wValue, wIndex, wLength (little-endian) */
} g_st;
static uint32_t g_starts, g_start_fails, g_cdc_dropped, g_cdc_rx, g_cdc_tx;
static uint32_t g_msc_rd, g_msc_wr, g_msc_err;

/* A start that never returned (a hang inside the USB stack, ended by the
 * watchdog) must not repeat at every boot: the marker survives the reset
 * (.noinit), and the next boot keeps USB off and says why. */
#define START_MARK 0x55534253u /* "USBS" */
__attribute__((section(".noinit"))) static volatile uint32_t g_start_mark;
static bool g_mark_checked;

void OTG_FS_IRQHandler(void);
void OTG_FS_IRQHandler(void)
{
    g_st.irqs++;
    g_st.gintsts_seen |= USB2_OTG_FS->GINTSTS & USB2_OTG_FS->GINTMSK;
    tud_int_handler(0);
}

/* Every device event TinyUSB's driver reports passes here first (the link
 * wraps dcd_event_handler: -Wl,--wrap, Makefile; TinyUSB itself is not
 * modified): bus resets, SETUP packets, suspend and resume, for the
 * diagnostics. Interrupt context: counting only. */
void __real_dcd_event_handler(const dcd_event_t *event, bool in_isr);
void __wrap_dcd_event_handler(const dcd_event_t *event, bool in_isr);
void __wrap_dcd_event_handler(const dcd_event_t *event, bool in_isr)
{
    switch (event->event_id) {
    case DCD_EVENT_BUS_RESET: g_st.bus_resets++; break;
    case DCD_EVENT_SUSPEND: g_st.suspends++; break;
    case DCD_EVENT_RESUME: g_st.resumes++; break;
    case DCD_EVENT_UNPLUGGED: g_st.unplugs++; break;
    case DCD_EVENT_SETUP_RECEIVED:
        g_st.setups++;
        memcpy((void *)g_st.last_setup, &event->setup_received, 8);
        break;
    default: break;
    }
    __real_dcd_event_handler(event, in_isr);
}

/* Wait (bounded) until (*reg & mask) == want; false on time-out. Uses the
 * SysTick millisecond counter, so it never hangs even if the USB clock is
 * missing. */
static bool wait_bits(volatile uint32_t *reg, uint32_t mask, uint32_t want, uint32_t ms)
{
    uint32_t t0 = g_ticks_ms;
    while ((*reg & mask) != want)
        if (g_ticks_ms - t0 > ms)
            return false;
    return true;
}

/* Everything TinyUSB's dcd_init waits for, checked first with time limits
 * (its own loops have none): the 48 MHz kernel clock (PLL3 locked, USBSEL =
 * PLL3Q), the VDD33USB detector, the core answering on the AHB bus
 * (OTG_GSNPSID reads "OT" + the core version, RM0433 OTG register map) and a
 * core soft reset completing (OTG_GRSTCTL: AHBIDL, then CSRST self-clears;
 * it needs the PHY clock, so a missing 48 MHz shows up here). NULL when all
 * is well, else the reason. */
static const char *preflight(void)
{
    if (!(RCC->CR & RCC_CR_PLL3RDY))
        return "PLL3 not locked (no 48 MHz)";
    if (((RCC->D2CCIP2R & RCC_D2CCIP2R_USBSEL_Msk) >> RCC_D2CCIP2R_USBSEL_Pos) != 2)
        return "USB kernel clock is not PLL3Q";
    if (!(PWR->CR3 & PWR_CR3_USB33DEN) || !wait_bits(&PWR->CR3, PWR_CR3_USB33RDY, PWR_CR3_USB33RDY, 20))
        return "VDD33USB not ready (PWR_CR3.USB33RDY)";
    if ((USB2_OTG_FS->GSNPSID & 0xFFFF0000u) != 0x4F540000u)
        return "OTG core not answering (GSNPSID)";
    USB2_OTG_FS->GUSBCFG |= USB_OTG_GUSBCFG_PHYSEL; /* the embedded full-speed PHY */
    USB2_OTG_FS->GCCFG |= USB_OTG_GCCFG_PWRDWN;     /* transceiver on */
    if (!wait_bits(&USB2_OTG_FS->GRSTCTL, USB_OTG_GRSTCTL_AHBIDL, USB_OTG_GRSTCTL_AHBIDL, 50))
        return "OTG core: AHB not idle (time-out)";
    USB2_OTG_FS->GRSTCTL |= USB_OTG_GRSTCTL_CSRST;
    if (!wait_bits(&USB2_OTG_FS->GRSTCTL, USB_OTG_GRSTCTL_CSRST, 0, 50))
        return "OTG core soft reset timed out (48 MHz clock?)";
    return NULL;
}

/* Start: pins, clock, a reset of the peripheral, the bounded checks, then
 * TinyUSB (core soft reset, device mode, FIFOs, soft connect). On a failure
 * the clock goes off again and no retry happens until VBUS comes again. */
static bool usb_start(void)
{
    g_starts++;
    gpio_config(P_USB_DM, 10, PULL_NONE);
    gpio_config(P_USB_DP, 10, PULL_NONE);
    gpio_set_speed(P_USB_DM, 3);
    gpio_set_speed(P_USB_DP, 3);
    RCC->AHB1ENR |= RCC_AHB1ENR_USB2OTGFSEN;
    (void)RCC->AHB1ENR;
    RCC->AHB1RSTR |= RCC_AHB1RSTR_USB2OTGFSRST;
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_USB2OTGFSRST;
    /* keep the bus clock in Sleep (WFI between ticks, hal_idle): AHB1LPENR
     * USB2OTGFSLPEN (reset value 1); the ULPI clock is cleared by TinyUSB's
     * dwc2_phy_init (the embedded PHY needs none, and an enabled ULPI clock
     * in Sleep stops the FS core) */
    RCC->AHB1LPENR |= RCC_AHB1LPENR_USB2OTGFSLPEN;
    USB2_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN; /* no VBUS pin: B-valid is forced by dcd_init */
    const char *why = preflight();
    if (!why) {
        NVIC_SetPriority(OTG_FS_IRQn, USB_IRQ_PRIO);
        const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
        g_start_mark = START_MARK;
        bool ok = tud_rhport_init(0, &init);
        g_start_mark = 0;
        if (!ok)
            why = "TinyUSB device init failed";
    }
    if (why) {
        NVIC_DisableIRQ(OTG_FS_IRQn);
        RCC->AHB1RSTR |= RCC_AHB1RSTR_USB2OTGFSRST;
        RCC->AHB1RSTR &= ~RCC_AHB1RSTR_USB2OTGFSRST;
        RCC->AHB1ENR &= ~RCC_AHB1ENR_USB2OTGFSEN;
        gpio_set_analog(P_USB_DM);
        gpio_set_analog(P_USB_DP);
        g_fail = why, g_gave_up = true, g_start_fails++;
        evlog_add("usb: not started: %s", why);
        return false;
    }
    g_running = true;
    g_fail = "";
    tud_cdc_set_wanted_char(3); /* Ctrl-C: tud_cdc_rx_wanted_cb */
    con_log("usb: started (VBUS)");
    return true;
}

/* Stop: soft disconnect and TinyUSB's deinit (interrupt off), then the
 * peripheral reset and its clock off; the pins go back to analog. */
static void usb_stop(void)
{
    if (!g_running)
        return;
    g_reconnecting = false;
    tud_deinit(0);
    NVIC_DisableIRQ(OTG_FS_IRQn);
    RCC->AHB1RSTR |= RCC_AHB1RSTR_USB2OTGFSRST;
    RCC->AHB1RSTR &= ~RCC_AHB1RSTR_USB2OTGFSRST;
    RCC->AHB1ENR &= ~RCC_AHB1ENR_USB2OTGFSEN;
    gpio_set_analog(P_USB_DM);
    gpio_set_analog(P_USB_DP);
    g_running = false;
    usbdisk_host(false);
    g_was_mounted = false;
    con_log("usb: stopped");
}

static void task(void)
{
    if (!g_running || g_in_task)
        return;
    g_in_task = true;
    tud_task();
    bool m = tud_mounted();
    if (m != g_was_mounted) {
        g_was_mounted = m;
        usbdisk_host(m);
        con_log("usb: %s", m ? "configured by a host" : "not configured");
    }
    g_in_task = false;
}

/* Main loop (hal_service): VBUS debounce, start/stop, then the stack. */
void usb_service(void)
{
    uint32_t now = g_ticks_ms;
    if (!g_mark_checked) {
        g_mark_checked = true;
        if (g_start_mark == START_MARK) {
            g_fail = "the last start hung (watchdog): USB off until `usb start`";
            evlog_add("usb: the last start hung (watchdog): USB off");
            g_gave_up = true;
        }
        g_start_mark = 0;
    }
    bool raw = gpio_read(P_USB_VBUS_SENSE) != 0;
    if (raw != g_vbus_raw)
        g_vbus_raw = raw, g_vbus_since = now;
    if (raw != g_vbus && now - g_vbus_since >= USB_DEBOUNCE_MS) {
        g_vbus = raw;
        if (!g_vbus) {
            usb_stop();
            if (g_start_mark == 0 && strncmp(g_fail, "the last", 8))
                g_gave_up = false; /* a new plug-in is a new try */
        }
    }
    if (g_vbus && !g_running && !g_gave_up)
        usb_start();
    if (g_reconnecting && g_running && (int32_t)(now - g_reconnect_at) >= 0) {
        g_reconnecting = false;
        tud_connect(); /* D+ pull-up on: the host enumerates the device again */
    }
    task();
}

void hal_usb_poll(void)
{
    static uint32_t last;
    if (g_running && g_ticks_ms != last) {
        last = g_ticks_ms;
        task();
    }
}

bool hal_usb_configured(void) { return g_running && tud_mounted() && !tud_suspended(); }

void hal_usb_detach(void)
{
    if (!g_running)
        return;
    tud_disconnect(); /* D+ pull-up off: the host sees the device leave */
    hal_delay_ms(20);
    usb_stop();
}

/* A drive's contents changed under a mounted volume: hosts do not poll a
 * mounted drive (macOS sends no TEST UNIT READY), so a SCSI media change
 * alone goes unseen and the host keeps writing through its stale FAT. Leave
 * the bus instead and come back (as DAPLink and UF2 boot loaders do): the
 * host drops the volume and mounts the new one. The serial port goes too. */
bool hal_usb_reconnect(uint32_t ms)
{
    if (!g_running || !tud_mounted())
        return false;
    tud_disconnect();
    g_reconnecting = true;
    g_reconnect_at = g_ticks_ms + ms;
    return true;
}

/* The device state in USB 2.0 chapter 9 terms (9.1.1), as far as it shows. */
static const char *dev_state(void)
{
    if (!g_vbus)
        return "detached (no VBUS)";
    if (!g_running)
        return g_gave_up ? "not started (see error)" : "VBUS, starting";
    if (tud_suspended())
        return "suspended";
    if (tud_mounted())
        return "configured";
    if (USB2_OTG_FS_DEVICE->DCFG & USB_OTG_DCFG_DAD)
        return "addressed";
    return g_st.bus_resets ? "default (reset, address 0)" : "powered (pull-up on, waiting for a host)";
}

void hal_usb_status(void (*kv)(const char *k, const char *v, void *u), void *u)
{
    char b[80];
    snprintf(b, sizeof b, "%s (PA2 now %d)", g_vbus ? "present" : "absent", g_vbus_raw);
    kv("VBUS", b, u);
    kv("Device", dev_state(), u);
    kv("Error", g_fail[0] ? g_fail : "none", u);
    snprintf(b, sizeof b, "PLL3 %s, USBSEL %lu, USB33 %s %s", RCC->CR & RCC_CR_PLL3RDY ? "ok" : "OFF",
             (unsigned long)((RCC->D2CCIP2R & RCC_D2CCIP2R_USBSEL_Msk) >> RCC_D2CCIP2R_USBSEL_Pos),
             PWR->CR3 & PWR_CR3_USB33DEN ? "en" : "off", PWR->CR3 & PWR_CR3_USB33RDY ? "rdy" : "NOT rdy");
    kv("Clocks", b, u);
    snprintf(b, sizeof b, "clock %s, %lu starts, %lu failed", RCC->AHB1ENR & RCC_AHB1ENR_USB2OTGFSEN ? "on" : "off",
             (unsigned long)g_starts, (unsigned long)g_start_fails);
    kv("OTG core", b, u);
    snprintf(b, sizeof b, "%lu, rst %lu, setup %lu, susp %lu/%lu", (unsigned long)g_st.irqs,
             (unsigned long)g_st.bus_resets, (unsigned long)g_st.setups, (unsigned long)g_st.suspends,
             (unsigned long)g_st.resumes);
    kv("Interrupts", b, u);
    const volatile uint8_t *s = g_st.last_setup;
    snprintf(b, sizeof b, "%02X %02X val %04X idx %04X len %u", s[0], s[1], s[2] | s[3] << 8,
             s[4] | s[5] << 8, (unsigned)(s[6] | s[7] << 8));
    kv("Last SETUP", g_st.setups ? b : "none", u);
    snprintf(b, sizeof b, "%08lX", (unsigned long)g_st.gintsts_seen);
    kv("GINTSTS", b, u);
    snprintf(b, sizeof b, "DTR %d, rx %lu tx %lu drop %lu", g_running && tud_cdc_connected(),
             (unsigned long)g_cdc_rx, (unsigned long)g_cdc_tx, (unsigned long)g_cdc_dropped);
    kv("Console", b, u);
    snprintf(b, sizeof b, "rd %lu wr %lu sectors, %lu errors", (unsigned long)g_msc_rd,
             (unsigned long)g_msc_wr, (unsigned long)g_msc_err);
    kv("Drives", b, u);
}

/* console `usb [start]`: the diagnostics; start retries after a failure */
void usb_status(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "start")) {
        g_gave_up = false, g_fail = "";
        return;
    }
    hal_usb_status(usb_kv_print, NULL);
}

/* ------------------------------------------------------------ console over CDC */
/* Ctrl-C (0x03) from the computer: the ON key's break (cas_interrupt, Python's
 * KeyboardInterrupt) at once, also while a computation holds the main loop
 * (tud_task runs from the CAS poll hook too); the console drops the byte. */
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char)
{
    (void)itf, (void)wanted_char;
    keyq_break();
}

/* The line source of core/console.c is shared (hal_stm32.c picks UART or USB
 * per line). Reads are non-blocking. */
int usb_console_read(char *buf, int max)
{
    if (!g_running || !tud_cdc_connected() || !tud_cdc_available())
        return 0;
    int n = (int)tud_cdc_read(buf, (uint32_t)max);
    g_cdc_rx += (uint32_t)n;
    return n;
}

/* Write while a terminal has the port open (DTR): '\n' becomes "\r\n" as on
 * the UART. A full FIFO is drained by running the stack (unless this is
 * already inside it, e.g. a log line from a class callback), for at most
 * CDC_WRITE_WAIT_MS per call; what does not fit then is dropped and counted,
 * so a host that stops reading never stalls the calculator. */
void usb_console_write(const char *s, size_t n)
{
    if (!g_running || !tud_cdc_connected())
        return;
    uint32_t t0 = g_ticks_ms;
    for (size_t i = 0; i < n; i++) {
        char two[2] = {'\r', s[i]};
        const char *p = s[i] == '\n' ? two : two + 1;
        uint32_t k = s[i] == '\n' ? 2 : 1;
        while (tud_cdc_write_available() < k) {
            tud_cdc_write_flush();
            if (g_in_task || g_ticks_ms - t0 > CDC_WRITE_WAIT_MS) {
                g_cdc_dropped += (uint32_t)(n - i);
                return;
            }
            task();
        }
        tud_cdc_write(p, k);
        g_cdc_tx += k;
    }
    tud_cdc_write_flush();
}

/* ------------------------------------------------------------ mass storage */
static void set_sense(uint8_t lun, const usbdisk_sense_t *s) { tud_msc_set_sense(lun, s->key, s->asc, s->ascq); }

uint8_t tud_msc_get_maxlun_cb(void) { return USBDISK_LUNS; }

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    usbdisk_inquiry(lun, (char *)vendor_id, (char *)product_id, (char *)product_rev);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    usbdisk_sense_t s;
    if (usbdisk_ready(lun, &s))
        return true;
    set_sense(lun, &s);
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    *block_count = usbdisk_blocks(lun);
    *block_size = 512;
}

bool tud_msc_is_writable_cb(uint8_t lun) { return usbdisk_writable(lun); }

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    if (load_eject && !start)
        usbdisk_eject(lun);
    return true;
}

/* CFG_TUD_MSC_EP_BUFSIZE is one block: offset is always 0, bufsize 512. */
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    usbdisk_sense_t s;
    if (offset || bufsize % 512 || usbdisk_read(lun, lba, buffer, bufsize / 512, &s)) {
        if (offset || bufsize % 512)
            s = (usbdisk_sense_t){0x05, 0x24, 0}; /* illegal request: invalid field in CDB */
        set_sense(lun, &s);
        g_msc_err++;
        return -1;
    }
    g_msc_rd += bufsize / 512;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    usbdisk_sense_t s;
    if (offset || bufsize % 512 || usbdisk_write(lun, lba, buffer, bufsize / 512, &s)) {
        if (offset || bufsize % 512)
            s = (usbdisk_sense_t){0x05, 0x24, 0};
        set_sense(lun, &s);
        g_msc_err++;
        return -1;
    }
    g_msc_wr += bufsize / 512;
    return (int32_t)bufsize;
}

/* Other SCSI commands (TinyUSB answers INQUIRY, READ CAPACITY, MODE SENSE,
 * REQUEST SENSE, TEST UNIT READY, START STOP, READ/WRITE 10 itself): accept
 * PREVENT ALLOW MEDIUM REMOVAL and SYNCHRONIZE CACHE, refuse the rest. */
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer, (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x1E: /* PREVENT ALLOW MEDIUM REMOVAL */
    case 0x35: /* SYNCHRONIZE CACHE (10) */
        return 0;
    }
    tud_msc_set_sense(lun, 0x05, 0x20, 0x00); /* illegal request: invalid command operation code */
    return -1;
}

#else /* no USB in this image (bring-up, or CONFIG_USB off) */

void usb_service(void) {}
void hal_usb_poll(void) {}
bool hal_usb_configured(void) { return false; }
void hal_usb_detach(void) {}
bool hal_usb_reconnect(uint32_t ms)
{
    (void)ms;
    return false;
}
int usb_console_read(char *buf, int max)
{
    (void)buf, (void)max;
    return 0;
}
void usb_console_write(const char *s, size_t n) { (void)s, (void)n; }
void usb_status(int argc, char **argv)
{
    (void)argc, (void)argv;
    con_printf("no USB in this firmware\n");
}
void hal_usb_status(void (*kv)(const char *k, const char *v, void *u), void *u) { kv("USB", "not in this firmware", u); }

#endif
