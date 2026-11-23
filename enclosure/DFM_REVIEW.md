# Design-for-manufacturing review: printed enclosure

> **A review from before the first full prints.** Its geometry changes are all in the current
> design. What printing has changed since: the keypad that works in PETG is the loose keys, and
> the spring key mats were removed, with their pegs in the front (finding 6) and the key test set;
> the transfer jig was removed too (finding 10: the artwork is aligned by eye on marks round and
> inside the front, [docs/FACE_ARTWORK.md](docs/FACE_ARTWORK.md)); the display snap fingers (finding 4) barely hold
> the panel, so a foam pad goes behind it and bar clips are designed ([V3_THIN.md](V3_THIN.md));
> the battery door latch (finding 5) was redesigned as an in-plane band; the ALPHA letters are
> now black; the face transfer is still untested in practice. The build and print steps are in
> the [README](README.md).

Scope (at the time): the top shell, back shell, battery door, key mats and key test set (`case.py`,
and the since-removed `keymat.py`), the face artwork (`overlay.py`) and the checks (`printcheck.py`,
`legendcheck.py`), reviewed for the process they are actually made with:

- **Printer**: Elegoo Neptune 4 Pro, 0.4 mm nozzle, 0.2 mm layers, single extruder (manual
  filament swaps), textured PEI plate (smooth PEI possible later).
- **Material**: PETG (235–245 °C nozzle, 75–80 °C bed, low fan). Compared with PLA: more
  stringing, a little elephant foot, weaker bridges and overhangs, small holes print under
  size, good layer adhesion when the fan is low, more flex before it breaks. Across the layer
  lines it is the weak direction: strength there is taken as ~25 MPa and the usable strain
  as ~1.5–2 % (about 60 % of the bulk values).
- **Face decoration**: gel-medium image transfer of a colour laser print, straight onto the
  shell face (no white toner; areas without toner end up clear).
- **Goal**: professional look, few defects, easy and repeatable assembly.

The PCB (rev A) is fixed: nothing here moves a hole, a key or the display.

Severity: **High** = a part breaks or does not assemble; **Medium** = visible defect, poor fit
or a fragile feature; **Low** = cosmetics or convenience.

## Summary

| # | Finding | Severity | Change |
|---|---|---|---|
| 1 | Back-shell standoffs Ø5.2 × 12.2 mm, free-standing, sharp root | High | Ø7.0, fused to ledge and wall, 1 mm root fillet, 4 gussets: ≈10× the side load |
| 2 | Top-shell insert bosses Ø6, 1.4 mm wall round the insert, free-standing, sharp root | High | Ø7.0 (1.9 mm wall), 1 mm root fillet, web to the side wall (and top wall): ≈2.6× before the web |
| 3 | Shell rabbet modelled with 0.075 mm **interference** per side (comment said clearance) | High | 0.9 mm tongue, 0.15 mm clearance, 0.95 mm lap; shells no longer touch in Z (0.2 mm reveal) |
| 4 | Display snap fingers: 1.9 % strain on paper, ~2.4 % real (inner face fused to the plate), sharp root, across layers | High | Slot frees all four sides; taper 0.9→0.8 mm, 0.4 mm root fillet, 0.25 mm lip: 1.2 % |
| 5 | Battery-door latch 1.6 % strain across layers, sharp root | Medium | 1 mm longer, taper 1.2→0.9 mm, 0.6 mm root fillet, 0.4 mm hook: 0.8 % |
| 6 | Key-mat pins Ø1.2 on Ø2.2 posts: sharp steps (Kt ≈ 2–2.5) | Medium | 0.3 mm fillet at the ceiling, 0.2 mm at the shoulder (pad bores chamfered to match): +55 %. *Since removed with the key mats.* |
| 7 | No elephant-foot allowance on any bed-facing edge or opening | Medium | 0.6 mm chamfer on face and floor edges, 0.25 mm on key openings, 1 mm window bevel, 0.4 mm on counterbores, door recess and door |
| 8 | Hole sizes nominal (PETG holes print 0.1–0.2 mm small) | Medium | M2 bore 2.4, counterbore 4.2 × 2.2 (heads 0.2 below flush), key clearance 0.25, insert mouth chamfer |
| 9 | Shrinkage over 194 mm (~0.6 mm) vs 0.4 mm board clearance | Medium | Clearance 0.5 mm per side; calibration step for XY shrinkage in docs/PRINTING.md |
| 10 | Dark overlay sheet over the whole face; ALPHA letters white | Medium | White body; transfer artwork = labels only, nothing over the openings; ALPHA dark grey |
| 11 | Bezel step and key-opening steps (knife guides) printed in mid-air on the bed face | Low | Replaced by 45° chamfers that grow from the bed |
| 12 | USB-C and microSD openings: sharp outer edges | Low | 0.5 mm × 45° outer chamfers |
| 13 | Seam line where the shells meet: any step shows | Low | 0.3 mm chamfers on both sides: a deliberate V reveal |
| 14 | Seam (slicer) placement not specified | Low | Shells oriented with the USB end at the back of the bed; seam "back" |
| 15 | printcheck only looked for supports | Low | Also thin walls (< 0.8 mm) and slender / fragile columns |

