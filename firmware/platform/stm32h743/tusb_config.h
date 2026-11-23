/*
 * TinyUSB configuration (third_party/tinyusb, see third_party/README.md) for
 * the calculator's USB device (platform/stm32h743/usb.c): OTG_FS (USB2_OTG_FS,
 * PA11/PA12, on-chip full-speed PHY) as root-hub port 0, device role only, no
 * RTOS. Classes: one CDC-ACM (the debug console) and one MSC with two logical
 * units (the CALC drive and the microSD card).
 *
 * Buffer sizes are small on purpose: TinyUSB's static data is linked into
 * DTCM (AXI SRAM is full; stm32h743zg.ld), about 4 KB in all. The CDC FIFOs
 * hold several console lines (a pipelined `module put` upload keeps up to 8
 * in flight, tools/calcctl.py --window); when the RX FIFO is full TinyUSB stops
 * arming the OUT endpoint, so the host is NAKed and nothing is lost.
 */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#define CFG_TUSB_MCU OPT_MCU_STM32H7
#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define BOARD_TUD_RHPORT 0
#define BOARD_TUD_MAX_SPEED OPT_MODE_FULL_SPEED

/* slave mode (the FS core has no DMA); 4-byte aligned buffers suffice */
#define CFG_TUD_DWC2_DMA_ENABLE 0
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))

#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 1
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 1024
#define CFG_TUD_CDC_TX_BUFSIZE 1024
#define CFG_TUD_CDC_EP_BUFSIZE 64

/* one 512-byte block per READ10/WRITE10 callback */
#define CFG_TUD_MSC_EP_BUFSIZE 512

#endif
