# Flashing the calculator (STM32H743ZGT6)

Images in this directory (rebuild with `make release` in `firmware/`):

| File | What | Where it goes |
|---|---|---|
| `bringup.elf/.bin/.hex` | Bring-up firmware: clocks, LED, console, rail/SDRAM/LCD/touch tests. 3V3_BUS stays **off** until you type `rail bus on` or `selftest`. Flash this first on a new board. | internal flash, 0x0800_0000 |
| `calc.elf`, `calc.hex` | The calculator, standard tier (app + real CAS + help tables and essential pages), **both flash banks** in one file | internal flash, 0x0800_0000 and 0x0810_0000 |
| `calc.bin` | Calculator, flash **bank 1** part (vectors, app, drivers) | internal flash, 0x0800_0000 |
| `calc_bank2.bin` | Calculator, flash **bank 2** part (section `.flash2`: CAS library, help tables). Always flash it together with `calc.bin` from the same release. | internal flash, 0x0810_0000 |
| `calc.map` | Linker map (for crash addresses) | — |
| `MODULES/MANUA010.MOD`, `MODULES/ANIMA010.MOD`, `MODULES/INDEX.TXT` | Content modules (the full manual; the Animation code module, which runs in place from the QSPI and is part of the advanced tier's `calc_qspi.bin`, docs/QSPI_CONTENT.md 12) for the microSD module library, named by version in 8.3 form (`MANUA010.MOD` = manual 0.1.0; files of earlier releases with other versions stay in the folder, and `INDEX.TXT` lists them all): copy the folder to `/CALC/MODULES/` on a card, then Memory › Install content (newest) or Memory › Modules (pick a version). Also `calcctl.py --upload-module`. | QSPI module store (installed by the firmware) |
| `calc_qspi.bin` | The QSPI **module store image** (every module, committed): OpenOCD writes it to the QSPI | QSPI, 0x9000_0000 |
| `calc_core.elf/.bin/.hex`, `calc_core_bank2.bin` | **Rollback firmware** (core tier, `TIER=core`): the whole manual and CAS in internal flash, uses no QSPI content at all, works with a dead QSPI chip | internal flash, like `calc.*` |
| `MANIFEST.txt` | Sizes, load addresses, vector-table check (written by `tools/release_info.py`) | — |
| `SHA256SUMS` | `shasum -a 256 -c SHA256SUMS` before flashing | — |

`calc.bin` and `bringup.bin` are raw images of flash bank 1 that start at **0x08000000** (the vector
table: initial SP 0x20020000, then `Reset_Handler`). `calc_bank2.bin` is the raw image of flash bank 2 at
**0x08100000**. On the STM32H743ZG (1 MB) the two banks are **not contiguous** (0x08080000–0x080FFFFF does
not exist), which is why there are two `.bin` files; a single raw image spanning both would make DFU
write to non-existent memory. `.hex` and `.elf` carry their own addresses and contain both banks, so
OpenOCD `program calc.elf` (or `.hex`) flashes everything in one go. The bring-up image uses bank 1 only.
`MANIFEST.txt` lists the bank usage (`make release` fails if a bank overflows).

Check the files first:

```
cd firmware/release && shasum -a 256 -c SHA256SUMS
```

Commands below assume the current directory is `firmware/`.

---

## 1. Raspberry Pi Debug Probe + OpenOCD (SWD) — normal method

Wiring: the probe's **D** (SWD) cable to the 1x6 DEBUG header (3V3, SWCLK, SWDIO, GND, TX, RX); the
probe's **U** (UART) cable to TX/RX of the same header. The board must be powered (USB-C or cells);
the probe's 3V3 pin is only a reference. The header has no reset line, so OpenOCD resets the MCU with
SYSRESETREQ (`reset_config none`).

```
# identify the chip (DBGMCU IDCODE: 0x10036450 = rev Y, 0x20036450 = rev V)
/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "init; mdw 0x5C001000; shutdown"

# bring-up image (ELF: addresses come from the file), verify, run
/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "program release/bringup.elf verify reset exit"

# calculator firmware
/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "program release/calc.elf verify reset exit"

# the same from .hex, or from the two .bin files with explicit addresses
/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "program release/calc.hex verify reset exit"
/opt/local/bin/openocd -f openocd/calc-cmsisdap.cfg -c "program release/calc_bank2.bin 0x08100000 verify; program release/calc.bin 0x08000000 verify reset exit"

# helper scripts (PROBE=stlink for an ST-Link on the 2x5 header)
openocd/flash.sh release/calc.elf
openocd/reset.sh
```

If the MCU is in a low-power state or the firmware reconfigured the SWD pins, hold the RESET button,
start the command, and release RESET right after "Listening on port ...". With an ST-Link on the
2x5 header (has NRST) use `-f openocd/calc-stlink.cfg`, which connects under reset.

Content (the QSPI module store; a new board's QSPI is empty and the firmware works without it, see
"Content on a new board" below): `openocd/flash_qspi.sh release/calc_qspi.bin 0`
(OpenOCD `stmqspi` bank 2; the config's `reset-init` handler sets up the QSPI pins and clock). This
writes the module store region only; the session log at 0xE00000 is untouched.

Console: `screen /dev/cu.usbmodemXXXX 115200` or
`python3 tools/calcctl.py --port /dev/cu.usbmodemXXXX "version"`.

---

## 2. USB DFU (ROM bootloader, BOOT0) — no probe needed

Install dfu-util with MacPorts: `sudo port install dfu-util` (Homebrew: `brew install dfu-util`).

1. Connect USB-C to the Mac.
2. Hold **BOOT**, press and release **RESET**, then release **BOOT**. (From running firmware the
   console command `bootloader` does the same in software.)
3. `dfu-util -l` must list `[0483:df11] ... name="@Internal Flash  /0x08000000/..."`.
4. Flash and start:

```
dfu-util -a 0 -s 0x08000000:leave -D release/bringup.bin

# calculator: bank 2 first (no :leave), then bank 1 with :leave
dfu-util -a 0 -s 0x08100000 -D release/calc_bank2.bin
dfu-util -a 0 -s 0x08000000:leave -D release/calc.bin
```

The content modules (the full manual, Python, ...) then go over USB as well: drop the `.MOD` files on
the CALC drive, or `python3 tools/calcctl.py --upload-module release/MODULES/MANUA010.MOD`.

**Rollback** (e.g. the QSPI chip is dead or a content problem must be ruled out): flash the core-tier
firmware the same way, it needs nothing from the QSPI:

```
dfu-util -a 0 -s 0x08100000 -D release/calc_core_bank2.bin
dfu-util -a 0 -s 0x08000000:leave -D release/calc_core.bin
```

`:leave` jumps to the new firmware afterwards. dfu-util then usually reports `Error during download
get_status`. This is expected: the board has already reset. If the board does not restart, press
RESET. On rev A (tested on board 3, through a USB hub, with the probe's UART wires off) `dfu-util -l`
shows `@Internal Flash /0x08000000/16*128Kg`, which covers bank 2 at 0x08100000 as well, and also
`@Option Bytes /0x5200201C`. The two-step calculator flash above worked and the calculator booted. Never
flash only one bank of the calculator: bank 1 code calls the CAS in bank 2.

If `dfu-util -l` lists nothing: try another cable/port (the USB-C port is sink-only, 5.1k CC pull-downs),
and disconnect the Debug Probe's UART cable. PA9 (USART1 TX on the DEBUG header) is also the STM32's
OTG_FS VBUS pin; if the ROM bootloader on this silicon samples VBUS there (AN2606 detail not yet
verified on hardware), an idle-high probe RX line or a floating PA9 could confuse it. SWD (§1) or the
UART bootloader (§3) always work.

---

## 3. UART bootloader via the DEBUG header (stm32flash)

Install: `sudo port install stm32flash` (if that port is not available: `brew install stm32flash`,
or build it from https://sourceforge.net/p/stm32flash). Version 0.6 or newer knows the STM32H74x
(device ID 0x450).

The ROM bootloader also listens on **USART1 = PA9 (TX) / PA10 (RX)**, which are the TX/RX pins
of the DEBUG header, i.e. the Debug Probe's UART. Enter it as for DFU (BOOT held during RESET),
then, with the probe's UART port:

```
stm32flash -b 115200 /dev/cu.usbmodemXXXX                        # identify (prints device 0x450)
stm32flash -b 115200 -w release/calc_bank2.bin -v -S 0x08100000 /dev/cu.usbmodemXXXX
stm32flash -b 115200 -w release/calc.bin -v -S 0x08000000 -g 0x08000000 /dev/cu.usbmodemXXXX
```

The bootloader uses **8 data bits, even parity, 1 stop bit** (stm32flash's default `-m 8e1`) and
auto-detects the baud rate from the first 0x7F byte. Close any terminal program on the port first.
The Debug Probe's CDC-UART passes the parity setting through (debugprobe firmware 2.x); if
`stm32flash` cannot connect, use DFU or SWD instead.

---

## Boot modes, option bytes and AN2606 notes

- **BOOT0 low** (default, 10k pull-down): the MCU boots from **BOOT_ADD0** = 0x0800 → 0x0800_0000
  (the firmware). **BOOT0 high**: it boots from **BOOT_ADD1** = 0x1FF0 → 0x1FF0_0000, the ST
  system memory, whose bootloader entry for STM32H74x/75x is **0x1FF0_9800** (AN2606).
  Both are the factory option-byte defaults; do not change BOOT_ADD0/BOOT_ADD1. Check them with
  OpenOCD if a board behaves oddly: `-c "init; mdw 0x52002040; shutdown"` (FLASH_BOOT_CUR,
  BOOT_ADD0 in bits 15:0, BOOT_ADD1 in bits 31:16; expected `0x1ff00800`).
- **Software entry** (`bootloader` console command): the firmware writes a magic word in DTCM
  `.noinit`, resets, and `Reset_Handler` jumps to 0x1FF09800 before touching any clock or peripheral,
  which is the state AN2606 requires. Untested until hardware; the BOOT button always works.
- **Clocks in the bootloader**: the H74x USB DFU bootloader runs USB from **HSI48 with CRS**
  (SOF synchronisation), so it works with or without the 25 MHz crystal. The USART bootloader uses
  HSI and auto-baud.
- **Interfaces the ROM bootloader scans** on this part: USART1 (PA9/PA10, on the DEBUG header),
  USART2/3, UART4, I2C1-3, SPI1-4, FDCAN1 and USB OTG FS (PA11/PA12, the USB-C port). On this board
  only USART1 and USB are wired to a connector; nothing else drives the scanned pins at reset, so
  no false detection is expected.
- **Read protection**: parts ship at RDP level 0. Nothing here changes RDP. Never set level 2.
- **Watchdog freeze (one-time per board)**: the IWDG must be frozen in Stop and Standby
  (FLASH_OPTSR FZ_IWDG_STOP = FZ_IWDG_SDBY = 0; factory default 1 = runs), or the firmware does not
  start its watchdog (a running IWDG would wake the switched-off calculator). Set them from the
  calculator console with `optbytes iwdg-freeze` then `reset`, or with a probe:
  `STM32_Programmer_CLI -c port=SWD -ob FZ_IWDG_STOP=0 FZ_IWDG_SDBY=0`. Check with `optbytes`
  (`fz_iwdg_stop=0 fz_iwdg_sdby=0`) or OpenOCD `-c "init; mdw 0x5200201C; shutdown"` (FLASH_OPTSR_CUR,
  bits 17 and 18 clear). Leave IWDG1_SW (bit 4) at 1, the software watchdog. Flashing new firmware
  (DFU, SWD, UART) keeps the option bytes.
- The internal flash erase granularity is 128 KB sectors; DFU and OpenOCD erase only the
  sectors they write.
- The saved user session lives in the QSPI flash (0x90E00000–0x90FFEFFF), which none of these methods
  touches: flashing new firmware keeps the user's variables, history and settings. The installed
  content modules (0x90000000–0x90DFFFFF) are kept too, and stay in use as long as the new firmware
  speaks their interface ABI (docs/QSPI_CONTENT.md §5.1); otherwise About says so and help falls back
  to its built-in pages until the new release's modules are installed.

## Content on a new board (empty QSPI)

The firmware boots with an empty QSPI: help shows its essential pages and "Manual not installed:
see Memory > Install content". Load the content modules with any of:

1. **microSD**: copy `release/MODULES/` to `/CALC/MODULES/` on a FAT card, insert it, Memory ›
   Install content (or answer Yes when the calculator offers it at start-up).
2. **Console** (Debug Probe UART): `python3 tools/calcctl.py --port /dev/cu.usbmodemXXXX
   --upload-module release/MODULES/MANUA010.MOD` (~25 s; the previous version stays in use until the new
   one is verified).
3. **OpenOCD**: `openocd/flash_qspi.sh release/calc_qspi.bin 0`.
4. **USB**: drop the `.MOD` files on the CALC drive, or `calcctl.py --upload-module` without `--port`
   (the calculator's own USB serial port).

Check with the console: `module list` (store contents and which version serves), `devices` (how
the QSPI, card, touch and power monitor answered), `state module_manual` (`ok`).

## Safe mode

Hold CLEAR, press ON, and keep both held together (at least 0.5 s, until the home screen shows SAFE
MODE): the calculator starts without content modules and without the saved session (which is kept).
ON alone, or CLEAR pressed after the start, never does it. It also starts in safe mode by itself after
three boots in a row that crashed or hung (the watchdog resets a hung firmware after ~2 s). Switch
off and on to leave it.

## After flashing

- Bring-up image: the PB8 LED blinks at 1 Hz and the console prints
  `# BRINGUP firmware: boot ...`, `# clock: SYSCLK 400 MHz from HSE ...` and
  `# sdram clock: SDCLK 100 MHz ... REV_ID 0x2003 (rev V)` (or `90 MHz ... 0x1003 (rev Y)`). Then follow the bring-up checklist in `firmware/README.md`
  (`power`, `rail bus on`, `selftest`, ...).
- Calculator: boot log lines `# clock: ...`, `# sdram clock: SDCLK ...`, `# sdram: ok`, `# lcd: ...`, `# touch: ...`, `# qspi: ...`;
  `version` reports `CAS=src`.
