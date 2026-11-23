# USB bring-up: test plan

The USB device (README.md "USB": `platform/stm32h743/usb.c`, TinyUSB 0.18.0, `core/vdrive.c`,
`core/usbdisk.c`, `app/usb_app.c`) is compile-verified and tested in the simulators only. This is the
plan for the first boards. It needs **no debug probe and no UART**: the board, a USB-C cable, a USB hub,
a Mac with `dfu-util` (MacPorts `sudo port install dfu-util`), and the BOOT button for the very first
flash. Commands run from `firmware/`.

**At every step, look at MEM › USB status first** ({apps} Memory, the last item; it updates four times a
second and works whether or not USB does). It shows:

| Line | Healthy value | If not |
|---|---|---|
| VBUS | `present (PA2 now 1)` with the cable in, `absent` without | PA2 divider / VBUS path (R values, the M7 diode) |
| Device | `configured` with a computer, `powered (pull-up on, waiting for a host)` on a charger | see the steps below |
| Error | `none` | the reason the start was refused (clock, VDD33USB, core reset): see "Suspects" |
| Clocks | `PLL3 ok, USBSEL 2, USB33 en rdy` | PLL3 / RCC_D2CCIP2R / PWR_CR3 set-up in `system.c` |
| OTG core | `clock on, 1 starts, 0 failed` | start failures counted here |
| Interrupts | count rising while connected; `rst` >= 1, `setup` in the tens after enumeration | no interrupts: NVIC / vector (`OTG_FS_IRQn` 101) |
| Last SETUP | the last request, e.g. `21 22 val 0003 idx 0000 len 0` (CDC SET_CONTROL_LINE_STATE, DTR on) | `none`: the host never got past reset |
| GINTSTS | the interrupt bits seen (hex) | |
| Console | `DTR 1` while a terminal has the port open | |
| Drives | sectors read and written, errors | |
| CALC drive | `in use, 2048 KB free, N written` | `none`: no SDRAM (small fallback arena) |

The console `usb` prints the same lines (`usb start` retries a start that failed).

Use a **powered USB hub** between the Mac and the calculator for every step (it isolates the Mac's port
and lets you replug quickly), unless a step says otherwise.

## 0. Flash the firmware (first time: ROM DFU with the BOOT button)

1. `make release` (or use a release folder). Hold **BOOT**, press and release **RESET**, release
   **BOOT**, connect USB.
2. `dfu-util -l` lists `[0483:df11] ... @Internal Flash /0x08000000/...`.
3. `dfu-util -a 0 -s 0x08100000 -D release/calc_bank2.bin`, then
   `dfu-util -a 0 -s 0x08000000:leave -D release/calc.bin`.
   Expected: both downloads complete; the second may end with "Error during download get_status"
   (expected, the board restarted); the calculator starts.

## 1. Enumeration

1. Calculator on, USB cable through the hub to the Mac.
2. MEM › USB status. Expected: VBUS `present`, Device `configured`, Error `none`, Interrupts rising,
   Last SETUP not `none`.
3. `ls /dev/cu.usbmodem*` → one new port (its name contains the calculator's serial number).
4. `system_profiler SPUSBDataType` → a device "Graphing Calculator", Manufacturer "Open Graphing
   Calculator", Vendor ID 0x1209, Product ID 0x0001, Serial Number = 24 hex digits, Speed up to 12 Mb/s,
   "Current Required (mA)" 250.
5. `python3 tools/calcctl.py --list` → the same port. Expected failures and what they mean: Device stays
   `powered` with `rst 0` → the host never saw the pull-up (DP/DM swapped, AF10 not applied, ESD part);
   `default` / `addressed` but never `configured` → descriptors (Last SETUP shows which request was last).

## 2. Console over USB

1. `python3 tools/calcctl.py version` (no `--port`). Expected: `version=0.2.0 (CAS=src)` for release images (`0.2.0+N-gHASH...` for others), `platform=stm32h743`.
   USB status: Console `DTR 1` while it runs, rx/tx counts rising.
2. `python3 tools/calcctl.py --shot /tmp/usb.png "state"` → a PNG of the screen and the state keys.
3. `python3 tools/calcctl.py -i`, type `usb` → the diagnostics lines; Ctrl-D.
4. Throughput: `time python3 tools/calcctl.py --upload-module release/MODULES/MANUA010.MOD`.
   Expected: `upload ...: OK: installed manual 0.1.0` in a few seconds (QSPI programming bound), the
   manual works afterwards (`manual graph` in help). This is the default 8 lines in flight; it used to end
   in `ERR line too long` on hardware (with `--window 1` it worked): under flow control macOS sends bulk
   transfers whose length is a multiple of 64 bytes, ended by a zero-length packet, and TinyUSB's dwc2
   driver completed that packet with the previous packet's bytes (fixed in `third_party/tinyusb`, see
   `third_party/README.md`). Run it a few times.
5. Both links at once (only if the UART is wired): typing on the UART while calcctl runs on USB never mixes
   lines.

## 3. The CALC drive

1. In Finder a drive "CALC" appears. `ls -l /Volumes/CALC` → `README.TXT INFO.TXT MODULES.TXT RESULT.TXT
   SCREEN.BMP CALCULATOR.HTML`, all read-only (`CALCULATOR.HTML` under its long name; `dir /x` on Windows
   shows its 8.3 alias `NBnnnn~1.HTM`).
2. `cat /Volumes/CALC/INFO.TXT` → firmware version and tier, battery, free module space, installed modules
   with versions. `open /Volumes/CALC/SCREEN.BMP` → the calculator's current screen (320 x 240).
