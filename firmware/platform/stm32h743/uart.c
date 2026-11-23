/*
 * uart.c — USART1 debug console (PA9 TX / PA10 RX, AF7; RM0433 section 48
 * "USART/LPUART"). Kernel clock: the reset-default USART1 source, PCLK2 =
 * 100 MHz (board.h PCLK_HZ), 16x oversampling, so BRR = f_ck / baud.
 *
 * Receive is interrupt driven into a single-producer / single-consumer ring
 * (the ISR advances g_rx_head, uart_read() in the main loop advances
 * g_rx_tail); bytes arriving when the ring is full are dropped and counted.
 * Transmit is polled (blocking): the console output volume is small and this
 * keeps the driver usable from any context. The hardware FIFO is not enabled
 * (FIFOEN = 0), so the *_RXFNE/_TXFNF flags behave as plain RXNE/TXE.
 */
#include "platform.h"

#define RX_SIZE 2048u /* power of two not required: indices wrap with % */
static volatile char g_rx[RX_SIZE];
static volatile uint32_t g_rx_head, g_rx_tail;
static volatile uint32_t g_rx_overflows;
static uint32_t g_baud;

/* Change the bit rate. Waits for transmission complete (TC) first so the last
 * queued character leaves at the old rate; BRR can only be written while the
 * USART is disabled (UE = 0). Rounds to the nearest divider. */
void uart_set_baud(uint32_t baud)
{
    while (!(USART1->ISR & USART_ISR_TC)) {
    }
    USART1->CR1 &= ~USART_CR1_UE;
    USART1->BRR = (PCLK_HZ + baud / 2) / baud; /* oversampling by 16 */
    USART1->CR1 |= USART_CR1_UE;
    g_baud = baud;
}

uint32_t uart_get_baud(void) { return g_baud; }

/* Enable USART1 8N1 with RX interrupt (NVIC priority 2). The pins are set to
 * AF7 by gpio_config_from_table(). */
void uart_init(uint32_t baud)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    USART1->BRR = (PCLK_HZ + baud / 2) / baud;
    g_baud = baud;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 2);
    NVIC_EnableIRQ(USART1_IRQn);
}

/* RX interrupt: drain RDR into the ring. Overrun/framing/noise errors are
 * cleared and otherwise ignored; an uncleared ORE would keep the interrupt
 * pending and block further reception. */
void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    while (USART1->ISR & USART_ISR_RXNE_RXFNE) {
        char c = (char)USART1->RDR;
        uint32_t next = (g_rx_head + 1) % RX_SIZE;
        if (next == g_rx_tail) {
            g_rx_overflows++;
        } else {
            g_rx[g_rx_head] = c;
            g_rx_head = next;
        }
    }
}

/* Baud change requested by a console command: applied at the next
 * uart_read(), i.e. after the command's reply has been written at the old
 * rate (uart_write() is blocking, and uart_set_baud() waits for TC). */
static volatile uint32_t g_pending_baud;
void uart_set_baud_deferred(uint32_t baud) { g_pending_baud = baud; }

/* Copy up to `max` received bytes into `buf` without blocking; returns the
 * number copied (0 if none). Main-context only (single consumer). */
int uart_read(char *buf, int max)
{
    int n = 0;
    if (g_pending_baud) { /* the previous reply has been fully queued by now */
        uart_set_baud(g_pending_baud);
        g_pending_baud = 0;
    }
    while (n < max && g_rx_tail != g_rx_head) {
        buf[n++] = g_rx[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % RX_SIZE;
    }
    return n;
}

/* Blocking polled write of n bytes, translating '\n' to "\r\n". At 115200
 * baud each byte takes ~87 us, so long output stalls the caller. */
void uart_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\n') { /* CRLF for terminal programs */
            while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {
            }
            USART1->TDR = '\r';
        }
        while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {
        }
        USART1->TDR = (uint8_t)s[i];
    }
}

/* Usable from fault handlers before/without init: (re)initialises at 115200.
 * If the clock tree is not (yet) at full speed the kernel clock is assumed to
 * be the 64 MHz HSI that the core runs from out of reset. No '\n' -> "\r\n"
 * translation; waits for TC so the text is on the wire before a reset. */
void uart_write_raw_polled(const char *s)
{
    if (!(RCC->APB2ENR & RCC_APB2ENR_USART1EN) || !(USART1->CR1 & USART_CR1_UE)) {
        RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
        gpio_config(P_UART_TX, 7, PULL_NONE);
        RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
        USART1->BRR = (SystemCoreClock >= SYSCLK_HZ ? PCLK_HZ : 64000000u) / 115200u;
        USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
    }
    while (*s) {
        while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {
        }
        USART1->TDR = (uint8_t)*s++;
    }
    while (!(USART1->ISR & USART_ISR_TC)) {
    }
}
