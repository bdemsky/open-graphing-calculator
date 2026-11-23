#!/usr/bin/env python3
"""Validate the STM32H743ZGT6 pin assignment against ST's open pin data.

Every function-mapped signal must exist on its pin (per STM32_open_pin_data),
no pin may be used twice, and plain GPIOs must be real I/O pins. Writes
pinmap.csv and pinmap.md next to this script. Exit status is non-zero on error.

This module is the single source of truth for the MCU pin-out: design.py
imports ASSIGN/KEY_ROWS/KEY_COLS to label the MCU's pins in the schematic,
check_netlist.py verifies the exported netlist against them, and build.sh
runs main() first so a bad assignment stops the build. The firmware's pin
definitions (firmware/platform/stm32h743/board.h) must agree with this table
(firmware/tools/check_pins.py checks that).

ST data: tools/STM32_open_pin_data (ST's published CubeMX pin database) lists
every pin of the LQFP-144 package with its position, type (I/O, power,
reset, ...) and the alternate-function signal names it supports. A signal is
accepted only if it is in that list for the pin; "GPIO_*" entries only need
an I/O pin. Keypad columns use EXTI wake-up, and the STM32 EXTI multiplexer
selects one port per line number, so no two columns may share a pin number.
"""
import csv
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
PIN_XML = os.path.join(HERE, "../../tools/STM32_open_pin_data/mcu/STM32H743ZGTx.xml")
NS = {"n": "http://dummy.com"}   # the XML namespace ST's files actually declare

