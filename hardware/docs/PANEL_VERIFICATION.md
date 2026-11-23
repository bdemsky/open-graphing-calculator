# Panel verification: ER-TFT032IPS-3.2 + ER-TPC032-3.1 against rev A

Date: 2026-10-20. Rev A is already ordered, so nothing here changes the fab outputs.

**Documents checked** (the vendors' PDFs, not included in this repository):

| File | What it is |
|---|---|
| `ER-TFT032IPS-3.2_Datasheet.pdf` | TFT datasheet, 24 pp. Still **Rev 1.0 "Preliminary Release", Jul-07-2023**: the "full set" is the same revision we designed from. It has no init code. |
| `ER-TPC032-3.1_Drawing.pdf` | Capacitive touch panel drawing (Rev A0, 2015): outline, coordinate origin, its own 6-pin tail |
| `ER-TP032-3_Drawing.pdf` | Resistive touch variant. Not used, and not checked in detail. |
| `ER-CON40HT-1.pdf` | EastRising's recommended 40-pin 0.5 mm top-contact FPC connector |
| `ST7789V2.pdf` | Sitronix ST7789V2 datasheet v1.0 (2016/11), 319 pp |
| `FT6236-FT6336-FT6436L-FT6436_Datasheet.pdf` | FocalTech FT6X36 datasheet v0.3 (18 pp) plus the FT6x06 CTPM application note (12 pp, register map) |

The JUSHUO AFC24 drawing and approval spec (LCSC C262686) were also fetched for the J3 comparison; they are not stored in the repo.

**Classes:** OK · FIRMWARE FIX (done, own commit) · BRING-UP CHECK (added to `firmware/README.md` → "First-board bring-up checklist") · HARDWARE PROBLEM.

## Summary

- **HARDWARE PROBLEM: none found.** Pinout, interface mode, power, backlight polarity, connector and fold geometry all agree with the datasheets.
- **FIRMWARE FIX (2):**
  1. LCD reads now keep RDX high ≥ 90 ns between reads (ST7789V2 TRDH). Commit `cf77c4d`.
  2. Touch I2C4 now runs at 100 kHz by default, because the FT6x36 bus-timing table is standard-mode. `tp speed fast` or `TOUCH_I2C_FAST=1` selects ~340 kHz. Commit `13e225f`.
- **BRING-UP CHECK (new or sharpened):**
  - FPC tail width and thickness, pin-1 continuity and fold reach on the first fit
  - MADCTL: expect 0xA0 if the panel is upright at MADCTL 0x00
  - RDDID 0x858552
  - CSX hold (TCSH): SWD fallback that ties CSX low
  - Gamma/VCOM tuning
  - 3V3_BUS level against the panel's 3.3 V max
  - Backlight LED count (5 or 6)
  - Touch: I2C address, speed comparison, map 5 (now documented)
  - Hibernate current

## 1. FPC pinout (J3) — OK

Datasheet §4.1 vs the J3 pad nets in `hardware/kicad/calc.kicad_pcb` (same as `fpc` in `gen/design.py`):

| Pin | Panel (datasheet §4.1) | J3 net | Result |
|---|---|---|---|
| 1 | XL/**IRQ** (CTP interrupt, low active) | TP_INT (PC1, 10k to 3V3_BUS) | OK |
| 2 | YU/**RST** (CTP reset, low active) | TP_RST (PA7, 10k to 3V3_BUS) | OK |
| 3 | XR/**SCL** | TP_SCL (I2C4 PD12, 4.7k) | OK |
| 4 | YD/**SDA** | TP_SDA (I2C4 PD13, 4.7k) | OK |
| 5 | GND | GND | OK |
| 6 | IOVCC "1.8/2.8 V" | 3V3_BUS | OK, see power below |
| 7 | VCI "2.8 V" | 3V3_BUS | OK, see power below |
| 8 | FMARK (TE) | LCD_TE (PG9) | OK |
| 9 | CS | LCD_CS (FMC_NE1, PD7) | OK |
| 10 | DC | LCD_DC (FMC_A16, PD11) | OK |
| 11 | WR | LCD_WR (FMC_NWE, PD5) | OK |
| 12 | RD | LCD_RD (FMC_NOE, PD4) | OK |
| 13 | SDI (serial only) | GND | OK (unused input tied low) |
| 14 | SDO (serial only) | NC | OK (output left open) |
| 15 | RESET (low active) | LCD_RESET (PD3, 100k pull-down) | OK |
| 16 | GND | GND | OK |
| 17–32 | DB0–DB15 | LCD_D0–LCD_D15 (1k from FMC_D0–D15) | OK, ascending as in the datasheet |
| 33 | LEDA | LCD_A (anode selector) | OK |
| 34–36 | LEDK | LCD_K (sink drain) | OK |
| 37 | GND | GND | OK |
| 38 | **IM0** | GND | OK |
| 39 | **IM1** | GND | OK |
| 40 | **IM2** | GND | OK |
| 41/42 | (connector tabs) | GND | OK |

- **Interface select.** The full datasheet shows pins 38–40 are IM0–IM2, not plain grounds. Note 1 says IM2..0 = 000 selects "8080 MCU 16-bit Bus Interface II": commands and registers on D[7:0], GRAM on D[15:0]. Tying them to GND is what we want, and the firmware writes commands on D7..0. There is no IM3 pin on this FPC. OK.
- **D0–D15 order.** The datasheet lists only "17–32 DB0–DB15". Ascending order is EastRising's convention and the only reading of that line. A reversal would put commands on D15..8, and the panel would not respond. Bring-up `lcd id` would show it. OK.
- **Touch pins.** The CTP's own 6-pin tail (drawing pin assignment /RESET, /INT, SDA, SCL, GND, VDD) is soldered onto the TFT FPC by EastRising and routed to pins 1–4 in the TFT datasheet's order (IRQ, RST, SCL, SDA), which is what we wired. The CTP's VDD comes from the panel's supply pins, both of which are 3V3_BUS here. OK.

### Pin 1 location and the fold — OK

- **Pin 1 position.** In the outline drawing's front view (§3.3, portrait, FPC at the bottom), the contact row is labelled "1" at the left end and "40" at the right end. The contacts are drawn on the front view, and the 5 mm stiffener is drawn on the back view, so the contacts are on the FPC's front face.
- **Fold.** Mounted landscape (rotated 90° clockwise, FPC on the left), the portrait-left end is north. The FPC folds 180° behind the panel about a north–south line. That keeps pin 1 north and turns the contacts to face the PCB.
- **J3 footprint.** Pad 1 is at local (+9.75, +1.61). Rotated 90°, it is at the north end (board y 30.77), with the signal pads east and the fitting nails west. The JUSHUO drawing puts the solder tails at the back-flip latch side and the FPC entry at the opposite (nail) side, so the opening faces west, toward the fold.
- **Contact positions.** The tail starts 17.27 mm from the panel edge, so pin 1 sits at 13 + 17.77 = 30.77 and pin 40 at 50.27. These are exactly J3's pad 1 and pad 40 positions. **Panel pin n → connector pad n holds.**
- **Contact side.** J3 has contacts on both sides, so the contact side is not critical. Even if it were bottom-only, contacts facing the PCB would be right.

## 2. Connector: ER-CON40HT-1 vs AFC24-S40FIA-00 — OK

| | ER-CON40HT-1 (EastRising) | AFC24-S40FIA-00 (J3) | Panel FPC |
|---|---|---|---|
| Pitch | 0.50 ± 0.05 | 0.50 ± 0.05 | 0.5, span 19.5 ± 0.05 |
| FPC thickness | 0.30 ± 0.02 | 0.30 ± 0.03 | not stated; 0.3 implied by EastRising's connector |
| FPC width | (n+1)·0.5 = 20.5 ± 0.05; slot B = 20.60 +0.10/−0 | 20.5 ± 0.05; slot B = 20.60 | **20.5 ± 0.2** |
| Contact length on FPC | 4.0 | ≥ 3.0 | 3.5 ± 0.2 (W 0.35) |
| Contact side | top only | double-sided ("F" type; JUSHUO spec: "products for the two-sided contact") | contacts on the front face |
| Lock | flip lid, top contact | easy-on back-flip, front insert | plain straight tail with stiffener, no ears |
| Height | 2.0 | 2.0 | |

- **Compatibility.** Our part takes the same 0.5 mm / 0.3 mm FPC. It accepts either contact orientation, so it works with the folded (contacts-down) FPC. EastRising's top-contact part would need the contacts facing up, which fits an unfolded FPC. **Compatible.**
- **Part-number decode.** The drawing's decode reads "F = Lower" contacts, while the approval spec and LCSC call the F variant double-sided. Either reading works for us, because our contacts face the PCB.
- **BRING-UP CHECK, tail width.** The FPC width tolerance (±0.2) is looser than both connectors' slots (20.6). A tail at 20.7 would bind in either connector. Measure the first panel's tail at the stiffener. If it is over 20.6 mm, shave the outer edges with a blade, keeping clear of pins 1 and 40 at 0.35 mm from the edge.

## 3. Mechanical — OK; fold reach is a BRING-UP CHECK

| Item | Datasheet | Our design | |
|---|---|---|---|
| CTP outline | 77.7 × 55.04, thickness 4.1 ± 0.2 (TFT alone 2.5) | `PANEL` 77.7 × 55.04, `PANEL_T` 4.1, clearance 0.35/side | OK |
| LCD outline | 77.5 × 55.04 | `PANEL_LCD_X0` 2.25 (0.1 inside the CTP) | OK |
| Active area | 64.8 × 48.6, 3.5 from the far edge, 3.22 top and bottom | AA from 9.4 (FPC side) / 3.22 | OK (±0.1 depending on LCD/CTP edge alignment; the window shows the whole VA, so this does not matter) |
| CTP view area | TFT datasheet: 65.95 × 49.2, 3.15 far edge; CTP drawing: 65.60 × 49.20, 3.15 / 8.95 | window from 8.6 (FPC side) / 2.92 | OK. The two EastRising documents differ by 0.35 mm at the FPC end; the larger value makes the window show at most 0.35 mm of the CTP's black border. Cosmetic only. |
| FPC wide section | 43 ± 0.3 wide, 6.5 from the portrait-left edge | `FPC_FOLD_Y` = y 19.5–62.5 (+1 mm relief each side) | OK |
| Tail | 20.5 ± 0.2 wide at 17.27 from the edge, 23.58 long; contacts 3.5; stiffener 5 ± 0.2 on the back | J3 pads 30.77–50.27 | OK |
| Total FPC reach | 41.5 ± 0.5 from the LCD edge (flat) | J3 at x 39.5, stop at ~38.0 | see below |

**Fold reach.**
- **Nominal.** The U-turn starts at x 2.25. The FPC leaves the glass 2.4–3.0 mm above the panel back, so the half-turn costs π·h/2 = 3.8–4.7 mm, and the dip into J3 costs 0.3–0.5 mm. Nominal end: 2.25 + 41.5 − 4.24 − 0.4 ≈ **x 39.1**. Full insertion needs ≥ ~38.0, so there is about 1.1 mm of margin. Extra length goes into the loop.
- **Worst case** (short FPC −0.5, panel +0.35 east, h = 3.0, dip 0.5): the end lands at ≈ 37.7, up to ~0.3 mm short of the stop. The 3.5 mm contacts still overlap the connector contacts, but a partly inserted FPC can lift when the latch closes. RSS of the tolerances gives ≈ 38.3, which is inside.
- **Clearance.** Nothing taller than an SOIC-8 (1.75 mm) sits under the folded FPC (≥ ~3.1 mm above the PCB, `PANEL_BACK` 4.37). Under the tail there are only flat test pads and 0402 parts. J3's latch keep-out is clear. OK.
- **BRING-UP CHECK, first fit.** Dry-fit before power-up: the tail must reach the stop with the latch closed and no tension on the fold. If it is short:
  1. Push the panel west in its pocket (0.35 mm of clearance per side) and crease the U-turn tighter.
  2. Last resort: a 0.5 mm 40-pin same-side FFC extension (two FPC connectors on a small adapter). This was not needed on paper.

## 4. Controller (ST7789V2) and init — OK, with one FIRMWARE FIX

**8080 write timing** (ST7789V2 table 4, VDDI 1.65–3.3 V). FMC mode A at ADDSET 5 / DATAST 15, with a 200 MHz kernel clock (180 MHz on rev Y):

| Parameter | Spec | Ours | |
|---|---|---|---|
| TWC write cycle | ≥ 66 ns | 105 / 117 ns | OK |
| TWRL / TWRH | ≥ 15 / 15 ns | 75 / 30 ns | OK |
| TDST data setup | ≥ 10 ns | ≥ 43 ns after the 1k RC | OK |
| TDHT data hold | ≥ 10 ns | ~11 ns (1 clk + 1k × panel C) | OK by design, measured by `lcdbus` |
| TAST / TAHT (DCX) | ≥ 0 / 10 ns | 25 ns / guarded by `cmd()` delays | OK |
| TCS (CS setup) | ≥ 15 ns | 25 ns | OK |
| **TCSH (CS hold after WR↑)** | ≥ 10 ns | **1 kernel clock = 5–5.6 ns** | **BRING-UP CHECK** |
| TRCS / TRCSFM, TRDL / TRDLFM, TRC / TRCFM | 45 / 355, 45 / 355, 160 / 450 ns | ADDSET 15 / DATAST 90: 450 ns low, ≥ 530 ns cycle | OK |
| **TRDH / TRDHFM (RDX high between reads)** | ≥ 90 ns | ADDSET 15 = 75 ns (field at its maximum) | **FIRMWARE FIX:** `rd()` in `lcd.c` adds a barrier + ≥ 25 ns after each read (RDDID, RAMRD test) |

**TCSH: BRING-UP CHECK.**
- The H743 FMC has no hold setting, so CSX rises 5 ns after WRX. That is the same limit as the data hold, which the 1k resistors fix for the data lines; CSX has no resistor. The panel latches on the WRX rising edge, so this is very likely harmless.
- `lcdbus` would show dropped writes. If they occur, the ST7789V2 power-sequence section allows "CSX ... permanently grounded". WRX, RDX and A16 only toggle for the LCD bank (the SDRAM uses its own SDNWE and has no A16), so CSX can be held low over SWD: PD7 to output low. The exact `openocd` command is in the checklist.

**Reset and power timing.**

| Requirement (ST7789V2) | Ours | |
|---|---|---|
| TRW reset low ≥ 10 µs | 50 ms (`lcd_init`); low during rail power-up (GPIO low + 100k) | OK |
| Wait ≥ 5 ms after RESX↑ before commands; ≥ 120 ms before SLPOUT if reset in Sleep Out | 150 ms | OK |
| After SLPOUT: 5 ms before the next command, 120 ms before SLPIN | 120 ms | OK |
| After SLPIN: 5 ms | 5 ms (`lcd_sleep`) | OK |
| VDD / VDDI in any order; RESX held during power-on | yes | OK |
| Power off in Sleep Out needs RESX low ≥ 120 ms before the rails drop; 0 ms in Sleep In | Standby path sends SLPIN first. Console `rail bus off force` does not (datasheet note 1: no damage) | OK |

**Init sequence vs the datasheet.**
- **Register defaults.** PORCTRL 0C/0C/00/33/33, GCTRL 35, LCMCTRL 2C, VDVVRHEN 01/FF, VDVS 20, FRCTRL2 0F (60 Hz) and PWCTRL1 A4/A1 equal the ST7789V2 power-on defaults in its tables. OK.
- **COLMOD and window.** COLMOD 0x55 (65K, one 16-bit write per pixel on the 16-bit bus) and CASET 0–319 / RASET 0–239 with MV = 1 are correct. OK.
- **INVON (0x21)** is sent, as an IPS panel needs. TEON with V-blank only is sent. OK.
- **RDDID default** is 0x85 / 0x85 / 0x52 in the V2 datasheet, which matches what the checklist expects. **BRING-UP CHECK:** note the value.
- **Panel tuning.** VCOMS 0x2B, VRHS 0x11 and both gamma tables come from Newhaven's ST7789 IPS example, because the EastRising datasheet has no recommended init code. **BRING-UP CHECK** (already listed): judge flicker, contrast and colour, and compare `LCD_PANEL_TUNING=0` and `lcd cmd bb ..`.

**MADCTL.**
- ST7789V2 fig. 33 shows that, of the four MV = 1 values, 0x20/0xE0 are 180° apart, and so are 0x60/0xA0; one pair is mirror images of the other. The default 0x20 assumed a panel that is upright at MADCTL 0x80 (true of the old Newhaven).
- If the ER panel is upright at 0x00 (the usual ST7789 module convention), FPC-left landscape needs **0xA0** (`LCD_ORIENTATION 3`), and 0x20 would show mirrored text.
- The documents do not settle which is true. **BRING-UP CHECK** with `pattern orient`. The checklist and the `board.h` comment now say to try 0xA0 first if 0x20 is mirrored.

**V2 vs V(I).** The firmware and docs said "ST7789V" or "ST7789VI". The commands used, their defaults, the 8080 AC table and the reset table are the same in the V2 datasheet; only the comments needed updating.

## 5. Power and backlight — OK

- **Logic supply.**
  - Datasheet §4.3: VCI 2.5–3.3 V (typ 2.8), VDDI 1.65–3.3 V, absolute maximum 4.6 V. The ST7789V2 is specified at VDD 2.4–3.3 V and VDDI 1.65–3.3 V.
  - We run both from 3V3_BUS: the TPS62162 gives 3.3 V ±3 % (PWM) or −3.5/+4 % (power-save), minus a few mV in the SY6280. That can sit up to ~0.1 V above the recommended maximum, but far below the absolute maximum, as is usual for 3.3 V ST7789 designs.
  - The FT6x36 is specified at VDDA 2.8–3.3 V (DS table 3-2; the AN says 2.8–3.6 V, absolute maximum 3.6 V), and the CTP drawing gives 2.8–3.3 V: the same situation as the panel.
  - **BRING-UP CHECK:** note the 3V3_BUS reading (`adc`).
- **Backlight.**
  - Datasheet §4.4: "5-chip parallel", Vf 3.2 typ / 3.4 V max at 100 mA, IF 90 typ / 100 max. The outline drawings' notes instead show **6** LEDs in parallel at "VF = 3.2 V, IF = 120 mA" (2.8–3.2 V on the no-touch drawing).
  - Our sink limits to 90 mA (clamp 100 mA), which is within both versions. Polarity is right: LEDA = pin 33 = anode supply, LEDK = pins 34–36 = sink.
  - The 3V3/VSYS anode selection handles the 3.2–3.4 V Vf as designed.
  - **BRING-UP CHECK:** the LED count only affects how far below its rating 90 mA runs. Do not raise `BL_LIMIT_MA` unless 6 LEDs / 120 mA is confirmed.
- **Brightness.** The datasheet now says 380 cd/m² typ (ARCHITECTURE.md §5 says 280). Documentation only.

## 6. Touch (FT6236) — OK, with one FIRMWARE FIX

| Item | FT6X36 datasheet / FT6x06 AN | Driver (`touch_focaltech.c`, `i2c.c`) | |
|---|---|---|---|
| Supply | VDDA 2.8–3.3 V (DS table 3-2; AN 2.8–3.6, abs. max 3.6); IOVCC 1.8–3.6 V | 3V3_BUS | OK (top of range, see §5) |
| Power-on | RST low before power-on; Tprt low ≥ 1 ms after power-on; Tris ≤ 3 ms | RST driven low before BUS_EN, released ≥ 5 ms after; rail rises in ~0.2 ms | OK |
| INT/I2C "advised low before power-on" | advisory | pins analog, pull-ups to the same rail (rise with it) | OK |
| Reset | Trst ≥ 5 ms (DS); ≥ 1 ms (AN) | 6 ms | OK |
| Start-up | Tpon / Trsi ≥ 300 ms | `READY_MS` 320 | OK |
| Power cycle | VDD < 0.3 V for ≥ 5 ms | `BUS_MIN_OFF_MS` 20 | OK |
| **I2C** | SCL 10–400 kHz, but tHD;STA / tSU;STO ≥ 4.0 µs, tBUF ≥ 4.7 µs, tSU;DAT ≥ 250 ns (standard-mode values) | was ~340 kHz (tHIGH 1.0 µs, setup ~100 ns) | **FIRMWARE FIX:** 100 kHz default (meets the table); `tp speed fast` / `TOUCH_I2C_FAST=1` for ~340 kHz. **BRING-UP CHECK:** compare `read_errors` at both speeds. |
| Address | not stated in either document | 0x38 | **BRING-UP CHECK:** `i2c scan 4` (already listed) |
| INT | trigger mode (default, G_MODE 0xA4 = 1): pulse per report; polling mode: low while touched. Wake from hibernate by a 0.5–1 ms INT low pulse or reset | falling-edge EXTI + 15 ms poll while down; works in both modes; wakes by reset | OK |
| Register map | 0x00 DEV_MODE, 0x01 GEST_ID, 0x02 TD_STATUS, 6 bytes per point from 0x03 (XH event 7:6, YH id 7:4), 0xA3 CIPHER, 0xA4 G_MODE, 0xA5 PWR_MODE, 0xA6 FIRMID, 0xA8 vendor (0x11) | same; GEST_ID not used (gestures in `core/touch.c`) | OK |
| Chip ID | AN: CIPHER default 0x06 (FT6206); FT6236 0x36 is not in these documents | 0x36 / 0x64 / 0x06 named, others accepted as 2-point | OK; note the value |
| Hibernate | "Hibernation mode", ~50 µA (sleep); the PWR_MODE value is not given | writes 0xA5 = 3 (FT5x06-style AN), wakes by reset | **BRING-UP CHECK:** `tp sleep` current drop (already listed) |
| Coordinates | CTP drawing front view: "0,0" top-left, "240,320" bottom-right, tail at the bottom (X 240, Y 320) | raw 240 × 320, default map 5 = SWAP_XY \| FLIP_X (screen_x = 319 − raw_y, screen_y = raw_x) | OK, now documented; `tp watch` confirms (checklist) |

## Notes for ARCHITECTURE.md (no fab impact; not edited here)

- §5: brightness is 380 cd/m², not 280. The backlight is "5 LEDs" in the datasheet table but 6 LEDs at 120 mA in the outline-drawing note.
- §5: pins 38–40 are IM0–IM2, tied low, which is the "IM2..0 hard-tied to GND" already described.
- §4/§5: the controller is the ST7789V2 according to EastRising's document set ("ST7789VI" in the memory table).
- §5: the folded FPC's end runs to x ≈ 39 (the tail itself is 23.58 mm long), which matches the layout comment; the "~40 mm in" wording refers to the whole folded FPC.
- §13 "Open, needs hardware: AFC24 FPC connector pin-1 geometry": resolved on paper (see §1 above).
