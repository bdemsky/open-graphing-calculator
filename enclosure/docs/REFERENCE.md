# Enclosure reference

Everything the [README](../README.md) leaves out: the build options and every output file, the
legends and colours, the keys and switches, the variants, the checks, and assembly details.

## Sources and tools

| File | What |
|---|---|
| `case.py` | the case, keys and door (build123d), with their 3D checks. Reads the PCB layout (`hardware/gen/layout.py`, `hardware/build/calc.net`) and the key layout (`firmware/keys/layout.py`). Writes `out/`. |
| `overlay.py` | the face artwork PDFs ([FACE_ARTWORK.md](FACE_ARTWORK.md)). `case.py` runs it. |
| `legendcheck.py [--strip]` | measures every key legend at its printed size (see [Legends](#legends-and-colours)); `--strip` writes the legend test plate |
| `printcheck.py [files...]` | checks print files for overhangs, long bridges, thin walls and slender columns (default: every STL in `out/`) |
| `render.py` | PNG renders in `renders/`; needs `hardware/build/3d/board.glb` (`kicad-cli pcb export glb`) |
| `slicing/` | slicer profiles and `slice.sh` ([PRINTING.md](PRINTING.md)) |

Run them with the repo's venv from `enclosure/` (`../.venv/bin/python case.py ...`). Each
`case.py` run takes 1-3 minutes.

## `case.py` options

The default build writes the tested set; each option builds only its own files and leaves the
rest of `out/` alone.

| Command | Writes | Status |
|---|---|---|
| `case.py` | `top_shell_base.stl` (the front), `back_shell_base.stl`, `battery_door.stl`, `{top,back}_shell_ears.stl`, `keys_loose_{all,test,rest,single}_{base,legend}.stl`, `overlay_{transfer,proof}.{pdf,svg}`, render files, `case_assembly.step` | **the tested set**, printed and used |
| `case.py --loose` | `keys_loose_{all,test,rest,single}_{base,legend}.stl` only | keys without the rest |
| `case.py --loose --pick NAMES` | `keys_loose_pick_{base,legend}.stl`: only these keys (comma-separated names) | reprints |
| `case.py --loose-gaps` | `keys_loose_gaps.stl` | gap test (done) |
| `case.py --word-test` | `keys_loose_words_{base,legend}.stl` | lettering test (done) |
| `case.py --door` | `battery_door.stl`, `render_door.stl` (door only, with its fit checks) | printed and used |
| `case.py --catch-test` | `catch_test_{top,back}.stl`: the top-right corner of the front and the back shell with both corner catches | not yet printed |
| `case.py --rev2` | `back_shell_rev2_{base,ears}.stl`, `battery_door_rev2.stl` | not yet printed |
| `case.py --bar-clips` | `top_shell_barclip_base.stl`, `barclip_test.stl`, `barclip_dummy.stl` | not yet printed |
| `case.py --clips` | `top_shell_clips_base.stl` | experimental |
| `case.py --low [--drop D]` | `top_shell_low[D]_base.stl` and its shorter keys `keys_loose_low[D]_all_{base,legend}.stl` | experimental: too shallow without trimming J5/J6 |

All files are in print orientation and need no supports. The fronts print face-down, the back
shells floor-down, the door plate-down, the keys legend-up. Every front is checked against both
back shells and against the loose keys at its own height.

## Legends and colours

Key legends, SHIFT functions, ALPHA letters, key colour groups and filament colours come from
**`firmware/keys/layout.py`**, the single source of the keyboard layout (it also generates the
firmware keymap and the web simulator's key map). Key positions and sizes come from the PCB.
Change a filament colour in `FILAMENTS` there and every part, legend, artwork colour and render
follows.

**Key legends** are inlaid in the keys' top two layers (0.4 mm), in a second colour, printed
legend-up on solid plastic.

- Digits, symbols and F-keys: Arial Black, as large as the key allows (digits 5 mm tall). ⌫ comes
  from Apple Symbols. Superscripts (`x^y`, `sin^−1`) are converted in `case.py` (`CAD_TEXT`).
- Word keys: **DIN Alternate Bold capitals**, the long words shortened (`CAP_WORDS` in `case.py`):
  SHFT ALPH ENT CLR HOME APPS MENU MATH VARS SIN COS TAN LN LOG ON CAS. Words in 3 mm Arial Black
  closed up in print; the word test (`--word-test`) picked this style.

`legendcheck.py` reports, per legend at its placed size, the font size, ink box, stroke width, and
the share of strokes and gaps too narrow for the nozzle. Its print-calibrated limit flags legends
with too much gap narrower than a 0.7 mm bead (`WARN_GAP_PRINT`, `BEAD_PRINT`): on the printed
keys, every legend over it closed up. `PROVEN` lists legends over a limit that printed cleanly
anyway (reported, not flagged).

**Colours.** The body is white. The printed keys are all beige with black legends. The renders
and the web simulator colour the keys by group (`FILAMENTS`, `PALETTE`):

| Filament | Role |
|---|---|
| white | shells, door; number keys; legends on the blue keys |
| beige | function keys; every printed key |
| black | legends on the white and beige keys; ALPHA letters of the artwork |
| blue (cobalt) | SHIFT, ALPHA, ÷ × − +, enter; SHIFT labels of the artwork |

Every legend contrasts at least 5:1 with its key. The back has no text.

## Keys

`case.py` builds 50 solid keys, each standing on a flat bottom, printed legend-up on one plate
(`loose_cap()`, `loose_keys()`). Each key's flange is wider than its hole and holds it in from the
inside; at rest the flange lies against the ceiling. ON and ENTER are cut clear of the two short
corner bosses up to the ceiling, so they go in with a slight tip. Above the flange the keys are
0.15 mm slimmer per side (`CAP_SLIM`, 0.4 mm a side in the holes): with only the holes' 0.25 mm
they rubbed on the printed front.

| Plate | Keys | Time |
|---|---|---|
| `keys_loose_all` | all 50 | 1 h 40 min |
| `keys_loose_test` | ON, ENTER, SHIFT, APPS, 8, F1, UP, LEFT | 20 min |
| `keys_loose_rest` | the other 42 | 1 h 35 min |
| `keys_loose_single` | APPS | 10 min |

**No notches.** The flanges are plain rectangles. Keys printed before this change have notches
round the pegs of the earlier spring-key-mat fronts; they still fit the current front. New keys do
not fit a front printed with those pegs (`top_shell_easy_base.stl` and the older default
`top_shell_base.stl`) unless the pegs are clipped off flush with the ceiling.

### Switch and travel

The switch is an XKB TS-1187A-B-A-B (LCSC C318884, from the BOM in `hardware/ARCHITECTURE.md`):
operating force **160 gf (1.57 N)**, travel **0.2 ± 0.1 mm** to the click, height 1.5 mm. Its 3D
model has a 5.2 mm square cover up to 1.2 mm and a 2.0 mm round plunger up to 1.5 mm (`SW_H`,
`SW_COVER`, `SW_HALF`, `T_ACT` in `case.py`).

Heights above the PCB (CAD, as in `case.py`):

| | mm |
|---|---|
| switch cover top / plunger top | 1.2 / 1.5 |
| key bottom at rest (`LOOSE_GAP` 0.5 over the plunger) | 2.0 |
| flange (touches the ceiling at rest) | 7.67-8.47 |
| ceiling / face / key top | 8.47 / 10.07 / 11.27 |

A press first closes the 0.5 mm gap, then moves the plunger about 0.2 mm to the click (0.7 mm in
all); after 0.3 mm of plunger travel (0.8 mm in all) the key bottom lands on the switch cover,
which is the end stop. The gap is the key's only free play: lying face-up, a key drops onto its
plunger; face-down it rests on its flange.

**Why 0.5 mm.** The gap test (`case.py --loose-gaps`, three APPS keys at 0.5, 0.7 and 0.9 mm) and
an earlier key at 0.15 mm settled it: at 0.15 mm the closed case held the switch pressed, at
0.5 mm the key clicks cleanly. Small gaps are eaten by the stack tolerances: the boss height, the
switch height (±0.1 mm) and the key length, and the front is printed face-down from its face, where
the slicer prints a layer only if its middle is inside the part, so the boss feet end about
0.07 mm short and the board sits that much closer to the ceiling than in CAD.

`case.py` checks the assembled keys at rest against the front and against every switch cover and
plunger (`check_keys()`), in the default build and for every front variant.

### Key sizes

| | |
|---|---|
| main-grid keys | 12.6 × 7.6 mm, 0.8 mm corner radius |
| rows A/B, soft keys, D-pad | 12.6 × 7.6, 12 × 6, 8 × 5 / 5 × 7 mm |
| flange overhang | 1.0 mm a side (D-pad 0.7), 0.8 mm thick, 45° chamfer under it |
| web between key rows | 3.05 mm (2.55 mm flat between the chamfers) |
| key openings | 0.25 mm round the key, 1.05 mm corner radius, 0.25 mm × 45° chamfer on the face |

The 7.6 mm key height was set by the earlier spring key mats and kept: it holds the 5 mm digits
with 1.1 mm margins and leaves wide webs between the rows for the artwork labels.

## Front variants

| Front | Status |
|---|---|
| `top_shell_base.stl` (default) | the printed and used front, less the pegs it had inside for the retired key mats |
| `--bar-clips` | original height; the display held by two bars that grab its border 1.5 mm, instead of the snap fingers. Designed (strain about 1.2-1.4 %), **not yet printed**: print the 14 min test piece first ([V3_THIN.md](../V3_THIN.md#bar-clips---bar-clips)) |
| `--clips` | stiffer snap fingers; they still grab only about 0.35-0.5 mm |
| `--low [--drop D]` | front up to 2 mm lower, with keys as much shorter. The printed `--low --drop 1.8` was too shallow: the debug header pins J5/J6 hit the inside of the face (1.5 mm short) unless they are trimmed. Advanced only. |

All fronts fit both back shells. Design notes for the variants: [V3_THIN.md](../V3_THIN.md).

## Rev2 back shell and door (not yet printed)

`case.py --rev2` builds a revised back shell and door. The front is unchanged; everything else
(PCB ledge, standoffs, USB-C and microSD openings) is as `back_shell_base.stl`, except:

- **Seam tongue**: 0.3 mm clearance per side in the front's lap instead of 0.15 mm. The tongue
  stays 0.8 mm (two perimeters); the PCB edge has 0.45 mm to it instead of 0.5 mm.
- **Door latch end**: no catch rib and no pry notch; a 16 × 5 mm thumb scoop continues the door
  recess. The door's thumb tab (12 mm wide, 0.5 mm thick) lies in it.
- **Door**: the hooks reach 1.2 mm over the floor (1.0 mm on the first door), 1.1 % band strain
  (1.3 % at the stop). To open, push the thumb tab toward the bottom end and lift the door by it.
  The rev2 door fits only the rev2 shell.

## Corner catches (not yet printed)

The top-right corner has no screw (the microSD socket fills it) and the microSD opening interrupts
the seam's lap there, so that corner sprang open. Two snap catches hold it: in the right wall
(board y 6-18) and in the top wall between the USB-C and microSD openings (board x 49.5-59). Both
back shells have them; the front has the matching pockets.

- **Leaf**: 12 (top wall: 9.5) × 0.8 mm, split by slots from the inner part of the back shell's
  wall, from 6 mm under the seam to 2.5 mm above the PCB top, with a 0.4 mm root fillet. The wall
  keeps a 0.6 mm outer skin. The PCB ledge stays under the board.
- **Bump**: 0.45 mm on the leaf's outer face, 30° lead-in on top, 50° back ramp under it (from
  the pull axis; with PETG on PETG it would lock only past about 68°). It clicks into an undercut
  in the front's wall over a lip (the lap, thickened 0.1 mm inward), 0.6 mm under the outer face.
  It engages 0.35 mm (0.15-0.55 mm with the shells shifted sideways in the seam's play).
- **Closed**: 0.1 mm round the crest and to the lip, 0.15 mm in Z on the ramps (how far the corner
  can open), 0.3 mm to the pocket's sides and top.
- **Snapping**: the leaf bends 0.35 mm inward at the bump, 0.8 % strain (1.3 % worst case), and
  0.25 mm at the PCB edge, which keeps 0.30 mm (0.14 mm worst case) to the leaf.
- **Force** (E 2000 MPa, μ 0.4, rough): about 4 + 3 N to close, 9 + 7 N to pull the corner open.

`case.py` prints these checks. `case.py --catch-test` writes the corner of each shell
(38.5 × 24.5 mm; front 11.3 mm tall face-down, back 17.9 mm floor-down) to try the click before
printing whole shells; they mate like the shells. Slice them with `slicing/slice.sh case`.

For shells printed before the catches, tape a printed 8 mm round × 11.7 mm pin between the back
shell's floor and the board's bare back under the microSD socket (centre at board (71, 11.5)).

## Assembly details

These add to the [README's assembly steps](../README.md#3-assemble).

**Inserts.** M2 × 3 mm heat-set inserts, OD about 3.5 mm, in 3.2 mm holes with a chamfered mouth,
at about 240 °C for PETG (220 °C for PLA). Stop when the knurls are in and the insert is flush; don't
push it deeper. The two bottom bosses are shorter, to clear the ON / ENTER keys.

**Display.** EastRising ER-TFT032IPS-3.2 with the ER-TPC032-3.1 capacitive touch panel
(77.7 × 55.04 × 4.1 mm), landscape, its single 40-pin FPC (display and touch) on the left.

1. Fold the FPC 180° round the panel's left edge, flat against the module back; don't crease it.
   The 43 mm-wide part with the touch controller lies on the left third of the panel back, parts
   facing away from the panel; hold it with Kapton tape. Its 20.5 mm tail continues toward the
   middle; its last ~17 mm stay free.
2. Hold the board component side toward the panel back, tilted open along its left edge (the free
   tail is the hinge). Put the tail end into J3 from the left, contacts facing down toward the PCB,
   and close J3's flip latch. Panel pin 1 goes to pad 1 (marked on the silkscreen).
3. The VHB frame (about 2 mm wide strips) sits in a 0.25 mm recess round the window, so the panel
   still ends up at its designed height. Push the panel in on its back near each finger in turn,
   not on the middle of the glass. The FPC fold sits in a relief in the pocket's left wall.
4. The five snap fingers (two top, two bottom, one right) have 0.25 mm lips and only retain the
   panel; they do not clamp it. The foam pad behind the panel (closed-cell PU or EVA, keep it off
   the folded FPC at x < 23 mm) presses the panel against the bezel when the case is closed. See
   [V3_THIN.md](../V3_THIN.md#what-actually-stops-the-panel-sliding-back-clamp-it).
5. To take the panel out, flex the fingers outward one at a time, no more than about 0.3 mm.

**Screws.** Five M2 × 16 mm socket-head screws from the back, about 0.15 N·m (a two-finger twist on
a small hex key). The heads seat 0.2 mm below the surface. The tip ends 2.8 mm into the 3 mm insert,
1.7 mm short of the end of the 4.5 mm hole.

**Battery door** (`battery_door.stl`). To close: hold it tilted about 10° with the top end out,
slide its two tabs under the floor at the bottom of the opening, lay it in and press the top end
until the two hooks click over the floor edge. Every fit has 0.4-0.6 mm clearance; nothing needs
force. To open: slip a fingernail into the notch at the top end of the recess, under the door's
thinned edge, push it toward the bottom end (the latch band gives about 2 mm and pulls the hooks
in) and lift; the door tips out on its tabs. A firm pull on the top end also opens it.

**Openings.** The USB-C opening is a 12.6 × 6.8 mm pocket through both shells, so overmolded plugs
up to about 12.3 × 6.5 mm reach the receptacle. The microSD slot has a 12 mm half-round finger
scallop. Both have a 0.5 mm 45° outer chamfer.

**Debugging.** The debug headers (J5 DEBUG, J1 SWD, J6 EXPANSION) and the RESET / BOOT buttons are
under the front's plate, with no holes over them: debug with the case open (five screws). With the
case closed, firmware updates go over USB from the running firmware (`calcctl flash`).

## Display stack-up

Heights above the PCB top surface (as in `case.py`): switch plunger 1.5; folded FPC (touch IC and
parts, ~1.3 mm) about 3.07-4.37; panel back 4.37 (`PANEL_BACK`); panel front and ceiling 8.47
(`CEIL` = `PANEL_BACK` + `PANEL_T` 4.1), against the VHB in its 0.25 mm recess; face 10.07; key tops
11.27. The window is the touch visual area (65.95 × 49.2 mm) plus 0.3 mm per side, with 0.5 mm
corner radii and a 1 mm 45° bevel. The LCD active area starts 0.3 mm inside the visual area on the
top, bottom and right, 0.8 mm on the FPC edge, so a thin black border shows. The panel pocket has
0.35 mm clearance per side. The snap-finger seats are at 4.22 mm, 0.15 mm under the nominal panel
back, for the module's +0.2 mm thickness tolerance.