# net name -> (pin, peripheral signal or "GPIO_*", note)
# (firmware/tools/check_pins.py reads this literal with ast.literal_eval: keep it a
# plain dict of string tuples)
ASSIGN = {
    # --- Clocks / system ---
    "HSE_IN": ("PH0", "RCC_OSC_IN", "25 MHz crystal"),
    "HSE_OUT": ("PH1", "RCC_OSC_OUT", ""),
    "LSE_IN": ("PC14", "RCC_OSC32_IN", "32.768 kHz crystal"),
    "LSE_OUT": ("PC15", "RCC_OSC32_OUT", ""),
    "SWDIO": ("PA13", "DEBUG_JTMS-SWDIO", ""),
    "SWCLK": ("PA14", "DEBUG_JTCK-SWCLK", ""),
    "SWO": ("PB3", "DEBUG_JTDO-SWO", "optional trace"),
    "UART_TX": ("PA9", "USART1_TX", "debug console"),
    "UART_RX": ("PA10", "USART1_RX", "debug console"),
    "KEY_ON": ("PA0", "PWR_WKUP1", "ON key, wakes from Standby"),
    # --- SDRAM: FMC bank 2 ---
    "FMC_D0": ("PD14", "FMC_D0", ""), "FMC_D1": ("PD15", "FMC_D1", ""),
    "FMC_D2": ("PD0", "FMC_D2", ""), "FMC_D3": ("PD1", "FMC_D3", ""),
    "FMC_D4": ("PE7", "FMC_D4", ""), "FMC_D5": ("PE8", "FMC_D5", ""),
    "FMC_D6": ("PE9", "FMC_D6", ""), "FMC_D7": ("PE10", "FMC_D7", ""),
    "FMC_D8": ("PE11", "FMC_D8", ""), "FMC_D9": ("PE12", "FMC_D9", ""),
    "FMC_D10": ("PE13", "FMC_D10", ""), "FMC_D11": ("PE14", "FMC_D11", ""),
    "FMC_D12": ("PE15", "FMC_D12", ""), "FMC_D13": ("PD8", "FMC_D13", ""),
    "FMC_D14": ("PD9", "FMC_D14", ""), "FMC_D15": ("PD10", "FMC_D15", ""),
    "FMC_A0": ("PF0", "FMC_A0", ""), "FMC_A1": ("PF1", "FMC_A1", ""),
    "FMC_A2": ("PF2", "FMC_A2", ""), "FMC_A3": ("PF3", "FMC_A3", ""),
    "FMC_A4": ("PF4", "FMC_A4", ""), "FMC_A5": ("PF5", "FMC_A5", ""),
    "FMC_A6": ("PF12", "FMC_A6", ""), "FMC_A7": ("PF13", "FMC_A7", ""),
    "FMC_A8": ("PF14", "FMC_A8", ""), "FMC_A9": ("PF15", "FMC_A9", ""),
    "FMC_A10": ("PG0", "FMC_A10", ""), "FMC_A11": ("PG1", "FMC_A11", ""),
    "FMC_A12": ("PG2", "FMC_A12", ""),
    "FMC_BA0": ("PG4", "FMC_A14", "SDRAM BA0 = FMC_A14/BA0"),
    "FMC_BA1": ("PG5", "FMC_A15", "SDRAM BA1 = FMC_A15/BA1"),
    "FMC_NBL0": ("PE0", "FMC_NBL0", "SDRAM LDQM"),
    "FMC_NBL1": ("PE1", "FMC_NBL1", "SDRAM UDQM"),
    "FMC_SDCLK": ("PG8", "FMC_SDCLK", "series 22-33R at MCU"),
    "FMC_SDNCAS": ("PG15", "FMC_SDNCAS", ""),
    "FMC_SDNRAS": ("PF11", "FMC_SDNRAS", ""),
    "FMC_SDNWE": ("PC0", "FMC_SDNWE", ""),
    "FMC_SDCKE1": ("PB5", "FMC_SDCKE1", ""),
    "FMC_SDNE1": ("PB6", "FMC_SDNE1", ""),
    # --- LCD: FMC NOR/SRAM bank 1, 8080 16-bit (shares D0-D15) ---
    "LCD_CS": ("PD7", "FMC_NE1", "CSX"),
    "LCD_RD": ("PD4", "FMC_NOE", "RDX"),
    "LCD_WR": ("PD5", "FMC_NWE", "WRX"),
    "LCD_DC": ("PD11", "FMC_A16", "DCX: 0=cmd, 1=data"),
    "LCD_RESET": ("PD3", "GPIO_Output", "/RES"),
    "LCD_TE": ("PG9", "GPIO_EXTI9", "tearing effect (own EXTI line)"),
    "LCD_BL_DAC": ("PA4", "DAC1_OUT1", "backlight current setpoint"),
    # --- QSPI flash ---
    "QSPI_CLK": ("PB2", "QUADSPI_CLK", ""),
    "QSPI_NCS": ("PG6", "QUADSPI_BK1_NCS", ""),
    "QSPI_IO0": ("PF8", "QUADSPI_BK1_IO0", ""),
    "QSPI_IO1": ("PF9", "QUADSPI_BK1_IO1", ""),
    "QSPI_IO2": ("PF7", "QUADSPI_BK1_IO2", ""),
    "QSPI_IO3": ("PF6", "QUADSPI_BK1_IO3", ""),
    # --- microSD, 4-bit SDMMC1 ---
    "SD_D0": ("PC8", "SDMMC1_D0", ""), "SD_D1": ("PC9", "SDMMC1_D1", ""),
    "SD_D2": ("PC10", "SDMMC1_D2", ""), "SD_D3": ("PC11", "SDMMC1_D3", ""),
    "SD_CK": ("PC12", "SDMMC1_CK", ""), "SD_CMD": ("PD2", "SDMMC1_CMD", ""),
    "SD_DETECT": ("PG7", "GPIO_Input", "card detect switch"),
    # --- USB FS device ---
    "USB_DM": ("PA11", "USB_OTG_FS_DM", ""),
    "USB_DP": ("PA12", "USB_OTG_FS_DP", ""),
    "USB_VBUS_SENSE": ("PA2", "GPIO_Input", "VBUS divider"),
    # --- Power management ---
    "BUS_EN": ("PG3", "GPIO_Output", "3V3_BUS load switch (SDRAM + LCD logic)"),
    "SD_EN": ("PG10", "GPIO_Output", "microSD load switch"),
    "VBAT_SENSE": ("PA3", "ADC1_INP15", "battery divider"),
    "VBAT_SENSE_EN": ("PA1", "GPIO_Output", "enables battery divider"),
    "LED_DEBUG": ("PB8", "GPIO_Output", "bring-up LED"),
    "BL_SRC_VSYS": ("PD6", "GPIO_Output", "backlight anode from VSYS (high = on; 100k pull-down)"),
    "BL_SRC_3V3": ("PC13", "GPIO_Output", "backlight anode from 3V3_BUS (high = on; 100k pull-down = off at reset)"),
    # --- Debug / observability (so the board can be debugged remotely) ---
    "I2C_SCL": ("PB10", "I2C2_SCL", "INA226 power monitor + header"),
    "I2C_SDA": ("PB11", "I2C2_SDA", "INA226 power monitor + header"),
    "PMON_ALERT": ("PF10", "GPIO_EXTI10", "INA226 alert (open drain, own EXTI line)"),
    "MON_3V3_BUS": ("PA6", "ADC1_INP3", "rail monitor, 1:2 divider"),
    "MON_3V3_SD": ("PC4", "ADC1_INP4", "rail monitor, 1:2 divider"),
    "BL_SENSE": ("PC5", "ADC1_INP8", "backlight current, 1 V/A"),
    "DBG_TX2": ("PC6", "USART6_TX", "second UART on expansion header"),
    "DBG_RX2": ("PC7", "USART6_RX", "second UART on expansion header"),
    "DBG_MCO": ("PA8", "RCC_MCO_1", "clock output for measurement"),
    "DBG_IO0": ("PA15", "GPIO_Output", "timing marker"),
    "DBG_IO1": ("PB9", "GPIO_Output", "timing marker"),
    "DBG_IO2": ("PB15", "GPIO_Output", "timing marker"),
    "DBG_IO3": ("PA5", "GPIO_Output", "timing marker"),
    "DBG_IO4": ("PB7", "GPIO_Output", "timing marker"),
    # --- Capacitive touch (FT6236 on the panel's 40-pin FPC, J3 pins 1-4), own I2C bus on
    # 3V3_BUS pull-ups ---
    "TP_SCL": ("PD12", "I2C4_SCL", "touch controller, I2C addr 0x38"),
    "TP_SDA": ("PD13", "I2C4_SDA", "touch controller"),
    "TP_INT": ("PC1", "GPIO_EXTI1", "touch interrupt (own EXTI line)"),
    "TP_RST": ("PA7", "GPIO_Output", "touch reset, active low"),
}

