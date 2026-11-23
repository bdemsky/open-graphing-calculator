# QSPI content: modules, tiers, loading and recovery

Status: **phase 1 implemented** (the help manual is the first module); **phase 2 code modules
implemented** (section 12: XIP code modules, the Animation module); CAS extension modules designed
(section 7). Code: `core/modstore.{h,c}` (store), `app/modules_ui.c`
(install / manage UI, console), `app/help.c` (manual reader and fallback), `core/safemode.{h,c}`
(recovery), `platform/stm32h743/iwdg.c` (watchdog), `tools/mkmodule.py` (packaging),
`tools/kconfig.py` + `Kconfig` + `configs/` (build configuration), `platform/hostdev/` (simulator
device models, the watchdog model and fault injection). The roadmap is section 11.

## 1. Why, and the ground rules

Both internal flash banks were ~78 % full; bank 2 held the CAS (~230 KB) and the compressed manual
(~135 KB with its tables). The 16 MB W25Q128 on QUADSPI held only the session log
(0xE00000–0xFFEFFF). USB DFU (the ROM bootloader) can program internal flash only. The MCU stays.
So large content moves into the QSPI, under these rules:

1. **The firmware always boots and stays usable** with the QSPI empty, stale, corrupt or dead.
   Nothing in the boot path, no interrupt handler and no code that runs while the QSPI is written
   lives in or reads from the QSPI.
2. **Content is packaged as independent modules**, each with its own version, compatibility range and
   commit marker. Installing, upgrading, reverting or removing one module never touches another.
3. **A module is used only if it is complete, verified and compatible** with the running core
   (interface ABI range, dependencies). Otherwise its feature falls back to what the core has.
4. **There is always a build that needs no QSPI at all** (the core tier, `TIER=core`), flashable with
   USB DFU alone, and every tier is built and tested by `make test`.

## 2. Tiers

| Tier | defconfig | Internal flash | QSPI module store | Works with dead QSPI | Status |
|---|---|---|---|---|---|
| **core** (rollback) | `configs/core_defconfig` (`CONTENT_INTERNAL`) | everything: app, full CAS, the essential help pages (`MANUAL_ESSENTIALS`, since 2026-10-20; the full manual no longer fits) | not used | yes, fully | implemented |
| **standard** (default) | `configs/standard_defconfig` (`CONTENT_QSPI`) | app, full core CAS, help tables + 9 essential pages | `manual` module (124 KB) | yes, minus the full manual | implemented (phase 1) |
| **advanced** | `configs/advanced_defconfig` (`CAS_EXT`) | as standard + the `cas` module interface | manual + CAS extension modules + data modules | yes: falls back to standard behaviour | interface reserved, modules phase 2 |
| **data** (above advanced, optional) | an advanced defconfig with data modules selected | as advanced | very large tables: integral rule base, special-function coefficients, units/constants DB, OEIS-style sequence tables | yes: falls back to advanced without the tables | design only |

The CAS **core** is the current `cas/src` and always stays in internal flash: every tier has the full
current CAS. Extension modules depend on the core, never the reverse. The numeric stub
(`cas_stub`, `CAS=stub` / `CONFIG_CAS_STUB`) remains a build option; with the tier design it is not
needed as a run-time fallback, because the core CAS never moves to the QSPI. (Should a future "XL core"
ever move the core CAS itself into the QSPI, the stub would be linked into internal flash as its
fallback through the same `cas.h` interface; section 7.4.)

What each tier enables, by state of its content (also shown in MEM › About, section 4):

| Feature | core | standard, manual OK | standard, manual missing / stale / corrupt / QSPI dead / safe mode | advanced, extension missing |
|---|---|---|---|---|
| Calculator, CAS core, graphing, all apps | yes | yes | yes | yes |
| Help: essential pages (index, getting started, keys, help, memory, installing, safe mode, power, errors) | yes | yes | yes | yes |
| Help: all ~390 pages | yes (built in) | yes (module) | no: "Manual not installed: see Memory > Install content" + reason + built-in pages | yes |
| CATALOG (all commands, summaries) | yes | yes | yes (tables stay in flash) | yes; extension commands marked |
| Extension commands (the `casx` module, 12.6) | yes (linked in) | after installing it from microSD | error "NAME needs the advanced CAS module (Memory > Modules)" | same error |
| Saved session | QSPI log | QSPI log | QSPI log; if the QSPI is dead: microSD fallback or a clear "not saved"; safe mode: not loaded/saved | QSPI log |

## 3. Module format and the store

### 3.1 Module file (`.MOD`) = installed extent

Defined in `core/modstore.h`, produced by `tools/mkmodule.py pack`.

| Offset | Size | Content |
|---|---|---|
| 0x0000 | 512 | **Header** (`mod_hdr_t`), CRC-32 in the last word |
| 0x0200 | — | 0xFF |
| 0x0F00 | 60 × 4 | **Activation slots**: programmed one by one with a store-wide sequence number; the extent's activation is the last programmed slot |
| 0x0FF0 | 4 | **Commit word** `CMIT`, programmed last by the installer (0xFFFFFFFF = not committed) |
| 0x0FF4 | 4 | **Remove word** (0xFFFFFFFF = present, 0 = removed: space reusable) |
| 0x1000 | `payload_len` | **Payload** |

Header fields: magic `CMOD`, format version (1), header size; **name** (16 bytes, `manual`);
**semantic version** major.minor.patch; **kind** (data / code-XIP / code-load); **interface** name
(`help`, `cas`) and the **ABI range** [abi_min, abi_max] of that interface the module works with;
**tier**; **Kconfig symbol** that selects it (`MANUAL_FULL`); **payload length, CRC-32, SHA-256**;
up to 8 **dependencies** `{name, min version, max version}`; `built_with` (firmware version + git rev,
informational). In a `.MOD` file the state words are erased; the installer ignores the file's bytes
512..4095 and writes its own.

All state changes of a module are single NOR word programs in its own header sector (commit,
activation, remove). There is **no central directory**: nothing shared is rewritten, so no operation
on one module can damage another.

### 3.2 The store

QSPI layout:

| QSPI offset | Use |
|---|---|
| 0x000000–0x9FFFFF (10 MB) | **module store** (`MODSTORE_QSPI_*`; 14 MB before the notebook store) |
| 0xA00000–0xDFFFFF (4 MB) | **notebook store** (`NB_STORE_*`, app/py_core.h; section 12.9) |
| 0xE00000–0xFFEFFF | session log (`app/session.h`, `core/pstore.c`), untouched |
| 0xFFF000–0xFFFFFF | reserved for the hardware `qspi test` command, untouched |

Extents start on 4 KB sector boundaries and occupy 4 KB + payload rounded up to 4 KB. Erases use
64 KB blocks (W25Q D8h) where aligned and 4 KB sectors elsewhere.

**Mount** (`modstore_mount`, at every boot; cheap): walk the region; at each sector read the first
word; on `CMOD` read and check the header (CRC, sizes) and the state words. A committed, not removed
extent is recorded and skipped as a whole; anything else (erased, damaged, uncommitted, removed)
advances one sector, because a newer extent may have been allocated inside the space of a removed
one. Cost: one 4-byte memory-mapped read per free sector (≤ 3584 for an empty 14 MB store, a few ms).
Payload CRCs are **not** checked at boot but on first use (`mod_read`), once per boot.

**Which version serves** a module name (`mod_boot` / `resolve`): among the live extents of that name,
newest activation first, the first that is
ABI-compatible and not known damaged; then versions whose dependencies are not served are ruled out,
repeated until stable. States: `ok`, `absent`, `abi` (installed but for an incompatible core),
`deps`, `corrupt` (payload CRC failed and no older version works), `nodev` (QSPI not responding),
`disabled` (safe mode), `builtin` (core tier).

**Install** (`mod_inst_*`, push API: the file arrives in order, any chunk size — from the SD card, the
console, or anything else):

1. header (bytes 0..511): format and CRC check; interface/ABI check (refused unless forced);
   **retention**: drop every other version of the same name except the active one (kept as the
   fallback for revert); allocate first-fit; if there is no gap, **compact** (3.3) and retry; only if
   that fails and the active version's space would make room is the active version removed too
   (reported); erase the first sector, program the header with the state words erased;
2. payload: erase ahead (64 KB blocks where aligned) and program as data arrives;
3. finish: read the payload back and compare its CRC; program the commit word; program an
   activation slot (the new version becomes active).

Power loss (or a dying console link, or a pulled card) **before 3** leaves an uncommitted extent that
mount ignores; the previously active version stays active. **Between commit and activation** the new
version is installed but inactive; the previous one stays active (the user can activate the new one by
"revert"). `tests/unit/test_modstore.c` cuts the power after every single erase and programmed byte of
an install and of a compaction and checks that the old or the new version is active and intact, and that
the store keeps working.

**Revert** (`modstore_revert`, Memory › Modules › Revert to x.y.z): program a new activation on the
previous version. **Remove**: program the remove word of every version. **Remove content**: remove all.

### 3.3 Fragmentation and compaction

First-fit allocation fragments the store when modules of different sizes come and go.
`modstore_compact` moves extents down into lower gaps that hold them whole (never overlapping the
source): copy header and payload, verify, commit, program the **same** activation number as the
original (so it is exactly as active as before), then remove the original. A power loss before the
commit leaves an ignored partial copy; after it, two identical copies, either of which may serve; the
next install or compaction drops one. An install runs compaction by itself when it finds no gap.
Phase-2 code modules relocated at install time (section 7) are re-relocated during the copy (the
relocation table is part of the payload).

Capacity: up to 32 live extents per store are tracked (all versions of all modules; raise
`MOD_MAX_EXTENTS` for the advanced tier: each costs ~60 bytes of DTCM).

### 3.4 The DFU stage (removed)

An earlier design let the ROM DFU bootloader write the store image into the last 128 KB sector of
flash bank 2 (0x08160000), read as a second, read-only store. Bank 2 has grown past that sector
(`make fw` reports both banks), and the USB console (`calcctl.py --upload-module`) and the CALC drive
now deliver modules over USB, so the stage and `hal_content_stage()` are gone; the QSPI is the only
module store.

### 3.5 Release file names