## Findings and changes

### 1. Back-shell standoffs (High)

**What.** Five standoffs carry the PCB from the back-shell floor: Ø5.2 mm with a Ø2.3 mm bore,
12.2 mm tall, standing free (they only touched the PCB ledge tangentially), with a sharp
corner at the floor. They are printed vertically, so a sideways knock loads the layer lines in
tension right at the root. This is the feature that snapped on the first PLA print.

**Why it matters.** Section modulus Z = π(D⁴ − d⁴)/(32 D) = 13.3 mm³; with ~25 MPa across the
layers and a stress concentration of ~2 at a sharp root, ~14 N at the top breaks it — a
dropped screwdriver or the back shell pressed on at an angle.

**Change.** `STANDOFF_R` 3.5 (Ø7.0 mm) with a 2.4 mm bore: Z = 33.2 mm³. Every hole is 3.5 mm
from a board edge, so the standoff now **merges with the PCB ledge and the wall** over its
full height. A `ROOT` 1.0 mm × 45° fillet cone at the floor and four 1.2 mm gussets, 3 mm
reach × 6 mm high (clipped inside the wall), carry the root.

| | Before | After |
|---|---|---|
| Diameter / bore | 5.2 / 2.3 mm | 7.0 / 2.4 mm |
| Wall round the bore | 1.45 mm | 2.3 mm |
| Root | sharp | 1.0 mm fillet + 4 gussets + fused to the wall |
| Side load at the top to break (est.) | ~14 N | > 130 N free-standing equivalent (critical section moves to the gusset tops); with the wall tie, much more |

### 2. Top-shell insert bosses (High)

**What.** Ø6 mm bosses, 8.47 mm long from the plate to the PCB, with a Ø3.2 × 4.5 mm hole for
the heat-set insert: 1.4 mm of plastic round the insert, a sharp root at the plate,
free-standing except the two short bottom ones (already tied to the corner by a gusset).

**Change.** `BOSS_R` 3.5 (Ø7.0, 1.9 mm round the insert: enough for the knurls to melt into
without bulging the boss), `ROOT` 1.0 mm fillet cone at the plate, and a **web 4.2 mm wide to
the side wall** (the top-left boss to the top wall too), from 1.2 mm above the PCB to the
plate. The insert hole keeps the vendor's 3.2 mm (an insert needs an undersize hole) and gets
a 0.4 mm lead-in chamfer that centres the insert. The relief round nearby top-side parts was
re-checked for the larger foot (nearest part J6, 0.9 mm clear). The two bottom bosses stay at
Ø6 / `BOSS_LOW` (the ON and ENTER flanges sit over them) and keep their corner gussets.

