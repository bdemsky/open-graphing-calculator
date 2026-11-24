# Assisted bring-up script: rev A

This is a two-person version of [`BRINGUP.md`](BRINGUP.md). One person does the hands-on work, marked **[USER]**.
A coding assistant (Claude Code) on the Mac runs the commands, parses the output and decides pass or fail,
marked **[CLAUDE]**.

- The phase and step numbers are the same as in `BRINGUP.md`, which has the explanations, the expected values and the fixes. This file adds who does what, what to report, and the exact non-interactive commands.
- The safety rule from `BRINGUP.md` ("Protecting your computer") applies unchanged:
  - bench supply first;
  - probe after Phase 3, GND first, Pico 3V3/VSYS never connected;
  - the board's USB-C only after 2.7 and 3.6 have passed, and through the hub first.

**Rules for [CLAUDE]:**

- **Never run an interactive program** (`screen`, `minicom`, `openocd` without `exit`/`shutdown`, `calcctl.py -i`). Use `hardware/bringup/bringup.py`. It has timeouts, logs to `firmware/build/bringup-logs/`, and returns exit status 0 for pass and 1 for fail.
- Run `cmd` steps with `--timeout` long enough for the command (memtest full: 600).
- Prefer software checks over asking the user: the ADC, INA226, memtest, key log, touch coordinates and pattern CRCs. Ask the user only for what software cannot see (meter readings, the picture on the screen, heat, smell).
- Before asking for any physical change (power, wires, cables), say so and wait for the user to confirm it is done.

All `bringup.py` commands are run from the repo root:
`python3 hardware/bringup/bringup.py [--port /dev/cu.usbmodemXXXX] SUBCOMMAND`.

---

## Phase −1: Mac preflight (can run before the boards arrive)

| # | Who | Action | Pass |
|---|---|---|---|
| −1.1 | [CLAUDE] | `bringup.py preflight` | `PREFLIGHT PASS`: openocd at `/opt/local/bin/openocd`, `arm-none-eabi-gcc`, Python ≥ 3.8 found; checksums OK. dfu-util and stm32flash are optional. |
| −1.2 | [CLAUDE] | If preflight reports the release behind HEAD or missing files: `cd firmware && make release` (allow ~10 min; run in the background with a timeout), then `bringup.py preflight` again. | `release/MANIFEST.txt` `checks: PASS`; `MODULES/INDEX.TXT`, `calc_qspi.bin` and `calc_core.elf` present |
| −1.3 | [CLAUDE] | `cd firmware && make mathquest-module` | `build/modules/MATHQ010.MOD` exists; `mkmodule.py check` passes |
| −1.4 | [USER] | Phase 0b.1–0b.2: flash `debugprobe_on_pico2.uf2` onto the Pico 2 (hold BOOTSEL, plug in, drag the file). Report "Pico flashed". | — |
| −1.5 | [CLAUDE] | `bringup.py ports`, then `bringup.py probe` | a `/dev/cu.usbmodem…` port, the USB product "Debugprobe on Pico (CMSIS-DAP)", `probe: CMSIS-DAP found` (the target is missing, which is normal) |
| −1.6 | [CLAUDE] | Note the probe's port name for `--port`, or `export CALC_PORT=...`. The console only shows up as this probe's UART bridge: the calculator has no USB device stack, so its own USB-C never shows up as a serial port. | the port is recorded |

The console is read non-interactively with `bringup.py cmd 'CMD'` (it waits for each `OK`/`ERR`) or
`bringup.py log SECONDS` (it collects asynchronous `# ...` lines). Underneath is `firmware/tools/calcctl.py`
(termios, no pyserial needed): `python3 firmware/tools/calcctl.py --port PORT --timeout 20 "CMD"`.

## Phase −0.5 (optional): JLC production photos (before shipping)

| # | Who | Action | Report back / pass |
|---|---|---|---|
| −0.5.1 | [USER] | If the JLC order page has production photos, download them (top and bottom) into a folder and give Claude the path. | the folder path, or "no photos" |
| −0.5.2 | [CLAUDE] | Render the reference views: `.venv/bin/python hardware/bringup/assembly_views.py`. Compare the photos part by part against the views and `hardware/fab/calc-cpl.csv`: IC pin 1 (U1–U10), Q1–Q12, the D1 band, D2, J3 opening west, BT1/BT2 +/− on the bottom, missing parts. | a list of mismatches with designators, or "none found". For a mismatch, Claude drafts the message to JLC, and the user sends it from the order page before shipping. |

