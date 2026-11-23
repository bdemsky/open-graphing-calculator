# Printing the enclosure

How the parts were sliced and printed, and what to check afterwards. The tested setup is an Elegoo
Neptune 4 Pro (Klipper) with a 0.4 mm nozzle, a textured PEI plate and Elegoo Rapid PETG. The
[README](../README.md) lists which files to print.

## Material

Everything is printed in **PETG**. The parts that flex are sized for it: the battery door's latch
band bends 1.2 % to free its hooks (1.4 % at its stop), in the plane of its plate, along the layers,
PETG's strong direction. The display snap fingers bend 1.2 %, across the layers, where PETG is
weakest. An early PLA print lost several thin board standoffs. The standoffs and bosses are now
7 mm, tied to the walls, with root fillets and gussets ([DFM_REVIEW.md](../DFM_REVIEW.md) has the
numbers).

The design allows for how PETG prints:

- small holes print undersize, so holes are modelled 0.2 mm over (M2 clearance 2.4 mm,
  counterbores 4.2 mm);
- the first layer spreads, so bed-facing edges and openings have 0.3-0.6 mm 45° chamfers;
- it strings, so keep retraction on;
- bridges sag more, so no bridge is over 10 mm;
- it shrinks about 0.2-0.4 %, so the board has 0.5 mm clearance per side (see
  [Shrinkage](#shrinkage)).

## Scripted slicing (`slicing/slice.sh`)

This is how the tested parts were sliced. It runs the ElegooSlicer command line
(`ELEGOO_SLICER`, default `/Applications/ElegooSlicer.app/Contents/MacOS/ElegooSlicer`) with the
profiles in `slicing/`, and the repo's venv Python (`PYTHON`, default `../../.venv/bin/python`).

    slicing/slice.sh case   OUT.gcode PART.stl [PART.stl ...]
    slicing/slice.sh keys   OUT.gcode BASE.stl LEGEND.stl

**`case`** puts the parts side by side, 20 mm apart, and slices them with `process_case.json` and
`filament_rpetg.json`. Use it for the shells, the door and any one-colour test piece (for
example `back_shell_base.stl battery_door.stl` on one plate).

**`keys`** slices any legend-up key plate with its legend: the loose keys (`keys_loose_*`), the
word test and the legend strip. It:

1. slices the whole plate on one filament (A), and the cap tops only, cut five layers under the
   legend, with the legend as a second filament (B). Both use Arachne walls, which draw the thin
   legend strokes; classic walls blur them. (Arachne fails on the whole plate with a separate
   legend part, hence the two slices.)
2. splices them with `splice.py`: A below the first filament change, B (raised back) from it on.
   The result has two `PAUSE`s: base colour, legend colour, base colour.
3. removes the slicer's 2 mm prime after each `PAUSE`. On the tested printer the `RESUME` macro
   already purges 100 mm at the park position, and the extra prime left a blob on the first key
   printed after the swap. If your printer's resume does not purge, purge by hand before you
   resume.
4. refuses the result if it has a brim (a brim fills every gap between the first-layer islands),
5. checks it layer by layer against the model with `verify_keys.py` (fused islands, stray or
   missing material, legend colours, pauses, no prime after a pause, Z moves). Set
   `VERIFY_PNG=DIR` for pictures of the checked layers.

`PLATE_XY="x y"` places the plate's centre at bed position x, y (default the bed centre,
112.5 112.5). Key plates have many small first-layer islands, so put them where the bed is
flattest: look at the printer's bed mesh (in Klipper's web interface) first.

The profiles, by file:

| File | Used for | Main settings |
|---|---|---|
| `machine_n4pro.json` | everything | Neptune 4 Pro, 0.4 mm nozzle, Klipper, single-extruder multi-material, retraction 0.8 mm, Z hop 0.4 mm |
| `process_case.json` | shells, door | 0.2 mm layers, Arachne, 2 walls, 5 top / 4 bottom layers, 15 % cubic, 0.5 mm first-layer lines, 5 mm outer brim, elephant-foot 0.15, seam aligned |
| `process_keys.json` | key plates | as above, but no brim and 0.42 mm first-layer lines |
| `process_keys_classic.json` | (kept for comparison) | classic walls instead of Arachne |
| `filament_rpetg.json` | shells | Rapid PETG, 262 °C (265 first layer), bed 70 °C (72 first layer), fan off for 3 layers then 15-30 %, retraction 0.5 mm |
| `filament_rpetg_c1_wipe.json` | key base colour (beige) | 250 °C (255 first layer), retraction 0.8 mm with wipe, Z hop 0.4 mm |
| `filament_rpetg_c2.json` | legend colour (black) | 250 °C, same retraction |
| `filament_rpetg_c1.json` | (older key base profile) | 262 °C; strung badly between the keys |

Elegoo Rapid PETG is a high-flow PETG and wants hotter than most. With another PETG, start from
its own recommended temperatures and keep the rest.

## Slicing in the ElegooSlicer GUI

Use these steps if you do not use `slice.sh`. Setting names are those of current ElegooSlicer
(based on OrcaSlicer); use the search box in the settings panel if a name differs.

### PETG settings

Start from the slicer's PETG profile for your filament and set:

| Setting | Value | Why |
|---|---|---|
| Nozzle | your filament's range (Rapid PETG: see the table above) | layer adhesion; the flexing parts need it |
| Bed | 70-80 °C (a little hotter for the first layer) | adhesion on textured PEI without glue |
| Part cooling fan | off for the first 3 layers, then 15-30 % | low fan keeps layers bonded |
| Retraction | 0.5-0.8 mm, wipe on, retract on layer change; Z hop 0.4 mm | PETG strings |
| Avoid crossing walls | on | fewer strings inside the key openings |
| Layer height | **0.2 mm**, first layer **0.2 mm**, no adaptive layers | every height in the design is a whole number of 0.2 mm layers (key gap, legend depth) |
| Wall generator | Arachne | draws the thin legend strokes |
| Elephant foot compensation | 0.15 mm | with the modelled chamfers, keeps openings true |
| X-Y hole / contour compensation | 0 | hole allowances are already in the model |
| First layer / bottom surface pattern | Monotonic | an even face for the artwork |
| Supports | off | no part needs them |
| Brim | 5 mm outer only for the shells; **none** for key plates and door | see [Warping](#warping-shells) |

### Colour swaps (key plates)

The key plates print in two colours on the single extruder. The slicer treats each colour as a
filament and pauses at each change; you swap the spool by hand.

1. In the printer settings, enable **single extruder multi-material** (and **manual filament
   change** if your version has it).
2. Set **Change filament G-code** to `PAUSE` (Klipper; `M600` if your firmware defines it). Save
   the printer profile under a new name.
3. Add a second filament in the filament list and give each a colour so the preview shows them.

At each pause the head parks. Unload the filament, load the other colour, purge until it runs
clean, remove the purged plastic from the nozzle, and resume. The scripted profiles use no prime
tower: the hand purge at each swap does its job.

### Loading a part

1. Drag the base file and its legend file in **together**. When asked *"Load these files as a
   single object with multiple parts?"*, answer **Yes**: the legends then sit in the base's
   recesses.
2. Don't rotate or flip anything: the files are in print orientation. Keep the shells' USB end
   (the end with two openings) at the back of the bed, as loaded, so the seam lands there.
3. Expand the object and give the legend part filament 2 (black). The base is filament 1.
4. Slice and check the preview: the legend colour must appear only in the top two layers of the
   keys.

For fewer swaps, set `INLAY = 0.2` in `case.py`: the legends are then one layer thick and may look
washed out on a light key.

## Shrinkage

PETG shrinks about 0.2-0.4 % as it cools: 0.4-0.8 mm over the 194 mm shells. The board has 0.5 mm
clearance per side, but the screw holes at the two ends are 181.8 mm apart. Measure the distance
between the outer screw counterbores of the first back shell (or between the top-left and
bottom-left bosses of a front) with calipers. If it is more than 0.2 mm short, set the filament's
**Shrinkage (XY)** to `181.8 / measured × 100 %` (for example 100.3 %) and print again.

## Warping (shells)

The shells are long and thin, printed flat on their face or floor, so their corners can lift.

- Brim **5 mm, outer only**, or instead the "mouse ears": load `top_shell_ears.stl` /
  `back_shell_ears.stl` with the shell as one object (same filament). They are four 16 mm discs,
  two layers thick, at the corners, cut back 0.5 mm round every opening in the shell's first
  layer. Trim the brim or ears off with a knife and scrape the edge flat; the 0.6 mm edge chamfer
  gives the knife a line to follow.
- Let the bed heat-soak for 5 minutes. No part cooling for the first 3 layers.
- Clean the plate with dish soap and water, or isopropyl alcohol, and don't touch it afterwards.
- No drafts: no fan, open window or air conditioning blowing at the printer.
- Let the part cool on the bed (below 40 °C) before you flex the plate.

Smooth PEI gives the front a glossy face, a better base for the artwork. PETG sticks to smooth PEI
too well: wipe on a thin layer of glue stick first as a release agent.

## Checks after each print

- **Face / floor**: flat on a table, no lifted corner (rock it diagonally). No elephant-foot lip
  round the edge or any key opening; the chamfers should show all round. If a lip remains, raise
  elephant foot compensation by 0.05 mm.
- **Bosses and standoffs**: no layer split at the root (press each sideways; it must not creak).
  The 3.2 mm insert holes are round and open; an M2 screw drops through the back's bores.
- **Key openings and window**: clean, no strings across (trim with a knife).
- **Snap fingers** (front): each springs back when bent outward 0.3 mm; no crack at the root.
- **Door**: the slot behind the latch band and the split in its middle are open. The door drops
  into its recess without force, with an even 0.4 mm gap, clicks shut and stays in when shaken.
- **Seam**: the shells close with an even V groove all round and no step you can feel.
- **Keys**: each legend is filled and flush; no strings between the keys (snip them). Each key
  drops into its hole from the inside without catching. A key with a closed-up or blobbed legend
  can be reprinted alone (`case.py --loose --pick NAME`).
- **Stringing** inside the shells: a quick pass with a heat gun, held well away, removes it. On the
  face use a knife only.

## Legend and fit tests

| Command | Files | What it tests |
|---|---|---|
| `case.py` or `case.py --loose` | `keys_loose_test_*` (8 keys), `keys_loose_single_*` (APPS) | legends and fit before the full plate |
| `case.py --loose-gaps` | `keys_loose_gaps.stl` | three APPS keys 0.5 / 0.7 / 0.9 mm above the switch plunger, engraved 5 / 7 / 9; one colour |
| `case.py --word-test` | `keys_loose_words_{base,legend}.stl` | the word keys in three lettering styles, plus reprints of 0, 4 and F1 |
| `legendcheck.py --strip` | `legend_test_{base,legend}.stl` | a 1.2 mm plate with the thinnest legends, about 10 min |
| `case.py --bar-clips` | `barclip_test.stl`, `barclip_dummy.stl` | one display bar clip and a printed panel edge, 14 min (untested) |

The gap test chose the 0.5 mm gap (`case.LOOSE_GAP`): at 0.15 mm the closed case held the switch
pressed; at 0.5 mm the key clicks cleanly ([REFERENCE.md](REFERENCE.md#switch-and-travel)). The word test chose the DIN capitals now on the word
keys.