A module file's name never matters to the firmware (it reads the headers), but the release names
each file by its version so that versions sit side by side in `/CALC/MODULES/` and on a computer:
`tools/mkmodule.py filename NAME VERSION` gives **`STEM` + major + minor + patch + `.MOD`** in FAT 8.3
form, `STEM` being the module name in upper case (letters and digits) cut to 5 characters and each
version field one base-36 digit (0–9, A–Z = 10–35): manual 0.1.0 → `MANUA010.MOD`, 0.2.0 →
`MANUA020.MOD`, 1.12.3 → `MANUA1C3.MOD`. 8.3 because the firmware's FAT layer writes and lists 8.3
names and a card written on any computer then shows the same name. `mkmodule.py` refuses a version
field above 35 and, in `index`, two module names with the same stem or two files with the same name
and version. `make modules` writes `build/modules/<versioned>.MOD`; `make release` copies it to
`release/MODULES/`, keeps the files of other versions already there (a file of the same version is
replaced) and rewrites `INDEX.TXT` over all of them (`name version file bytes sha256 tier kconfig`,
sorted), so the folder is a library of versions. Memory › Modules lists every version on the card
under the installed one and installs the one picked (section 5, path a).

### 3.6 The manual module (phase 1)

`tools/gen_help.py --content qspi` produces two things from `help/*.txt`:

- **in internal flash** (`build/gen/help_gen.c`): the topic table (ids, titles), the CATALOG tables
  (labels, inserts, summaries, categories), the Huffman code lengths, and the **@essentials** pages
  (the line `@essentials ...` in `help/content.txt`: index, getting-started, keys, help, mem,
  install-content, safe-mode, power, errors; 6 KB compressed);
- **the module payload** (`build/gen/manual.bin`, help ABI 1): its own topic table, strings, Huffman
  code and every compressed page (122 KB). Self-contained, so a manual version works with any core
  that speaks help ABI 1: a firmware update that does not change the viewer's format keeps the
  installed manual.

`app/help.c` takes a page from the module if it is served and has the page, else from the built-in
essentials, else it shows the stand-in page: **"Manual not installed: see Memory > Install content"**,
the reason (QSPI empty / incompatible / damaged / not responding / safe mode), and links to the
built-in pages. Help never fails and SHIFT HELP still lands on the right topic id.

## 4. Boot states and what MEM › About shows

Boot sequence (`app_init`): `safemode_boot()` (section 6) → help workspace → `modules_boot()`
(mount the stores unless safe mode / core tier) → session (disabled in safe mode) → home screen →
`modules_boot_notice()`: a toast "SAFE MODE: Memory > About" or "flash fault: Memory > About", or, when
the manual is missing and the microSD card holds a usable manual module, the question "Install the
manual from microSD?" (`MODULE_SD_OFFER`).