## Phase 0: Photo inspection for hand-fixable defects (board 1 first)

The user photographs the board and Claude inspects the photos (Claude Code reads images). The scope is
the same as `BRINGUP.md` Phase 0: only the iron-fixable group ("What can be fixed, and by whom").

| # | Who | Action | Report back / pass |
|---|---|---|---|
| 0.0 | [CLAUDE] | `.venv/bin/python hardware/bringup/assembly_views.py` → `firmware/build/assembly-views/`: `top_silk.png`, `bottom_silk.png` (mirrored, as seen from below), the region crops (`top_headers_usb`, `top_power`, `top_testpads_j3`, `top_fets_backlight`, `top_mcu_sdram`, `top_keypad`, `bottom_decoupling_mcu`, `bottom_decoupling_sdram`, `bottom_battery_holders`) and `*_3d.png`. Claude views these, and has `hardware/fab/calc-cpl.csv` and `calc-bom.csv` at hand for positions, rotations and values. | the views exist |
| 0.1p | [USER] | **Photos.** Phone camera in macro or 2× mode; bright, diffuse light (no flash, no glare on the solder); camera held square to the board; in focus (tap to focus on the parts). Take: **whole board, top and bottom**; then **close-ups by region**, matching the crops: the header and USB-C edge (J1, J5, J6, J4, SW1, SW2, D1, D2); the power section (U4, L1, R5, U9, Q3, Q5, U10, C1–C7); the test pads and J3; the FET and backlight area (Q1, Q2, Q4, Q6–Q12, U7); the bottom holders BT1/BT2 (pads at both ends); the bottom decoupling under U1 and U2. Optional: a second shot of each close-up at about 45° to show fillets and lifted ends. Save them to a folder. | the folder path |
| 0.2p | [CLAUDE] | Compare each photo with its reference crop, for the iron-fixable parts only: **present** (no empty pad pair where the reference has a part); **orientation of polarised parts**: D1 cathode band, D2 cathode mark, the SOT-23 Q1–Q12 sitting on their 3-pad footprints, the pin-1 dots of U5–U8 and U10, BT1/BT2 +/− marks (the board has no tantalum or electrolytic capacitors); **tombstoned or lifted** 2-terminal parts; **visible bridges** on headers, the holder pads, J2/J4 legs and between neighbouring passives; J1/J5/J6 present and soldered (from the bottom photo). | per region: OK, or a list of findings with designators and what to do (iron fix, or photo + claim if it is JLC's error) |
| 0.3p | [CLAUDE] → [USER] | **Limits, said plainly to the user.** Photos cannot show joints hidden under parts (the U4 exposed pad, the holder contacts under the holder body), and cannot reliably show hairline bridges on fine pitch (J4 signal pins, J3, U1/U2). Claude lists anything doubtful: a blurry region, a possible bridge, a part that may be lifted. The user checks each with a loupe, or with the meter in continuity mode between the two named pins. | the user's loupe/meter result for each doubtful item |
| 0.5 | [USER] | The panel: FPC 40 pins, contacts on the front face, pin 1 at the portrait-left end; measure the tail width at the stiffener (a photo of the FPC end helps) | the width in mm; "contacts front: yes/no"; the panel's part number |
| 0.x | [CLAUDE] | Give iron instructions for each fixable finding, then ask for new photos of the fixed spots. Width > 20.6 mm → give the shaving instructions. A different panel → stop. Anything unfixable that happens to be noticed: photos and a drafted JLC claim for the user to file ("What can be fixed, and by whom"). | go to Phase 1 when the fixable findings are cleared |

**Board order:** Claude works with board 1 only. The other boards stay unpowered until board 1 has passed Phase 3,
or until the cause of a failure is understood.

## Phase 0b: Pico 2 as the debug probe

Done in −1.4/−1.5. [USER] 0b.4: prepare five jumper wires (the series resistors are optional). Do not wire to the board yet.

## Phase 1: Hand soldering

Nothing is required. [CLAUDE] asks only if Phase 0 found J5 missing or a defect.

## Phase 2: Unpowered resistance checks

| # | Who | Action | Report back |
|---|---|---|---|
| 2.1–2.6 | [USER] | Meter on resistance, black on TP9. Measure TP4, TP2, TP1, TP1–TP2, TP3, TP5, TP8, TP5–TP8, TP6, TP7 (wait a few seconds for each reading to settle). | each value with its unit, e.g. "TP4 97.6k" |
| 2.7 | [USER] | **USB-C connector** (breakout in J4): VBUS–GND, CC1–GND, CC2–GND, D+–D−, D+–GND, D−–GND, D+–VBUS, D−–VBUS; flip the plug and repeat the CC readings | every value |
| 2.8–2.10 | [USER] | Holder series continuity; the Q1 diode drop BAT_RAW → BAT_P (both directions); BOOT0 to GND | the values |
| 2.x | [CLAUDE] | Check against `BRINGUP.md` Phase 2. **Any rail < 10 Ω → stop**, and ask for a close inspection of the parts named there. CC ≠ ≈ 5.1 kΩ, or D± shorted → no USB later until fixed. | — |

## Phase 3: First power-up (bench supply on the battery input)

[CLAUDE] cannot see the board yet, so this phase is the user's readings against the expected values.

| # | Who | Action | Report back | [CLAUDE] pass criteria |
|---|---|---|---|---|
| 3.0 | [USER] | Connect the supply: + to BT2's + contact (BAT_RAW), − to BT1's − contact or TP9. Output off, 0 V, limit **50 mA**. | "connected, polarity checked" | — |
| 3.1 | [USER] | Output on, raise slowly to 3.0 V | the supply V and I at 3.0 V; whether it sat at the limit and for how long | a brief limit at turn-on is OK; a constant limit → **off**, back to Phase 2 |
| 3.2 | [USER] | Raise to 4.8 V; fingertip-check U1, U4, L1, Q1–Q3, U9 | the supply V and I; any warm part | ≈ 5–25 mA (estimate), nothing warm. > 50 mA or warm → off; ask for a photo of the warm part and its neighbours |
| 3.3–3.5 | [USER] | Meter: TP3, TP2, TP1, TP5, TP8, TP6, TP7 | each voltage | BAT_P ≈ 4.8; VSYS_IN ≈ VSYS ≈ 4.8; 3V3 and VDDA 3.18–3.43; 3V3_BUS and 3V3_SD ≈ 0 |
| 3.6 | [USER] | **Back-feed:** VBUS at TP4 and at the breakout's VBUS pad, with the board running | both voltages | **< 0.1 V (estimate)**. Higher → stop, no USB connection, check D1 |
| 3.8 | [USER] | Raise the limit to **250 mA** | "done" | — |

## Phase 4: Debug probe and device ID

| # | Who | Action | Report back / pass |
|---|---|---|---|
| 4.1 | [USER] | Board off. Pico plugged into the Mac (directly or via the hub). Wire GND first (Pico pin 3 → J5 pin 4), then GP2 → J5-2 (SWCLK), GP3 → J5-3 (SWDIO), GP5 ← J5-5 (UART_TX), GP4 → J5-6 (UART_RX). J5 pin 1 open; no Pico 3V3/VSYS wire. | "wired" |
| 4.2 | [USER] | Board on (4.8 V) | the supply current (should match 3.2) |
| 4.3 | [CLAUDE] | `bringup.py id` | `DEV_ID 0x450`, REV_ID 0x1003 (rev Y, SDCLK 90) or 0x2003 (rev V); FLASH_BOOT_CUR `0x1ff00800`; RDP level 0. Record the revision. |
| 4.4 | [CLAUDE] / [USER] | On failure: `bringup.py --speed 1000 id`; if that fails, ask the user to check the wires, then to **hold RESET (SW1)** while Claude starts `bringup.py id` and to release it on cue | — |

## Phase 5: Bring-up firmware, console, clocks, SDRAM, QSPI

| # | Who | Action | Pass |
|---|---|---|---|
| 5.1 | [CLAUDE] | `bringup.py flash firmware/release/bringup.elf` | `FLASH PASS (verified)` |
| 5.2 | [CLAUDE] | `bringup.py log 6 --reset` | lines `# BRINGUP firmware`, `clock: SYSCLK 400 MHz from HSE`, `io-comp ready`, `sdram clock: SDCLK 90 MHz ... REV_ID 0x1003` (or 100/0x2003, matching 4.3) |
| 5.2u | [USER] | Look at D2 | "D2 blinking about once a second: yes/no" |
| 5.3 | [CLAUDE] | `bringup.py cmd version clocks` | `hse` ok; record LSE |
| 5.4 | [CLAUDE] | `bringup.py cmd 'i2c scan 2' ina226 adc power` | 0x40 found; `vsys_v` ≈ the 4.8 V the user set; 3V3 3.1–3.5; 3V3_BUS ≈ 0 |
| 5.4u | [USER] | Read the supply display | the supply V and I. [CLAUDE] compares the current with `isys_ma` (expect agreement within a few %; ≈ 1.2 % gain error is known) |
| 5.5 | [CLAUDE] | Record the baseline `isys` | ≈ 80–160 mA (estimate); much higher → ask the user to feel for warm parts |
| 5.6 | [USER] then [CLAUDE] | User: raise the limit to **400 mA**, say "done". Claude: `bringup.py cmd 'rail bus on' adc ina226` | `sdram=ok`; `v3v3_bus` 3.0–3.6 (record it); `isys` up by tens of mA. A large jump → `bringup.py cmd 'rail bus off force'` at once |
| 5.7 | [CLAUDE] | `bringup.py --timeout 120 cmd 'memtest quick noreset'`, then `bringup.py --timeout 900 cmd 'memtest full noreset'` | `OK`, 0 errors. On errors, classify by the README step 7 pattern |
| 5.8 | [USER] | Only if 5.7 fails or is intermittent: scope SDCLK at R16 and U2 pin 38 (≥ 500 MHz probe, ground spring) while Claude loops `memtest quick noreset` | screenshots, or the frequency, rise time and ringing |
| 5.9 | [CLAUDE] | `bringup.py --timeout 60 cmd 'qspi id' 'qspi status' 'qspi test' 'qspi mmap' 'qspi stress 3'` | `ef4018`, QE=1, all OK |
| 5.10 | [CLAUDE] | `bringup.py --timeout 30 cmd selftest` | ina226, 3v3, hse, qspi, 3v3_bus, sdram and memtest PASS (lcd/touch fail with no panel, which is expected) |

## Phase 6: Display

| # | Who | Action | Report back / pass |
|---|---|---|---|
| 6.0 | [CLAUDE] | `bringup.py cmd 'rail bus off'`, then ask the user to power off | — |
| 6.1 | [USER] | Continuity J3 pad 1 → R63 (TP_INT); pads 37–40 → GND | the values |
| 6.2 | [USER] | Dry-fit and seat the FPC (contacts toward the PCB, pin 1 north, latch closed, no tension) | "seated, latch closed"; any shortfall |
| 6.3–6.4 | [USER] then [CLAUDE] | User: power on. Claude: `bringup.py cmd 'rail bus on' 'lcd id' 'lcd te'` | RDDID `858552` (record it); TE ≈ 60 Hz |
| 6.5 | [CLAUDE] | `bringup.py cmd 'pattern bars'`, then ask the user | [USER]: "describe the screen: the colour bars in order, from left to right" |
| 6.5b | [CLAUDE] | `pattern grid`, `pattern gradient`, `pattern white`, `pattern black`, one at a time | [USER]: for each one, "clean / defects (where)" |
| 6.6 | [CLAUDE] | `bringup.py cmd 'pattern orient'`; for M in 20, E0, 60, A0: `cmd 'lcd madctl M' 'pattern orient'` and ask | [USER]: "red square top-left and arrow up: yes/no" for each value. Record the one that works. |
| 6.7 | [CLAUDE] | `bringup.py --timeout 60 cmd 'lcd flush' lcdbus` | ≈ 8 ms; `min DATAST` ≤ 10, `back-to-back ok` |
| 6.8 | [CLAUDE] | `bringup.py --timeout 60 cmd backlight 'backlight sweep' 'bl 50'` | gain 0.85–1.15, regulates to 90 mA; [USER] confirms the brightness changes with `bl 10` / `bl 100` |
| 6.9 | [CLAUDE] | `bringup.py --timeout 30 cmd selftest` | all PASS |

## Phase 7: Touch

| # | Who | Action | Pass |
|---|---|---|---|
| 7.1 | [CLAUDE] | `bringup.py cmd 'i2c scan 4' tp` | 0x38; `present=1`, `chip=FT6236`; record the firmware and vendor ids |
| 7.3 | [CLAUDE] + [USER] | Claude starts `bringup.py --timeout 20 cmd 'tp watch 15000'` and asks the user to touch top-left, top-right, bottom-right, bottom-left, in that order, about 2 s each | Claude reads the `screen=` values: ≈ (0,0), (319,0), (319,239), (0,239). Otherwise Claude tries `tp map N` and repeats; record N |
| 7.4 | [USER] + [CLAUDE] | User holds two fingers; Claude runs `tp raw` | two ids |
| 7.5 | [CLAUDE] + [USER] | User drags for a minute during `tp watch 60000`; Claude reads `read_errors` from `tp`; then `tp speed fast` and repeat | 0 errors at std |
| 7.6 | [CLAUDE] | `cmd 'ina226' 'tp sleep' 'wait 500' 'ina226' 'tp wake'` | the current drops by a few mA |

## Phase 8: Keypad

| # | Who | Action | Pass |
|---|---|---|---|
| 8.1 | [CLAUDE] + [USER] | Claude: `bringup.py keys 300`. User: press every key once, row by row, including ON, and say "done". | `KEYS PASS`. If keys are missing, Claude lists them, and the user reports what happens when each is pressed; Claude maps a dead row or column with `gen/design.py` KEYMAP. Also check that the names arrive in the order pressed (a swapped name = a matrix-map issue). |
| 8.2 | [CLAUDE] + [USER] | User holds SHIFT (then SHIFT+7); Claude runs `bringup.py cmd kpd` | one bit set (then two); `ghost_events=0`; `on=1` while ON is held |

## Phase 9: Calculator firmware and microSD

| # | Who | Action | Pass |
|---|---|---|---|
| 9.1 | [CLAUDE] | `bringup.py flash firmware/release/calc.elf` (or `calc_core.elf` if QSPI failed), `bringup.py log 8 --reset`, `bringup.py --timeout 30 cmd version 'eval "factor(x^2-1)"'` | the boot log shows sdram/lcd/backlight/touch/qspi ok; `CAS=src`; `result=(x-1)*(x+1)` |
| 9.2 | [CLAUDE] | `cmd optbytes`, `cmd 'optbytes iwdg-freeze'`, `cmd reset`, `log 6` | the log shows `watchdog: on (2 s; frozen in Stop and Standby)` |
| 9.3 | [USER] + [CLAUDE] | The user inserts the card into the Mac. Claude: `diskutil list` and shows the candidate disk; **the user confirms the disk number**; then Claude runs `diskutil eraseDisk FAT32 CALC MBRFormat /dev/diskN`, copies `firmware/release/MODULES/*` and `build/modules/MATHQ010.MOD` to `/Volumes/CALC/CALC/MODULES/`, and ejects with `diskutil eject`. | the files are copied; never erase without the user's explicit confirmation of the disk |
| 9.4 | [CLAUDE] + [USER] | Card out: `cmd 'sd info'`; the user inserts the card; `cmd 'sd info' 'sd test'` | the detect state flips (record the polarity); OCR/CID shown |
| 9.5–9.7 | [CLAUDE] | `cmd 'fs info' 'fs ls /CALC/MODULES' 'sdtool backup' 'sdtool shot'`; `--timeout 120 cmd 'module install /CALC/MODULES/MANUA010.MOD' 'module list' 'state module_manual'` | FAT32; BACKUP.SES and SHOT001.PNG written; `rc=0`; `module_manual=ok` |
| 9.7s | [CLAUDE] | `python3 firmware/tools/calcctl.py --port PORT --timeout 60 --shot /tmp/shot.png "manual graph"`, then look at the PNG | the help page is visible; the user confirms the panel shows the same |
| 9.8 | [USER] + [CLAUDE] | Safe mode: the user does CLEAR+ON; Claude runs `log 5` and `cmd 'state screen'` | SAFE MODE |

## Phase 10: Power monitor, batteries, USB

| # | Who | Action | Report back / pass |
|---|---|---|---|
| 10.1 | [USER] + [CLAUDE] | User: meter in series in the + lead. Claude: `cmd 'bl 100' 'wait 2000' power`, then `cmd 'bl 10' 'wait 2000' power` | User: the meter mA and the supply V/I at each point. Claude computes `scale = I_meter / isys_ma`, expects within ≈ ±2 % (≈ 1.2 % known), and records it. Setting `INA226_CURRENT_SCALE` is a firmware configuration change and needs the user's decision. |
| 10.2 | [USER] + [CLAUDE] | Sweep: the user steps the supply 6.4 → 3.4 V by 0.2 V and says each value; Claude runs `cmd power` and `log 3` at each step | Claude tabulates supply vs `vbat_v`/`vsys_v`/`isys_ma`/`v3v3_v`; expects `battery: LOW` below 4.0 V and `battery: EMPTY ... powering off` below 3.6 V; stop at 3.4 V. Then the user sets 4.8 V and presses ON; Claude confirms `standby-wake` or a normal boot. |
| 10.3 | [USER] + [CLAUDE] | Claude: `cmd 'pwr standby'`; then the user **unplugs the SWD wires** (GND last) and reads the supply/meter current | µA reading (≈ 35 µA target). The user reconnects, GND first. |
| 10.4 | [CLAUDE] | Confirm from the Phase 2.7 and 3.6 reports: CC1 and CC2 ≈ 5.1 kΩ, VBUS back-feed < 0.1 V. Only then ask for USB. | — |
| 10.5 | [USER] + [CLAUDE] | Supply at 4.8 V on the battery input. User: plug the board's USB-C into a **hub port** (USB-A to USB-C cable, the hub's adapter in if it has one). Claude: `cmd power` and `log 3`. Then the user unplugs; Claude: `log 3`, `cmd power`. | User: the supply current before / with USB / after. Pass: with USB `usb=1` and the supply current ≈ 0; after the unplug `usb=0`, the supply current is back, and **no boot banner** in the log. |
| 10.6 | [USER] + [CLAUDE] | User: supply off and disconnected, 4 AAA in, ON. Claude: `log 6`, `cmd power` | `vbat_v` 4.8–6.4 V |
| 10.7 | optional | DFU: the user holds BOOT and taps RESET with USB connected (hub first, direct later); Claude runs `dfu-util -l` (if installed) | `[0483:df11]` |