3. Drag-and-drop install: copy a module the calculator does not have (or another version), e.g.
   `cp release/MODULES/ANIMA010.MOD /Volumes/CALC/` (or drag it in Finder).
   Expected within ~2 s after the copy: the progress screen "Installing content ... animation from USB",
   then the toast "Installed Animation 0.1.0"; the drive disappears and comes back (Finder may say the
   disk was not ejected properly: expected); `cat /Volumes/CALC/RESULT.TXT` → "Installed Animation 0.1.0
   ... installed and verified"; `MODULES.TXT` lists animation; ANIMA010.MOD is no longer on the drive.
   USB status: Drives `wr` grew by about the file size / 512.
4. In use: open APPS › Geometry, copy `release/MODULES/GEOME010.MOD` to the drive. Expected: toast "Leave
   Geometry to install it", nothing written; press {home}: the install runs ("Installed Geometry 0.1.0").
   Your construction is still there afterwards.
5. Corrupt file: `python3 -c "d=bytearray(open('release/MODULES/CASX010.MOD','rb').read()); d[8000]^=0x5A;
   open('/tmp/CASX010.MOD','wb').write(d)"` then `cp /tmp/CASX010.MOD /Volumes/CALC/`. Expected: toast
   "advanced CAS 0.1.0: damaged file", RESULT.TXT "Nothing was installed ... does not match its
   checksum", Memory › Modules unchanged.
6. Not a module: `echo hi > /Volumes/CALC/NOTE.TXT`. Expected: toast "Not a module: NOTE.TXT", nothing else.
7. The browser notebook (README.md "Browser notebook"): `cmp /Volumes/CALC/CALCULATOR.HTML release/CALCULATOR.HTML`
   (identical); open it in Chrome, Connect, choose the calculator. Expected: green dot "Connected: calculator
   0.1.0"; Run all on the welcome notebook gives `[-sqrt(2), sqrt(2)]`, typeset integrals and a graph; a cell
   `while True: pass` stops with `KeyboardInterrupt` on Interrupt; Screen shows the display and the keypad
   works. Then drop a module on CALC: the calculator leaves the bus for 0.5 s to refresh the drive; the page says
   "Reconnecting..." and is connected again by itself (no new port prompt).
8. Eject / replug: eject CALC in Finder, unplug, plug in again. Expected: USB status `detached` then
   `configured` again, the drive mounts fresh (RESULT.TXT keeps the last result until the calculator is
   switched off).

## 4. The microSD card on the computer

1. Card in the slot, USB connected. APPS › microSD tools › Connect card to computer. Expected: full
   screen "Connected to computer", a second drive (the card's own volume name) mounts on the Mac;
   `ls /Volumes/<card>` shows its files; copy a file to it.
2. On the calculator, nothing uses the card meanwhile (screenshots, backups say "microSD: connected to
   the computer.").
3. Press EXIT without ejecting → "Not ejected on the computer: take it back?" (No first). Choose No.
4. Eject the card's drive in Finder → the screen says "Ejected: you can press EXIT."; press EXIT.
   Expected: toast "Card back in the calculator"; microSD tools › Card info works and sees the copied file.
5. Without a card: the menu item says "No card in the slot."; without a computer: "Connect a computer first".

## 5. Charger only, power

1. Plug into a USB charger (no computer). Expected: USB status VBUS `present`, Device `powered (pull-up on,
   waiting for a host)`, Error `none`; the calculator works normally, the status bar shows USB power, and
   it still switches itself off after 5 minutes without input.
2. With the computer: leave it alone for more than 5 minutes. Expected: it stays on (Device `configured`).
   Put the Mac to sleep: Device `suspended`, and the 5-minute auto-off applies again.
3. Unplug with cells fitted: no reset (boot log unchanged), USB status `detached (no VBUS)`.

## 6. Firmware update without the BOOT button

1. `python3 tools/calcctl.py flash` (add `--core` for the rollback images). Expected output: "restarting
   the calculator into its USB DFU bootloader ...", the two dfu-util runs (bank 2 then bank 1 with :leave;
   the "get_status" error after the second is expected), "flashed; waiting for the calculator to start
   ...", "calculator back on /dev/cu.usbmodem...". The session is restored after the update.
2. Manually: `python3 tools/calcctl.py dfu` → the calculator's screen goes dark; `dfu-util -l` lists
   `[0483:df11]` within ~2 s.

## Suspects (register-level code to check first if a step fails)

- `usb.c` preflight: `OTG_GSNPSID` is expected to read `0x4F54xxxx` on the H743's OTG2 core; the core soft
  reset (`GRSTCTL.CSRST`) needs the 48 MHz PHY clock, so "core soft reset timed out" means no USB kernel
  clock (PLL3Q 48 MHz, `RCC_D2CCIP2R.USBSEL = 2`).
- `GCCFG.VBDEN` cleared and B-session valid forced (`GOTGCTL.BVALOEN/BVALOVAL`, done by TinyUSB's
  `dcd_init`): PA9 is the UART TX, not VBUS. If the host never sees a connect, check `DCTL.SDIS` is clear
  after start (TinyUSB's `dcd_connect`).
- `RCC_AHB1LPENR.USB2OTGFSULPILPEN` must be 0 in Sleep (TinyUSB clears it): with it set the FS core stops
  in WFI (`hal_idle`), seen as enumeration stalling after the first requests.
- `PWR_CR3.USB33DEN` / `USB33RDY` (VDD33USB fed from 3V3, regulator off): the start waits 20 ms for RDY.
- The DFU restart relies on the existing `.noinit` magic checked first in `Reset_Handler` and on the
  watchdog not running across the reset (a software IWDG stops at reset). If the ROM bootloader does not
  enumerate, compare with the BOOT-button path (step 0).
- NVIC: `OTG_FS_IRQn` (101) at priority 6 in the vector table (`startup.c`).
