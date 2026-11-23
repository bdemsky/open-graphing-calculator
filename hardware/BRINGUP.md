# First power-on and bring-up: graphing calculator rev A

A step-by-step guide for bringing up an assembled rev A board on your own. Each step says what to do, what to
measure or watch, the expected result, and what to do when it is wrong. Work through the phases in order and
do not skip a failed step.

Sources: [`ARCHITECTURE.md`](ARCHITECTURE.md), [`ORDER.md`](ORDER.md), [`pinmap/pinmap.md`](pinmap/pinmap.md),
[`docs/PANEL_VERIFICATION.md`](docs/PANEL_VERIFICATION.md), the schematic generator `gen/design.py` (with the
reference designators in `gen/refs.json`), the parts' datasheets (download them by the LCSC numbers in the BOM),
[`../firmware/release/FLASHING.md`](../firmware/release/FLASHING.md) and the firmware README's "First-board
bring-up checklist" ([`../firmware/README.md`](../firmware/README.md)). Values marked **(estimate)** are not in
any datasheet or document. Treat them as rough guides, and record what your board actually measures.

---

## What arrives assembled, and what you solder

**JLC assembles every part on the board. Nothing has to be hand-soldered.**

- `fab/hand_parts.txt` is empty (only its header line).
- Every one of the board's 210 placements is in `fab/calc-bom.csv` and `fab/calc-cpl.csv`.
- Assembly is two-sided:
  - **Bottom:** the battery holders **BT1/BT2** (BH-AAA-B5AA001, C964881), the MCU and SDRAM decoupling capacitors C10–C20, C22–C27, C32–C38, C57 and C58 under U1/U2, and the ferrite bead **FB1** (29 placements).
  - **Top:** everything else, including the three through-hole headers **J1** (SWD 2×5, 1.27 mm), **J5** (DEBUG 1×6, 2.54 mm) and **J6** (EXPANSION 2×6, 2.54 mm). JLC fits these in a through-hole step.
- There is no DNP list. The 10 test pads TP1–TP10 and the 5 mounting holes are bare copper and have no part.

Two things to confirm on arrival (Phase 0):

