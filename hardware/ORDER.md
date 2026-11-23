# Ordering: graphing calculator rev A

**Order from `fab/` exactly as committed.** These are the files rev A was fabricated and assembled from, and
the boards built from them passed bring-up (`bringup/BOARD_RECORDS.md`: no board rework, only firmware
fixes). The KiCad files in `kicad/` are the same as-built rev A design.

Do not regenerate `kicad/` or `fab/` for an order. The generators in `gen/` were updated after the order for
documentation only (each key switch's value is its key name instead of the old key label, and the title
block reads rev A.1); running them rewrites `kicad/` with those labels. Copper, drill, BOM and CPL come out
the same, but the committed as-built files are what was proven, so use them.

`gen/annotate.py --key-labels` (optional, not on the built boards) adds each key's name on the front
silkscreen; only `calc-F_Silkscreen.gto` changes. It is for a future order that wants it, after its own
review in JLC's viewer.

Goal: five calculators: one for us (brought up and validated first), one bring-up/rework board, and units to give away once ours passes the full bring-up checklist. Fixed JLC costs are per order, so each extra assembled board adds only ~$30 (its parts and joints).

## 1. JLCPCB: PCB fabrication + assembly

Upload `fab/calc-gerbers.zip`. Use these settings, and leave everything else at the default:

| Setting | Value |
|---|---|
| Layers | **4** |
| Dimensions | 82 × 188.8 mm (read from the Gerbers) |
| PCB qty | **5** |
| Thickness | 1.6 mm |
| Surface finish | **ENIG** (flat pads for the 0.5 mm-pitch LQFP and FPC connector; HASL lead-free also works) |
| Stack-up | JLC default 4-layer (JLC04161H-7628), no impedance control needed |
| Via covering | Tented |
| Mark on PCB | "Remove mark" or "Specify location" (keeps the order number off the keypad area) |

**PCB Assembly:** turn on.

| Setting | Value |
|---|---|
| PCBA type | **Standard** (needed for two-sided assembly) |
| Assembly side | **Both sides** (bottom: decoupling caps under the MCU/SDRAM, ferrite FB1, battery holders) |
| PCBA qty | **5** (all five bare PCBs) |
| Tooling holes | Added by JLC |
| Confirm parts placement | **Yes** |

Then upload `fab/calc-bom.csv` (BOM) and `fab/calc-cpl.csv` (pick-and-place). In JLC's placement preview, check the orientation of every polarized part. Rotations were derived by matching each footprint's pads against JLC's own footprint (121 placements checked independently, 0 mismatches), but JLC's bottom-side convention has changed before, so look at these specifically:
- Top: U1 pin 1 (dot), Q1–Q12, U5–U8 and U10 (SOT-23 pin 1), D1 cathode band, D2 LED cathode, J3 opening facing west (toward the panel's FPC fold), J4 USB-C and J2 microSD on their pads.
- Bottom: BT1/BT2 polarity (+ toward the marked pad), FB1 and the decoupling caps on their pads.
If a bottom-side part looks rotated 180°, use JLC's rotate button rather than re-uploading. Every part is placed by JLC, including the three through-hole pin headers J1/J5/J6 (JLC adds a through-hole assembly step and fee; the headers are unpolarized, so their rotation only has to line the pins up with the holes). `fab/hand_parts.txt` is empty.

**Expected cost** (JLC rates of Sep 2026; JLC's live quote is authoritative):

| Item | Approx. |
|---|---|
| 5 PCBs, 4-layer ENIG | $35–80 |
| Assembly setup + stencil + fixtures | ~$84 |
| Extended-part loading (~19 × $1.53) | ~$29 |
| Through-hole assembly (3 headers, 28 joints) | a few $; see the live quote |
| Parts for 5 boards (with attrition) | ~$170 |
| Joints, X-ray, misc | ~$7 |
| DHL shipping | $25–40 |
| **JLC total** | **~$350–420** |

Stock is moderate on the STM32H743ZGT6 (~670); order soon. The battery holders (C964881) may be treated as a manual/glued step on the bottom side.

## 2. LCSC: spares and insurance (optional)

Add these to an LCSC order (it can ship together with the JLC order):

| Part | LCSC | Qty | Notes |
|---|---|---|---|
| AFC24-S40FIA-00 40p 0.5 mm FPC (spare for J3) | C262686 | 2 | repair spares |
| FFC 40p 0.5 mm 50 mm, same-side | C5242726 | 1 | insurance: with a 40p 0.5 mm FPC extension board (below), extends the panel FPC if it falls short of J3 |

## 3. Display: BuyDisplay (EastRising), ships from China

| Part | Options to select | Qty |
|---|---|---|
| **ER-TFT032IPS-3.2** (3.2" 240×320 IPS, ST7789) | **Capacitive touch panel** (ER-TPC032-3.1, FT6236, touch on the main FPC); interface 8080 16-bit is set by our board (IM pins); no pin header / no adapter board | 6 (one per calculator plus a spare; about $15 each) |

Order from https://www.buydisplay.com/3-2-inch-240x320-ips-tft-lcd-display-optl-capacitive-touchscreen-st7789 . Allow 1–3 weeks shipping. The datasheet is marked "Preliminary"; check the FPC (40 pins, 0.5 mm pitch, contacts on the front face, pin 1 at the portrait-left end) and the touch controller on arrival before folding the FPC.

## 4. Tools and mechanical parts (Amazon / Adafruit / McMaster)

| Part | Qty |
|---|---|
| Raspberry Pi Debug Probe (CMSIS-DAP + UART) | 1 |
| 40-pin 0.5 mm FPC extension / adapter board (two 40p ZIF connectors, same-side) | 1 (insurance) |
| M2 × 3 mm heat-set brass inserts (OD ≈ 3.5 mm) | 30 (5 per case + spares) |
| M2 × 16 mm socket-head cap screws | 30 |
| AAA cells (alkaline or NiMH) | 20 (no charger on the board: tell recipients) |
| PETG filament: white (case), beige (keys), black (key legends); see `enclosure/README.md` | about 120 g a calculator |
| Face artwork: acrylic gel medium (matte), water-based polyurethane (satin), 400/600 grit wet-and-dry paper | — |
| Thin foam pad, non-conductive, about 1–3 mm (behind the display) | — |
| Optional: dfu-util (`sudo port install dfu-util`) | — |

## 5. After the boards arrive
Follow `firmware/README.md` → "First-board bring-up". Flash `firmware/release/bringup.elf` first and run `selftest`, then flash `calc.elf`. `firmware/release/FLASHING.md` has the exact commands.