| | Before | After |
|---|---|---|
| Diameter | 6.0 mm | 7.0 mm |
| Wall round the insert | 1.4 mm | 1.9 mm |
| Root | sharp | 1.0 mm fillet + web to the wall |
| Side load at the foot to break (est., no web) | ~31 N | ~83 N |

### 3. Rabbet interference (High)

**What.** The top shell's lap was modelled 1.075 mm thick over a back-shell tongue of 1.0 mm
in a 2.0 mm wall: the two overlapped by 0.075 mm per side, although the comment called it a
slip fit. With PETG's slight over-extrusion the shells would not have closed without force,
or would have bowed the long sides. The shells also met in Z at two faces (lap bottom and
tongue top) as well as clamping the PCB: over-constrained.

**Change.** `RAB_KEEP` 0.9 mm tongue (two perimeters), `RAB_CLR` 0.15 mm per side, lap 0.95 mm.
The back's outer wall stops `SEAM_GAP` 0.2 mm below the lap and the tongue 0.2 mm below the
top shell's wall, so the PCB (boss foot on top, standoff below) alone sets the stack. Both
edges of the seam get a `SEAM_CH` 0.3 mm chamfer: a crisp 0.8 mm V reveal that hides any
mismatch between the shells. `case.py` now asserts that the assembled shells do not intersect.

### 4. Display snap fingers (High)

**What.** Five 0.8 mm cantilevers retain the panel (the VHB tape holds it). The docstring
gave 1.5 × 0.8 × 0.4 / 5.05² = 1.9 % strain, already about PETG's across-layer limit. But the
slot started 0.25 mm outside the panel outline while the finger's inner face is 0.05 mm
outside it, so above the tape recess the finger was fused to the plate on its inner side: the
real free length was ~4.5 mm, ~2.4 % strain, at a sharp root.

**Change.** The slot now frees the finger on all four sides (0.5 mm also on the panel side)
up to `FINGER_ROOT`. The finger tapers from 0.9 mm at the root to 0.8 mm at the tip (never
under two perimeters), has a 0.4 mm × 45° root fillet on its outer face, and a 0.25 mm lip
(the VHB holds the panel; the lip only retains it). `case.finger_strain()` integrates the
tapered beam: **1.2 %**. A screw-retained clamp was considered and rejected: the panel spans
nearly the full board width, there is no room for screw bosses beside it, and the PCB under
the panel back is populated.

| | Before | After |
|---|---|---|
| Section | 0.8 mm uniform | 0.9 → 0.8 mm taper |
| Free length (root to lip) | ~4.5 mm (fused) | 5.15 mm |
| Lip | 0.4 mm | 0.25 mm |
| Root | sharp | 0.4 mm fillet |
| Strain to snap over | ~2.4 % (1.9 % claimed) | 1.2 % |
| Margin vs ~1.5–2 % across layers | < 1 | 1.3–1.7 |

### 5. Battery-door latch (Medium)

**What.** 1.2 mm × 7.5 mm cantilever, 0.5 mm hook: 1.6 % strain, sharp root, bending across
the layers (the door prints plate-down).

**Change.** `LATCH_Z` 1 mm higher (8.5 mm cantilever, its top still 1.5 mm below the PCB),
taper 1.2 → 0.9 mm, 0.6 mm × 45° root fillet on the inner face, 0.4 mm hook: `latch_strain()`
= **0.8 %**, margin ≈ 2. The catch rib's buttress now runs up to 1 mm under the rib top. The
door plate gets 0.8 mm corners (the recess 1.0 mm) instead of square ones, keeping the 0.2 mm
gap even round the corners.

### 6. Key-mat anchor posts and pins (Medium)

*Since removed: the key mats are retired and the front has no pegs.*

**What.** 67 posts (Ø2.2 × 3.4 mm) hang from the ceiling and end in a Ø1.2 × 2.8 mm pin. Both
steps (ceiling to post, post to pin) were sharp; the pin root has Kt ≈ 2.5. In service the pin
is inside a glued pad and carries ~0.1 N; the risk is handling before and during assembly.