- `ORDER.md` warns that JLC may treat the battery holders as a manual or glued bottom-side step. Check that all 8 holder pads are soldered.
- Check that J1, J5 and J6 are actually there. If the through-hole step was dropped, J5 is an easy 6-pin 2.54 mm header to solder yourself (LCSC C492405, or any 1×6 0.1" pin strip).

**Not on the board:**

- **The display.** It is the EastRising/BuyDisplay **ER-TFT032IPS-3.2 with the ER-TPC032-3.1 capacitive touch panel**. One 40-pin, 0.5 mm FPC carries the TFT and the FT6236 touch I2C.
  - There is no separate touch connector on rev A. The old Newhaven 2.8" panel's 6-pin touch tail, its Molex connector and J7 were removed when the panel changed on 2026-10-19.
  - **A Newhaven NHD-2.8-240320AF-CSXP-F must not be plugged into J3**: its 40-pin pinout is different. Its pin 1 is GND where J3 pin 1 is TP_INT, and its LED-A on pin 38 would land on a grounded IM pin.
- **The batteries.** No charger is fitted: use alkaline or NiMH AAA cells.

---

## What can be fixed, and by whom

- **With a soldering iron (the scope of Phase 0).** Parts with few pins, from `fab/calc-bom.csv`:
  - **2-terminal passives:**
    - 0402 resistors and capacitors: most of the R and C, including the bottom-side decoupling C10–C20, C22–C27, C32–C38, C57 and C58. They are small, but workable with a fine tip and tweezers. There are no 0201 parts on the board.
    - 0603: C6, C8, C21, C39, C41, C43, C48, C55, C56; R1, R6, R8, R48, R65, R66; FB1 (bottom); D2 (LED).
    - 0805: C1, C2, C4, C7, C54.
    - Others: L1 (1008), R5 (2512 shunt), D1 (SMA diode), Y2 (3215 2-pad crystal).
  - **SOT parts:** Q1–Q12 (SOT-23); U5, U6 (SY6280) and U7 (TLV9001), all SOT-23-5; U8 (USBLC6) and U10 (TLV3012), both SOT-23-6.
  - **Connectors and mechanical:** the through-hole headers J1, J5, J6; the battery holders BT1/BT2; the tactile switches SW1–SW51 and SW61; the J2 (microSD) contacts and the J2/J4 shell legs.
  - **Hard for an iron, despite few pins:** U4 TPS62162 (WSON-8, 2 × 2 mm, exposed pad: hot air); Y1 (3225 crystal, pads underneath: hot air); the J4 USB-C signal pins (fine pitch, partly under the body).
- **Hot air or a rework shop:** U9 (MSOP-10), U3 (SOIC-8), the 40-pin 0.5 mm FPC connector J3, and the parts flagged above.
- **Not fixable at home:** U1 (LQFP-144), U2 (TSOP-54 SDRAM).

Phase 0 does not hunt for defects in the many-pin ICs. If one is noticed anyway, or power-up fails:

1. **Photograph it.**
2. **File a claim** through the JLC order's support or ticket page promptly, with the photos and the designator. The repo does not record JLC's claim window. Don't modify the board before the claim.
3. **Learn what you can.** An unfixable defect makes the board a write-off either way, so it is fine to power it current-limited and watched (Phase 3) to see what else works. Stop if anything heats or smokes.
4. **Rule out a design error.** A wrong orientation that comes from our own CPL or footprint would be on every board.

**Board order:** bring up board 1 first. Keep the others unpowered until board 1 has passed Phase 3, or until the cause of any failure is understood. A bad board is still useful as a reference and a parts donor.

## Running with a broken optional part

A defect in an optional part need not stop a working calculator. What the firmware does when each part is
missing or dead, from the source:

| Part | Without it |
|---|---|
| **Touch** (FT6236 on the panel FPC) | `touch_focaltech.c`: if the probe gets no answer, touch is disabled, it is logged, and the keys keep working; `tp init` retries. The calculator is fully usable from the keypad. |
| **INA226** (U9) | `ina226_present()` is false: the readings are NaN, About › Devices says "not found (no current readings)", and the `ina226` command errors. The low-battery cutoff still works from VBAT_SENSE through the ADC (`power.c`); only the fast under-voltage alert backstop is lost. |
| **microSD** (J2/U6) | The card is only a library and backup target. Without it, the screens say "microSD: no card". Content can still be installed over the console (`calcctl.py --upload-module`) or with OpenOCD (`flash_qspi.sh`). |
| **QSPI flash** (U3) | Flash `release/calc_core.elf`: the rollback tier keeps the manual and CAS in internal flash and needs no QSPI content. With a dead chip, the saved session (whose log lives in the QSPI) and the code modules (Sparkle Town, Animation) are lost. |
| **SDRAM** (U2) | The firmware falls back to a 320 KB AXI-SRAM arena and a D2-SRAM framebuffer ("small memory mode", About › Devices). Basic use works, with less memory for the CAS. |
| **VBUS comparator** (U10) | Pure hardware, no firmware involvement. It is powered from VBUS, so it only matters while USB is plugged in. If it is dead (output low), the battery switch stays closed on USB. The cells and USB then share the load through the diode-OR: fresh cells out-vote USB and drain while plugged in, but nothing resets on unplug. Battery-only use is unaffected. `power` still reports `usb=` from its own divider (PA2). |
| **Expansion header** (J6) | Debug and measurement only (I2C2, USART6, MCO, timing markers); nothing depends on it. |
| **HSE crystal** (Y1) | The clocks fall back to the 64 MHz HSI (`HSE FAILED` in the boot log); everything runs. LSE (Y2) failing only loses the RTC-timed wake-ups. |

---

## Protecting your computer

The board's USB-C port does not touch a computer until the board has passed some standalone checks. The rule:

> **Power the board from a current-limited bench supply until its rails are verified (Phases 2–3). Before the
> board's USB-C first goes to a computer, do the USB connector checks in Phase 2.7 and the back-feed check in
> Phase 3.6. They take two minutes and catch the real design-error case. Then make the first connection
> through a USB hub, and go direct once the board has behaved.**

Why this is enough on this board (from `gen/design.py`):

- **The board's USB-C is a sink only.** Its parts:
  - CC1 and CC2 each have a 5.1 kΩ Rd to GND (R49, R50).
  - VBUS enters through the M7 silicon diode D1 (anode on VBUS, cathode on VSYS_IN). The board's own supply cannot drive VBUS through it.
  - Everything else on VBUS is a load: the TLV3012 comparator U10, powered *from* VBUS; the dividers R3/R6 and R51/R52; C48; and the USBLC6 ESD array U8.
  - So VBUS reads about 0 V while the board runs from the bench supply. Phase 3.6 checks it.
- **The USB is Full Speed** (USB_OTG_FS, PA11/PA12, 12 Mb/s).
- **The board's USB is not needed for most of bring-up.**
  - The firmware has no USB device stack yet. The console is the UART on the DEBUG header, read through the debug probe.
  - USB-C is only used for USB power (Phase 10.5) and for the optional ROM DFU fallback (Phase 10.7).
- **The first USB connection goes through a hub.**
  - A powered hub running from its own adapter is the best buffer. An unpowered (bus-powered) hub still adds some separation, and it is a cheaper part to sacrifice.
  - From a USB-A hub port, use a USB-A to USB-C cable. Such cables carry a 56 kΩ pull-up on CC, so the board's Rd sink detection works. A USB 3.0 hub is fine for this USB 2.0 full-speed device. If the hub has a power adapter, use it.
  - A USB isolator (ADuM4160 class, which suits full speed) or an inline USB-C power meter is optional extra caution.

**The debug probe** (a Raspberry Pi Pico 2) can be plugged into the computer directly, or through the hub. Three free precautions:

- Connect it to the board only after Phase 3 has passed.
- Connect **GND first**.
- **Never connect the Pico's 3V3(OUT), VSYS or VBUS to the board**: J5 pin 1 (3V3) stays unconnected.

The board's I/O is 3.3 V: every MCU pin, SWD and UART included, runs from the TPS62162's fixed 3.3 V rail (at most about 3.43 V), which matches the Pico's 3.3 V GPIO.

Optional: 100–330 Ω in series with SWCLK and SWDIO.

- At the config's 4 MHz SWD clock this is harmless: an RC of a few ns against a 125 ns half-period (estimate).
- It limits current if only one side is powered.
- Either way, power the Pico before the board, and power the board off before unplugging the Pico.

---

## Quick reference

**Test pads** (bare 1.5 mm pads, front side, silkscreen TPn). Positions are measured from the board's top-left
corner, with the USB-C and header edge at the top. **They sit under the display**, so measure them before
fitting the panel, or lift it.

| Pad | Net | Approx. position (x, y) mm | Expected, running |
|---|---|---|---|
| TP1 | VSYS | 16, 39 | ≈ VSYS_IN (minus I × 0.1 Ω) |
| TP2 | VSYS_IN | 19, 39 | bench supply on the battery input: supply minus the battery-path drop (≈ 0.1 V at 0.3 A, `firmware/README.md`); USB: VBUS minus the M7 drop (≈ 4.3 V) |
| TP3 | BAT_P | 16.7, 42 | battery or bench voltage (after the reverse-protection FET Q1); 0 V with no source |
| TP4 | VBUS | 13, 39.8 | 0 V without USB (back-feed check); 5 V on USB |
| TP5 | 3V3 | 15.6, 36 | 3.20–3.40 V (TPS62162 ±3 %; −3.5/+4 % at light load) |
| TP6 | 3V3_BUS | 18.6, 35.9 | 0 V until `rail bus on`, then ≈ 3V3 |
| TP7 | 3V3_SD | 78, 44 | 0 V until the card is powered, then ≈ 3V3 |
| TP8 | VDDA | 78.7, 47 | ≈ 3V3 (through ferrite FB1) |
| TP9, TP10 | GND | 13.6, 42.8 / 12.2, 36.6 | — |

**Other named points:**

- **Buttons:** SW1 RESET (NRST to GND) and SW2 BOOT (BOOT0 to 3V3), both at the top left. SW61 is the ON key (KEY_ON, PA0).
- **Bring-up LED:** D2 (red), on PB8.
- **Power path:** BOOT0 has a 10 kΩ pull-down (R14). BUS_EN and SD_EN have 100 kΩ pull-downs (R7, R9). Shunt R5 is 0.1 Ω (VSYS_IN → VSYS). Buck U4 (TPS62162) with L1 2.2 µH. Load switches U5 (3V3_BUS) and U6 (3V3_SD) are SY6280, with Rset 13 kΩ (R66, R8), so Ilim = 6800/13k ≈ 0.52 A.
- **Battery input:** BT2 pad 3 = pack + (BAT_RAW) and BT1 pad 2 = pack − (GND). The two holders are wired in series on the PCB. Q1 (AO3401A, gate to GND) is the reverse-polarity FET between BAT_RAW and BAT_P.

**Headers:**

| J5 DEBUG (1×6, 2.54 mm; pin 1 = square pad) | J1 SWD (2×5, 1.27 mm, ARM 10-pin) | J6 EXPANSION (2×6, 2.54 mm) |
|---|---|---|
| 1 3V3 · 2 SWCLK (PA14) · 3 SWDIO (PA13) · 4 GND · 5 UART_TX (PA9, MCU out) · 6 UART_RX (PA10, MCU in) | 1 3V3 (VTref) · 2 SWDIO · 3 GND · 4 SWCLK · 5 GND · 6 SWO (PB3) · 9 GND · 10 NRST | 1 3V3 · 2 GND · 3 I2C_SCL · 4 I2C_SDA · 5 TX2 (PC6) · 6 RX2 (PC7) · 7 MCO (PA8) · 8–12 DBG_IO0–4 |

**Console:** USART1, 115200 8N1, on J5. Every command ends with `OK` or `ERR ...` (`firmware/README.md`, "Console protocol").

**Optional helper**: `hardware/bringup/bringup.py`. It needs only the Python standard library.

- It wraps the OpenOCD and console steps below with timeouts, and logs everything to `firmware/build/bringup-logs/`.
- Subcommands: `preflight`, `ports`, `probe`, `id`, `flash FILE`, `reset`, `log SECONDS [--reset]`, `cmd 'CMD' ...` and `keys SECONDS`.
- Everything it does can also be done by hand with the commands given in each step.
- `hardware/bringup/assembly_views.py` (run it with the repo's `.venv/bin/python`, which has PyMuPDF; it needs `kicad-cli`) renders reference views of both sides for the inspection.

---

## Phase −1: Workstation preflight (before the boards arrive)

| # | Do | Expect | If wrong |
|---|---|---|---|
| −1.1 | Install the tools: OpenOCD 0.12 (`sudo port install openocd`; the scripts use `/opt/local/bin/openocd`, override with `$OPENOCD`), the Arm GNU Toolchain (the scripts look in `/Applications/ArmGNUToolchain/15.3.rel2/`), Python ≥ 3.8. Optional: `dfu-util`, `stm32flash`. | `python3 hardware/bringup/bringup.py preflight` prints `PREFLIGHT PASS` | install what it lists as MISSING |
| −1.2 | **Check that the release images match the source.** `bringup.py preflight` reads the source revision from `release/MANIFEST.txt` and prints how many commits behind HEAD it is. If it is behind, or `calc_qspi.bin`, `calc_core.*` or `MODULES/` are missing, run `cd firmware && make release`. | `release/MANIFEST.txt` ends with `checks: PASS`; `release/MODULES/` holds `MANUA010.MOD`, `ANIMA010.MOD` and `INDEX.TXT` (names follow the module versions) | Fix the build first. Never bring up a board with images that do not match the source. |
| −1.3 | Build the Sparkle Town module (it is not part of the release): `make mathquest-module`. | `build/modules/MATHQ010.MOD` (version from `CONFIG_MOD_MATHQUEST_VERSION`, default 0.1.0) | — |
| −1.4 | Verify the checksums: `cd release && shasum -a 256 -c SHA256SUMS`. | every file `OK` | rebuild |

## Phase −0.5 (optional): Review JLC's production photos (before the boards ship)

JLC often posts photos of the assembled board on the order page around the SMT/QC step, especially when
photo confirmation was chosen. If photos are there, a quick look is worthwhile: a problem caught before shipping is fixed by JLC for free.

| # | Do | Expect | If wrong |
|---|---|---|---|
| −0.5.1 | Download every production photo from the order page (top and bottom). | — | No photos: ask JLC support for them. |
| −0.5.2 | Make our reference views to compare against: `.venv/bin/python hardware/bringup/assembly_views.py`. It writes, to `firmware/build/assembly-views/`, a pads-and-silkscreen view of each side (the bottom mirrored, as seen from below) with designators and pin-1/polarity marks, close-up crops by region, and KiCad 3D renders. The rotations are in `hardware/fab/calc-cpl.csv`. | — | — |
| −0.5.3 | Check, part by part: **the pin 1 of every IC**: U1 (dot; the CPL says 270°), U2, U3, U4, U9, U5–U8, U10. Q1–Q12 (SOT-23: one pin on one side, two on the other, matching the footprint). The D1 cathode band. The D2 LED. J3 opening west. **BT1/BT2** rotation and +/− marks on the bottom. Bottom caps and FB1 present. No missing parts. | everything matches the views | Reply on the order before it ships, quoting the designator and the expected orientation. If the error is in *our* CPL or footprint, it affects every board: say so, and ask JLC to rotate the part in their placement. |

## Phase 0: Unboxing and inspection for hand-fixable defects (unpowered)

Look only for what you can fix with a soldering iron (see "What can be fixed, and by whom"). Use good light and
a loupe, and start with board 1. The reference views from `hardware/bringup/assembly_views.py` (see −0.5.2) show
where each designator sits and its polarity mark.

| # | Do | Expect | If wrong |
|---|---|---|---|
| 0.1 | **Headers J1** (2×5, 1.27 mm), **J5** (1×6) and **J6** (2×6): present, all pins soldered on the bottom, no bridges between neighbouring pins | present and soldered | Missing (the through-hole step was dropped): solder a 1×6 0.1" header into J5 (only J5 is needed for bring-up). Bridges or opens: reflow with the iron. |
| 0.2 | **Battery holders BT1/BT2** (bottom): all 8 pads soldered (`ORDER.md`: they may have been a manual step), and the holder's moulded +/− marks match the silkscreen. The pack ends are BT2 pad 3 (+) and BT1 pad 2 (−). | soldered; marks match | Unsoldered pad: solder it. Marks reversed: the pack would be reversed. Q1 then blocks it and nothing is damaged, but photograph it and claim (or re-fit the holder) before using cells. |
| 0.3 | **2-terminal parts and SOT parts** (the list in "What can be fixed", both sides). For each: present; not tombstoned (standing on one end), lifted, or bridged to a neighbour. Polarised parts: D1's cathode band toward VSYS_IN (away from the USB-C), D2's cathode mark as on the silkscreen; SOT-23 Q1–Q12 sitting on their footprint (one pin on one side, two on the other); the pin-1 dot of U5–U8 and U10 matching the silkscreen. All the capacitors are ceramic: there are no tantalums or electrolytics. | all present, flat, oriented | Missing, tombstoned, lifted or bridged: fix with the iron (flux, tweezers). A reversed D1/D2 or SOT part: if it is JLC's error, photograph it and claim first (see above), otherwise turn it with the iron. |
| 0.3b | **Large pins:** the J4 USB-C and J2 microSD shell legs soldered; J2's contacts unbridged; no visible bridge on J4's signal pins; the 52 key switches sitting flat with all 4 legs soldered | as described | Reflow with the iron. A J4 signal-pin bridge needs flux and wick, or hot air. USB is optional, and Phase 2.7 will catch a short. |
| 0.4 | **J3** (FPC connector): the latch moves freely; no bent contacts visible | — | Bent contacts or a broken latch: rework shop. The board can still be brought up without the display, up to Phase 5. |
| 0.5 | **The panel** (it may arrive before the boards; do this on arrival). Part: ER-TFT032IPS-3.2 with ER-TPC032-3.1. FPC: **40 pins at 0.5 mm, contacts on the front face** (the stiffener is on the back), **pin 1 at the portrait-left end** of the contact row (the outline drawing's front view: "1 … 40"). Measure the **tail width at the stiffener: it must be ≤ 20.6 mm** (drawing 20.5 ± 0.2; the connector slot is 20.6). Thickness ≈ 0.3 mm. | as described | Over 20.6 mm: shave the outer edges with a blade, staying 0.35 mm clear of pins 1 and 40 (`PANEL_VERIFICATION.md` §2). If it is a different panel or FPC, stop: rev A's J3 pinout matches only this panel. |

**Go/no-go:** go to Phase 1 when 0.1–0.3b are fixed. Anything else noticed along the way follows "What can be
fixed, and by whom" (photos, claim). The current-limited first power-up in Phase 3 is the safety net for
whatever an inspection misses.

## Phase 0b: Turn the Pico 2 into a debug probe

The repo's OpenOCD configs are written for a CMSIS-DAP probe (`openocd/calc-cmsisdap.cfg`: `interface/cmsis-dap.cfg`,
SWD, 4 MHz, `reset_config none`). Any Raspberry Pi Pico 2 running the official **debugprobe** firmware is one.

| # | Do | Expect | If wrong |
|---|---|---|---|
| 0b.1 | Download **`debugprobe_on_pico2.uf2`** from the GitHub releases of `raspberrypi/debugprobe`. (The plain-Pico build is `debugprobe_on_pico.uf2`; the official Debug Probe's is `debugprobe.uf2`; do not mix them up.) | — | — |
| 0b.2 | Hold **BOOTSEL** while plugging the Pico 2 into the computer's USB. Drag the UF2 onto the **RP2350** drive that appears. | the drive disappears and the Pico re-enumerates; its LED (GP25) lights when USB is connected | Use a USB cable with data lines. Retry while holding BOOTSEL. |
| 0b.3 | Check that it is a CMSIS-DAP probe: `python3 hardware/bringup/bringup.py probe` (or `/opt/local/bin/openocd -f firmware/openocd/calc-cmsisdap.cfg -c "init; shutdown"` and look for `CMSIS-DAP: FW Version`). `ls /dev/cu.usbmodem*` shows its UART bridge. | `probe: CMSIS-DAP found` (the target is missing, which is normal here); one `/dev/cu.usbmodem…` port | Check `bringup.py ports` (the USB product name should be "Debugprobe on Pico (CMSIS-DAP)"). Re-flash. |
| 0b.4 | Prepare five female–female jumper wires, ≤ 15 cm (optionally with 100–330 Ω in series in SWCLK and SWDIO). They are wired to the board in Phase 4. | — | — |

**Pico 2 debugprobe pins.** These were checked against `include/board_pico_config.h` in `raspberrypi/debugprobe`
(master, fetched 2026-10-24). That header serves both `RASPBERRYPI_PICO` and `RASPBERRYPI_PICO2` builds and defines
SWCLK = GP2, SWDIO = GP3, RESET = GP1, UART TX = GP4, UART RX = GP5 (uart1, 115200). The Pico pin numbers are the
standard Pico/Pico 2 header pinout.

| Pico 2 pin | GPIO (debugprobe function) | via | Board J5 pin |
|---|---|---|---|
| 3 (or any GND) | GND | direct, **connect first** | 4 GND |
| 4 | GP2 = SWCLK | wire (opt. 220 Ω) | 2 SWCLK (PA14) |
| 5 | GP3 = SWDIO | wire (opt. 220 Ω) | 3 SWDIO (PA13) |
| 7 | GP5 = UART RX (probe input) | wire | 5 UART_TX (PA9, MCU output) |
| 6 | GP4 = UART TX (probe output) | wire | 6 UART_RX (PA10, MCU input) |
| — | 3V3(OUT), VSYS, VBUS | **never connected** | 1 3V3: leave open |
| 2 | GP1 = target reset (optional) | wire | NRST: J1 pin 10, or the NRST side of SW1. The repo config uses `reset_config none` (SYSRESETREQ), so this wire is not needed. |

## Phase 1: Hand soldering

**Nothing is required.** Optional:

- J5, if JLC did not fit it (see the top of this guide).
- A small adapter with the optional series resistors (Phase 0b.4).
- Short wires to two TPs for a permanent meter hookup.

Do not change any part values.

## Phase 2: Unpowered resistance checks

All with no power, no probe, no cells, no card and no panel. Use the meter's resistance range, red lead on the net
and black on TP9 (GND). Caps make readings creep upward. Read the value after a few seconds.

**A reading below about 10 Ω from any rail to GND is a short: stop and find it before powering.**

| # | Between | Expected | Notes |
|---|---|---|---|
| 2.1 | TP4 VBUS – GND | **≈ 98 kΩ** (R3+R6 = 282 kΩ in parallel with R51+R52 = 151 kΩ); possibly somewhat lower through U8 and U10 | Under 1 kΩ: check U8 (USBLC6), U10 (TLV3012) and C48. |
| 2.2 | TP2 VSYS_IN – GND, TP1 VSYS – GND | high, rising as C1/C7/C54 (22 µF) and C2 charge; **> 10 kΩ (estimate)** | VSYS feeds the buck U4 and the INA226 bus input; neither has a DC path to GND. |
| 2.3 | TP1 – TP2 (across the shunt R5) | **≈ 0 Ω** (0.1 Ω, below most meters' resolution) | open = R5 not soldered |
| 2.4 | TP3 BAT_P – GND | high (MΩ range) **(estimate)** | No resistor from BAT_P to GND (R2 and R10 go to FET terminals). |
| 2.5 | TP5 3V3 – GND, TP8 VDDA – GND | ESD and core loads of an unpowered MCU, flash and INA226; **typically kΩ to tens of kΩ, rising (estimate)** | **TP5 – TP8 ≈ 0 Ω** (ferrite FB1). A few Ω on 3V3 is a short: look for bridges on U1, U3 and U9 and at the 0402 decoupling caps. |
| 2.6 | TP6 3V3_BUS – GND; TP7 3V3_SD – GND | **≤ 200 kΩ** each: the monitor divider R57+R58 (or R59+R60) in parallel with the SDRAM/LCD-side loads, or the 47 kΩ SD pull-ups into the MCU; **> 1 kΩ (estimate)** | 3V3_BUS low: U2 bridges, C6/C39/C43/C44/C52. |
| 2.7 | **USB-C connector checks** (easiest with a cheap USB-C male breakout plugged into J4; without one, use fine probe tips on J4's pads, or on R49/R50 for CC): VBUS–GND ≈ 98 kΩ (as 2.1); **CC1–GND ≈ 5.1 kΩ; CC2–GND ≈ 5.1 kΩ** (R49, R50); CC1–CC2 ≈ 10.2 kΩ; **D+ and D− not shorted** to each other, to VBUS or to GND (diode mode may show a ≈ 0.5–0.7 V junction drop one way through U8; that is fine). Flip the plug and repeat (CC1 and CC2 swap). | as listed | CC open: R49/R50 or J4 pads. A D+/D− short: J4 bridges (A6/A7, B6/B7) or U8. |
| 2.8 | Battery input: BT2 pad 3 + contact (BAT_RAW) to BT1 pad 2 − contact (GND) | high (open) | The pack is in series over the PCB: BT1 pad 1 ↔ pad 4 and BT2 pad 1 ↔ pad 4 should beep (holder internal and the series traces). |
| 2.9 | Diode mode, red on BAT_RAW (BT2 + contact), black on TP3 BAT_P | ≈ 0.5–0.8 V (the body diode of Q1) **(estimate)**; open in reverse | open both ways: Q1 or its pads; ≈ 0 both ways: Q1 shorted (reverse protection lost) |
| 2.10 | SW2 BOOT0 pad – GND | ≈ 10 kΩ (R14) | — |

## Phase 3: First power-up (bench supply on the battery input)

**Where:** the battery input, so that the supply goes through the same path as the cells (Q1 reverse
protection → Q2/Q3 battery switch → VSYS_IN → shunt R5 → VSYS → buck), and the INA226 sees all of it.

- Clip **+** to BT2's + contact (pad 3, BAT_RAW) and **−** to BT1's − contact (pad 2), or to TP9/TP10 GND.
- **Not to VBUS:** the parts in front of D1 (U8, U10, C48) have no reverse protection, and a USB source is not needed.
- **Not to TP2 VSYS_IN directly:** that bypasses Q1.

**Polarity:**

- The battery input is protected: Q1 is a P-FET with its gate on GND. A reversed supply only sees Q1 off and the board does nothing. Still, check the clip polarity twice.
- VBUS (before D1) and the TPs are **not** protected.

**Voltage:**

- The board's range is the 4×AAA pack: **3.6–6.4 V** (fresh alkaline ≈ 1.6 V per cell; the firmware's empty point is 3.6 V; NiMH sits at about 4.8–5.0 V).
- The TPS62162 itself accepts 3–17 V. Its UVLO is 2.6–2.82 V falling, with 180 mV hysteresis.
- The battery divider is sized for 6.4 V max (→ 2.05 V at the ADC).
- **First power: 4.8 V.**
- **Never exceed 6.5 V** on this board.

**Current limit and the expected currents.**

- Start at **50 mA**. A blank MCU plus the buck's quiescent current needs far less, so a short trips the limit before anything heats.
- Input current ≈ I(3V3) × 3.3 V / (V_in × η), with buck efficiency η ≈ 0.85–0.9 **(estimate)**.

| Stage | 3V3 load | Supply current at 4.8 V | Limit to use |
|---|---|---|---|
| Blank MCU (factory-empty flash, HSI, no firmware), rails off | MCU stalled on an empty vector table, QSPI idle, INA226 ≈ 0.3 mA, buck 17 µA Iq | **≈ 5–25 mA (estimate)** | 50 → 100 mA |
| Bring-up firmware, 400 MHz, 3V3_BUS off | MCU running + caches **(estimate ≈ 100–200 mA at 3.3 V)** | **≈ 80–160 mA (estimate)**; the firmware then measures it (`ina226`) | 250 mA |
| + `rail bus on`: SDRAM idle, LCD logic, touch, 20 mA backlight | + tens of mA (README step 6) + 20 mA | + 30–70 mA **(estimate)** | 400 mA |
| Calculator, computing, backlight 40 mA | ≈ 220 mA at 3V3 (`ARCHITECTURE.md` §6) | ≈ 170 mA **(estimate)**, plus up to 90 mA more if the backlight runs from VSYS | 400 mA |
| Board worst case | — | < 450 mA (INA226 calibration note, `i2c.c`) | never above 600 mA |

| # | Do | Measure / observe | Expected | If wrong |
|---|---|---|---|---|
| 3.1 | Supply output off: set 0 V, limit 50 mA. Connect as above. Turn on and **raise slowly to 3.0 V**, watching the current. | supply current | It may sit at the limit for a moment while the ≈ 100 µF of rail caps charge, then fall to a few mA | Stays at the limit: **turn off**, go back to Phase 2 (a short). |
| 3.2 | Raise to **4.8 V**. | supply V and I; touch-test U1, U4, L1, Q1–Q3 and U9 with a fingertip | **≈ 5–25 mA (estimate)**; nothing warm | Limit hit, > 50 mA, or anything warm: off. Find the warm part (at 3.0 V / 50 mA) and look at it and its neighbours for a bridge or a reversed part. Photograph it, and follow "What can be fixed, and by whom". |
| 3.3 | Rail voltages, black lead on TP9. | TP3 BAT_P, TP2 VSYS_IN, TP1 VSYS | BAT_P ≈ 4.8 V; VSYS_IN ≈ VSYS ≈ 4.8 V less a few mV | VSYS_IN ≈ 0 V while BAT_P is OK: the battery switch Q2/Q3 is not on. Check Q4 (gate BATSW_EN should equal BAT_P via R2 47k), Q5 (gate VBUS_DET ≈ 0 V) and R1 2M. |
| 3.4 | | TP5 3V3, TP8 VDDA | **3.20–3.40 V** (TPS62162 ±3 % in PWM; −3.5/+4 % at light load: 3.18–3.43 V) | 0 V: U4 (EN = VSYS, check its pins), L1. Above 3.45 V: stop (FB/VOS). VDDA ≠ 3V3: FB1. |
| 3.5 | | TP6 3V3_BUS, TP7 3V3_SD | **≈ 0 V** (BUS_EN and SD_EN held low by R7/R9 100k; the SY6280 discharges its output) | Near 3.3 V: U5/U6 EN or pull-down problem. Note it and carry on (the firmware drives them), but find it before Phase 6. |
| 3.6 | **Back-feed check:** VBUS at TP4, and at the USB-C breakout's VBUS pad. | V | **≈ 0 V (< 0.1 V, estimate)**: the board never drives VBUS | Any real voltage: stop, and **do not connect USB to anything**. Check D1's orientation (cathode band toward VSYS_IN) and whether D1 is shorted. |
| 3.7 | Optional scope: L1 switch node and the 3V3 ripple at C4 | — | clean switching; ripple of tens of mV **(estimate)** | — |
| 3.8 | Raise the limit to **250 mA** for Phase 4/5 (the firmware runs at 400 MHz). | — | — | — |

**USB fallback, for builders without a bench supply** (no current limit, so less safe):

- Do Phase 2 first, including 2.7.
- Then power the board from a **USB wall charger** (through an inline USB-C meter if you have one), not from a computer. A charger is a cheap, current-limited source that cannot be harmed by back-feed.
- Expect VSYS_IN ≈ 4.3 V (VBUS minus the M7 drop) and the same rail voltages as above.
- Unplug at once if the current is clearly above the blank-board estimate, or anything gets warm.

## Phase 4: Connect the debug probe (protected) and read the device ID

| # | Do | Expect | If wrong |
|---|---|---|---|
| 4.1 | Board **off**, Pico 2 plugged in. Wire it to J5 as in Phase 0b, **GND first**, then SWCLK, SWDIO, then the two UART wires. Leave J5 pin 1 open. | — | — |
| 4.2 | Power the board (4.8 V, 250 mA limit). | the supply current is unchanged from 3.2 | It rises: the probe is back-powering something or a wire is wrong; off, recheck the table. |
| 4.3 | Read the ID: `/opt/local/bin/openocd -f firmware/openocd/calc-cmsisdap.cfg -c "init; mdw 0x5C001000; shutdown"` (run it from `firmware/`, or use `bringup.py id`). | `0x10036450` = **rev Y** (SDRAM runs at 90 MHz), `0x20036450` = **rev V** (100 MHz) | See 4.4. |
| 4.4 | If there is no connection: swap SWCLK/SWDIO; shorten the wires (≤ 15 cm); lower the clock with `-c "adapter speed 1000"` right after `-f …` (`bringup.py --speed 1000 id`); hold RESET (SW1), start the command, and release RESET right after "Listening on port" (`FLASHING.md` §1). | connects | Still nothing: is 3V3 (TP5) good? Probe pins correct? Check the NRST (J1 pin 10) level ≈ 3.3 V. |
| 4.5 | Boot address and option bytes: `-c "init; mdw 0x52002040; mdw 0x5200201C; shutdown"`. | FLASH_BOOT_CUR = `0x1ff00800`; OPTSR RDP byte 0xAA (level 0); FZ_IWDG bits 17/18 = 1 on a new chip | Never change RDP. The watchdog bits are set in Phase 9.2. |

## Phase 5: Bring-up firmware, console, clocks, SDRAM and QSPI

Run the commands from `firmware/`. The console: `screen /dev/cu.usbmodemXXXX 115200` (quit with Ctrl-A K), or
`python3 tools/calcctl.py --port /dev/cu.usbmodemXXXX "CMD" ...` (exits after each command's `OK`/`ERR`), or
`bringup.py cmd 'CMD'`.

| # | Do | Expect | If wrong |
|---|---|---|---|
| 5.1 | Flash the bring-up image: `/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "program release/bringup.elf verify reset exit"` | `** Verified OK **` | 4.4; check that `release/` was rebuilt (Phase −1.2) |
| 5.2 | Open the console and press RESET (SW1), or run `bringup.py log 5 --reset`. | `# BRINGUP firmware: boot ...`, `# clock: SYSCLK 400 MHz from HSE`, `io-comp ready`, `# sdram clock: SDCLK 90 MHz ... REV_ID 0x1003 (rev Y)` (or 100 MHz / 0x2003 rev V), the reset reason. LED D2 blinks at 1 Hz. | No output: TX/RX swapped (J5 pin 5 → Pico GP5). `HSE FAILED`: Y1 and C28/C29. |
| 5.3 | `version`, `help`, `clocks`. Optional scope on J6 pin 7 (MCO): `mco hse 4` → 6.25 MHz; `mco pll1q 4` → 25 MHz; once `lse=ok`, `mco lse 1` → 32.768 kHz. | as listed | LSE not ok: Y2 and C30/C31 (non-fatal). |
| 5.4 | Power monitor: `i2c scan 2`, `ina226`, `adc`, `power`. Compare `vsys_v` with TP1 and `isys_ma` with the supply's display. | finds 0x40; `vsys` ≈ supply (4.8 V); `isys` ≈ supply current (Phase 10.1 checks the gain; ≈ 1.2 % gain error is known on this tap); `alert_pin=1`; 3V3 ≈ 3.3 V (VREFINT); 3V3_BUS ≈ 0 | no 0x40: U9 and R54/R55 |
| 5.5 | Write down the **MCU-only baseline**: `isys` and the supply current. | **≈ 80–160 mA at 4.8 V (estimate)** | Far higher: a part is drawing current or something is warm. Investigate before the bus is powered. |
| 5.6 | Raise the limit to **400 mA**. `rail bus on`, then `adc` and `ina226`. | `sdram=ok`; 3V3_BUS ≈ 3.3 V (note the value: the panel is rated 2.5–3.3 V, abs. max 4.6 V); current up by tens of mA | A large jump: `rail bus off force` at once (a short on 3V3_BUS; the SY6280 limits it to ≈ 0.52 A). |
| 5.7 | **SDRAM:** `memtest quick noreset`, then `memtest full noreset` (it runs over the whole 32 MB arena; allow a few minutes). | `OK`, 0 errors | Address-bit pattern: A0–A12 or BA0/BA1. Byte lane: NBL0/NBL1. Random or intermittent: see 5.8. |
| 5.8 | **SDCLK check (known open item).** SDCLK (36 mm) is partly routed on B.Cu without a continuous reference, and it runs at **90 MHz on rev Y**. If 5.7 is flaky, scope SDCLK with a ≥ 500 MHz probe and a short ground spring, at **R16** (22 Ω, near U1) and at **U2 pin 38** (CLK). | monotonic edges, no ringing back across the thresholds, ≈ 90 MHz (rev Y) / 100 MHz (rev V) | Note the edges; try `SDRAM_RPIPE=1` or CL changes (`fmc.c`, firmware README step 7). `make BUILD=build/sd90 FW_EXTRA=-DSDRAM_SDCLK_MHZ=90 fw bringup` forces 90 MHz on rev V too. |
| 5.9 | **QSPI:** `qspi id`, `qspi status`, `qspi test`, `qspi mmap`, `qspi stress 3`. | `ef4018`; QE = 1; all pass at 66.7 MHz | U3 joints, R17–R19. If it fails, use the rollback firmware `release/calc_core.elf` later (it does not need the QSPI). |
| 5.10 | `selftest` (with no panel yet, `lcd_*`, `backlight` and `touch` fail; that is expected). | ina226, 3v3, hse, qspi, 3v3_bus, sdram and memtest PASS | — |

## Phase 6: Display

| # | Do | Expect | If wrong |
|---|---|---|---|
| 6.1 | Before plugging in (repeat 0.5 if needed), with the board **unpowered**: continuity from J3 pad 1 (north, silk "1") to TP_INT (R63, the 10 kΩ to 3V3_BUS) and from pads 37–40 to GND. | as listed | J3 joints |
| 6.2 | **Dry-fit the fold**: panel face-down beside the board, insert the FPC with the **contacts facing the PCB**, pin 1 north, fully into J3; close the latch; lay the panel over into its pocket. The tail must reach the stop with the latch closed and no tension on the fold (nominal end x ≈ 39.1 mm, full insertion needs ≥ 38.0). | fully seated, latch flat | If it is short: push the panel west in its pocket and crease the fold tighter. Last resort: the 40-pin 0.5 mm same-side FFC extension (`ORDER.md` §2/§4). |
| 6.3 | Power on, `rail bus on`, `lcd id`. | RDDID **0x858552** (ST7789V2 default; note it) | 000000/FFFFFF: the bus or panel is not answering (FPC seating, pin 1 orientation, 3V3_BUS). Garbled: D0–D15 reversed. |
| 6.4 | `lcd te` | ≈ 60 Hz | 0: FPC pin 8 (FMARK) → PG9 |
| 6.5 | `pattern bars`, `pattern grid`, `pattern gradient`, `pattern white`, `pattern black` | clean bars, a straight grid, a smooth gradient | Screen stays white: see Troubleshooting. Flicker or poor contrast: try `lcd cmd bb 20`, or build with `LCD_PANEL_TUNING=0`. |
| 6.6 | **Orientation:** `pattern orient`, then `lcd madctl 20`, `E0`, `60`, `A0` in turn | the red square top-left and the arrow pointing up for one value (likely **A0** if the panel is upright at MADCTL 0x00) | Write down the value. Setting `LCD_ORIENTATION` in `board.h` is a firmware change for later. |
| 6.7 | `lcd flush`, `lcdbus` | ≈ 8 ms per frame; `min DATAST` ≤ 10 and `back-to-back ok` | Writes drop at every DATAST: the CSX-hold test in firmware README step 8 |
| 6.8 | Backlight: `backlight`, `backlight sweep`, `bl 10`, `bl 50`, `bl 100` | gain 0.85–1.15; `src=3v3 mode=auto`; regulates to 90 mA (moving to VSYS where 3V3 saturates) | `NO CURRENT`: LED pins 33–36, Q8, R48 |
| 6.9 | `selftest` | every item PASS | the item that failed |

## Phase 7: Touch

| # | Do | Expect | If wrong |
|---|---|---|---|
| 7.1 | `i2c scan 4`, `tp` | 0x38; `present=1`, `chip=FT6236 cipher=0x36` (0x64 is also fine), `max_points=2`, `int_pin=1`; note the firmware and vendor ids | No answer: FPC pins 1–4 (TP_INT, TP_RST, TP_SCL, TP_SDA), R61–R64; `tp init` retries. |
| 7.2 | Touch the screen; `tp`, `tp raw` | `irqs` rises; one point | — |
| 7.3 | `tp watch 10000`; touch the four corners, top-left first | `screen=` ≈ (0,0), (319,0), (319,239), (0,239) | Try `tp map 0`–`7` (3 = origin at the FPC end); note the value for `TOUCH_ORIENTATION`. |
| 7.4 | Two fingers: `tp raw` | two points, different ids | — |
| 7.5 | I2C speed: note `read_errors` after a minute of drags; `tp speed fast`; repeat | 0 errors at 100 kHz; fast is optional | Stay at 100 kHz. |
| 7.6 | `tp sleep`, `ina226`, `tp wake` | the current drops by a few mA; touch is back within ≈ 300 ms | — |

## Phase 8: Keypad (49 keys + ON)

| # | Do | Expect | If wrong |
|---|---|---|---|
| 8.1 | With the bring-up image, press every key once and watch the console for `# key NAME down/up` lines (or run `bringup.py keys 300`, which ticks them off and lists the missing ones). | all 50 names: F1–F5, SHIFT, ALPHA, DEL, HOME, APPS, MENU, UP, LEFT, RIGHT, DOWN, CAS, MATH, VARS, X, CLEAR, SIN, COS, TAN, LN, LOG, SQR, POW, LPAREN, RPAREN, DIV, SQRT, 7, 8, 9, MUL, COMMA, 4, 5, 6, SUB, ASSIGN, 1, 2, 3, ADD, ON, 0, DOT, NEG, ENTER; each name matches the key pressed | A whole row or column dead: that row (PG11–PG14, PB0, PB1, PB4) or column (PE2–PE6, PB12, PB13); see `gen/design.py` KEYMAP. A single key: its switch's solder. A wrong name: report it (the matrix map). |
| 8.2 | `kpd` while holding one key, then two keys | one bit; `ghost_events` stays 0; ON shows `on=1` | The matrix has no diodes: three keys in an L shape can ghost (this is by design). |

## Phase 9: Calculator firmware and microSD

| # | Do | Expect | If wrong |
|---|---|---|---|
| 9.1 | Flash the calculator: `openocd ... -c "program release/calc.elf verify reset exit"` (both banks). If the QSPI failed in 5.9, flash `release/calc_core.elf` instead. | boot log `sdram: ok`, `lcd: ...`, `backlight: ok`, `touch: FT6236 ok`, `qspi: ... ok`; `version` shows `CAS=src`; `eval "factor(x^2-1)"` → `(x-1)*(x+1)` | `FLASHING.md` |
| 9.2 | **Watchdog option bytes (once per board):** `optbytes` (`fz_iwdg_stop=1 fz_iwdg_sdby=1` on a new chip), `optbytes iwdg-freeze`, `reset`. | boot log `watchdog: on (2 s; frozen in Stop and Standby)` | firmware README step 14 |
| 9.3 | **Format the card on the computer.** The firmware reads and writes FAT16/FAT32 (MBR or no partition table) and cannot format a card itself. On macOS: `diskutil list`, then `diskutil eraseDisk FAT32 CALC MBRFormat /dev/diskN` (**check N: this erases that disk**). Use a card of 32 GB or smaller. Copy `firmware/release/MODULES/` to `/CALC/MODULES/` and `build/modules/MATHQ010.MOD` into the same folder. | — | exFAT is not supported |
| 9.4 | Card **out**: `sd info`. Card **in**: `sd info`, `sd test`. | the detect flips between the two (the polarity is unverified, assumed active-low); OCR/CID print | Inverted: note it (`SD_DETECT_ACTIVE_LOW`); 3V3_SD at TP7 ≈ 3.3 V while powered; check U6 and R21–R25. |
| 9.5 | `fs info`, `fs ls /CALC/MODULES` | FAT32, free space; the .MOD files listed | — |
| 9.6 | **Backup:** `sdtool backup`, `sdtool shot` (or Memory › SD tools). | `/CALC/BACKUP.SES` and `/CALC/SHOT001.PNG` exist; the PNG opens on a computer | — |
| 9.7 | **Module install:** Memory › Install content (or `module install /CALC/MODULES/MANUA010.MOD`), then `module list`, `state module_manual`, `manual graph`. | `ok`; the full help page shows | `QSPI_CONTENT.md` §3 |
| 9.8 | **Safe mode:** switch off; hold CLEAR, press ON, keep both held ≥ 0.5 s. | the home screen shows SAFE MODE; switching off and on leaves it | `FLASHING.md`, "Safe mode" |

## Phase 10: Power monitor, batteries and USB

| # | Do | Expect | If wrong |
|---|---|---|---|
| 10.1 | **INA226 vs the supply and the meter.** Put a meter in series in the + lead (all supply current goes through the shunt). Set a steady load (`bl 100`, screen on). Compare `power` `isys_ma` with the meter and the supply display; repeat at `bl 10`. | Agreement within ≈ 1–2 %. **Known: the VSYS_IN Kelvin tap has ≈ 1.2 % gain error.** | Compute `scale = I_meter / I_ina226` and record it. Applying it means setting `INA226_CURRENT_SCALE` (`board.h`, default 1.0; e.g. `make FW_EXTRA=-DINA226_CURRENT_SCALE=0.988f fw`), which is a firmware configuration change: do it on purpose and note it. `vsys_v` vs TP1 should agree to a few mV. |
| 10.2 | **Battery-range sweep** (calculator firmware, bench supply on the battery input, no USB). Step 6.4 → 3.4 V in 0.2 V steps. At each step note the supply V/I and `power` (`vsys_v`, `vbat_v`, `isys_ma`, `v3v3_v`). | vbat ≈ supply; vsys ≈ supply − I × ≈ 0.3 Ω (README); 3V3 steady at 3.3 V down to ≈ 3.5 V of VSYS; below 4.0 V: `# battery: LOW` (a red status-bar battery); below 3.6 V, two readings 1 s apart: `# battery: EMPTY ... powering off` (it hibernates within ≈ 11 s); with a fast load step the INA226 alert at VSYS < 3.55 V may come first | Don't go below 3.0 V (buck UVLO 2.6–2.82 V falling; the MCU browns out). Raise to 4.8 V and press ON: it resumes. |
| 10.3 | **Standby current:** `pwr standby` (or SHIFT ON) with the **SWD wires disconnected** | ≈ 35 µA (a few tens of µA on the supply's display, if it resolves them) | Higher: touch/LCD rail not off, INA226 not shut down; firmware README step 17 |
| 10.4 | **Before the first USB connection:** confirm 2.7 and 3.6 passed (CC ≈ 5.1 kΩ each, VBUS back-feed ≈ 0 V). | — | Do not proceed. |
| 10.5 | **VBUS comparator handover.** Supply on the battery input at 4.8 V (it stands in for cells). Connect J4 to a **USB hub port** (USB-A to USB-C cable; the hub's adapter plugged in if it has one), or to a USB wall charger. | On plug-in: `power` `usb=1`; the bench supply current drops to ≈ 0 (USB priority: the battery switch Q2/Q3 opens once VBUS > ≈ 4.30 V); VSYS_IN ≈ 4.3 V. On unplug: the supply current comes back, **no reset** in the log, `usb=0`. | It resets on unplug: scope VBUS_DET (U10 pin 1) and VSYS_IN; check R3/R6/R65. The bench current doesn't drop on USB: check Q4/Q5 and U10. |
| 10.6 | **Battery-only:** remove the bench supply; fit 4 × AAA (check the holder +/− marks); press ON. | boots; `vbat_v` ≈ 4.8–6.4 V depending on the cells; `isys` ≈ the bench values | Nothing: cell polarity (Q1 blocks a reversed pack), holder contacts |
| 10.7 | (Optional) ROM DFU over USB (through the hub at first, then direct): hold BOOT, press RESET, `dfu-util -l` | `[0483:df11] ... @Internal Flash /0x08000000` | Disconnect the probe's UART wires (PA9 note in `FLASHING.md` §2). |

## Phase 11: Final acceptance

| # | Do | Expect |
|---|---|---|
| 11.1 | Calculator on cells: arithmetic, `factor`, a graph (`Y1=sin(x)`, GRAPH, TRACE), the help manual, touch and keys | everything works; no faults in `crash` |
| 11.2 | SHIFT ON (off), wait, ON: the session resumes | yes |
| 11.3 | **Sparkle Town:** Memory › Modules → install `MATHQ010.MOD` from the card; APPS → Sparkle Town; play a round by keys and by touch; switch off and on; the progress is kept | it runs and the progress survives |
| 11.4 | Record: the silicon rev, RDDID, MADCTL, touch map, SD detect polarity, the INA226 scale, the baseline currents, the standby current | one line per board (for the give-away units) |

---

## Troubleshooting (one page)

| Symptom | Likely causes | Next check |
|---|---|---|
| **Doesn't power on**: the supply stays at its current limit | a short: a 3V3 bridge at U1/U3/U9; a reversed SOT-23 FET; a reversed holder | Phase 2 rail resistances; feel for the warm part at 3.0 V / 50 mA |
| Doesn't power on: the supply current ≈ 0, 3V3 = 0 | supply polarity reversed (Q1 blocks it); a holder rotated 180°; Q2/Q3 battery switch off | BAT_P (TP3) vs BAT_RAW; VSYS_IN (TP2); Q4 gate = BAT_P, Q5 gate ≈ 0 |
| VSYS OK, 3V3 = 0 or wrong | U4 joints (WSON exposed pad), L1 | U4 pins 2/3 (VIN) and 6 (VOS); a scope on L1 |
| 3V3_BUS stuck at 0 after `rail bus on` | U5 EN (PG3), a short on the bus (SY6280 current limit) | `adc`, `ina226`, TP6; `rail bus off force` |
| **Doesn't connect** (SWD) | probe not flashed / wrong UF2; SWCLK/SWDIO swapped; no common GND; board unpowered; the firmware put the MCU in low-power | `bringup.py probe`; 4.4 (slower clock, connect while holding RESET); TP5 = 3.3 V |
| SWD connects, no console | TX/RX crossed wrongly; wrong `/dev/cu.usbmodem` port; baud | J5 pin 5 → Pico GP5, pin 6 ← GP4; 115200 8N1 |
| Boot log `HSE FAILED` | Y1 or C28/C29 | it still runs on HSI; rework Y1 |
| memtest errors | SDRAM joints (TSOP-54), an address/BA line, a byte lane, SDCLK signal integrity | 5.7 pattern; 5.8 SDCLK scope at R16 / U2 pin 38 |
| `qspi id` ≠ ef4018 | U3 joints, NCS pull-up R17 | `calc_core.elf` rollback works without QSPI |
| **Screen stays white** (backlight on, no image) | the panel never got its init: FPC not fully in or latch open; FPC reversed (pin 1 south); a bridge on J3; 3V3_BUS off; RESX held low (PD3 / R42); IM pins not low | `lcd id` (000000/FFFFFF = no answer); reseat the FPC (contacts toward the PCB, pin 1 north); J3 pads 37–40 to GND; `rail bus on`; `lcd reinit` |
| Screen dark (no backlight) | backlight loop: LED pins 33–36, Q8, R48, the anode selector Q9–Q12 | `backlight` (`NO CURRENT`), `backlight src vsys` + `bl 50` |
| Image mirrored / rotated | MADCTL | 6.6 |
| Touch dead | FPC pins 1–4; 3V3_BUS; I2C4 pull-ups R61/R62 | `i2c scan 4`, `tp init` |
| Touch offset / mirrored | coordinate map | 7.3 `tp map` |
| Keys: a row or column dead | a matrix line joint at U1 or a switch | 8.1, `kpd` |
| SD not detected / inverted | card-detect polarity unverified; 3V3_SD | 9.4 |
| Calculator resets on USB unplug | the TLV3012 handover (VBUS_DET), the VSYS_IN bulk caps | 10.5 |
| Voltage appears on VBUS while on the bench | D1 reversed or shorted | 3.6: **no USB until fixed** |
