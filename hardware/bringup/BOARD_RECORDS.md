# Rev A board records

These are the results of `BRINGUP.md` Phase 11.4 for the first three rev A boards, brought up on 2026-10-27.
Boards are numbered in the order they were brought up. Mark each board's number on its bottom side so the
records stay matched to the boards.

## Results

| Item | Board 1 | Board 2 | Board 3 |
|---|---|---|---|
| Silicon (DBGMCU_IDCODE) | 0x20036450, rev V | 0x20036450, rev V | 0x20036450, rev V |
| SDRAM | 100 MHz, `memtest full` pass | 100 MHz, `memtest full` pass | 100 MHz, `memtest full` pass |
| QSPI (W25Q128, ef4018) | `qspi stress` pass at 66.7 MHz (5 passes at 100 MHz also passed) | pass at 66.7 MHz | pass at 66.7 MHz |
| HSE / LSE | ok / ok | ok / ok | ok / ok |
| LCD RDDID | 0x858552 | 0x858552 | 0x858552 |
| MADCTL | 0xA0 (`LCD_ORIENTATION` 3) | 0xA0 | 0xA0 |
| LCD bus margin | min DATAST 5, production 15 | min DATAST 5 | min DATAST 5 |
| Touch | FT6236 (cipher 0x36, fw 0x02, vendor 0x11), map 5 (SWAP\|FLIP_X) | FT6236 | FT6236 |
| Backlight gain at 20 mA | 0.92; regulates 5–90 mA from 3V3 | 0.94 | 0.91 |
| Keypad | 50/50 (after the scan fix) | 50/50 | 50/50 |
| SD card detect | active-low (confirmed: absent/present) | present | present |
| INA226 scale | 1.000 (supply 0.05 A at isys 57 mA, 10 mA display resolution; 10.1 not yet done) | 1.000 | 1.000 |
| Baseline current at 4.8 V, 3V3_BUS off | 57 mA | 57 mA | 57 mA |
| Bring-up `selftest` with panel (bus on, 20 mA backlight) | 105 mA | 128 mA | 125 mA |
| VBUS back-feed (TP4) | < 0.1 V | < 0.1 V | < 0.1 V |
| Watchdog option bytes | `iwdg-freeze` set | set | set |
| Firmware | `release/calc.elf`; manual and mathquest modules installed from the card | same | same |
| Standby current (10.3) | not yet measured | — | — |

All three boards were powered from a bench supply on the battery input, at 4.8 V with a 50 mA limit at first.
Phase 2 (unpowered resistance checks) was skipped. Phase 3 rail voltages were checked on board 1. TP4 was
checked on every board.

## Firmware fixes found on board 1

None of these is a board fault. Boards 2 and 3 passed with the fixed images.

- **QSPI** (73457be): with FSIZE set to the exact 16 MB, an indirect read of the last sector set TEF at
  byte 0, and `qspi test`/`qspi stress` failed at every clock (ES0392, last-byte erratum). FSIZE is now 24.
- **Keypad** (9206569): with no row pull-up and 2 µs of settling, every key also showed on the next one or
  two rows. Rows are now pulled up during the scan only, and each step waits 10 µs.
- **Display orientation** (9206569): this panel is upright at MADCTL 0xA0. 0x20 is mirrored and 0x60 is
  upside down. Touch needed no change.
- **Debug access** (9206569): after a power-on reset, SWD could not reach the core while the bring-up
  image slept in WFI. The bring-up image now sets the DBGMCU debug-in-sleep bits.

## Open items

- Phase 10: the INA226 check against a series meter, the battery-range sweep, the standby current, cells,
  and the first USB connection (through a powered hub).
- Phase 9.8 safe mode, and Phase 11 acceptance on cells.
- A full-frame `lcd flush` takes 21 ms against an expected ≈ 8 ms.
- The firmware version line keeps an old build stamp after `make release`.
- `fs info` takes over 60 s on a 64 GB FAT32 card, because the free-space count scans the whole FAT.