**Change.** `POST_FILLET` 0.3 mm cone at the ceiling, `PIN_FILLET` 0.2 mm cone at the
shoulder; the pad bores get a matching 0.25 mm chamfer at the top (so the pad still seats on
the shoulder) and the cap flanges are relieved round the post fillet. Pin side load to break
at its tip (est.): 0.6 → 0.95 N (+55 %); post: 2.1 → 3.2 N. Larger pins were not possible:
the D-pad and soft-key pads are Ø2.3 mm (`CUSTOM_PAD_R`), leaving 0.55 mm of tube wall round
the 1.2 mm bore. printcheck lists the pins as slender (h/w ≈ 5): accepted. The test-shell legs
(Ø2.2 × 8.5 mm) get the same 0.3 mm root fillet.

### 7. Elephant foot on bed-facing edges (Medium)

**What.** The top shell prints face-down: its face, the key openings and the window are on the
bed, where PETG's first layer spreads ~0.1–0.3 mm. The back shell's floor, its screw
counterbores and door recess, and the door plate are on the bed too. Sharp edges there grow a
lip that is visible, closes the key gaps unevenly and catches the transfer paper.

**Change.** `EDGE_CH` 0.6 mm × 45° round the face and round the back's floor; `KEY_CH`
0.25 mm × 45° round every key opening (with the 0.25 mm clearance this keeps the label webs
as wide as the old 0.2 clearance + 0.3 mm step did, so the smallest labels stay 1.35 mm); `WINDOW_CH` 1.0 mm bevel round the window with 0.5 mm corners (the window used to
have square corners); `EF` 0.4 mm on the screw counterbores, the door recess and the door
plate. All are 45°, so they print from the bed without support. Slicer elephant-foot
compensation 0.15 mm on top of that (docs/PRINTING.md).

### 8. Hole compensation and fits (Medium)

| Feature | Before | After | Note |
|---|---|---|---|
| M2 clearance bore (back) | 2.3 | **2.4** | + `HOLE_COMP` 0.2 on 2.2 nominal |
| Socket-head counterbore | 4.2 × 2.0 | **4.2 × 2.2**, 0.4 mm chamfer | head Ø3.8 × 2.0 sits 0.2 below the surface (flush look, no proud heads) |
| Heat-set insert hole | 3.2 × 4.5 | 3.2 × 4.5 + 0.4 mm lead-in | vendor size; undersize on purpose |
| Key opening | cap + 0.2 per side | cap + **0.25** per side | caps print slightly large, openings slightly small |
| PCB in shell | 0.4 per side | **0.5** per side | shrinkage, see 9 |
| Panel pocket | 0.35 per side | unchanged | ±0.2 module tolerance + PETG |
| Door plate in recess / plug in opening | 0.2 / 0.2 per side | unchanged, rounded corners | |
| Rabbet tongue in lap | −0.075 (interference) | **+0.15** per side | see 3 |
| Transfer paper in jig fence | — | 1.0 per side (guide only; the pins register) | see 10; jig since removed |
| Punched paper on jig pins | — | Ø5.7 pins in 6.0 punched holes: 0.15 per side | see 10; jig since removed |
| Pad bore on pin | 1.2 on 1.2 (press fit) | unchanged | key mats since removed |

### 9. Shrinkage and warping of the long parts (Medium)

PETG shrinks ~0.2–0.4 %: 0.4–0.8 mm over the 194 mm shells. The board now has 0.5 mm per side
(`GAP`), so the board never binds on the end walls. The screw holes at the two ends are
181.8 mm apart, and the PCB does not shrink: the M2 bores have 0.2 mm of slop and docs/PRINTING.md
adds a calibration step (measure the printed hole spacing, set the slicer's XY shrinkage
compensation if it is more than 0.2 mm short). Warping: the outline already has 6.4 mm corner
radii (no sharp corners to lift); mouse ears (checked clear of every first-layer opening) or a
5 mm outer brim, 80 °C first layer, no fan for 3 layers.

