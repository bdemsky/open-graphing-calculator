/*
 * USB identity of the calculator: the one place it is defined. The device
 * descriptor (platform/stm32h743/usb.c) uses these, and tools/calcctl.py reads
 * this file to find the calculator among the computer's serial ports.
 *
 * PLACEHOLDER: 1209:0001 is the pid.codes test VID:PID ("pid.codes Test PID",
 * https://pid.codes/1209/0001/), for development only and shared with anything
 * else under test. A released device needs its own PID (pid.codes assigns one
 * free of charge to open-hardware projects): change both numbers here, and
 * nothing else.
 */
#ifndef USB_IDS_H
#define USB_IDS_H

#define USB_VID 0x1209
#define USB_PID 0x0001

#endif