MEM › About lists: **Start-up** first: the **last reset** (`power-on`, `switched on`, `restart`,
`crash (fault handler)`, `watchdog (the firmware hung)`, `reset pin`, `brown-out`; hardware: RCC_RSR,
the Standby flag and the fault handler's note) and **safe mode** with its reason, or `off`; then
firmware version, platform, **tier**; **Content**: the manual's source and version
(`Manual: module manual 0.1.0 from the QSPI (390 pages)` / `not installed (Memory > Install content);
9 built-in pages` / `installed, but damaged` / `installed, but made for an incompatible firmware` /
`the QSPI flash is not responding` / `not used in safe mode` / `full, built into the firmware`), other
modules, QSPI store use, the CAS; **Devices**: how each optional
device answered at boot (`hal_device_report`), e.g. `QSPI: not responding (JEDEC 000000)`,
`microSD: no card`, `Touch: not found (keys only)`, `Power monitor: not found`, `SDRAM: FAILED its test
(small memory mode)`, `Watchdog: on (2 s; frozen in Stop and Standby)` or `off: option bytes let it run
in Standby (console optbytes iwdg-freeze)`; **Memory**: CAS heap and the session state (`Session: NOT saved: the QSPI flash is
not responding; insert a microSD card to keep it`, `kept on the microSD card`, `not loaded or saved
(safe mode)`).

## 5. Loading paths

In order of priority for users:

| # | Path | Needs | How | Notes |
|---|---|---|---|---|
| a | **microSD module library** | a FAT card | copy the release folder `MODULES/` to `/CALC/MODULES/`; **Memory › Install content** installs, per module name, the newest version this firmware can use; **Memory › Modules** lists, under each installed module, every version of it on the card with new / upgrade / downgrade / reinstall / incompatible, and installs the one picked; offered automatically at boot when the manual is missing | the card is a *library*, never executed from (SDMMC is a block device, not memory-mapped; code would have to be copied to RAM first and random access is slow). Versions sit side by side under versioned 8.3 names (`MANUA010.MOD`, section 3.5); the firmware goes by the headers, so a renamed file still works. |
| b1 | **Console upload** (UART, debug probe's USB-UART) | the debug cable | `tools/calcctl.py --port /dev/cu.usbmodemX --upload-module MANUA010.MOD` (`module begin` / `module put HEX` × N / `module end`) | ~25 s for the manual at 115200 baud; tested end to end by `make upload-test` (interrupted upload, corrupted upload, real upload, reboot) |
| b2 | **OpenOCD stmqspi** | a debug probe | `openocd/flash_qspi.sh build/modules/calc_qspi.bin 0` writes the whole store image (modules committed and activated) | for factory/bring-up; replaces the whole store |
| c | **USB** | USB-C | drop the `.MOD` file on the CALC drive (installed from the main loop), or `tools/calcctl.py --upload-module MANUA010.MOD` over the USB serial console (several lines in flight) | the end-user path; the TinyUSB CDC + MSC device (README "USB") |

Recommendation for end users: USB (c) or the microSD card (a); (b) is the development and recovery
path.

Times (W25Q128JV datasheet, typical / max): the 124 KB manual extent = 1 block erase (150 ms / 2 s)
+ 15 sector erases (45 ms / 400 ms each) + 496 page programs (0.4 ms / 3 ms) ≈ **1.1 s typical, 9 s
worst case**, plus reading the card (~0.1 s). The installer shows a progress bar. Verification: every
payload is read back and CRC-checked before the commit; `Memory › Modules › Verify` re-checks.
Wear: 100 000 erase cycles per sector; an install erases each sector of its extent once, so wear is
irrelevant; the session log has its own region and wear levelling. Besides the power-off and idle-minute
saves, a game may ask for a save soon (`session_save_soon`, modx_api_t.save_soon: Sparkle Town after an
order won, a round, a purchase or a placement): written at the next 1.5 s without input, at most once
every 30 s. At about 100 KB a write into the 2 MB log, two hours of play a day are about 240 writes, some
12 erases of each log sector a day: 100 000 cycles last over 20 years.

**Firmware update without content update**: modules are tied to an **interface ABI**, not to a build.
A firmware that keeps help ABI 1 keeps using the installed manual (an older manual simply lacks new
pages, which show the stand-in page with "install the manual of this firmware's release"). A firmware
that changes the format bumps the ABI; the old module is then `abi` (disabled, About says so, help falls
back) until the new release's module is installed. `make release` always produces the firmware, its
modules (`MODULES/*.MOD` under versioned names, `INDEX.TXT` listing every version in the folder),
the store image `calc_qspi.bin` and the core-tier rollback firmware together, with SHA-256 sums.

### 5.1 ABI versioning rules

- Each interface the core offers (`core_ifaces` in `app/modules_ui.c`: `help` = 1; `cas` = 1 reserved
  in the advanced tier) has one integer ABI version.
- Bump it when anything a module relies on changes incompatibly: payload layout (help: topic table,
  string pool, compression format), function-table layout, calling convention, the meaning of a
  field, or a core function a module calls changing its signature or semantics.
- Additions that old modules do not see (a new optional field at the end of a table with a size
  field, a new core function appended to the API table) do not bump the ABI; modules check the table
  size before using new entries.
- A module declares the range it was built and tested for, normally [current, current]; a module
  that also works with a later core may widen abi_max when it is re-released.
- The core never accepts a module outside its range (`mod_abi_ok`); `module begin force` exists for
  development only.
- Module versions are semantic: major = incompatible change for dependants (other modules' dependency
  ranges), minor = additions, patch = fixes. Dependencies name ranges (`min..max`).

## 6. Safe mode and failure handling

**Safe mode** (`core/safemode.c`) boots plain: no modules, no saved session (it stays untouched for the
next normal start), no SD offer. Entered when

- **ON and CLEAR are held together at power-on**, both down for **0.5 s** (`SAFEMODE_KEY_MS`; hold
  CLEAR, press ON, keep both down until SAFE MODE shows). `safemode_keys_held()` decides once, in
  `app_init`: both must be down when it runs and stay down until they have been held together for
  0.5 s; the time they were already held counts (`hal_keypad_held_ms`). On the hardware the key
  scanner runs from the first SysTick after the pins are configured (`keypad_init` is the step right
  after `gpio_config_from_table` in `hal_init`, before LCD, SDRAM, touch and microSD), so keys held
  through the reset are debounced ~15 ms after it and timestamped; ON is PA0 (WKUP1: `keypad_init`
  gives the pin back to the GPIO after a Standby wake), CLEAR a matrix key. The boot normally takes
  longer than 0.5 s, so there is no extra wait. A plain ON press never triggers it (CLEAR is not
  down), nor does CLEAR pressed after the start (the check runs once); releasing either key before
  0.5 s gives a normal start. The held keys are dropped from the key queue in safe mode. Or
- after **3 boots in a row that did not reach 10 s of uptime** (`CONFIG_SAFEMODE_CRASHES`): the
  boot-attempt counter lives in `.noinit` DTCM, which survives the fault handler's reset and a
  watchdog reset but not Standby or a power loss; Standby (power-off), the console `reset` and
  `reboot` clear it, a boot that runs 10 s clears it.

Leave safe mode by switching off and on.

### 6.1 Watchdog

The independent watchdog **IWDG1** (`platform/stm32h743/iwdg.c`, `CONFIG_WATCHDOG`, default on) turns
a hang into a reset: LSI / 64, reload 999 → **2.0 s** (1.9–2.2 s over the LSI tolerance). A watchdog
reset sets RCC_RSR.IWDG1RSTF; the boot-attempt record survives it, so **a hang counts as an unfinished
boot exactly like a crash** and a hang loop at start-up reaches safe mode after three resets. About
shows `Last reset: watchdog (the firmware hung)`; the boot log says `boot: watchdog ...`.

Fed by: the main loop (`calc_step`, every iteration; the idle is WFI with the 1 kHz SysTick, so at
least every millisecond), the CAS through its poll hook (`app_cas_poll`, installed in `app_init`;
the time-boxed helpers restore it instead of NULL), the console dispatcher, and the long blocking
operations in their wait loops: QSPI erase/program (`wait_ready`: a 64 KB block erase may take 2 s),
the microSD install loop and card waits, the session save, the Standby entry (waiting up to 5 s for ON
to be released), the SDRAM memtest, `tp watch`, `pwr sleep`. Not fed: `hal_delay_ms`, so a loop that
only waits is still caught (`wdg hang` is such a loop). Started by `calc_setup` after `hal_init`,
i.e. not during the bounded device probing; the bring-up image never starts it.

**Sleep, Stop and Standby** (RM0433: the IWDG runs on the LSI in every mode unless the option bytes
FZ_IWDG_STOP / FZ_IWDG_SDBY freeze it; factory default: runs). Between key presses the MCU is only in
Sleep (WFI), where the main loop runs every millisecond: no issue. **Standby is "off"** and ends only
with a reset, so "wake up to refresh" is impossible there: an IWDG running in Standby would reset the
switched-off calculator 2 s after SHIFT ON, i.e. switch it on again (and boot ~1 s on every cycle,
defeating the ~35 µA budget). **Decision: freeze the IWDG in Stop and Standby with the option bytes**
(`FZ_IWDG_STOP = FZ_IWDG_SDBY = 0`), a one-time board set-up step (bring-up checklist in README.md:
console `optbytes iwdg-freeze`, or STM32CubeProgrammer `-ob FZ_IWDG_STOP=0 FZ_IWDG_SDBY=0`; the
other option bytes, RDP included, are left as they are). The firmware checks the setting at every
boot (`boot log: watchdog option bytes: ...`, console `optbytes`, `wdg`, About › Devices) and does
**not start** the watchdog when Standby is not frozen (About: `Watchdog: off: option bytes let it run
in Standby`), so an unconfigured board keeps working as before. Stop (only the console `pwr stop`
uses it) is also covered without the option byte: `power_enter_stop` then cuts the Stop into RTC
wake-ups of ≤ 1.5 s and refreshes in between. DBGMCU_APB4FZ1.DBG_IWDG1 freezes it while a debugger
halts the core. A hardware-watchdog option byte (IWDG1_SW = 0) is handled too: `iwdg_early()`
stretches its 0.5 s reset default to 2 s first thing in `hal_init`.

**Simulators**: a watchdog model on the virtual clock (`platform/hostdev`): it expires 2 s of
virtual time after the last kick; the main loop then restarts the app as the reset would
(`app_crash_restart(HAL_RESET_WATCHDOG)`: counts as an unfinished boot). The console `wdg hang` loops
without kicking (on hardware the IWDG resets it). The interactive simulator in real time has the
model off. `tests/ui/85_watchdog.txt` hangs three times and checks the reset cause, the counter, safe
mode and About.

**Device probing**: QSPI, microSD, touch, INA226 and SDRAM are probed at boot with bounded waits
(QSPI: JEDEC ID check, every poll bounded, `leave_mmap` bounded; SD: 1 s init timeout; touch and
INA226: I2C timeouts with bus reset; SDRAM: quick check with an SRAM fallback). A failure is logged on
the console, shown in About, and the feature degrades.

**Session fallback when the QSPI is unusable** (recommended and implemented, `SESSION_SD_FALLBACK`):
the session is written to `/CALC/AUTOSAVE.SES` on the microSD card (payload + CRC; the FAT layer
publishes a file only on close, so a power loss keeps the previous copy) whenever the QSPI log is
absent or a save to it fails, and read back at boot when the log has no snapshot. Without a card About
says "NOT saved". Alternatives considered: a spare internal-flash sector (128 KB sectors, 10 k cycles,
and bank space is the scarce resource) and RTC backup SRAM (4 KB, too small for the 256 KB session).

### 6.2 Failure matrix

| Failure | What still works | What the user sees | Recovery |
|---|---|---|---|
| QSPI dead (no JEDEC answer) | everything except the manual module and the QSPI session log | toast "flash fault"; About `QSPI: not responding (JEDEC xxxxxx)`; help stand-in page; session on the microSD card or "NOT saved" | core-tier firmware via USB DFU (no QSPI needed); fix/replace the chip |
| QSPI answers garbage / writes fail | as above (modules fail their checks; saves fall back to the card) | install fails with "flash write failed"; session "last save FAILED" or kept on the card | as above |
| QSPI empty (new board) | everything except the full manual | help stand-in page, About `not installed`, boot offer if a card has the module | Install content (card), the CALC drive, console upload, OpenOCD |
| Module stale (ABI mismatch after a firmware update) | as empty | About `made for an incompatible firmware`; help says so | install the release's modules; or revert the firmware |
| Module corrupt (payload CRC) | previous installed version if any, else as empty | About `installed, but damaged` | reinstall; Memory › Modules › Verify / Revert |
| Install interrupted (power cut, pulled card, broken link) | the previously active version | install message "flash write failed" / card error | install again |
| Bad module crashes the firmware at boot | — | 3 crashes → safe mode by itself | safe mode → Memory › Modules › Remove / Revert, or Remove content |
| Firmware hangs (no fault: an endless loop, a stuck peripheral wait) | after the watchdog reset: everything | a restart after ~2 s, About `Last reset: watchdog`; a hang at every start → safe mode after 3 | as for a crash; boards without the option bytes set have no watchdog (About says so): `optbytes iwdg-freeze` |
| Corrupt session crashes or hangs at boot | — | safe mode (ON + CLEAR, or the crash loop) | safe mode, then Memory › Reset all, or restore a backup |
| microSD dead / absent | everything; content via console/probe/DFU | `microSD: ...` messages, About `microSD: not responding` | other load paths |
| Touch controller dead | everything by keys | About `Touch: not found (keys only)` | — |
| INA226 dead | everything; no current readings | About `Power monitor: not found` | — |
| SDRAM failed its test | calculator in the 320 KB SRAM fallback arena (no graph cache, small help workspace) | boot log, About `SDRAM: FAILED` | hardware |
| Internal flash image bad | — | — | USB DFU / SWD / UART bootloader (ROM, cannot be bricked) |
| Advanced extension module missing (phase 2) | standard behaviour | the command reports "needs advanced content (install from Memory)" | install the module |

## 7. Phase 2: the CAS extension modules (advanced tier)

Goal: approach Mathematica's breadth within the compute and memory limits: special functions, extended
integration (a large rule base as data, Rubi-style), sums and recurrences, number theory and
combinatorics, assumptions, sparse multivariate polynomials, extended linear algebra, statistics and
distributions, transforms, a large units and constants database. Several MB of code and data; the
store has 14 MB.

### 7.1 Module architecture

- **Core** (`cas/src`, internal flash) works alone and owns parsing, the expression representation,
  the evaluator and the simplifier. It exports a versioned **core API table** (`cas_core_api_t`:
  `{abi, size, alloc, expr constructors/accessors, eval, simplify, error, poll, ...}`) passed to each
  module's init. Modules call the core only through this table; the core never calls a module except
  through the registry.
- A **code module** starts with a descriptor (`cas_module_desc_t`: magic, ABI, the module's functions
  `{name, arity, flags, handler}`, rewrite hooks `{phase, pattern head, handler}`, init/fini, the RW
  size it needs). The core allocates the module's RW state from the CAS arena at init; **modules have
  no static RW data** (no .data/.bss), so their image is pure read-only code and constants.
- **Registry**: at boot the app resolves which `cas` modules are served (section 3.2) and the core
  registers their functions and hooks. Command lookup: core table → registered modules → the
  **known-commands table** (generated from the help sources, which know every command's module via a
  `module:` field on `@cmd` pages) → otherwise unknown. A command known but not served gives
  `ERR:UNSUPPORTED needs advanced content (install the special module from Memory)`: never a crash.
- **Dependencies** only on the core (via its ABI) and on other modules (by name + version range);
  the resolver disables a module whose dependencies are not served.
- **Catalog and help** show the tier: gen_help adds a marker to CATALOG labels of module commands and a
  line "Provided by the advanced module `special` (installed / not installed)" to their pages.
- **Kconfig**: each module is a `tristate` symbol under "Extension modules": `y` = linked into the core
  in internal flash, `m` = built as a QSPI module, `n` = left out (the placeholders are `bool` today;
  selecting one stops the build: "no sources yet").

### 7.2 Execution model

Options for MBs of extension code:

| | XIP from QSPI | Copy all into SDRAM at boot | Load on demand into an SDRAM code cache | Copy into AXI SRAM |
|---|---|---|---|---|
| RAM cost | none | all of it (MBs out of the ~24 MB CAS arena) | a bounded cache (e.g. 1–2 MB) | impossible (512 KB, nearly full) |
| Boot / first-use cost | none | ~30 ms per MB at 33 MB/s, after **every** power-on (SDRAM is off in Standby, and wake is a reset) | per module at first call (~30 ms per MB) | — |
| Miss cost (32-byte cache line) | ~1.3 µs random at 66.7 MHz quad (8 + 6 + 2 + 4 + 64 clocks), ~0.85 µs at 100 MHz | ~0.2 µs (100 MHz 16-bit SDRAM, CL2) | ~0.2 µs | — |
| Constraint | no XIP code may run while the QSPI is in indirect mode (session save, install) | MPU: SDRAM executable (XN=0, Normal, write-back); D-cache clean + I-cache invalidate after copying | same as SDRAM, plus eviction | — |

Internal flash for comparison: a 32-byte line in a few AXI cycles (~25–35 ns at 200 MHz with 2 wait
states and the flash's 256-bit read width). The M7 has 16 KB I-cache and 16 KB D-cache. Estimated
slowdown of code running from QSPI XIP relative to internal flash, with ~10 instructions per cache
line: ~1× in hot loops that fit the I-cache, ~2× at a 5 % line-miss rate, ~5× at 20 % (cold, branchy
code); from SDRAM ~1.2–1.8×. Data read in place from QSPI (rule tables, coefficients) costs the same
per line, but such tables are accessed sparsely, so copying them would be the larger cost.

**Recommendation: hybrid.** Extension code runs **XIP from the QSPI by default** (zero RAM, zero load
time, MBs possible); modules (or functions) marked `hot` in their descriptor, or found hot by a
per-module call counter, are copied into a **bounded SDRAM code cache** (default 1 MB of the arena,
LRU by module, reloaded lazily after every boot) and run from there. **Large read-only data is always
read in place** from the memory-mapped QSPI (section 7.3). QSPI clock: 66.7 MHz (/3) stays the
default; **`CONFIG_QSPI_100MHZ`** (Kconfig › Hardware) selects /2 = 100 MHz, and the console
`qspi clock 66|100` switches at run time (until the next boot) to try it. Raise it only after the
bring-up checklist step "QSPI clock" (README.md): measure CLK and IO0–IO3 at the flash (ringing,
rise times, setup/hold against the W25Q128JV and DS12110 QUADSPI timings), then `qspi clock 100`
and `qspi stress` (whole-chip CRC through the memory-mapped window and by indirect reads against a
66.7 MHz reference, plus test-sector erase/program/verify, N passes) with no errors, then build with
the option. (The W25Q128JV allows 133 MHz for EBh; the H743's QUADSPI is specified up to ~133 MHz
SDR.) Continuous-read mode (M7-0 = 0x20, skips the instruction byte on random reads) would give
another ~10 % less miss time.

The write conflict, solved by rule: all QSPI writes (pstore session saves, installs, compaction) run in
the main loop with the CAS idle; the writer (`qspi.c`, `pstore.c`, `modstore.c`, `session.c`), all
interrupt handlers and everything they call stay in internal flash or RAM (the linker script keeps
module code out of `.text` by construction: modules are separate images). A session save requested
while a CAS evaluation is in progress is deferred until it returns (the autosave already runs only
after 60 s idle; the rule becomes an assertion: `mod_xip_active` must be false in `pstore_save`).
`cas_set_poll` callbacks must not write the QSPI. If a long erase ever had to coexist with XIP reads,
the W25Q's erase suspend/resume (75h / 7Ah) would allow it; not needed.

### 7.3 Data in place

Data modules (`kind = data`) are read through `mod_read()` today (a copy into a caller buffer, which
works on every platform and store). Phase 2 adds `mod_map(name, &len)`: a pointer into the
memory-mapped window (hardware) or the emulated flash (simulators) for zero-copy reads of rule bases,
coefficient tables and the units database; valid until the next QSPI write (the same rule as XIP).

### 7.4 Linking a module

- Built from `cas/ext/<module>/` into its own image: `arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb
  -ffunction-sections -fno-common -mlong-calls`, linked at virtual base 0 with `--emit-relocs`
  (`-q`); no .data/.bss allowed (the link fails on them).
- `mkmodule.py pack --kind code-xip` stores the image plus its `R_ARM_ABS32` relocation list
  (absolute pointers in constant tables: the descriptor, jump tables, string addresses; Thumb
  branches inside the image are PC-relative and need none; calls to the core go through the API
  table, so there are no relocations against the core).
- The **installer relocates while it writes** (adds the extent's mapped address to each listed word as
  the payload streams through), so the installed copy is ready to execute in place; compaction
  re-relocates when it moves an extent; the SDRAM code cache relocates to the cache address when it
  loads. Position-independent code (`-fpic -msingle-pic-base`) was the alternative: no relocation, but
  every global access through r9 costs code size and speed in the core's hottest call paths.
- The stub and the real CAS keep presenting the same `cas.h`; extension modules plug in behind it.
- **Simulator and web**: link every module directly (all sources compiled into the host binary,
  registered statically), but the registry still asks the module store whether the module is served:
  the factory content image the simulators preload contains each module's header (and payload), so
  `module remove special`, `module coreabi cas N` or `--no-content` emulate "not installed" /
  "incompatible" exactly like on hardware, and UI tests can cover both.

### 7.5 Build plumbing (already in place)

- `Kconfig` describes every option; `make menuconfig`, `make defconfig TIER=x`, `make savedefconfig`;
  the tiers are `configs/{core,standard,advanced}_defconfig`; `make fw TIER=x` is shorthand. Each
  build directory has its own `.config` → `autoconf.h` (`-include`d everywhere) + `config_gen.c`
  (`build_config[]`, `build_tier`). kconfiglib 14.1.0 (pure Python, ISC) is vendored in
  `third_party/kconfiglib`.
- `make modules` builds `build/modules/*.MOD`, `INDEX.TXT` and `calc_qspi.bin` (store image) from
  the selected options; the simulators link the store image as their factory content
  (`build/gen/content_img.c`).
- `make fw` checks each internal flash bank against `CONFIG_FLASH_BUDGET_PCT` (90 %; 80 % until the home notebook) and reports the
  QSPI store use; over budget it names the options to turn off or
  move to the QSPI.
- `make test` checks every defconfig (`config-check`), runs the UI tests on the simulator of every
  tier (`tier-test`; tier-specific scripts use `requirecfg` / `ifcfg`), links every tier's firmware
  when the ARM toolchain is present, and tests the console upload end to end (`upload-test`).
- Release: firmware, `MODULES/` (one file per module + `INDEX.TXT`), `calc_qspi.bin`, and the
  core-tier rollback firmware `calc_core*.{bin,hex,elf}`; the tiers' defconfigs choose which modules
  a "full install" contains.

## 8. QUADSPI indirect vs memory-mapped (RM0433)

- CCR may only be written while BUSY = 0. In memory-mapped mode BUSY stays set (the peripheral may
  hold nCS low for a prefetch), so every indirect command first sets ABORT and waits for ABORT and BUSY
  to clear (`leave_mmap`, now with bounded loops), and `qspi_memory_mapped()` restores mode 3 after
  every indirect operation. The timeout counter (TCEN) is off: nCS stays low between memory-mapped reads, because reads that race the timeout can return wrong data.
- The window (0x9000_0000) is backed by the flash only in memory-mapped mode; during an indirect
  command nothing may fetch from or read it. All window users (module reads through
  `hal_storage_read`, and phase-2 XIP) run in the main loop, which is inside the writer during a write;
  no interrupt handler touches the window. The MPU maps 0x9000_0000–0x9FFF_FFFF no-access except the
  16 MB of the chip (read-only, cacheable, write-through), so speculative accesses cannot reach
  unmapped addresses.
- After programs and erases, `invalidate_view()` invalidates exactly the affected D-cache lines (never
  the whole D-cache, which would drop dirty SDRAM lines). Phase-2 XIP adds an I-cache invalidate of
  the range after installing or compacting a code module (`SCB_InvalidateICache` is global and
  harmless).
- Erase sizes: 4 KB (20h, tSE ≤ 400 ms), 64 KB (D8h, tBE2 ≤ 2 s; new `qspi_erase_block64`, used by
  `hal_storage_erase` for aligned 64 KB runs).

## 9. Testing (phase 1)

- `tests/unit/test_modstore.c`: empty store, install, header/ABI/size errors, upgrade keeps the
  previous version, revert, remove, space reuse, store full / fragmentation, power cut at every
  operation of an install (30 k cuts) and of a compaction (58 k cuts), resolution: ABI mismatch,
  dependencies, damaged active version falling back, safe mode.
- `tests/ui/86_modules.txt`: valid / empty (boot offer) / power cut mid-install / damaged / ABI
  mismatch ("stale") content, the help stand-in screens, About, Memory › Install content, Modules
  with two versions on the card (`MANUA010.MOD`, `MANUA020.MOD`: upgrade, downgrade, install of a
  picked version, Install content taking the newest), Remove content. `87_device_faults.txt`: QSPI
  absent, reading 0xFF / 0x00 with failing writes, dead microSD, the session on the card.
  `88_safemode.txt`: CLEAR alone and ON alone (no safe mode), ON + CLEAR released after 0.3 s (no
  safe mode, console `hold MS KEYS`), CLEAR pressed after the start, ON + CLEAR held (safe mode),
  crash loop, recovery, counter reset after 10 s. `85_watchdog.txt`: the simulated watchdog: a hung
  loop resets, the reset cause, three hangs → safe mode. `89_core_tier.txt`: the rollback build.
- `tests/run_upload.py` (`make upload-test`): console upload end to end.
- Every UI test also runs in WebAssembly (`make web-test`), and on the core and advanced tiers
  (`make test` → `tier-test`).

## 10. Decisions (former open questions)

1. **Watchdog**: IWDG1 at ~2 s, frozen in Stop and Standby by the option bytes (one-time board
   set-up), not started without them; watchdog resets count towards safe mode (section 6.1).
2. **QSPI clock**: 66.7 MHz stays the default; `CONFIG_QSPI_100MHZ` / `qspi clock 100` after the
   bring-up measurement and `qspi stress` (section 7.2). Continuous-read mode: with phase-2 XIP.
3. **USB**: TinyUSB CDC as the end-user install path, planned for phase 2 (section 11.1); not
   implemented now.
4. **Safe-mode key**: ON + CLEAR held together for 0.5 s at power-on (section 6).
5. **Release file names**: versioned 8.3 names (`MANUA010.MOD`, section 3.5), versions side by
   side, Memory › Modules picks one.
6. Bank 1 is at 74.2 %, bank 2 at 71.3 % (budget 80 %) with the code-module core side (12.3).

## 11. Roadmap

Phase 1 (done): the manual as a module, the tiers, safe mode (ON + CLEAR, crash and hang loops),
the watchdog, device-fault resilience, versioned module files, the core-tier rollback build.

Phase 2 (planned):

1. **USB as the end-user module-install path: done.** TinyUSB 0.18.0 on OTG FS (README.md "USB",
   docs/USB_BRINGUP.md): CDC-ACM carries the console next to the UART, so `tools/calcctl.py
   --upload-module` works over USB without a debug cable (it finds the calculator by VID:PID and keeps
   8 lines in flight); and a mass-storage "CALC" drive installs the `.MOD` files dropped on it
   (checked whole first, then the same installer as Memory > Modules, from the main loop, never while
   the module runs). VBUS comes from the PA2 divider (debounced start/stop); the interrupt only queues
   events and the stack runs from the main loop, all in internal flash (bank 2) and RAM, as section 7.2
   requires. Console `dfu` restarts into the ROM DFU bootloader without the BOOT button (`calcctl.py
   flash`). The flash it needed (~24 KB) came from moving the Geometry app into a code module (12.7).
2. CAS extension modules (section 7) on the code-module mechanism of section 12 (done: XIP,
   API table, descriptor, binding, fallback, Animation); still to do: the CAS registry, the
   bounded SDRAM code cache for hot modules (a `code-load` kind: copy, `hal_code_sync`, run from
   SDRAM with MPU region 1 made executable).
3. `mod_map()` zero-copy data modules (section 7.3) and the data tier.
4. QSPI at 100 MHz and continuous-read mode once measured on the board (section 7.2).

## 12. Phase 2: code modules (implemented; Animation is the first)

Code: `app/modx.{h,c}` (API table, descriptor, binding, trampolines, console `modx`),
`core/modstore.c` `mod_map` (XIP base), `platform/stm32h743/qspi.c` (`xip_allow`),
`hal_storage_xip` / `hal_code_sync` (hal.h), `modules/modx_image.ld` + `modules/modx_rt.c` (image
link), `tools/modlink.py` (relocation check), `tools/mkmodule.py` (`--kind code-xip`, `placeholder`,
`store --library`), the first module `modules/animation/` with its core hooks `app/anim_core.c`.
Tests: `tests/unit/test_modx.c`, `tests/unit/test_gif.c`, `tests/ui/38_animation_2d.txt`,
`tests/ui/39_animation_3d.txt`; bring-up: README.md checklist step 15, "Code modules".

### 12.1 What a code module is

A content module (3.1) with `kind = code-xip` and interface `modx` (ABI 1, `core_ifaces` in
`app/modules_ui.c`), whose payload is a machine-code image:

| Image offset | Content |
|---|---|
| 0 | descriptor `modx_desc_t` (40 bytes): magic `CDMX`, modx ABI, descriptor size, name, RW size, **entry offset** (Thumb bit set), image size, API-table size it needs |
| 40 | code and read-only constants (`.text`, `.rodata`) |

- **No static RW data.** The link script refuses `.data`, `.bss` and GOT entries. The module's state
  is one block the core allocates from the app arena (`MODX_POOL` = 512 KB of SDRAM, carved in
  `app_init`, big arena only) and zeroes at every bind; the Animation module takes 316 KB of it (its
  GIF encoder's tables and frame buffers).
- **Position-independent instead of install-time relocation** (7.4 offered both). Compiled with
  `-fPIE -fvisibility=hidden -ffreestanding`: every reference to the module's own code and
  constants is PC-relative (`R_ARM_REL32` literals, `bl`), so the image runs at whatever address
  the store gave it, the payload CRC/SHA in the header stay those of the file, and install,
  and compaction need no relocation pass. The price is that a constant table may not
  hold pointers (tables of `char[]`, function addresses taken in code). `tools/modlink.py` checks
  the `-q` (emit-relocs) link: the only absolute relocations allowed are the descriptor's entry and
  size words, which are link addresses = offsets because the image is linked at 0. It also checks
  the descriptor in the raw binary.
- **Calls into the core only through the API table** `modx_api_t` (passed to init; 36 entries:
  time, watchdog, input pending, snprintf / fmt_num / strcmp / floor, gfx primitives and fonts,
  framebuffer and flush, toast / message / soft keys, console print and errors, the CAS
  compile/eval API, the graphing hooks `anim_target` / `anim_apply` / `anim_finish` / `g3_grid`,
  one microSD file at a time). No C library is linked (`-nostdlib`; `modx_rt.c` has memcpy/memset
  for compiler-generated calls), so a module cannot reach anything else.
- **Exports**: init fills `modx_ops_t` (tick, event, draw, console, state), called only through
  the core's trampolines.
- Versioning (5.1): appending API entries keeps ABI 1 (`size` grows; `desc.api_size` says what a
  module needs); changing an entry, the descriptor, the ops or `anim_target_t` bumps `MODX_ABI`
  (and `MODX_ABI` in the Makefile, which stamps the .MOD header).

### 12.2 Binding and execution

`modx_get(name)` binds on first use after boot or after any store change: the module must be
served (`mod_state == ok`, which already includes the interface/ABI and dependency checks of 3.2);
its descriptor is read with `mod_read` (which verifies the payload CRC before anything of it is
used) and checked (magic, ABI, name, `kind`, API size, RW size within the pool); then

- **hardware**: `mod_map()` gives the payload's address in the memory-mapped window
  (0x9000_0000 + extent + 4 KB), the entry
  must be a Thumb address inside the image, `hal_code_sync()` invalidates the I-cache (QSPI writes
  bypass the caches; qspi.c already invalidates the D-cache lines of written ranges), and the
  code **executes in place**. MPU region 4 (`mpu_cache_init`) maps the 16 MB as Normal,
  write-through, read-only, executable; the M7 I-cache (16 KB) holds the hot loop, so a frame of the
  Animation module (~6 KB of code) runs from cache after the first one;
- **simulators** (native and web): the same checks against the stored payload (the factory content
  holds the real ARM image, or a descriptor-only placeholder without the ARM toolchain), then the
  linked host function of `modx_host[]`; same API table, same pool, same trampolines.

The bound module's init runs once per bind; a failed bind is remembered with its reason
(`modx_why`) until the store changes, so a missing module costs one check.

**The write rule, enforced** (7.2): every call into a module goes through a trampoline that counts
it busy (`modx_busy`). Nothing reachable from the API table writes the QSPI except through
`session_save`, which now refuses while a module is busy (returns -2 and leaves the session dirty:
the idle autosave or power-off saves it). Installs, removals, revert, format, compaction and
`module coreabi` first unbind every module (`modx_unbind_all`: only forgets pointers, never calls
into module code that may already be gone) and run from the Memory menus or the console, never
from module code. Interrupt handlers never call modules. While an indirect QSPI command owns the
bus, `qspi.c` sets MPU region 4 execute-never (`xip_allow(false)` in `leave_mmap`, cleared again
in `qspi_memory_mapped`): RM0433 gives window accesses no meaning outside memory-mapped mode, and
the Cortex-M7 does not fetch speculatively from XN regions, so not even a predicted branch into a
module can touch the bus during a write. Rewriting RASR of an enabled region in place is allowed
(Armv7-M ARM B3.5.3); DSB + ISB make the change apply to the next fetch.

**Fallback.** `modx_get` returns NULL and the feature shows `modx_why`: "Install the Animation
module (Memory > Modules)." (absent), "... for another firmware ..." (ABI), "... damaged ..."
(CRC), "... QSPI flash is not responding ..." (no device), "Modules are not used in safe mode",
"... needs N KB of the SDRAM ..." (small arena), "... is not part of this firmware (core tier)."
The console `modx off NAME` emulates "not installed" on every platform (`modx on NAME` undoes it),
as does `module remove NAME`; `module library` (simulators) writes the release's module files to
the simulated card so tests can install what the factory content lacks. The simulators (native and web) also
list these bundled modules (release and extras) in Memory › Modules as "emulator X.Y.Z" rows, under a
last row "Emulator: bundled modules": they install through the same installer and checks as a card
file (`module bundled` lists them, `module bundled install` installs the newest of each; the web
page's "Install all modules" and `?preset=all`). Hardware builds have no such rows.

### 12.3 Build, tiers, release

| | core | standard | advanced |
|---|---|---|---|
| modx core side (binding, API table, hooks) | no (fallback message only) | yes | yes |
| ANIMA010.MOD built, in `MODULES/` of `make release` | — | yes | yes |
| in the store image / factory content (`MOD_ANIMATION`) | — | no (install from microSD) | yes |

`make modx` builds `build/modx/animation.{elf,bin,map}`; `make modules` packs `ANIMA010.MOD`
(`mkmodule.py pack --kind code-xip --iface modx --abi 1 --tier advanced --kconfig MOD_ANIMATION`).
Without the ARM toolchain a placeholder payload stands in (simulators only; the hardware refuses it:
the entry is not inside the image). Internal flash: the core side is `app/modx.o` (API table,
binding, ~3 KB) and `app/anim_core.o` (graph hooks, settings, menu, ~3 KB) plus small hooks in the
graph screens, the slider form and the 3D model (~10 KB in all, with the new help tables); the
module itself (6.2 KB of code, 10.3 KB as ANIMA010.MOD) is only in the QSPI. Standard tier: bank 1
71.7 → 74.2 %, bank 2 71.8 → 71.3 % (`g3d_model.o` went back to bank 1, so that the core tier's
bank 2, which also carries the whole manual, stays at 89.3 % under its 90 % budget).

### 12.4 Modules with a screen of their own (Math Quest)

The API table grew (ABI 1, `size` larger; a module declares what it needs in
`desc.api_size`) for modules that own a whole screen: `softkey` (labels of F1..F5),
`leave` (back home, done after the module call returns), `held_key` (hold gates),
`key_char` (the text a key types), lines and the large fonts, reading card files
(`file_open` / `file_read` / `dir_list`, the same one-file slot as `file_create`) and
**saved bytes**: `save_put(name, buf, n)` copies up to `MODX_SAVE_MAX` (96 KB since API size 7,
16 KB before; 6 KB before API size 3, so a module that needs more reads `save_max`, an entry only
size 3 has, and cannot bind to an older core; size 7 appends `save_size`, the bytes kept, and the
blob `modx.save` became version 2 with a u32 length, version 1 still loading) into a
buffer carved from the arena next to the pool, which the core saves with the session as
the blob `modx.save`, and `save_get` returns them at the next bind. A module's RW block
is zeroed at every bind, so this is how progress survives unbinding, store changes,
module updates and power-off (one module at a time keeps saved bytes). Size 3 also
appends `screenshot` (the screen as `/CALC/SHOTnnn.PNG`, `sdbk_screenshot`). Input reaches
the module as events: `MODX_EV_ENTER`, `MODX_EV_KEY` (key | mod << 8) and `MODX_EV_TOUCH`
(`MODX_TOUCH(type, x, y)`). The host screen is ~150 lines of core code
(`app/mquest_core.c`). Math Quest (docs/MATHQUEST.md) is the first such module; it is not
part of the release (`make mathquest-module`, `MOD_MATHQUEST`), and the simulators carry
it as an extra library file (`module library extra`).

### 12.5 The Animation module

What is animated is decided by the core (`anim_target`): the selected 2D slider parameter in any
graph mode (FUNC `sin(x-t)`, PAR/POL with a free letter, IMPL `x^2+y^2=r^2`), the comet drawing of
PAR/POL/SEQ curves (`gr_comet`: the parameter range drawn so far, with a head dot; SEQ terms one by
one, the cobweb step by step), or the free letter of the 3D entries (t preferred; compiled as an
extra variable of `cas_compile_vars`, so surfaces are re-sampled per frame without touching the
CAS variable), or on the home screen the selected animated graph cell (`ANIM_T_CELL`: t over the
cell's own range, seconds and mode; each frame draws the cell's block again; docs/NOTEBOOK.md
"Animated graph cells"). The module decides when: a 40 ms frame timer, loop / bounce / once, speed, the phase
advancing by real time (at most 4 frame periods per frame), pause on any key or touch (consumed),
on leaving the screen, before power-off (`platform_hibernate_save`), and after 2 minutes without
input; frame-rate statistics (`state anim_fps`, `anim_frame_us`, `anim_grid`). 3D frames keep an
80 ms budget by re-sampling on a coarser grid (x3/4 per slow frame, back up while under a third of
the budget; a paused frame is always full). TRACE follows the moving curve at its parameter and a
shown CALC result is computed again each frame (`graph_anim_frame`). Settings per letter (start,
end, seconds, mode) are saved (blob "anim") and edited in the slider form or the ANIMATION form.
GIF recording: one cycle at 10 fps (≤ 120 frames) to `/CALC/ANIMnnn.GIF`; palette = the first
frame's 40 most frequent colours exactly + a 6x6x6 cube; frames after the first are the bounding
box of the change; GIF LZW (hashed table, 9–12 bit codes, clears).

Speed (frame = set the value, draw the plot or evaluate and render the 3D scene, present; the LCD
transfer is extra): simulator (Apple M-series, `-O1`, no sanitizers) 2D `sin(x-t)` 35 µs, three
functions 56 µs, 3D 24 x 24 ripple 0.19 ms. Estimated on the STM32H743 at 400 MHz (factor 30–50
for this double-precision, branchy code): 2D 1–3 ms, 3D 24 x 24 6–10 ms, 3D 64 x 64 ~50–80 ms (the
budget then drops to ~48 x 48); plus ≤ 6 ms full-frame LCD flush. So the frame timer's 25 fps is
the limit for 2D and typical 3D, ≥ 12 fps for the largest 3D grids. XIP costs nothing measurable
here: the module's per-frame code is a few hundred instructions (the work is in the core's
plotting code in internal flash).

### 12.6 The advanced CAS module (`casx`): the first CAS extension

The CAS extension interface of section 7, built on the code modules of this section.
Code: `cas/src/casx.{h,c}` (API table, registry, dispatch, the known-command list),
`cas/ext/casx_mod.h` (how extension code calls the core), `cas/ext/adv/` (the extension:
`ntcmds.c` number theory, combinatorics and list statistics; `matcmds.c` expm, sinm, cosm,
sqrtm, jordan, minpoly, rsolve; `adv.c` its init), `modules/casx/casx_entry.c` (its modx entry).
Tests: `cas/tests/unit_gaps.inc` (every registered command is in the known list),
`tests/ui/45_cas_module.txt` (missing, installed from the card, bound on first use, rebound
after a store change), and every CAS test, which links it directly.

- **The table** `casx_api_t` (CASX_ABI 1; `abi`, `size`, appended entries keep the ABI):
  `&K` (the current context: constants `K->c0`, settings, interrupt flag), `reg` (register a
  command table), scratch allocation, `cas_throw`, the poll step, the expression constructors
  (`mk_*`, `s_*`), integer and number helpers, `expand` / `rat_normal` / `factor_expr` / `subst`
  / `diff` / `eval_cmd`, polynomial helpers (`up_from`, `poly_coeff`, ...), linear algebra and
  the core's matrix-function and linear-fit helpers (`mat_fun`, `mat_rpow`, `roots_mult`,
  `lin_fit`). Layouts an extension reads directly (`E`, `cas_ctx` up to the constants, `UP`)
  are part of the ABI.
- **Extension code** includes `casx_mod.h`: every function takes `const casx_api_t *X` and
  the core names expand to `X->...`, so the same source is the XIP image and the host code.
  No static data (GF(p) state of `factormod` moved into a per-call struct), no pointers in
  constant tables (command names are `char[14]`), `__builtin_fabs` / `__builtin_floor`; the
  64-bit divisions come from libgcc (position-independent leaf code). The image link fails on
  any direct reference into the core, so nothing can bypass the table.
- **Registration**: the module's init (modx entry) gets the table from the modx API
  (`casx_api`, appended to `modx_api_t`) and calls `reg("casx", table, n, handler)` once per
  table. The CAS looks a name up in core tables first (xcmds.c `ext_cmd` → matfn.c
  `gap_cmd`), then in the registered tables (`casx_dispatch`: argument count checked, the
  handler runs inside a try frame that keeps `cas_ext_busy` exact even when it throws).
- **Binding on first use**: a name of the known list that is not registered calls the
  application's loader (`cas_ext_set_loader`; modx.c binds `casx` with `modx_get`, whose
  init registers it) and retries; otherwise the command fails with `ERR:UNSUPPORTED NAME
  needs the advanced CAS module (Memory > Modules)` (no error while a session is being
  restored: the call stays unevaluated). `modx_unbind_all` (every store change, `modx off`)
  calls `cas_ext_unload`, so the CAS never calls into code that may be gone; the next use
  binds again. `modx_busy` includes `cas_ext_busy`, so a session save waits for it.
- **Tiers**: advanced has `CASX010.MOD` in the factory content (`MOD_CASX`); standard and
  every QSPI tier build it into the release library (`MODULES/`, install from microSD); the
  core tier links `cas/ext/` into flash bank 2 and registers it at the first lookup
  (`CONFIG_CONTENT_INTERNAL`), as do every host build of the CAS (`CASX_BUILTIN`: library,
  REPL, unit tests, Wester and the suites). The simulators and the web build link the
  sources but bind through the module store like the hardware (the help examples install the
  library first in the standard tier).
- **Rewrite hooks** (appended to the table: `reg_hook`): `simplify_full` applies the module's
  PRE rewrites to its input and ranks its CAND results with its own candidates (`casx_hook`).
  The module's `radsimp.c` uses them for radical denesting (n-th roots of a + b sqrt d),
  sqrt(z) sqrt(conj z) = |z|, perfect-square bases P^c = (sqrt(k) Q)^(2c), half-angle square roots
  of constants, exponentials with commensurate exponents and cancellation of irrational common
  roots. Without the module simplify still works, only without these rewrites; the first
  simplify after a store change asks the loader once.
- **Flash**: standard and advanced bank 2 76.1 % → 74.6 % (7.5 KB: ntcmds.o and the moved
  half of matfn.o); the module was 10.6 KB of code (no RW), 13.7 KB with radsimp.c. Core tier
  bank 2 73.8 % → 74.2 % (the same code linked in, slightly larger through the table). After the
  rest of CAS round 2 in the core (definite integrals, assumptions, transforms, gcd images,
  robustness checks) bank 2 is at 77.2 % (core tier 77.3 %), just under the 78 % stop line:
  further CAS code belongs in this module.
- **Science part** (CAS round 3, `cas/ext/sci/`, init `sci.c` called from `casx_adv_init`):
  `veccalc.c` grad / divergence / curl / laplacian in polar, cylindrical and spherical
  coordinates (the core's commands forward a third argument and the plane curl through
  `casx_dispatch`, a few bytes in `cmds.c`), `potential`, `vpotential`, `lineint`, `surfint`,
  `flux`. Same module (`casx`) in every QSPI tier. In the **core tier** it is left out of
  flash by default (Kconfig `CASX_CORE_SCI`, off: `adv.c` does not call `casx_sci_init`, so the
  linker drops it); the names stay in the known list and report "needs the advanced CAS
  module". With it linked, core-tier bank 2 was 78.5 % (over the 78 % line); without it 77.5 %.
- **PDEs** (same part): `pde.c` `pdesolve` (characteristics with a numeric residual check,
  separation of variables for heat / wave / Laplace with Fourier coefficients from the
  integrator, d'Alembert and the heat kernel on the line), `pdenum.c` `pdenum` (explicit and
  Crank-Nicolson heat, leapfrog wave, SOR Laplace/Poisson; memory checked against the free
  scratch and heap first) and `gridval`. The core hooks are small: `numeric.c` compiles
  `gridval(M,...)` of a numeric matrix into a table lookup (OP_GRID), so a grid can be drawn by
  the 3D graph and played on a 2D graph by the Animation module; the graph's slider scan skips
  letters that hold a list or matrix (`gr_model.c`), and the editor keeps a one-letter name with
  several arguments (`u(x,t)`) a call (`editor.c`). API appends: `mk_fn`, `mk_flt`, `sym_name`,
  `try_cmd` (eval_cmd that returns NULL instead of throwing). Bank 2 after this: 77.7 % in
  every tier (core tier without the science part).
- **Algorithm slots and the algebra part** (`cas/ext/alg/`, gated like `cas/ext/sci`): the
  table's `reg_fn(slot, fn)` puts an extension function behind a core algorithm, and
  `casx_pgcd` / `casx_pfactor` (casx.c) call it with the busy count and a try frame that turns
  running out of memory into "declined" (scratch given back, the core's own method runs).
  `CASX_FN_PGCD` (`mgcd.c`): Brown's dense modular gcd (images over GF(p) with interpolation,
  CRT over primes below 2^31, checked by exact division or, when that would not fit, by
  univariate images in every variable at random points) behind `p_gcd` for larger inputs with a
  common factor. `CASX_FN_PFACTOR` (`mfactor.c`): multivariate factorisation by Hensel lifting
  with the leading coefficient imposed on every factor, behind `factor_sqf_part` (result checked
  by exact division). The polynomial layout `P` (poly.h) is part of the ABI with these slots;
  the table gained `p_*`, `up_*`, `int_mul`, `int_gcd`, `num_cmp`, `num_sgn`. In the core tier
  without the module parts the slots and their call sites compile away (`CASX_SLOTS` 0).
- **Sums, products, limits** (`cas/ext/alg/sums.c`, slots `CASX_FN_SUM`, `CASX_FN_PRODUCT`,
  `CASX_FN_LIMIT` through `casx_eslot`, tried after the core's own methods by `cmds.c`): lower
  bounds shifted to 0, binomial sums with a 1/(k+1) part, sums of sin / cos of a k + b in closed
  form, products of linear factors through gamma (every finite closed form checked against the
  explicit sum or product for two bounds), limits at inf with gamma / factorial by Stirling's
  series at two orders (accepted when they agree, directly or through the logarithm) and with a
  difference of exponentials taken apart.
- **Room in bank 2**: the Laplace transforms moved from `ode.c` into the module
  (`cas/ext/adv/lapl.c`, the core's `laplace` / `ilaplace` forward to it): standard and
  advanced bank 2 78.0 % → 77.2 %. The core tier keeps them (it links `cas/ext/adv`) and
  compiles out the slots and the grid lookup of the graph compiler: 77.9 %.
- **Integrator and special functions** (CAS round 4, `cas/ext/int/`, init `casx_int_register`
  called with the science part, so the core tier leaves it out like `cas/ext/sci`): slot
  `CASX_FN_INTEGRATE` behind the core's `integrate` (`integ.c` calls it after its own search
  fails, nested at most twice, errors other than an interrupt mean "not found"). `intx.c`
  drives the strategies and checks every antiderivative by differentiation at sample points
  (a disagreement where the integrand is real rejects it): exponentials of atanh / atan /
  asinh / acosh made algebraic; `intrad.c` radicals of up to three linear forms (two of them
  radical) rationalised by t = L1^(1/k) L2^(-1/k) with locally constant factors K = B^q /
  prod L^(q e) kept as symbols (exact on every branch), u = x^n for Chebyshev binomials (also
  with a symbolic n); `intrat.c` binomial denominators a + b t^k scaled to numeric ones and
  trinomials split over the roots of the quadratic; `intinv.c` u = asin(c x + d) and the
  other inverse functions; `inttrig.c` u = sin, cos, tan (and the hyperbolic ones) by the
  symmetries of R(S, C), otherwise with C = K sqrt(1 - u^2); `intsf.c` Ei / Si / Ci for
  R(x) F(a + b x), erf / erfi for Gaussians, Ei for x^m / ln, commensurable exponentials, and
  dilogarithms for R(x) ln(a + b x). Functions defined by the module (`fnx.c`: `polylog`,
  `besselj`, `bessely`, `besseli`, `besselk`) are plain symbolic calls; three more slots give
  their derivatives (`CASX_FN_DIFF`, from `diff.c`), complex values (`CASX_FN_CEVAL`, from
  `numeric.c`: approx, plots, quadrature) and special values (`CASX_FN_FSIMP`, from `s_fn`).
  `spfn.c` adds the commands chebyshevt/u, legendre, legendrep, hermite, laguerre, digamma,
  residue, minimize, maximize and cartprod. In `cas/ext/alg`: `eigblk.c` (slot
  `CASX_FN_EIGENVALS`) splits a reducible matrix into its strongly connected blocks before
  the determinant (eigenvalues of (2 - a) I_100), and `mfactor.c` compacts its scratch after
  every lifting step (sparse three-variable factorisation, Wester H11). The core gained the
  slot calls, piecewise derivatives (almost everywhere), ln(e^c) on the principal branch for
  complex constants, and `-Os` for the cold files `ode.c`, `sum.c`, `session.c`, `gbasis.c`
  (3.3 KB back in bank 2). Bank 2 after this: 76.9 % in the standard and advanced tiers, 77.5 %
  in the core tier; the module grew from 59.5 KB to 84.8 KB.
- **Antiderivatives in textbook form** (`cas/ext/int/tidy.c`, `tidyrad.c`, `tidytrig.c`,
  module only; the core calls the slot once more with its final antiderivative and keeps the
  result only when it differentiates back to the integrand at the sample points). The terms
  without functions of x are rationalised in up to two square roots of polynomials (also of a
  function of x): S^2 = Q reduces N(x, S)/D(x, S) to A(x) + B(x) S through the conjugate of the
  denominator, so the Euler substitutions' t = x + sqrt(Q) disappear (x/sqrt(x^2 + 1) ->
  sqrt(x^2 + 1)); a term whose derivative is k/sqrt(Q) becomes k times the core's 1/sqrt(Q)
  antiderivative (asin forms). Logarithms with constant coefficients lose constant factors and
  powers of their arguments, conjugate factors and common denominators are merged, and
  logarithms with commensurable coefficients are combined when the argument simplifies.
  Rational functions of sin, cos, tan of one angle (also of the half angle the Weierstrass
  substitution leaves, through T^2 = (1 - cos u)/(1 + cos u)) are written as polynomials in
  cos, sin or tan, whichever is smallest (sec(x)^4 tan(x)^2 -> tan(x)^3/3 + tan(x)^5/5).
  Complex exponentials of real integrands become real, exponential pairs of hyperbolic
  integrands sinh / cosh, expanded denominators are factored. `cas/tests/intsize.py` ranks the
  results by leaf count against Rubi's optimal and SymPy's antiderivatives. The core is
  unchanged (bank 2 as before); the module grew by about 10 KB.

### 12.7 The Geometry module: a whole app moved out of internal flash

The Geometry app (APPS > Geometry) is the first existing app moved into a code module, to make
room in internal flash for USB (section 11, phase 2 item 1). Code: `modules/geometry/`
(`geo_kernel.c` the construction model, `geo_ui.c` the screen and its tools, `geo_io.c` the module
entry, saved payload, console `geo` and GRAPH drawing; `geo.h` the model and the RW block,
`geo_mod.h` the name mapping), `app/geo_core.{h,c}` (what stays in the core). Tests:
`tests/unit/test_geo.c` (the kernel through the same tables), `tests/ui/75`-`78_geo_*.txt`.

- **What stays in the core** (`app/geo_core.c`, ~4 KB, flash bank 2): everything the rest of the
  firmware refers to by address -- `scr_geo`, the APPS entry, the soft-key bar, the six menus
  (items come from the module), the session blob `geo`, the GRAPH overlay, the copy hook, the
  prompt line's editor, File > Open and the `.GEO` files. Each callback forwards to the module
  or, without it, behaves like the other modules: APPS > Geometry shows "Install the Geometry
  module (Memory > Modules).", the console `geo` gives the same sentence.
- **Two tables**: `geo_core_t` (core to module, reached through the appended modx API entry
  `geo_core`, API size 10; its own `GEO_CORE_ABI`): the libm and libc functions the app uses,
  drawing, the prompt editor, Y= functions, GRAPH helpers, file save/load, the console's expect
  counter. `geo_ops_t` (module to core, in the module's RW block, handed over by `attach` from
  init): enter / draw / key / touch / soft key, the menu items, save / load / reset of the
  construction, the overlay, the copy text. `geo_mod.h` maps the usual names (`sin`, `gfx_line`,
  `snprintf`, ...) onto the tables, so the app's code reads as before; the same sources are the
  XIP image, the simulators' code and the unit test.
- **State without static data**: the construction (`geo_t`, ~10 KB), the view and the tool state
  are one RW block (`geo_rw_t`), reached through **`MODX_RW()`**: since API size 10 every call into
  a module (trampolines, init, `modx_call_begin` / `modx_call_end` for a module's own tables)
  points one word at the module's RW block and restores the previous value afterwards (calls
  nest). On the hardware that word is `modx_ctx`, the first word of DTCM (section `.modx_ctx`,
  asserted at 0x2000_0000 by the linker script): an image has no other way to reach core data,
  and a constant address needs no relocation. Existing modules ignore it (ABI unchanged).
- **The construction survives the module**: the core keeps the session blob's bytes while the
  module is not bound (before its first use, while it is absent or switched off) and gives them to
  it when it attaches; `modx_unbind_all` (every store write, `modx off`) first asks a bound
  geometry module for its construction (`geo_core_unbind`: the code is still in place, every
  write unbinds first), and `modx_forget_all` (boot) never calls a module. GRAPH's overlay reads
  "Show on GRAPH" and the object count from the kept bytes, so the module is bound only when there
  is something to draw.
- **Tiers**: standard and advanced have `GEOME010.MOD` in the factory content (`MOD_GEOMETRY`,
  default on) and in the release library. The **core tier leaves the app out** by default: the
  app (~38 KB through the tables) and USB do not both fit in the rollback image's two banks under
  the 80 % budget, and USB (console, firmware update, the drives) is what a rollback needs. The
  option `GEOMETRY_BUILTIN` links the same sources into bank 2 and binds them without a store
  (`modx.c`, a 64 KB pool); with USB turned off it fits. The blob is still kept as bytes without
  the app, so a construction survives a rollback. UI tests that use the app run only where it is
  (`requirecfg GEOMETRY` / `ifcfg GEOMETRY`, a derived Kconfig symbol).
- **Flash** (before USB): standard tier bank 1 80.0 % -> 78.7 %, bank 2 78.4 % -> 74.7 % (the
  module's ~31 KB of code and constants left bank 2; `geo_core.o` and `finance.o` moved there from
  bank 1). Core tier: bank 1 78.7 % -> 77.3 %, bank 2 78.7 % -> 74.2 %. The image is 32.5 KB
  (GEOME010.MOD 36.6 KB); its RW block 10 KB of the module pool. DTCM: the app's 17 KB of
  statics are gone; the core keeps 8 KB (the kept blob and the file image).

### 12.8 The Python module: the first code-load module

Python (APPS > Python, help pages `python` and `python-calc`) is MicroPython v1.29.0
(`third_party/micropython`, unmodified) with the calculator's CAS as the library `calc`. Code:
`modules/python/` (`py_entry.c` the module entry and the MicroPython port, `py_calc.c` the `calc`
library, `py_latex.c` LaTeX from the CAS's linear form, `py_ui.c` the console screen,
`mpconfigport.h`), `app/py_core.{h,c}` (what stays in the core), `modules/modx_load.ld`,
`tools/modreloc.py`, `tools/py_genhdr.py` (MicroPython's generated headers). Tests:
`tests/ui/84_python.txt` (the screen), `tests/ui/84_python_console.txt` (`py` / `pyexec`, the
binding, the fallback), `tests/run_python.py` with `tests/python/basics/` (503 of MicroPython's own
tests/basics that apply to this configuration; expected output from CPython or upstream's .exp)
and `tests/python/test_calc.py` (the calc bridge), all in `make test` (`python-test`).

- **Why not XIP.** A position-independent XIP image (12.1) may have neither static RW data nor
  pointers in constant tables. MicroPython has both everywhere: its state (`mp_state_ctx`), and
  every type, module and method table is a constant structure full of pointers (about 4700
  absolute relocations). Reaching the state through `MODX_RW` would have taken a few macros, the
  tables not. So this module is the planned **code-load** kind (`MOD_KIND_CODE_LOAD`, 7.2):
  - the image is linked at address 0 like an ordinary program, with `.data` and `.bss`
    (`modules/modx_load.ld`); `-q` keeps the relocations; `tools/modreloc.py` lists every
    `R_ARM_ABS32` word (not those against absolute or undefined weak symbols, not the
    descriptor's) and refuses any other absolute kind (MOVW/MOVT pairs, GOT entries). The payload
    is the image (code, constants, initialised data) followed by the list;
  - the descriptor is `modx_load_desc_t` (`modx_desc_t` + `bss_size`, `reloc_off`,
    `reloc_count`; `desc_size` 56) and the `.MOD` header says `kind = code-load`, which an older
    core refuses as "for another firmware" (it accepted only code-XIP);
  - binding (`modx.c` `load_image`) copies the image with `mod_read` (CRC checked) into the
    **code area**, adds the load address to every listed word, zeroes the bss, writes the D-cache
    back and invalidates the I-cache (`hal_code_sync`) and calls the entry there. The code area is
    the last `MODX_CODE_KB` (1 MB) of the SDRAM arena, aligned to its size and made executable by
    MPU region 5 (`hal_code_region`: Normal, write-back, executable; the default memory map makes
    0xC000_0000-0xDFFF_FFFF execute-never, and an MPU region's attributes replace it). It is used
    front to back and given back with the RW pool when every module is unbound. The simulators
    load and relocate the real image too (which checks the list), then run the linked host code;
  - the image runs from SDRAM, so the QSPI write rule (12.2) does not concern its code; the busy
    count still defers a session save while Python runs (the next quiet moment saves);
  - it links newlib's `libm` / `libc` (maths, `memcpy`) and `libgcc`; nothing else of the core is
    reachable except through the API table.
- **The API** (ABI 1, size 11): `modx_api_t.py_core` returns `py_core_t` (`PY_CORE_ABI` 1): the
  CAS (`cas_eval_quiet` with the stack Python has used taken off the CAS's limit), the poll a long
  CAS evaluation uses (watchdog, USB), the break count, a delay, Python's stack limit, the entry
  line (a core editor), the graph (`plot`) and screen switches after the module call.
- **State and memory.** MicroPython's state and the module's statics are the image's own data
  (zeroed at every bind). The GC heap is the module's RW block, `PYTHON_HEAP_KB` (2 MB), so the
  pool (`MODX_POOL`) is 2 MB larger in every QSPI tier: with the code area the CAS has about 3 MB less of
  its ~29 MB. The image: 196 KB of code and data, 46 KB of bss (the console's scrollback and
  history, the program buffer of `py add`), 4715 relocations; `PYTHO010.MOD` 219 KB.
- **Stack.** Python runs on the main stack. Every call from the core marks the top that the
  collector scans and that `MICROPY_STACK_CHECK` measures from: `PYTHON_STACK_KB` (40 KB) on the
  calculator, so deep recursion raises `RuntimeError` instead of reaching the part the CAS and the
  interrupts need. A CAS call from Python gets the CAS's usual limit minus what Python used.
- **The break key.** The ON key's hook (`app.c` `cas_break`, in the key interrupt) also counts
  the press (`py_break_isr`); the VM hook (every 64 backward jumps or returns,
  `MICROPY_VM_HOOK_POLL`) sees a new press and schedules `KeyboardInterrupt`; `time.sleep` polls
  every 10 ms; on the calculator the image is linked with `--wrap=mp_iternext`, so loops inside
  built-ins (`sum(range(10**8))`) poll every 1024 items. The same hook runs the core's poll every
  20 ms (watchdog, USB) and shows new output. A CAS computation stopped by ON raises
  `KeyboardInterrupt` too. Long single operations inside the interpreter (a huge integer power)
  do not poll.
- **Running code** (`py_run`, the reusable part for a notebook): a REPL entry (single input: an
  expression's value printed), a program, or a **cell** (a program whose last statement, when it
  is an expression, gives the value: its repr and, for a `calc.Expr`, its LaTeX). Output goes to
  any of the screen, the console (`out=` lines; `out+=` a piece without its newline) or a capture
  buffer.
- **Flash.** Internal flash keeps `app/py_core.o` (1.6 KB, bank 2: the screen, the APPS entry,
  the console, the services), the loader (bank 2), the API entry and the break counter. The CAS's
  own LaTeX printer (`cas_latex`) would have cost 3.3 KB of bank 2 (nothing linked it before), so
  the module renders LaTeX from the linear form itself. Standard tier: bank 1 417 692 -> 418 116
  bytes (79.7 %), bank 2 414 800 -> 416 936 (79.1 % -> 79.5 %). Core tier (no Python: APPS >
  Python says it is not part of this firmware): bank 1 78.3 % (410 320 -> 410 624), bank 2
  78.5 % -> 78.6 % (411 648 -> 411 972).
- **Simulators.** The native simulator links MicroPython (the same configuration, the NLR by
  setjmp, the collector compiled without ASan's stack checks). The browser simulator leaves Python
  out: WebAssembly keeps locals outside the memory a conservative collector can scan.
- **Phase 2 (the notebook).** The interpreter, `py_run` (cells with value and LaTeX), the output
  sinks and the break key do not depend on the console screen; a notebook screen adds cells (text,
  CAS, Python, graph) through the same `py_core_t` and module ops, stores `.ipynb` files (the
  module store or the microSD card) and shows them in `NOTEBOOKS/` of the CALC drive
  (`app/usb_app.c`, `core/vdrive.c`). Not built yet: files for `open()` and `import` of user
  modules, graphing a Python function, `input()`. The notebook is built: section 12.9.

### 12.9 The Notebook and its store

The notebook's module side (the home screen shows the notebook; the module adds the list of notebooks and
the cell editor, APPS > Notebook; help pages `notebook`, `notebook-cells`, `notebook-files`; the
`.ipynb` mapping in `docs/NOTEBOOK.md`) is part of the Python module: `modules/python/nb_*.c`
(the screens, the document, JSON, PNG, plots, the store). The core keeps the screen
`scr_notebook` (its draw / key / touch go to the module like the console's, soft-key labels come
from the module) and one service entry, `py_core_t.nb_io(op, a, p, n)` (`NBIO_*`, appended:
`py_core_t` size grows, `PY_CORE_ABI` stays 1): the store region, textbook layout
(`m2d_math_box`), display/CAS text, the card's long-named files, the USB drive's folder and
imports. A module without `nb_io` is never asked (the core checks `size`).

- **Why a store of its own.** The module store keeps whole, rarely changed extents (no directory,
  first fit, 32 extents in all); a notebook is rewritten at every change. The session log keeps one
  snapshot. So the notebooks get a region: **0xA00000–0xDFFFFF, 4 MB** (the module store shrank
  from 14 MB to 10 MB: the factory content is 0.5 MB, the release library ~0.6 MB; modules are
  allocated from the bottom, so an existing store is not cut; a sector there that still holds a
  module extent's header is never touched by the notebook store).
- **Format** (`nb_store.c`): 1024 sectors of 4 KB. A version of a notebook (its `.ipynb`, up to
  256 KB) is a chain of sectors, each a 64-byte header (magic `NBK1`, the version's store-wide
  sequence number, the sector's index, a last-sector flag, the byte count, the CRC-32 of the
  bytes, the file's size, the name, the header's CRC-32) and 4032 bytes. No directory: mounting
  reads the 1024 headers (64 KB) and collects the versions; a version is complete when sectors
  0..last are all there and the last one is marked. Sectors of incomplete versions are free.
- **Power loss.** A version is written sector by sector: erase, program the bytes, read them back
  against the CRC, program the header; the last sector's header goes last. A cut leaves either
  the new version complete or an incomplete one that is ignored, and the version before stays
  the notebook. A delete clears the first word of each version's last sector (NOR clears bits
  without an erase): that version is incomplete from then on. A damaged sector (CRC) found when a
  notebook is opened falls back to the previous version. `tests/unit/test_notebook.c` cuts the
  power at every device write of a save and checks a mount finds the old or the new version.
- **Wear.** Free sectors are taken in ring order from where the last write ended, so every
  sector is erased in turn: a notebook of 20 KB saved on every change is 5 erases a save; at 100 000
  cycles per sector that is 20 million saves.
- **Versions.** The newest complete version of a name is the notebook; older ones stay until
  their sectors are needed (reclaimed oldest first), so "previous version" (the list's and the
  page's menu, an import that replaces a notebook) usually has something to go back to. Versions
  listed on the USB drive are pinned: reclaimed last while a computer may still read them.
- **The card.** `CALC/NOTEBOOKS/NAME.ipynb` through the module API's one-file slot and
  `nb_io(NBIO_SD_*)` (create a long-named file, remove, list with long names). `core/fat.c` now
  writes long names: a name that is not 8.3 gets VFAT entries before a short alias `NAMEXX~N.EXT`.
- **USB.** At every generation of the CALC drive the module lists the stored notebooks
  (`nb usb`, `NBIO_USB_FILE` with each version's sector offsets); `NOTEBOOKS/NAME.ipynb` is read
  straight from those QSPI sectors. If module code is running when the drive is generated, the
  folder is generated empty and again from the main loop. Leaving the app after a change
  regenerates the drive (a media change). A dropped `.ipynb` is handed to the module
  (`nb import NAME`, the bytes through `NBIO_USB_READ`), parsed whole, stored and reported
  (`NBIO_USB_RESULT`: the toast and `RESULT.TXT`).
- **Memory.** The module's bss holds the notebook (an arena of 256 KB for the cells' text, 200
  cells), the editor's buffer, the parser's string buffer (64 KB) and two plot images: the image
  is 241 KB of code and data and 658 KB of bss, within the 1 MB code area (`MODX_CODE_KB`).
- **Flash.** Internal flash keeps the Notebook's core side: `app/py_core.c` (the screen, `nb_io`),
  `app/usb_app.c` (the folder, imports), `core/vdrive.c` (a subdirectory, long names), `core/fat.c`
  (long names) and `m2d_math_box`. To make room the cold core files (the console, the module store,
  the session log, the hardware console commands, the QSPI, power and I2C drivers) are compiled for
  size (`FW_COLD_SRC` in the Makefile) and `core/vdrive.o` moved to bank 1.

