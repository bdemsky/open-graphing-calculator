#!/usr/bin/env python3
"""The calculator schematic, as code. Run build.sh to regenerate KiCad files.

Net names for MCU pins come from pinmap/pinmap.py (validated against ST data).
See ARCHITECTURE.md for the reasoning behind each block.

Each section below is one schematic sheet (power, mcu, memory, display, usb,
keypad, debug). Parts are added with sheet.add(lib_id, ref, value, {pin: net},
footprint, LCSC=...); a ref without a number ("R", "C", "Q") is numbered at
write time and remembered in refs.json, so references stay stable across
regenerations. Resistors and capacitors go through R()/C(), which pick a
JLC Basic part by value (parts.py). Connectivity is by net name only: two
pins on the same net name are connected, on any sheet.

Outputs (via kicad_sch.Design.write): kicad/calc.kicad_sch and one
kicad/<sheet>.kicad_sch per sheet, the project library tables, refs.json and
parts_cache.json. build.sh then runs KiCad ERC, exports the netlist and runs
check_netlist.py; layout.py / pcb_build.py read that netlist for the board,
and also import LAYOUT/KEYMAP from here.
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../pinmap"))
sys.path.insert(0, HERE)

import parts  # noqa: E402
import pinmap  # noqa: E402
from kicad_sch import NC, Design, Library  # noqa: E402

# Logical key layout (firmware/keys/layout.py), loaded by path: its module name clashes with
# hardware/gen/layout.py (the PCB placement). Key switches are named after their logical key.
_spec = importlib.util.spec_from_file_location("keys_layout", os.path.join(HERE, "../../firmware/keys/layout.py"))
keys_layout = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(keys_layout)

LIB = Library({"calc": os.path.join(HERE, "../lib/calc.kicad_sym")})
# Rev A.1 = rev A as ordered, documentation-only update: same copper, drill, mask, paste,
# silkscreen, BOM part numbers and placements; schematic text and key switch values only.
# The same title block goes on the board (kicad_pcb.Board.write, annotate.py).
TITLE_BLOCK = {"title": "Graphing Calculator", "rev": "A.1", "date": "2026-10-20",
               "comments": ["Rev A.1: documentation update of rev A, fabricated outputs unchanged",
                            "Key switch values = logical key names (firmware/keys/layout.py)"]}
D = Design("calc", LIB, TITLE_BLOCK["title"], rev=TITLE_BLOCK["rev"], date=TITLE_BLOCK["date"],
           comments=TITLE_BLOCK["comments"])
D.power_nets = ["GND", "VSYS", "VSYS_IN", "3V3", "VBUS", "VDDA", "BAT_P"]  # nets with no power_out driver


# ------------------------------------------------------------- helpers ---
def R(sheet, value, a, b, pkgs=("0402", "0603")):
    """Resistor of value (e.g. "10k") between nets a and b; smallest Basic package in pkgs."""
    p = parts.pick("R", value, pkgs)
    return sheet.add("Device:R", "R", value, {"1": a, "2": b}, p["footprint"], LCSC=p["lcsc"])


def C(sheet, value, a, b="GND", min_v=6.3, pkgs=None):
    """Capacitor of value (e.g. "100nF") from net a to b (GND by default), rated >= min_v volts."""
    if pkgs is None:  # >=4.7 uF in 0402 loses most of its capacitance at 3.3 V
        pkgs = ("0603", "0805") if parts._si(value.replace("F", "")) >= 4.6e-6 else ("0402", "0603", "0805")
    p = parts.pick("C", value, pkgs, min_v)
    return sheet.add("Device:C", "C", value, {"1": a, "2": b}, p["footprint"], LCSC=p["lcsc"])


def decouple(sheet, net, *values):
    """One capacitor to GND per value on net."""
    for v in values:
        C(sheet, v, net)


def switch(sheet, ref, value, a, b):
    """TS-1187A: pads 1-2 (A-B) and 3-4 (C-D) are internally common (XKB drawing)."""
    return sheet.add("calc:TS-1187A-B-A-B", ref, value, {"1": a, "2": a, "3": b, "4": b},
                     "calc:SW-SMD_4P-L5.1-W5.1-P3.70-LS6.5-TL_H1.5", LCSC="C318884")


# MCU pin name ("PD7") -> net name, from the validated pin map; every MCU pin not
# listed here (and not a supply/special pin) is marked no-connect below
MCU_NET = {pin: net for net, (pin, _sig, _n) in pinmap.ASSIGN.items()}
for i, pin in enumerate(pinmap.KEY_ROWS):
    MCU_NET[pin] = f"KEY_ROW{i}"
for i, pin in enumerate(pinmap.KEY_COLS):
    MCU_NET[pin] = f"KEY_COL{i}"

# =============================================================== POWER ===
pw = D.sheet("power", "Power")
# Two holders, two AAA each. Per holder (datasheet MY-CP-0144, top view):
# slot A: pad 2 = -, pad 1 = +; slot B: pad 3 = +, pad 4 = -.
# Pad 1 -> pad 4 puts the slots in series; pads 2 (-) and 3 (+) are the ends.
pw.add("calc:BH-AAA-B5AA001", "BT1", "2xAAA", {"2": "GND", "1": "BT1_MID", "4": "BT1_MID", "3": "BAT_MID"},
       "calc:BAT-SMD_BH-AAA-B5AA001", LCSC="C964881")   # bottom side, JLC-placed
pw.add("calc:BH-AAA-B5AA001", "BT2", "2xAAA", {"2": "BAT_MID", "1": "BT2_MID", "4": "BT2_MID", "3": "BAT_RAW"},
       "calc:BAT-SMD_BH-AAA-B5AA001", LCSC="C964881")   # bottom side, JLC-placed
# Reverse-battery protection: P-FET, drain to battery, gate to GND.
pw.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"G": "GND", "D": "BAT_RAW", "S": "BAT_P"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
# USB-priority power path. Fresh cells (6.4 V) would out-vote USB through a plain diode-OR,
# so the battery goes through back-to-back P-FETs that open whenever VBUS is present:
#   no VBUS: Q_VB off -> BATSW_EN pulled to BAT_P -> Q_EN on -> gates low -> battery FETs on.
#   VBUS:    Q_VB on -> Q_EN off -> 2M ties gates to the common source -> both FETs off.
# Gates reference the common source, so neither source can back-feed the other.
# USB enters through a silicon diode (nA leakage, unlike a Schottky).
pw.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"D": "BAT_P", "S": "BATSW_S", "G": "BATSW_G"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
pw.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"D": "VSYS_IN", "S": "BATSW_S", "G": "BATSW_G"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
# 2M: ~3 ms gate turn-off at USB attach (10M took ~20 ms, back-feeding weak cells meanwhile)
# and tolerates ~200 nA of Q_EN leakage (AO3400A IDSS is only specified at 1 uA).
R(pw, "2M", "BATSW_S", "BATSW_G")
pw.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"D": "BATSW_G", "S": "GND", "G": "BATSW_EN"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
# 47k: Q_EN turns on ~15 us after VBUS_DET falls (costs ~130 uA from the cells while on USB).
R(pw, "47k", "BAT_P", "BATSW_EN")
pw.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"D": "BATSW_EN", "S": "GND", "G": "VBUS_DET"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
# VBUS detect. On unplug VBUS does not collapse by itself: its 10 uF drains through D1 into the
# load, so VBUS and VSYS_IN fall together and a plain VBUS/2 divider into Q_VB would only hand
# over to the cells after the buck had dropped out (MCU reset). The TLV3012 (push-pull, 1.242 V
# reference) is powered from VBUS, so it costs the cells nothing, and switches at VBUS ~4.30 V
# rising / ~4.21 V falling (200k/82k, 10M hysteresis): the cells are back while VSYS_IN is still
# ~3.4 V. When VBUS is gone the 100k keeps Q_VB off.
pw.add("calc:TLV3012BIDBVR", "U", "TLV3012BIDBVR",
       {"V+": "VBUS", "V-": "GND", "IN+": "VBUS_DIV", "IN-": "VBUS_REF", "REF": "VBUS_REF",
        "OUT": "VBUS_DET"}, "calc:SOT-23-6_L2.9-W1.7-P0.95-LS2.8-BL", LCSC="C20345924")
C(pw, "100nF", "VBUS", min_v=16)
R(pw, "200k", "VBUS", "VBUS_DIV")
R(pw, "82k", "VBUS_DIV", "GND")
R(pw, "10M", "VBUS_DET", "VBUS_DIV")
R(pw, "100k", "VBUS_DET", "GND")
pw.add("Device:D", "D", "M7", {"A": "VBUS", "K": "VSYS_IN"}, "Diode_SMD:D_SMA", LCSC="C95872")
# Bulk on VSYS_IN carries the load for the ~20 us USB -> battery handover (22 uF/25 V 0805
# is ~12 uF at 5 V; three give ~0.15 V droop at 0.3 A).
for _ in range(3):
    C(pw, "22uF", "VSYS_IN", min_v=25)
# 0.1 R shunt: all system current flows VSYS_IN -> VSYS and is measured by the INA226 (debug sheet).
# Two-terminal 2512 part; route the INA226 sense traces from the pad centres (Kelvin) in layout.
pw.add("Device:R", "R", "0.1 1%", {"1": "VSYS_IN", "2": "VSYS"}, "Resistor_SMD:R_2512_6332Metric",
       LCSC="C160587", MPN="RLP25FEER100")
C(pw, "10uF", "VSYS", min_v=25, pkgs=("0805",))
C(pw, "100nF", "VSYS", min_v=16)
# 3V3 buck: TPS62162 fixed 3.3 V, 17 uA Iq. FB to AGND per datasheet (fixed version).
pw.add("Regulator_Switching:TPS62162DSG", "U", "TPS62162DSGR",
       {"VIN": "VSYS", "EN": "VSYS", "PG": NC, "SW": "BUCK_SW", "VOS": "3V3", "FB": "GND",
        "AGND": "GND", "PGND": "GND", "PAD": "GND"},
       "Package_SON:Texas_DSG0008A_WSON-8-1EP_2x2mm_P0.5mm_EP0.9x1.6mm", LCSC="C40256")
pw.add("Device:L", "L", "2.2uH", {"1": "BUCK_SW", "2": "3V3"}, "Inductor_SMD:L_1008_2520Metric",
       LCSC="C5832372", MPN="FTC252012S2R2MBCA")
C(pw, "22uF", "3V3", min_v=10)
# Switched rails: SY6280 load switches, Ilim = 6800 / Rset. SDRAM and LCD logic share
# 3V3_BUS because they share FMC D0-D15: one powered while the other is off would
# back-power it through its I/O clamps (the SY6280 discharges its output when off).
# 13k = 0.52 A (>= 0.39 A at -25%): the 3V3_BUS worst case is ~0.24 A, and the lower limit
# halves the inrush into the 33 uF on the rail.
for name, en, rset in (("3V3_BUS", "BUS_EN", "13k"), ("3V3_SD", "SD_EN", "13k")):
    pw.add("calc:SY6280AAC", "U", "SY6280AAC",
           {"IN": "3V3", "EN": en, "OUT": name, "GND": "GND", "ISET": f"{name}_ISET"},
           "calc:SOT-23-5_L3.0-W1.7-P0.95-LS2.8-BL", LCSC="C55136")
    R(pw, rset, f"{name}_ISET", "GND", pkgs=("0603",))
    R(pw, "100k", en, "GND")  # default off while the MCU is in reset
    C(pw, "10uF", "3V3")   # SY6280 datasheet: 10 uF on IN
    C(pw, "10uF", name)
# Battery gauge: high-side P-FET switch so the divider draws nothing when off.
pw.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"G": "VBAT_SW_G", "S": "BAT_P", "D": "VBAT_DIV"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
R(pw, "100k", "BAT_P", "VBAT_SW_G")
pw.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"G": "VBAT_SENSE_EN", "S": "GND", "D": "VBAT_SW_G"},
       "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
R(pw, "100k", "VBAT_SENSE_EN", "GND")
R(pw, "100k", "VBAT_DIV", "VBAT_SENSE")   # 6.4 V max -> 2.05 V at the ADC
R(pw, "47k", "VBAT_SENSE", "GND")
C(pw, "100nF", "VBAT_SENSE")

# ================================================================= MCU ===
mcu = D.sheet("mcu", "MCU")
conns = {"VDD": "3V3", "VSS": "GND", "VSSA": "GND", "VDDA": "VDDA", "VREF+": "VDDA", "VBAT": "3V3",
         "VDD33_USB": "3V3", "PDR_ON": "3V3", "BOOT0": "BOOT0", "NRST": "NRST",
         "#71": "VCAP1", "#106": "VCAP2"}  # one 2.2 uF per VCAP pin (AN4938)
u1 = LIB.get("MCU_ST_STM32H7:STM32H743ZGTx")
for p in u1.pins:
    if p.name in conns or f"#{p.number}" in conns:
        continue
    conns[p.name] = MCU_NET.get(p.name, NC)
unused = sorted(n for n, v in conns.items() if v is NC)
mcu.add("MCU_ST_STM32H7:STM32H743ZGTx", "U1", "STM32H743ZGT6", conns,
        "Package_QFP:LQFP-144_20x20mm_P0.5mm", LCSC="C730184")
# Decoupling per AN4938: 100 nF per VDD pin + bulk; 2.2 uF per VCAP; filtered VDDA/VREF+.
# VBAT and VDD33_USB are tied to 3V3 and get their own 100 nF too.
n_vdd = sum(1 for p in u1.pins if p.name in ("VDD", "VBAT", "VDD33_USB"))
for _ in range(n_vdd):
    C(mcu, "100nF", "3V3")
C(mcu, "4.7uF", "3V3")
C(mcu, "2.2uF", "VCAP1")
C(mcu, "2.2uF", "VCAP2")
mcu.add("Device:FerriteBead_Small", "FB", "120R@100MHz", {"1": "3V3", "2": "VDDA"},
        "Inductor_SMD:L_0603_1608Metric", LCSC="C14709")
decouple(mcu, "VDDA", "1uF", "100nF", "100nF")
# Reset and boot
C(mcu, "100nF", "NRST")
switch(mcu, "SW", "RESET", "NRST", "GND")
R(mcu, "10k", "BOOT0", "GND")
switch(mcu, "SW", "BOOT", "BOOT0", "3V3")
# Clocks
mcu.add("Device:Crystal_GND24", "Y1", "25MHz", {"1": "HSE_IN", "3": "HSE_OUT", "2": "GND", "4": "GND"},
        "Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm", LCSC="C9006", MPN="X322525MOB4SI")
C(mcu, "15pF", "HSE_IN")
C(mcu, "15pF", "HSE_OUT")
# CL = 7 pF part: gm_crit ~0.9 uA/V, comfortable for the H7 LSE driver (CL 12.5 pF was marginal).
mcu.add("Device:Crystal", "Y2", "32.768kHz", {"1": "LSE_IN", "2": "LSE_OUT"},
        "Crystal:Crystal_SMD_3215-2Pin_3.2x1.5mm", LCSC="C97604", MPN="SC-32S 7pF")
C(mcu, "10pF", "LSE_IN")
C(mcu, "10pF", "LSE_OUT")
# Debug: ARM 10-pin 1.27 mm SWD header for ST-Link (JLC through-hole assembly); see also the debug sheet.
mcu.add("Connector:Conn_ARM_JTAG_SWD_10", "J", "SWD",
        {"VTref": "3V3", "SWDIO/TMS": "SWDIO", "SWCLK/TCK": "SWCLK", "SWO/TDO": "SWO", "~{RESET}": "NRST",
         "GND": "GND", "GNDDetect": "GND", "KEY": NC, "NC/TDI": NC},
        "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical", LCSC="C22438120")
# Bring-up LED
mcu.add("Device:LED", "D", "red", {"A": "LED_DEBUG_A", "K": "GND"}, "LED_SMD:LED_0603_1608Metric",
        LCSC="C2286", MPN="KT-0603R")
R(mcu, "1k", "LED_DEBUG", "LED_DEBUG_A")

# ============================================================== MEMORY ===
mem = D.sheet("memory", "Memory")
sd = {"VDD": "3V3_BUS", "VDDQ": "3V3_BUS", "VSS": "GND", "VSSQ": "GND", "NC": NC,
      "LDQM": "FMC_NBL0", "UDQM": "FMC_NBL1", "~{WE}": "FMC_SDNWE", "~{CAS}": "FMC_SDNCAS",
      "~{RAS}": "FMC_SDNRAS", "~{CS}": "FMC_SDNE1", "CKE": "FMC_SDCKE1", "CLK": "SDRAM_CLK",
      "BS0": "FMC_BA0", "BS1": "FMC_BA1", "A10/AP": "FMC_A10"}
for i in range(16):
    sd[f"DQ{i}"] = f"FMC_D{i}"
for i in list(range(10)) + [11, 12]:
    sd[f"A{i}"] = f"FMC_A{i}"
mem.add("calc:W9825G6KH-6", "U2", "W9825G6KH-6", sd, "calc:TSOP-54_L22.2-W10.2-P0.80-LS11.8-BL",
        LCSC="C62246")
R(mem, "22", "FMC_SDCLK", "SDRAM_CLK")  # source termination, place at the MCU
for _ in range(7):
    C(mem, "100nF", "3V3_BUS")
C(mem, "10uF", "3V3_BUS")
# QSPI NOR
mem.add("Memory_Flash:W25Q128JVS", "U3", "W25Q128JVSIQ",
        {"~{CS}": "QSPI_NCS", "CLK": "QSPI_CLK", "DI/IO_{0}": "QSPI_IO0", "DO/IO_{1}": "QSPI_IO1",
         "~{WP}/IO_{2}": "QSPI_IO2", "~{HOLD}/~{RESET}/IO_{3}": "QSPI_IO3", "VCC": "3V3", "GND": "GND"},
        "Package_SO:SOIC-8_5.3x5.3mm_P1.27mm", LCSC="C97521")
R(mem, "10k", "QSPI_NCS", "3V3")
R(mem, "10k", "QSPI_IO2", "3V3")
R(mem, "10k", "QSPI_IO3", "3V3")
C(mem, "100nF", "3V3")
# microSD (4-bit SDMMC). Pull-ups to the card rail per SD spec; CD switch closes to GND.
mem.add("calc:TF-01A", "J", "microSD",
        {"DAT2(RSV)": "SD_D2", "CDDAT3(CS)": "SD_D3", "CMD(DI)": "SD_CMD", "VDD": "3V3_SD",
         "CLK(SCLK)": "SD_CK_C", "VSS": "GND", "DAT0(D0)": "SD_D0", "DAT1(RSV)": "SD_D1",
         "GND": "GND", "CD": "SD_DETECT"},
        "calc:TF-SMD_TF-01A", LCSC="C91145")
R(mem, "22", "SD_CK", "SD_CK_C")
for n in ("SD_D0", "SD_D1", "SD_D2", "SD_D3", "SD_CMD"):
    R(mem, "47k", n, "3V3_SD")
# SD_DETECT uses the MCU's internal pull-up while polling (no standby leakage).
C(mem, "10uF", "3V3_SD")
C(mem, "100nF", "3V3_SD")

# ============================================================= DISPLAY ===
dsp = D.sheet("display", "Display")
# EastRising ER-TFT032IPS-3.2 with capacitive touch (ER-TPC032-3.1, FT6236 on the panel's FPC):
# 3.2" 240x320 IPS, ST7789. One 40-pin 0.5 mm FPC carries the TFT and the touch I2C (datasheet
# Rev 1.0 sect. 4.1). 16-bit 8080 interface II: IM2..0 = 000. VCI/IOVCC 2.5-3.3 V.
fpc = {"1": "TP_INT", "2": "TP_RST", "3": "TP_SCL", "4": "TP_SDA", "5": "GND",
       "6": "3V3_BUS", "7": "3V3_BUS", "8": "LCD_TE", "9": "LCD_CS", "10": "LCD_DC",
       "11": "LCD_WR", "12": "LCD_RD", "13": "GND", "14": NC, "15": "LCD_RESET", "16": "GND",
       "33": "LCD_A", "34": "LCD_K", "35": "LCD_K", "36": "LCD_K", "37": "GND",
       "38": "GND", "39": "GND", "40": "GND", "41": "GND", "42": "GND"}
for i in range(16):
    fpc[str(17 + i)] = f"LCD_D{i}"
# Pin 13 (SDI, serial modes only) is tied low; pin 14 (SDO) is left open.
# Mounting: panel landscape (portrait rotated 90 deg clockwise, FPC on the left). The FPC is made
# to fold flat behind the module (outline "with FPC folded"): it wraps the panel's left edge, its
# 43 mm-wide section (touch controller) lies against the panel back, and the 20.5 mm tail runs
# ~40 mm in under the panel and dips into J3. Contacts are on the FPC's front face (stiffener on
# the back), so after the fold they face the PCB. Pin 1 is at the portrait-left end of the
# contact row (drawing front view: "1 ... 40"), i.e. the north end in landscape, and the fold
# (about a north-south line) keeps it north. J3 (AFC24-S40FIA-00, front-insert, back-flip,
# double-sided contacts) is rotated 90 deg with pad 1 north and its opening facing the fold
# (west), so panel pin n -> connector pad n.
dsp.add("calc:AFC24-S40FIA-00", "J", "LCD FPC 40p", fpc, "calc:FPC-SMD_P0.50-40P_QCHF-SM-H2.0", LCSC="C262686")
decouple(dsp, "3V3_BUS", "10uF", "1uF", "100nF")
# 1k series resistors where the LCD branch leaves the MCU-SDRAM bus. With the panel's ~8-12 pF
# input + FPC they delay every data edge at the panel by ~5-7 ns, so the ST7789's 10 ns data hold
# after WRX rises is met although the H743 FMC holds data for only one kernel clock (5 ns) and the
# next (SDRAM) transaction may drive the shared bus right after. They also cut the LCD stub's
# capacitance off the 100 MHz SDRAM data lines. Cost: ~20 ns data edges at the panel, well inside
# the 75 ns write pulse (DATAST 15); WRX has no resistor.
for i in range(16):
    R(dsp, "1k", f"FMC_D{i}", f"LCD_D{i}")
R(dsp, "100k", "LCD_RESET", "GND")  # hold the panel in reset while the MCU is in reset
# Backlight: anode from 3V3_BUS or VSYS (firmware-selected, below),
# cathodes into a constant-current sink: TLV9001 (rail-to-rail out) + AO3400A + 1R sense.
# Setpoint from DAC1 (PA4) in unbuffered mode (true 0 V; Ro 10-16k in series):
# 3.3 V * 1k/(10k+Ro+1k) = 122-157 mV -> ~140 mA full scale (panel: 5 LEDs in parallel,
# 90 mA typ / 100 mA max, Vf 3.2-3.4 V). PC5 reads the actual current.
# Anode supply, chosen by firmware: 3V3_BUS (efficient: the buck makes it) for normal brightness,
# VSYS only when the LEDs (Vf 3.2-3.4 V) need more headroom than 3.3 V leaves for the sink.
#   3V3 path: P-FET with SOURCE on LCD_A, so its body diode (3V3_BUS -> LCD_A) blocks VSYS from
#     back-feeding 3V3_BUS. Gate held to LCD_A (off) by 100k, pulled low (on) by an N-FET whose gate
#     has a 100k pull-DOWN: OFF at reset; BL_SRC_3V3 high turns it on. Both paths must default off:
#     with a pull-up, a reset while on VSYS turned the 3V3 path on ~30 us after reset but the VSYS
#     path off only after 100-200 us, tying VSYS (up to 6.4 V) to 3V3_BUS (final review).
#   VSYS path: P-FET, source VSYS; gate 100k to VSYS (off), pulled low by an N-FET (BL_SRC_VSYS high).
#     Its body diode (LCD_A -> VSYS) never conducts (LCD_A <= 3.3 V < VSYS while the buck runs).
# Both on at once would tie VSYS to 3V3_BUS through the two channels: firmware sets the LED current
# to 0, turns one path off, waits >= 1 ms (gate RC ~65 us), then turns the other on.
dsp.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"S": "LCD_A", "D": "3V3_BUS", "G": "BLS_3V3_G"},
        "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
R(dsp, "100k", "LCD_A", "BLS_3V3_G")
dsp.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"D": "BLS_3V3_G", "S": "GND", "G": "BL_SRC_3V3"},
        "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
R(dsp, "100k", "BL_SRC_3V3", "GND")
dsp.add("Transistor_FET:AO3401A", "Q", "AO3401A", {"S": "VSYS", "D": "LCD_A", "G": "BLS_VSYS_G"},
        "Package_TO_SOT_SMD:SOT-23", LCSC="C15127")
R(dsp, "100k", "VSYS", "BLS_VSYS_G")
dsp.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"D": "BLS_VSYS_G", "S": "GND", "G": "BL_SRC_VSYS"},
        "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
R(dsp, "100k", "BL_SRC_VSYS", "GND")
# TLV9001 SOT-23-5 pinout (datasheet SBOS833 table 6-1): OUT 1, V- 2, IN+ 3, IN- 4, V+ 5 = OPA333 DBV symbol.
dsp.add("Amplifier_Operational:OPA333xxDBV", "U", "TLV9001IDBVR",
        {"+": "BL_SET", "-": "BL_FB", "#1": "BL_GATE_DRV", "V+": "3V3_BUS", "V-": "GND"},
        "Package_TO_SOT_SMD:SOT-23-5", LCSC="C398363")
C(dsp, "100nF", "3V3_BUS")
R(dsp, "10k", "LCD_BL_DAC", "BL_SET")
R(dsp, "1k", "BL_SET", "GND")
R(dsp, "100", "BL_GATE_DRV", "BL_GATE")
R(dsp, "1k", "BL_SENSE", "BL_FB")
C(dsp, "1nF", "BL_GATE_DRV", "BL_FB")
R(dsp, "100k", "BL_GATE", "GND")
dsp.add("Transistor_FET:AO3400A", "Q", "AO3400A", {"G": "BL_GATE", "D": "LCD_K", "S": "BL_SENSE"},
        "Package_TO_SOT_SMD:SOT-23", LCSC="C20917")
R(dsp, "1", "BL_SENSE", "GND", pkgs=("0603",))

# Capacitive touch: FT6236 (FocalTech, I2C 0x38) on the panel's own FPC, pins 1-4 of J3.
# Own I2C bus pulled up to 3V3_BUS: the touch controller is unpowered when the rail is off,
# and nothing then drives or pulls its pins.
R(dsp, "4.7k", "TP_SCL", "3V3_BUS")
R(dsp, "4.7k", "TP_SDA", "3V3_BUS")
R(dsp, "10k", "TP_INT", "3V3_BUS")
R(dsp, "10k", "TP_RST", "3V3_BUS")
C(dsp, "1uF", "3V3_BUS")
C(dsp, "100nF", "3V3_BUS")

# ================================================================= USB ===
usb = D.sheet("usb", "USB")
usb.add("calc:TYPE-C-31-M-12", "J", "USB-C",
        {"VBUS": "VBUS", "GND": "GND", "CC1": "USB_CC1", "CC2": "USB_CC2", "DP1": "USB_DP", "DP2": "USB_DP",
         "DN1": "USB_DM", "DN2": "USB_DM", "SBU1": NC, "SBU2": NC, "EH": "GND"},
        "calc:USB-C_SMD-TYPE-C-31-M-12_1", LCSC="C165948")
R(usb, "5.1k", "USB_CC1", "GND")
R(usb, "5.1k", "USB_CC2", "GND")
usb.add("Power_Protection:USBLC6-2SC6", "U", "USBLC6-2SC6",
        {"I/O1": "USB_DM", "I/O2": "USB_DP", "GND": "GND", "VBUS": "VBUS"},
        "Package_TO_SOT_SMD:SOT-23-6", LCSC="C7519")
C(usb, "10uF", "VBUS", min_v=25)
R(usb, "51k", "VBUS", "USB_VBUS_SENSE")
R(usb, "100k", "USB_VBUS_SENSE", "GND")

# ============================================================== KEYPAD ===
kp = D.sheet("keypad", "Keypad")
# Physical key positions, by logical key name (firmware/keys/layout.py, which says what
# each key is). The name is also the switch's value in the schematic. The top row is the
# soft-key row under the display. ON is wired separately (wake pin).
LAYOUT = [  # rows top to bottom
    ["F1", "F2", "F3", "F4", "F5"],
    ["SHIFT", "ALPHA", "DEL"],
    ["HOME", "APPS", "MENU"],
    ["UP", "LEFT", "RIGHT", "DOWN"],          # arrow diamond, right of the two rows above
    ["CAS", "MATH", "VARS", "X", "CLEAR"],
    ["SIN", "COS", "TAN", "LN", "LOG"],
    ["SQR", "POW", "LPAREN", "RPAREN", "DIV"],
    ["SQRT", "7", "8", "9", "MUL"],
    ["COMMA", "4", "5", "6", "SUB"],
    ["ASSIGN", "1", "2", "3", "ADD"],
    ["0", "DOT", "NEG", "ENTER"],             # ON sits in column 0 of this row
]
keys = [k for row in LAYOUT for k in row]
assert len(keys) == 49, len(keys)
keys_layout.check(LAYOUT)
KEYMAP = {}  # key name -> (row, col) in the 7x7 matrix, filled row-major
for idx, k in enumerate(keys):
    KEYMAP[k] = (idx // 7, idx % 7)
    r, c = KEYMAP[k]
    switch(kp, "SW", k, f"KEY_ROW{r}", f"KEY_COL{c}")
switch(kp, "SW", "ON", "KEY_ON", "3V3")
R(kp, "100k", "KEY_ON", "GND")

# =============================================================== DEBUG ===
dbg = D.sheet("debug", "Debug and test")
# INA226 at I2C address 0x40 (A0 = A1 = GND). Kelvin sense across the VSYS shunt.
# The sense pins get their own nets through 10R (datasheet: <= 10 R input filter, matched),
# so the 0.8 mm battery-path copper never runs through the INA226's 0.5 mm-pitch pins and the
# sense taps sit right at the shunt pads.
dbg.add("calc:INA226AIDGSR", "U", "INA226AIDGSR",
        {"VIN+": "PMON_INP", "VIN-": "PMON_INN", "VBUS": "VSYS", "VS+": "3V3", "GND": "GND",
         "SCL": "I2C_SCL", "SDA": "I2C_SDA", "Alert": "PMON_ALERT", "A0": "GND", "A1": "GND"},
        "calc:MSOP-10_L3.0-W3.0-P0.50-LS5.0-BL", LCSC="C49851")
C(dbg, "100nF", "3V3")
R(dbg, "10", "VSYS_IN", "PMON_INP")
R(dbg, "10", "VSYS", "PMON_INN")
R(dbg, "4.7k", "I2C_SCL", "3V3")
R(dbg, "4.7k", "I2C_SDA", "3V3")
R(dbg, "10k", "PMON_ALERT", "3V3")
# Rail monitors: 100k/100k into ADC, 10 nF for the ADC sample cap. Switched rails only,
# so they draw nothing when the rail is off. (3V3 is measured via VREFINT; VSYS by the INA226.)
for rail, mon in (("3V3_BUS", "MON_3V3_BUS"), ("3V3_SD", "MON_3V3_SD")):
    R(dbg, "100k", rail, mon)
    R(dbg, "100k", mon, "GND")
    C(dbg, "10nF", mon)
# Debug header for the Raspberry Pi Debug Probe (0.1" cables): SWD + UART on one row.
dbg.add("Connector_Generic:Conn_01x06", "J", "DEBUG",
        {"1": "3V3", "2": "SWCLK", "3": "SWDIO", "4": "GND", "5": "UART_TX", "6": "UART_RX"},
        "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical", LCSC="C492405")
# Expansion / logic-analyzer header.
dbg.add("Connector_Generic:Conn_02x06_Odd_Even", "J", "EXPANSION",
        {"1": "3V3", "2": "GND", "3": "I2C_SCL", "4": "I2C_SDA", "5": "DBG_TX2", "6": "DBG_RX2",
         "7": "DBG_MCO", "8": "DBG_IO0", "9": "DBG_IO1", "10": "DBG_IO2", "11": "DBG_IO3", "12": "DBG_IO4"},
        "Connector_PinHeader_2.54mm:PinHeader_2x06_P2.54mm_Vertical", LCSC="C492423")
# Test points on every rail plus ground.
for net in ("VSYS", "VSYS_IN", "BAT_P", "VBUS", "3V3", "3V3_BUS", "3V3_SD", "VDDA", "GND", "GND"):
    dbg.add("Connector:TestPoint", "TP", net, {"1": net}, "TestPoint:TestPoint_Pad_D1.5mm")

if __name__ == "__main__":
    out = os.path.join(HERE, "../kicad")
    D.write(out)
    parts.save()
    n = sum(len(s.parts) for s in D.sheets)
    print(f"wrote {len(D.sheets)} sheets, {n} symbols to {os.path.relpath(out)}; "
          f"{len(D.global_nets)} global nets; unused MCU pins: {' '.join(unused)}")
