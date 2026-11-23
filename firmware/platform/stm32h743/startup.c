/*
 * Startup for the STM32H743: vector table, Reset_Handler, fault handlers.
 *
 * Written in C (no assembly startup file). The vector table g_vectors is
 * placed in .isr_vector at the start of flash bank 1 (stm32h743zg.ld), so the
 * core fetches the initial MSP and Reset_Handler from it at reset; VTOR is
 * set to it again explicitly in Reset_Handler.
 *
 * Reset_Handler order: optional jump to the ROM bootloader, FPU enable,
 * copy/zero the RAM sections (ITCM code, DTCM data/bss, AXI .data/.bss), VTOR,
 * paint the stack, C++/constructor init, main(). Clocks are still HSI 64 MHz
 * at this point; hal_init() (called from main) raises them.
 *
 * Fault handling: HardFault/MemManage/BusFault/UsageFault go through a naked
 * stub that passes the stacked exception frame to fault_report(), which saves
 * a crash record in .noinit (survives the following reset; console `crash`
 * prints it), prints it on the UART with polled I/O, notes the cause
 * (g_reset_note, so the next boot reports a crash rather than a software
 * reset) and resets. Unhandled interrupts and an NMI that is not the clock
 * security system land in loops that report and hang; the watchdog (iwdg.c,
 * ~2 s) then resets the MCU, and like a fault that counts as a failed boot
 * for safe mode (core/safemode.h). Before the watchdog is started (early boot,
 * or option bytes not set) recovery is a manual reset.
 */
#include <stdio.h>
#include <string.h>
#include "platform.h"

extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;
extern uint32_t _siitcm, _sitcm, _eitcm;
extern uint32_t _sd2_bss, _ed2_bss;
extern uint32_t _sidtcm_data, _sdtcm_data, _edtcm_data, _sdtcm_bss, _edtcm_bss;
extern void __libc_init_array(void);
extern int main(void);

/* Survive reset: bootloader request + crash record. .noinit (DTCM) is neither
 * copied nor zeroed by Reset_Handler, so values written before a software
 * reset are still there afterwards; after power-on they are random, hence the
 * magic numbers (BOOT_MAGIC_DFU, CRASH_MAGIC). */
__attribute__((section(".noinit"))) volatile uint32_t g_boot_magic;
__attribute__((section(".noinit"))) crash_record_t g_crash;
__attribute__((section(".noinit"))) volatile uint32_t g_reset_note; /* RESET_NOTE_FAULT: power.c */

#define SYSTEM_BOOTLOADER_ADDR 0x1FF09800u /* AN2606, STM32H74x/75x */

void Reset_Handler(void);
void Default_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SysTick_Handler(void);
void USART1_IRQHandler(void);
void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);
void RTC_WKUP_IRQHandler(void);
void OTG_FS_IRQHandler(void);

/* Weak defaults so drivers can override. */
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void SysTick_Handler(void) WEAK_DEFAULT;
void USART1_IRQHandler(void) WEAK_DEFAULT;
void EXTI0_IRQHandler(void) WEAK_DEFAULT;
void EXTI1_IRQHandler(void) WEAK_DEFAULT;
void EXTI2_IRQHandler(void) WEAK_DEFAULT;
void EXTI3_IRQHandler(void) WEAK_DEFAULT;
void EXTI4_IRQHandler(void) WEAK_DEFAULT;
void EXTI9_5_IRQHandler(void) WEAK_DEFAULT;
void EXTI15_10_IRQHandler(void) WEAK_DEFAULT;
void RTC_WKUP_IRQHandler(void) WEAK_DEFAULT;
void OTG_FS_IRQHandler(void) WEAK_DEFAULT; /* usb.c */

/* 16 Cortex-M7 system exceptions + 150 STM32H743 interrupt lines (RM0433
 * NVIC vector table). Every entry defaults to Default_Handler; the designated
 * initializers below override the ones in use (hence -Woverride-init off).
 * Entry 0 is the initial main stack pointer, not a handler. */