# Keypad: 7 rows (driven) x 7 columns (inputs with pull-ups, EXTI wake) = 49 keys + ON.
# Columns need distinct pin numbers because EXTI lines are shared by number.
KEY_ROWS = ["PG11", "PG12", "PG13", "PG14", "PB0", "PB1", "PB4"]
KEY_COLS = ["PE2", "PE3", "PE4", "PE5", "PE6", "PB12", "PB13"]


def base(name):
    """'PA13 (JTMS/SWDIO)', 'PC14-OSC32_IN', 'PC2_C' -> 'PA13', 'PC14', 'PC2_C'."""
    m = re.match(r"P[A-K]\d+(_C)?", name)
    return m.group(0) if m else name


def load_pins():
    """{pin: (package position, pin type, {signal names})} from ST's MCU XML."""
    root = ET.parse(PIN_XML).getroot()
    pins = {}
    for p in root.findall("n:Pin", NS):
        sigs = {s.get("Name") for s in p.findall("n:Signal", NS)}
        pins[base(p.get("Name"))] = (p.get("Position"), p.get("Type"), sigs)
    return pins


def main():
    """Check ASSIGN + keypad pins, write pinmap.csv/.md (sorted by package position),
    print the free I/O pins and any errors; return the exit status."""
    pins = load_pins()
    assign = dict(ASSIGN)
    for i, pin in enumerate(KEY_ROWS):
        assign[f"KEY_ROW{i}"] = (pin, "GPIO_Output", "open-drain row")
    for i, pin in enumerate(KEY_COLS):
        assign[f"KEY_COL{i}"] = (pin, f"GPIO_EXTI{pin[2:]}", "pull-up input")

    errors = []
    used = {}
    for net, (pin, sig, _) in assign.items():
        if pin not in pins:
            errors.append(f"{net}: pin {pin} not in package")
            continue
        if pin in used:
            errors.append(f"{net}: {pin} already used by {used[pin]}")
        used[pin] = net
        _, ptype, sigs = pins[pin]
        if sig.startswith("GPIO"):
            if ptype != "I/O":
                errors.append(f"{net}: {pin} is not an I/O pin ({ptype})")
        elif sig not in sigs:
            errors.append(f"{net}: {pin} has no signal {sig} (has: {sorted(sigs)})")

    col_numbers = [int(p[2:]) for p in KEY_COLS]
    if len(set(col_numbers)) != len(col_numbers):
        errors.append("keypad columns share an EXTI line number")

    # spare I/O pins, ordered by port letter then pin number (PA0, PA1, ..., PB0, ...)
    free = sorted(
        (n for n, (_, t, _) in pins.items() if t == "I/O" and n not in used),
        key=lambda n: (n[1], int(re.sub(r"\D", "", n))),
    )

    pos = lambda pin: int(pins[pin][0]) if pin in pins else 0
    rows = sorted(assign.items(), key=lambda kv: pos(kv[1][0]))
    with open(os.path.join(HERE, "pinmap.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["position", "pin", "net", "function", "note"])
        for net, (pin, sig, note) in rows:
            w.writerow([pos(pin), pin, net, sig, note])
    with open(os.path.join(HERE, "pinmap.md"), "w") as f:
        f.write("# STM32H743ZGT6 pin map (generated by pinmap.py — do not edit)\n\n")
        f.write("| Pos | Pin | Net | Function | Note |\n|---|---|---|---|---|\n")
        for net, (pin, sig, note) in rows:
            f.write(f"| {pos(pin)} | {pin} | {net} | {sig} | {note} |\n")
        f.write(f"\nFree I/O ({len(free)}): {', '.join(free)}\n")

    print(f"{len(assign)} nets on {len(used)} pins; {len(free)} I/O free")
    print("free:", " ".join(free))
    for e in errors:
        print("ERROR:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