### 10. Colour scheme and face artwork (Medium)

**What.** The body was space grey and the overlay a full-face laser print on vinyl or film with
a grey background, white (unprinted) ALPHA letters and hole guide lines, cut out on the shell.
For a transfer, a large toner area is the worst case: any bubble, fibre or rub-through shows,
and it cannot be white.

**Change.** White body (shells and door). The artwork is now only the labels: SHIFT functions in
blue (= the SHIFT key), ALPHA letters in **dark grey**, a small grey name line, about
170 mm² of toner in all (`overlay.py` prints the figure). No toner over the key openings or
the window (checked with a 0.2 mm margin) and nothing outside the face: the paper over the
openings washes away. The page is mirrored (`overlay_transfer.pdf`) with a non-mirrored proof
(`overlay_proof.pdf`) that also shows the shell outline and openings as hairlines for a
light-table check. Registration: the paper is cut square (8 mm outside the shell at the sides, 20 mm at the
ends), punched at two printed targets with an office hole punch (ISO 838, 6 mm) and put over
two pins on the deck of a new `transfer_jig.stl`, which surrounds the shell; the deck is one
layer below the face and gets no medium, so nothing printed on it transfers. One round pin
(top) and one diamond pin (bottom, flats along the pin axis) register position and rotation
without over-constraining the paper; the pins sit asymmetrically (x +10 / −25 mm, 12 mm
beyond the face's ends), so the paper fits only one way. The pins are Ø5.7 × 3 mm above the
deck on solid columns, with a 1 mm 45° root fillet in a 1 mm well (so the fillet stays below
the paper) and a 0.6 mm tip chamfer: in PETG they will not snap, and a brayer at the face's
end clears them. The fence was opened to 1 mm a side so it no longer fights the pins.
Alignment is checked before pressing: crosshairs printed 3.5 mm beyond the face's corners sit
over crosshair slots (0.6 mm wide) through the deck, and L marks 0.8 mm inside the display
window's corners sit inside the open window; on a light, a mis-punched sheet is caught before
the medium is touched. `overlay.py` asserts that no mark lands on the face (corner marks and
targets clear the jig's inner wall; window marks 0.3 mm inside the aperture) and `case.py` that
no slot runs over a rib under the deck. No separate punch guide: the targets' witness ring
shows a mis-punched hole at once, and the see-through check catches the rest. The
vinyl/film overlay, its cutting jig and the knife-guide steps are gone. *(Later the transfer jig,
its pins, crosshairs and punch targets were removed as well: the paper, now 8 mm larger than the
shell all round, is aligned by eye on L marks round the front's corners and outlines inside the key
holes and window.)*

Keycaps for a white body (`firmware/keys/layout.py`; only colour values and the group →
filament mapping changed): number keys **white** with dark grey legends, function keys **light
grey** with dark grey legends, SHIFT / ALPHA / ÷ × − + / enter **cobalt blue** with white
legends; one-piece set light grey with dark grey legends. Every legend contrasts ≥ 5:1 with its
cap. The cap filament partition is the same as before (fn / num / accent + alpha), so the
key-mat routing is unchanged. Renders: `renders/assembled_front.png`, `renders/keypad_top.png`,
`renders/overlay.png`.

### 11–13. Surfaces and openings (Low)

- The old bezel step (0.6 mm deep, 1.2 mm wide) and the 0.3 × 0.2 mm key-opening steps were
  knife guides for cutting the vinyl overlay. On the face-down print their floors start in
  mid-air (short bridges, rough). With the transfer they are not needed; 45° chamfers replace
  them and grow cleanly from the bed.
- USB-C pocket and microSD slot + scallop get a 0.5 mm 45° chamfer round their outer edges
  (`OPENING_CH`): no sharp lip, the plug finds its way in, and the layer ends at the opening
  are hidden in the bevel.
- The seam between the shells is a deliberate V reveal (finding 3).
- The back stays text-free; screw heads sit 0.2 mm below the surface.

### 14. Seam placement and surface quality (Low)

The top shell is now exported flipped about Y (not X), so both shells lie with their **USB end
at the back of the bed**: with seam position "back" the outer-wall seam runs up the end wall
that already has the openings. Face and floor: monotonic first-layer pattern (an even surface
for the transfer), 25–30 mm/s first layer, Z hop to keep the nozzle from scarring the webs.
Textured PEI leaves a fine texture on the face; the transfer goes on after a 400–600 grit
sanding. Smooth PEI (with glue stick as a release agent) is the upgrade for a smooth face.

### 15. printcheck (Low)

`printcheck.py` now also slices every part each layer and reports:
- **thin walls**: anything narrower than 0.8 mm (two perimeters) larger than 1 mm² per
  layer fails (the inlaid legend layers are skipped; `legendcheck.py` covers them);
- **slender columns**: free-standing islands followed through the layers; those thinner than
  2.5 mm and taller than twice their width are listed, those taller than 8× their width fail.
Bridges and overhangs are checked as before (≤ 10 mm span, ≥ 45°).

## Check results

All run on the final geometry (`case.py`, then `printcheck.py`, `legendcheck.py`, `render.py`):

| Check | Result |
|---|---|
| Key-mat routing (`keymat.check`) | 50 caps, 100 flexures, 67 pads, 17 ties: OK; worst flexure strain 0.86 % (unchanged) |
| 3D interference (mats vs shell, switches, each other; key test set) | none |
| Shells vs each other, jig vs top shell (new asserts) | no interference |
| Snap fingers / door latch strain (new) | 1.18 % / 0.85 % |
| `printcheck.py` (supports, bridges ≤ 10 mm, thin walls, columns) | all 10 print files OK; longest span 9.7 mm (jig deck, at the pin ends); 0 thin walls; slender: the 67 pins (1.2 mm, h/w 5.2), test legs (2.2 mm, h/w 3.8), cap stems inside the mats (h/w ≤ 2.5); none fragile |
| `legendcheck.py` | 50 legends, 0 flagged; strokes ≥ 0.44 mm |
| Mouse ears vs first-layer openings | top: clear of 51 openings; back: clear of 6 |
| Artwork (`overlay.py`) | 71 labels, 1.35–2.8 mm; 167 mm² of toner, none over an opening or outside the face; alignment marks (4 corner crosshairs, 2 punch targets, 4 window L marks) none on the face, 0.72 mm clear; PDFs are US Letter, cut rectangle measures 103.0 × 233.8 mm as designed |
| Firmware (`make -C firmware keydoc-check`, `make test`) | key docs regenerated and up to date; all tests pass |

printcheck's first run on the new geometry found, and the second confirmed fixed: the USB and
microSD cuts left a 0.2–0.3 mm sliver of PCB ledge (cuts now go through the ledge), the key
test shell's cut edge left a 0.1 mm sliver of a post (edge moved to x 47.0) and its board tabs
were 0.75 mm (now 1.05), the first finger taper reached 0.6 mm (now 0.8), and the jig deck had
11.7 mm corner bridges (diagonal ribs added).

## Left as recommendations

- **Smooth PEI for the top shell** once available: a glossy face takes the transfer with
  less sanding.
- **Panel retention** is now VHB plus 0.25 mm lips. If a module ever comes loose in a drop,
  add a foam pad (2 mm EVA) between the panel back's free right two thirds and the PCB rather
  than larger lips.
- **Screw torque**: PETG creeps; snug the M2 screws, don't crank them (README). Brass inserts
  in the back shell instead of plain bores would allow repeated opening without wear, but the
  screws thread into the top shell's inserts already, so the back's bores see no wear.
- **Halftone toner**: the dark grey and blue are toner mixes; if a printer dithers them
  visibly, set `ALPHA_INK` to pure black (`#000000`) for solid toner.
- **Transfer durability**: the varnish is the wear layer; renew it if the face is used hard.