#define NVEC (16 + 150)
typedef void (*vector_t)(void);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverride-init"
__attribute__((section(".isr_vector"), used)) const vector_t g_vectors[NVEC] = {
    [0 ... NVEC - 1] = Default_Handler,
    [0] = (vector_t)(uintptr_t)&_estack,
    [1] = Reset_Handler,
    [2] = NMI_Handler,
    [3] = HardFault_Handler,
    [4] = MemManage_Handler,
    [5] = BusFault_Handler,
    [6] = UsageFault_Handler,
    [7 ... 10] = 0,
    [11] = Default_Handler, /* SVC */
    [12] = Default_Handler, /* DebugMon */
    [13] = 0,
    [14] = Default_Handler, /* PendSV */
    [15] = SysTick_Handler,
    [16 + USART1_IRQn] = USART1_IRQHandler,
    [16 + EXTI0_IRQn] = EXTI0_IRQHandler,
    [16 + EXTI1_IRQn] = EXTI1_IRQHandler,
    [16 + EXTI2_IRQn] = EXTI2_IRQHandler,
    [16 + EXTI3_IRQn] = EXTI3_IRQHandler,
    [16 + EXTI4_IRQn] = EXTI4_IRQHandler,
    [16 + EXTI9_5_IRQn] = EXTI9_5_IRQHandler,
    [16 + EXTI15_10_IRQn] = EXTI15_10_IRQHandler,
    [16 + RTC_WKUP_IRQn] = RTC_WKUP_IRQHandler,
    [16 + OTG_FS_IRQn] = OTG_FS_IRQHandler,
};
#pragma GCC diagnostic pop

/* Section copy/clear helpers. Word-wise: the linker script aligns every
 * section start and end to 4 bytes. They must not call memcpy/memset, which
 * may themselves live in a section that has not been copied yet. */
static void copy_words(uint32_t *dst, const uint32_t *src, const uint32_t *end)
{
    while (dst < end) *dst++ = *src++;
}

static void zero_words(uint32_t *dst, const uint32_t *end)
{
    while (dst < end) *dst++ = 0;
}

/* Load MSP = sp and branch to pc (AAPCS: sp in r0, pc in r1). Naked so the
 * compiler emits no prologue that would touch the old stack after the MSP
 * switch. Used to enter the ROM bootloader with its own initial stack. */
__attribute__((noreturn, naked)) static void jump_to(uint32_t sp, uint32_t pc)
{
    (void)sp;
    (void)pc;
    __asm volatile("msr msp, r0\n"
                   "bx r1\n");
}

__attribute__((noreturn)) void Reset_Handler(void)
{
    /* Bootloader request from hal_enter_bootloader(): we are fresh out of
     * reset (HSI, no peripherals), which is what the ROM bootloader expects. */
    if (g_boot_magic == BOOT_MAGIC_DFU) {
        g_boot_magic = 0; /* one-shot: the next reset boots the firmware again */
        /* The ROM's own vector table: word 0 = its initial SP, word 1 = its reset entry. */
        const volatile uint32_t *bl = (const volatile uint32_t *)SYSTEM_BOOTLOADER_ADDR;
        SCB->VTOR = SYSTEM_BOOTLOADER_ADDR;
        jump_to(bl[0], bl[1]);
    }

    /* FPU: full access for CP10/CP11 */
    SCB->CPACR |= (0xFu << 20);
    __DSB();
    __ISB();

    /* Load-address (_si*, in flash) -> run-address copies, then .bss clears.
     * ITCM first: functions placed in .itcm must not be called before this. */
    copy_words(&_sitcm, &_siitcm, &_eitcm);
    copy_words(&_sdtcm_data, &_sidtcm_data, &_edtcm_data);
    zero_words(&_sdtcm_bss, &_edtcm_bss);
    copy_words(&_sdata, &_sidata, &_edata);
    zero_words(&_sbss, &_ebss);
    /* statics in D2 SRAM (stm32h743zg.ld .d2_bss): its clocks are off at reset (system.c turns
     * them on again later, harmlessly) */
    RCC->AHB2ENR |= RCC_AHB2ENR_SRAM1EN | RCC_AHB2ENR_SRAM2EN | RCC_AHB2ENR_SRAM3EN;
    (void)RCC->AHB2ENR;
    __DSB();
    zero_words(&_sd2_bss, &_ed2_bss);

    SCB->VTOR = (uint32_t)(uintptr_t)g_vectors;
    stack_paint();
    __libc_init_array(); /* newlib: run .preinit_array/.init_array constructors */
    main();
    for (;;) NVIC_SystemReset();
}

/* Any vector without a driver handler. uart_write_raw_polled works from any
 * context: it busy-waits on the USART and even sets it up if it is not yet
 * enabled. */
void Default_Handler(void)
{
    uart_write_raw_polled("\r\n# FAULT: unhandled interrupt\r\n");
    for (;;) {
    }
}