## Phase 11: Final acceptance

| # | Who | Action | Pass |
|---|---|---|---|
| 11.1 | [CLAUDE] | `cmd 'type "2+3\n"' 'state last_output' 'eval "factor(x^2-1)"' crash`; `calcctl.py --shot` of a graph (`type "Y1=..."` per the console docs) | `5`; factored; no crash record; the PNG shows the graph |
| 11.1u | [USER] | Use it by hand: keys, touch, a graph, help | "works" or what didn't |
| 11.2 | [USER] + [CLAUDE] | SHIFT ON; wait 10 s; ON. Claude: `log 6`, `cmd 'session info'` | the session resumed |
| 11.3 | [USER] + [CLAUDE] | Memory › Modules → install MATHQ010.MOD (or Claude: `cmd 'module install /CALC/MODULES/MATHQ010.MOD'`); APPS → Sparkle Town; the user plays a round by keys and touch; off/on | Claude: `cmd 'module list'` shows mathquest; `calcctl.py --shot` of the game screen; the user confirms that the progress survived the power cycle |
| 11.4 | [CLAUDE] | Write the board record (silicon rev, RDDID, MADCTL, touch map, SD polarity, INA226 scale, baseline/standby currents) into the session log and report it to the user | — |

For failures, use the troubleshooting table at the end of `BRINGUP.md`.