void NMI_Handler(void)
{
    /* HSE clock security system: the crystal stopped. Recover on HSI and go on. */
    if (clock_css_nmi()) {
        uart_write_raw_polled("\r\n# clock: HSE FAILED at runtime (CSS), running from HSI\r\n");
        return;
    }
    /* Otherwise most likely a flash ECC double error. */
    uart_write_raw_polled("\r\n# FAULT: NMI\r\n");
    for (;;) {
    }
}

/* Called from the naked fault stubs with the stacked frame.
 *   frame      : the hardware-stacked exception frame (r0-r3, r12, lr, pc,
 *                xPSR, in that order) on whichever stack was active;
 *   exc_return : the EXC_RETURN value from LR on entry;
 *   kind       : 0 HardFault, 1 MemManage, 2 BusFault, 3 UsageFault.
 * Records the frame and the fault status registers (CFSR, HFSR, MMFAR, BFAR;
 * Armv7-M ARM "System Control Block") in g_crash, prints them, then either
 * breaks into an attached debugger or resets after a short delay so the UART
 * can drain. Never returns. The stack may be corrupt, so the 256-byte buffer
 * here is the only stack use beyond snprintf. */
__attribute__((used, noreturn)) void fault_report(uint32_t *frame, uint32_t exc_return, uint32_t kind)
{
    static const char *const names[] = {"HardFault", "MemManage", "BusFault", "UsageFault"};
    if (g_crash.magic != CRASH_MAGIC) g_crash.count = 0; /* first crash since power-on: count is garbage */
    g_crash.magic = CRASH_MAGIC;
    g_crash.r0 = frame[0];
    g_crash.r1 = frame[1];
    g_crash.r2 = frame[2];
    g_crash.r3 = frame[3];
    g_crash.r12 = frame[4];
    g_crash.lr = frame[5];
    g_crash.pc = frame[6];
    g_crash.xpsr = frame[7];
    g_crash.cfsr = SCB->CFSR;
    g_crash.hfsr = SCB->HFSR;
    g_crash.mmfar = SCB->MMFAR;
    g_crash.bfar = SCB->BFAR;
    g_crash.exc_return = exc_return;
    g_crash.sp = (uint32_t)frame + 32u; /* basic frame; an FP frame adds 72 bytes */
    g_crash.stack_used = stack_used();
    g_crash.count++;

    char buf[256];
    snprintf(buf, sizeof buf,
             "\r\n# FAULT: %s pc=%08lx lr=%08lx psr=%08lx cfsr=%08lx hfsr=%08lx mmfar=%08lx bfar=%08lx\r\n"
             "# r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\r\n",
             names[kind & 3], (unsigned long)g_crash.pc, (unsigned long)g_crash.lr,
             (unsigned long)g_crash.xpsr, (unsigned long)g_crash.cfsr, (unsigned long)g_crash.hfsr,
             (unsigned long)g_crash.mmfar, (unsigned long)g_crash.bfar, (unsigned long)g_crash.r0,
             (unsigned long)g_crash.r1, (unsigned long)g_crash.r2, (unsigned long)g_crash.r3,
             (unsigned long)g_crash.r12);
    uart_write_raw_polled(buf);

    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __BKPT(0); /* debugger attached: stop here */
    }
    uart_write_raw_polled("# resetting (crash record kept; console `crash` shows it)\r\n");
    for (volatile uint32_t i = 0; i < 20000000u; i++) { /* ~0.15 s: well inside the watchdog's 2 s */
    }
    g_reset_note = RESET_NOTE_FAULT;
    __DSB();
    NVIC_SystemReset();
    for (;;) {
    }
}

/* Naked fault entry: EXC_RETURN bit 2 (tested via LR) says which stack the
 * exception frame was pushed on (0 = MSP, 1 = PSP). Pass that stack pointer,
 * EXC_RETURN and the fault kind to fault_report() in r0-r2 and tail-branch to
 * it, without pushing anything on a possibly overflowed stack. */
#define FAULT_STUB(name, kind)                                   \
    __attribute__((naked)) void name(void)                       \
    {                                                            \
        __asm volatile("tst lr, #4\n"                            \
                       "ite eq\n"                                \
                       "mrseq r0, msp\n"                         \
                       "mrsne r0, psp\n"                         \
                       "mov r1, lr\n"                            \
                       "mov r2, #" #kind "\n"                    \
                       "b fault_report\n");                      \
    }

FAULT_STUB(HardFault_Handler, 0)
FAULT_STUB(MemManage_Handler, 1)
FAULT_STUB(BusFault_Handler, 2)
FAULT_STUB(UsageFault_Handler, 3)
