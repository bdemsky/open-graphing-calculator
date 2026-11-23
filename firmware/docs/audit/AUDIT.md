# Sparkle Town audit: difficulty, clarity, controls (pass 5)

Independent audit of `firmware/modules/mathquest` at commit `4876254` (branch `worktree-agent-a47fa1d6c639da127`). Nothing in the module was changed. Method: the simulator (`make sim`) driven with the test hooks (`mq new`, `mq engine stop`, `mq seed`, `mq skill S D`, `mq mech`, `mq step`, `mq try`, `state`) rendered **every skill x every difficulty (0-3) x 3 seeds** in the game the engine tells it in: the opening frame, the acted story, the play frame, the frame after one miss and after two (1,032 orders, 5,160 frames), plus every skill in every game it fits (`mq mech`, 1,376 frames). The frames were reviewed as contact sheets (`shots/sheet_*.png`); `orders_table.txt` lists all 1,032 orders (skill, difficulty, game, text, answer). Six simulated new players (three profiles x two seeds, engine on, default settings) played the placement and three sessions of four rounds (`players.json`). The worst items were re-shot on the Mac page (`web/dist/sparkle.html`, headless Chrome via a copy of `web/test/run_mac_shots.js`: `shots/mac_*.png`). Scripts: `tools/` (paths point to a scratch directory).

## Summary

**36 findings: 4 critical, 14 high, 15 medium, 3 low.**

The parent's report ('jumping into pretty hard problems') is reproduced exactly: at default settings **every simulated new player's first four orders were grade 4 and a grade-5 order came within the first eight** (`1071 stars into 17 clouds`), because the starting grade defaults to 4 and the placement climbs a grade after a single right answer (D1). After the placement nothing pulls a struggling child back down: simulated players ran at 26-67 % first-try accuracy for three sessions (target 85 %) while grade-4/5 skills kept coming (D2). Inside the skills the ladders are mostly not ladders: 33 of 86 generators ignore the difficulty, others only widen a range, and some difficulty-0 items are the hardest variant (900 - 594, 5/6 = ?/30) (L1-L4). There is no 'like on paper' column workspace anywhere and no worked example when a new complication arrives (L4, L5).

On clarity: the order board shows only a short caption, and for several skills that caption has **no numbers at all** ('Pie in all?', 'Which tray has more pie?', 'Paint the whole stage!', 'Ribbons of 7/4 in?') so the task is unsolvable on screen (T1); many other captions drop the unknown, including the very bug the parent saw ('5.4 kg into 6 baskets!' is still there; 'Fill the egg boxes!' does not say full boxes vs boxes needed) (T2). Pictures sometimes contradict the numbers (base-ten blocks, a 'square' drawn as a rectangle) (P1-P3); the goal banner and the guide cover coins, graph keys and bin labels (O1); Shape families grades a right answer wrong (G1); rounding gives the answer away (G2). Controls are inconsistent per game, the lock wheels and keypads are 3-6 mm targets, nothing supports a Mac scroll wheel (C1-C5).

What is good: Pie Shop, Clock Tower, Treasure Dig and Hop Along read well; the acted story stage is charming and clear for groups/sharing stories; the first-time demos, idle hints and the helper friend are present in every game; no padlocks anywhere (one leftover sentence, K1); right/wrong reactions are visible in every game.

## Status after the fixes (pass 6)

Re-run of the drivers (`tools/gen.py all`, `tools/parse.py`, `tools/players.py`; paths now follow the
checkout, scratch in `$AUDIT_SCRATCH`, and `gen.py` walks every skill's rungs from `mq rung S` instead of
difficulties 0-3): 1,023 orders (86 skills x their 2-8 rungs x 3 seeds) and 1,376 skill-in-game frames
rendered; no console errors except the expected 'does not fit this order' for games a picture cannot be
drawn in. The six simulated new players now start at grade 3, meet no grade-5 order in three sessions,
and the slowest game speed is the floor (20 of 100, about half the cruise) instead of 1-7.

| Id | Status | Where |
|---|---|---|
| D1, D2, T1, T2, G1, X1, W1, K1 | closed (earlier merge) | placement, strand controller, board text, captions, shape families, x0/x1, game words, finale |
| L1, L2, L3, L4 | closed | every generator rewritten as a ladder of named rungs, one complication each (`mq_rung.c`, `mq_gen.c`; `test_mq_ladders.c`) |
| L5 | closed in part | teaching moments for every big idea (`mq_teach.c`) and the notebook telling each column step (`mq_cols.c`); a new rung inside an idea has no moment of its own |
| L4 (workspace) | closed | the notebook: column add / take away / multiply / long division in the Heist, the till, the kitchen (`mq_cols.c`; `test_mq_cols.c`, `tests/ui/49_mathquest_notebook.txt`) |
| P1, P2, P3, P4, Z3, B1 | closed | pictures fit their numbers; a validator per visual (`mq_vis_check`) over every skill, rung and seed (`test_mq_models.c`); games are chosen only where the picture can be drawn |
| X2, X3 | closed | plain wording ('The big pot has 12 seeds. That is ...', 'What is the 4 worth in 40?'), plurals |
| O1, Z1 | closed | the HUD line holds the goal, the demo's words and hints; the Kart place pill moved into it |
| C1, C2, C3, C5, C6 | closed | the widget library (`mq_widget.c`): wheels with arrows / drag / flick / scroll and the value kept after a dud, 48 px hit areas, the nearest clock hand by angle, the Mac scroll wheel, r/R mapped, focus rings, 'Dial the answer, then open!' (`test_mq_widgets.c`) |
| C4 | closed | one protractor scale (the angle's own side); LEFT / RIGHT a degree, UP / DOWN five; the readout stays off where reading the protractor is the task |
| G2 | closed | Hop Along starts at the number, both ends labelled, 'land!' needs a hop |
| P5 | closed | Peekaboo's key caps only beside a friend who is out |
| H1 | closed | the similar order is drawn behind the helper friend's entrance |
| S1 | closed | the game speed has a floor (20 of 100) |
| Z2 | closed | Sort Slide's card shows the whole order when it fits, else its line with the numbers (the board has it all) |
| F1 | closed in part | 'You tried 696' with the why in the Heist; per-story misconception lines still open |
| X4 | closed in part | the unknown's place follows the ladder (3 x ? = 12 first, ? / 3, then 24 / ?); the split mural still has no picture |

## Top 10 (checked on the calculator frames and on the Mac page)

1. **D1 (critical)** any (first game) / placement (all strands): A new player at default settings starts every strand at grade 4 (MQ_START_GRADE 4) and the placement climbs to grade 5 after ONE right answer. All six simulated new players got four grade-4 orders first ('48 seeds is 6 times the pot's. The pot?', '4717 + 2057'... Evidence: players.json (all 6 simulated players, orders 0-8); shots/sheet_ov_d0_c_play_72.png (s74).
2. **T1 (critical)** Bubble Pop, Sort Slide, Treasure Heist, Peekaboo / 32 Add and take slices; 31 Compare any slices; 51 L-shaped rooms; 64 Fraction line plots; 76 Expressions: The order board shows only the caption (p->cap) when it is set; for these skills the caption has no numbers and the stage draws nothing (stg = 0), so the numbers of the task are NOWHERE on screen: 'Pie in all?' (5/8 + 6/8 hidden), 'Which tray has more pie?' (5... Evidence: shots/s32_d0_r1_c_play.png, s31_d0_r1_c_play.png, s51_d0_r1_c_play.png, s64_d0_r1_c_play.png, s76_d0_r1_c_play.png, mac_pie_in_all.png.
3. **D2 (critical)** all / engine / 85% controller: The success-rate controller only moves a skill's difficulty (0..3) after 6 answers of that skill; when accuracy is far below 85% across a strand there is no step DOWN in grade. Simulated players ran at 26-67% first-try accuracy for 3 sessions (target 85%) whil... Evidence: players.json summary (acc by session); mq log in players.json.
4. **G1 (critical)** Sort Slide / 53 Shape families: A square is drawn and the bins are rectangle / square / rhombus / trapezoid; only 'square' is accepted. A square IS a rectangle and a rhombus (3.G.1 is exactly this idea), so two mathematically right answers are graded wrong and the helper teaches the misconce... Evidence: shots/s53_d0_r1_c_play.png, mac_shape_family.png.
5. **T2 (high)** Treasure Heist, Shop Till, Bubble Pop, Peekaboo / 10 Leftover stories; 74 Two-digit sharing; 73 Decimal sums; 45 Making change; 7 Two-step; 43 Measuring stories; 46 Shop stories; 78/82 grade 5 stories; DECOPS: Captions drop the unknown: 'Fill the egg boxes!' (43 eggs, boxes of 6: full boxes 7 or boxes needed 8? The picture shows the same 'x?' for 'Everyone must ride!' whose answer rounds UP), '442 stars, 13 clouds!', '2 jars of 0.2 l honey!', '5.4 kg into 6 baskets!... Evidence: shots/s10_d0_r3_c_play.png vs s10_d0_r2_c_play.png, s74_d0_r1_c_play.png, s73_d0_r1_c_play.png, s45_d0_r1_c_play.png, mac_eggs_ambig.png.
6. **L1 (high)** all / 33 skills (4, 6, 11, 13, 14, 17, 19, 27, 28, 33, 34, 38, 39, 41, 43, 44, 50, 54, 55, 61, 64, 66, 68, 70, 72, 73, 75, 77, 79, 80, 81, 82, 83): The generator ignores the difficulty for 33 of 86 skills: with the same seed difficulty 2 gives exactly the problems of difficulty 0 (and 3 those of 1). Many others only WIDEN the range (R(q, 2, 3 + d)) so d3 still serves 2 x 2 and d0 can be the hardest varian... Evidence: orders_table.txt; AUDIT.md ladder table.
7. **P1 (high)** Gate Dash, Sort Slide / 13 Hundreds tens ones; 14 Bigger parcel; 18; 19: The base-ten blocks drawn in the field do not match the number: 685 is drawn as 3 hundreds (capped/clipped by the field), the ones are hidden behind the gates; the same 3-flat picture appears for 441, 512, 574 and 695. In Sort Slide only ONE number's blocks ar... Evidence: shots/s13_d0_r1_c_play.png, s13_d3_r3_c_play.png, s14_d0_r1_c_play.png, mac_base10_gate.png, sheet_sk13.png.
8. **O1 (high)** all games / all: The goal banner ('Drop in the coins!', 'Slide it in the right bin!', 'Pop the bubble that fits!') and the guide sprite in the bottom-left are drawn OVER the play field: they hide the pennies to be counted (Count coins), the graph key 'each star = 2' (Scaled gr... Evidence: shots/s44_d0_r1_c_play.png, s62_d0_r1_c_play.png, s53_d0_r1_c_play.png, s00_d0_r1_c_play.png, mac_coins_hidden.png, mac_base10_gate.png.
9. **C1 (high)** Treasure Heist / every heavy order (add/sub, multiply, divide, time, measures): The lock wheels are tap-only (top half rolls up, bottom half down) with a hit box of 20 x 32 logical px (about 4 x 6.5 mm on the 3.2" panel; big enough only on the Mac, where a logical px is ~4 CSS px): no drag or flick to spin, no mouse-wheel/trackpad scroll ... Evidence: mq_play.c:2558 (LOCK_X +-10, LOCK_Y +-16); web/page/sparkle_page.js:670 (wheel ignored without ctrlKey); shots/mac_heist_wheels.png.
10. **L4 (high)** Treasure Heist, Bubble Pop / 22 Multiply bigger; 23 Divide parcels; 21 Big add and subtract; 66 Mega multiply; 74 Two-digit sharing: Jumps too big: Multiply bigger goes 2-digit x 1 (d0) -> 3-digit x 1 (d1) -> 4-digit x 1 (d2, 5 x 8999) -> 2 x 2 (d3); Divide parcels 45/9 (d0) -> 253/3 (d1) -> 7313/9 = 812 R 5 (d2). Mega multiply starts at 288 x 34 at d0. No column ('like on paper') workspace... Evidence: orders_table.txt rows 21-23, 66, 74; shots/s23_d0_r1_c_play.png, s66_d0_r1_c_play.png.

## Findings (severity-ranked)

| id | sev | category | game | skill | diff | issue | evidence | suggested fix |
|---|---|---|---|---|---|---|---|---|
| D1 | critical | difficulty | any (first game) | placement (all strands) | placement | A new player at default settings starts every strand at grade 4 (MQ_START_GRADE 4) and the placement climbs to grade 5 after ONE right answer. All six simulated new players got four grade-4 orders first ('48 seeds is 6 times the pot's. The pot?', '4717 + 2057', '1/6 = ?/12', '8 km = ? m' for seed 1) and a grade-5 order within their first eight (e.g. order 5: '1071 stars into 17 clouds', two-digit division), struggling profile included. Converting units with decimals ('1.4 m in cm', '730 cm = ? m') followed in the same session. This is exactly the 'jumping into pretty hard problems' report. | players.json (all 6 simulated players, orders 0-8); shots/sheet_ov_d0_c_play_72.png (s74) | Default start grade 3 (or ask the grown-up/age at setup); start each probe at difficulty 0 of the grade BELOW the start grade and climb; require 2 of 2 right (first try, not a guess-speed answer) to step a strand up; never probe grade 5 in the placement unless the grown-ups set grade 5; cap the placement result at start_grade+1. (`mq.h:429 MQ_START_GRADE; mq_engine.c probe_skill(), mq_place_next() (*diff = 1), mq_place_answer()`) |
| D2 | critical | difficulty | all | engine / 85% controller | all | The success-rate controller only moves a skill's difficulty (0..3) after 6 answers of that skill; when accuracy is far below 85% across a strand there is no step DOWN in grade. Simulated players ran at 26-67% first-try accuracy for 3 sessions (target 85%) while grade-4 and grade-5 skills kept coming (1-3 grade-5 orders a session even for the struggling profile). Assumed skills (below the placed grade) come back at difficulty 2, i.e. harder than her level. | players.json summary (acc by session); mq log in players.json | Add a strand-level controller: rolling first-try rate per strand over the last 10 orders; below 70% lower the strand's working grade by one (un-assume the skills below), above 90% allow the next grade. Pick goals so the expected rate is ~85% (use mastery/history to predict). Assumed skills should be checked at difficulty 0-1, not 2. (`mq_coach.c mq_coach_answer(); mq_engine.c mq_engine_diff() (imax(r->diff, 2)), assumed(), goal picking`) |
| T1 | critical | text/picture | Bubble Pop, Sort Slide, Treasure Heist, Peekaboo | 32 Add and take slices; 31 Compare any slices; 51 L-shaped rooms; 64 Fraction line plots; 76 Expressions | all | The order board shows only the caption (p->cap) when it is set; for these skills the caption has no numbers and the stage draws nothing (stg = 0), so the numbers of the task are NOWHERE on screen: 'Pie in all?' (5/8 + 6/8 hidden), 'Which tray has more pie?' (5/6 vs 3/4 hidden; the card shows only '(Psst: think of half a pie.)'), 'Paint the whole stage!' (no L shape drawn in the Heist), 'Ribbons of 7/4 in?' (no line plot in the Heist), 'How many times as big?' (no expression). The task cannot be solved without discovering that tapping the board shows the full story. | shots/s32_d0_r1_c_play.png, s31_d0_r1_c_play.png, s51_d0_r1_c_play.png, s64_d0_r1_c_play.png, s76_d0_r1_c_play.png, mac_pie_in_all.png | Rule: if stg == 0 the board shows the full text (all three lines), never the caption; or write captions that carry the numbers ('5/8 + 6/8 of a pie?'). A unit test: for every skill and difficulty, every number of text[] appears on the drawn board/field (render to the text log and compare). Games that cannot draw the model (Heist for V_LSHAPE, V_LINEPLOT) must not be chosen for that order (mq_mech_fits). (`mq_play.c:2705 draw of the board; mq_gen.c:783, 808, 1172, 1787 CAP(); mq_stage.c`) |
| G1 | critical | grading | Sort Slide | 53 Shape families | all | A square is drawn and the bins are rectangle / square / rhombus / trapezoid; only 'square' is accepted. A square IS a rectangle and a rhombus (3.G.1 is exactly this idea), so two mathematically right answers are graded wrong and the helper teaches the misconception ('shape look'). | shots/s53_d0_r1_c_play.png, mac_shape_family.png | Choose distractors that are NOT super-families of the shape (for a square: trapezoid, pentagon, hexagon, kite-not-rhombus) or ask 'Which name fits BEST?' with only one valid; or accept every family the shape belongs to. (`mq_gen.c:1198 S_SHAPES (others[] from pick[])`) |
| T2 | high | text | Treasure Heist, Shop Till, Bubble Pop, Peekaboo | 10 Leftover stories; 74 Two-digit sharing; 73 Decimal sums; 45 Making change; 7 Two-step; 43 Measuring stories; 46 Shop stories; 78/82 grade 5 stories; DECOPS | all | Captions drop the unknown: 'Fill the egg boxes!' (43 eggs, boxes of 6: full boxes 7 or boxes needed 8? The picture shows the same 'x?' for 'Everyone must ride!' whose answer rounds UP), '442 stars, 13 clouds!', '2 jars of 0.2 l honey!', '5.4 kg into 6 baskets!' (still present), 'A surprise gift!' (making change), '2 at 10c each, paid $1!', '2 bottles of 353 ml!', 'Fair shares, then more!', '1/2 + 1/3 of a pizza eaten!', '3 cups, 1/3 cup a glass!'. | shots/s10_d0_r3_c_play.png vs s10_d0_r2_c_play.png, s74_d0_r1_c_play.png, s73_d0_r1_c_play.png, s45_d0_r1_c_play.png, mac_eggs_ambig.png | Every caption must end with the question and its unit ('How many FULL boxes?', 'Stars on each cloud?', 'Liters of honey?', 'Change back?'). Add a test: the caption contains '?' and one of the answer's unit words/question words; for remainder stories the caption must name full / needed / left over. (`mq_gen.c CAP() calls (1720, 1728, 1741, ...); mq_story.c caps table (ST_FULL, ST_BUS, ST_CHANGE, ...)`) |
| P1 | high | picture | Gate Dash, Sort Slide | 13 Hundreds tens ones; 14 Bigger parcel; 18; 19 | all | The base-ten blocks drawn in the field do not match the number: 685 is drawn as 3 hundreds (capped/clipped by the field), the ones are hidden behind the gates; the same 3-flat picture appears for 441, 512, 574 and 695. In Sort Slide only ONE number's blocks are drawn although two are compared, and no scale/pans are drawn for 'Which way does the scale tip?'. | shots/s13_d0_r1_c_play.png, s13_d3_r3_c_play.png, s14_d0_r1_c_play.png, mac_base10_gate.png, sheet_sk13.png | Draw base-ten models only when they fit exactly (scale the flats, or show a place-value chart with digits); in Sort Slide draw both numbers on two pans; never draw a partial model. (`mq_vis.c v_base10(); mq_play.c Gate Dash / Sort Slide field layout; mq_gen.c:462 S_CMP3`) |
| O1 | high | clutter/occlusion | all games | all | all | The goal banner ('Drop in the coins!', 'Slide it in the right bin!', 'Pop the bubble that fits!') and the guide sprite in the bottom-left are drawn OVER the play field: they hide the pennies to be counted (Count coins), the graph key 'each star = 2' (Scaled graphs), the bin labels (Sort Slide: 'square', 'rhombus', 'composite'), the 6/7/5 of the clock face, and answer bubbles that float into the bottom-left. Flying coins cover gate labels (Gate Dash '19' read as '9'). | shots/s44_d0_r1_c_play.png, s62_d0_r1_c_play.png, s53_d0_r1_c_play.png, s00_d0_r1_c_play.png, mac_coins_hidden.png, mac_base10_gate.png | Reserve a banner strip outside the field (or put the goal in the board); never draw decorations/coins over answer objects or labels; spawn bubbles outside the guide's corner. Add a layout test: no answer object or model label rectangle intersects the banner/guide rectangles. (`mq_play.c draw of the goal banner (goal[] at :54) and mq_guide.c corner guide`) |
| C1 | high | controls | Treasure Heist | every heavy order (add/sub, multiply, divide, time, measures) | all | The lock wheels are tap-only (top half rolls up, bottom half down) with a hit box of 20 x 32 logical px (about 4 x 6.5 mm on the 3.2" panel; big enough only on the Mac, where a logical px is ~4 CSS px): no drag or flick to spin, no mouse-wheel/trackpad scroll on the Mac (the page forwards only ctrl+wheel as a pinch), no visible up/down arrows. The wheels reset to 0000 after a dud so she cannot see what she entered. | mq_play.c:2558 (LOCK_X +-10, LOCK_Y +-16); web/page/sparkle_page.js:670 (wheel ignored without ctrlKey); shots/mac_heist_wheels.png | Use a shared Wheel widget (see AUDIT.md 'Widget library'): >= 48 x 60 px hit area, visible arrows above/below, tap arrow or above/below, drag/flick with snap, Mac wheel scroll, UP/DOWN and digits, focus ring; keep the entered value after a dud and show it in the why bubble. (`mq_play.c ME_VAULT; sparkle_page.js wheel handler`) |
| C2 | high | controls | Cake Kitchen, Pet Shop/Shop Till, digit tiles (made answers), Treasure Dig | all typed answers | all | Hit targets far below 10-12 mm: digit tiles 15 x 17 px (3 x 3.5 mm), Cake Kitchen pad keys 30 x 20 px, till keys 34 x 26 px, Treasure Dig cells 22 x 18 px, the 'go' buttons ~70 x 30 px (6 mm high). | mq_play.c:1598 TILE_W 15, :2219 pad_key, :2524 till keys, :2542 dig cells | Minimum 48 x 48 logical px for every tappable control (the panel is ~4.9 px/mm); one shared Keypad widget for every typed answer; Dig: tap selects, a magnifier/crosshair shows the cell, arrows nudge. (`mq_play.c`) |
| L1 | high | ladder | all | 33 skills (4, 6, 11, 13, 14, 17, 19, 27, 28, 33, 34, 38, 39, 41, 43, 44, 50, 54, 55, 61, 64, 66, 68, 70, 72, 73, 75, 77, 79, 80, 81, 82, 83) | all | The generator ignores the difficulty for 33 of 86 skills: with the same seed difficulty 2 gives exactly the problems of difficulty 0 (and 3 those of 1). Many others only WIDEN the range (R(q, 2, 3 + d)) so d3 still serves 2 x 2 and d0 can be the hardest variant. There is no rung structure, no explanation when a rung changes. | orders_table.txt; AUDIT.md ladder table | Rewrite each generator as an explicit rung list (AUDIT.md), one new complication per rung, a floor that rises with the rung, and a worked example ('new step!') the first time each rung appears. (`mq_gen.c every case`) |
| L2 | high | ladder | Treasure Heist | 16 Add and subtract | d0 | Difficulty 0 is meant to be two-digit, but one subtraction in three takes the 'borrowing across a zero' branch that ignores the difficulty: 900 - 594, 600 - 265, 300 - 251 appear at d0 (new players saw them in session 1). | orders_table.txt rows 16 d0; shots/s16_d0_r1_c_play.png; players.json | Gate the across-zero branch to the last rung (d3), after 3-digit regrouping; d0 = 2-digit no regroup, d1 = 2-digit regroup, d2 = 3-digit one regroup, d3 = across zeros. (`mq_gen.c:510 (the !big && R(q,0,2)==0 branch)`) |
| L3 | high | ladder | Peekaboo | 30 Equivalent fractions | d0 | S_EQUIVMUL ignores difficulty: d0 gives 5/6 = ?/30 and 4/5 = ?/25 (x5) as readily as 1/2 = ?/4. | orders_table.txt rows 30; shots/s30_d0_r1_c_play.png | d0: halves/quarters x2; d1: x2..x3 with den <= 6; d2: x2..x5; d3: find the multiplier from the other side (?/4 = 6/8). (`mq_gen.c:761`) |
| L4 | high | ladder | Treasure Heist, Bubble Pop | 22 Multiply bigger; 23 Divide parcels; 21 Big add and subtract; 66 Mega multiply; 74 Two-digit sharing | d0-d2 | Jumps too big: Multiply bigger goes 2-digit x 1 (d0) -> 3-digit x 1 (d1) -> 4-digit x 1 (d2, 5 x 8999) -> 2 x 2 (d3); Divide parcels 45/9 (d0) -> 253/3 (d1) -> 7313/9 = 812 R 5 (d2). Mega multiply starts at 288 x 34 at d0. No column ('like on paper') workspace exists for any multi-digit item: the answer is dialled into wheels or typed, with the work in her head. | orders_table.txt rows 21-23, 66, 74; shots/s23_d0_r1_c_play.png, s66_d0_r1_c_play.png | Rungs in AUDIT.md; add a Column workspace widget (digits in place-value columns, carry/borrow row, one column at a time, right to left, partial products / long-division bring-down) used by the Heist and Cake Kitchen for heavy orders. (`mq_gen.c:622, 643, 496, 1555, 1736`) |
| X1 | high | text | Peekaboo | 6 Fact garden | all | A new player's facts are x0/x1 at every difficulty, told as 'Pip has 0 plates with 1 strawberries on each!' / 'Pip shares 0 strawberries into 1 equal bags!' -- nonsense stories, wrong plurals, no question; after a miss: 'Hmm, that's only a few! Every group counts.' for 0 x 1. | shots/s06_d0_r1_c_play.png; orders_table.txt rows 6 | Do not introduce x0/x1 as stories (state them as rules once); start fluency at x2/x5/x10; fix plurals and add the question ('How many strawberries?'). (`mq_play.c:555-562 retell(); mq_facts.c mq_fact_next()`) |
| X2 | high | text | Bubble Pop, Gate Dash | 9 Times as many; 18 Big number places; 13 (stamp worth) | all | Cryptic, adult-telegraphic wording below grade-3 reading level: '36 seeds is 6 times the pot's. The pot?', '600 stamps is 10 sheets. One sheet?', 'How much is the stamp 9 worth in 695?'. The inverse times-as-many (division) is mixed into d0. | shots/s09_d1_r1_c_play.png, sheet_ov_d0_c_play_12.png (s18) | 'The big pot has 36 seeds. That is 6 times as many as the small pot. How many seeds in the small pot?'; 'What is the 9 worth in 695?' with the digit highlighted; inverse comparison only from d2. (`mq_gen.c:317, 450, 571`) |
| P2 | high | picture | Bubble Pop | 60 Mirror lines | d0 | 'How many lines of symmetry does this square have?' -- the shape drawn is a tall rectangle (about 105 x 125 px), whose answer would be 2, not 4. | shots/s60_d0_r1_c_play.png | Draw squares with equal sides in logical px (and test aspect ratio of every V_SHAPE). (`mq_vis.c v_shape()`) |
| G2 | high | give-away/grading | Hop Along | 15 Round to 10 and 100; 20 Round big numbers | all | The frog starts on the line's left end, which IS the answer whenever the number rounds down (half the items): tapping 'land!' without moving is graded right. The right end of the line has no label, so 'the nearer end' is not shown. | shots/s15_d0_r1_c_play.png ('281' with the frog on 280), mac_hop_start.png | Start the frog at the number itself (the parcel's spot) and require a hop to an END; label both ends; count a 'land!' with zero moves as no answer. (`mq_play.c ME_JUMP setup (g->aim initial), mq_gen.c:475 vis v[0]/v[1]`) |
| B1 | high | intention | Build It | 48 Count squares; 49 Area by multiplying; 54 Area & perimeter rules | all | Three different skills are the same task: 'A rug of 8 squares, 2 tall! Drag the corner' (a division/factor task). Count squares (3.MD.5/6) never asks her to count unit squares; the art studio's decorative paint dots are drawn inside the dotted build grid and read as squares/flowers. | shots/s48_d0_r1_c_play.png, sheet_ov_d0_c_play_48.png | Count squares: show a shape on the grid and ask how many squares (tap to count, each tapped square marks); Area by multiplying: 'w by h rug: how many squares?' with the sides labelled; keep the build-a-rug for 'find the missing side'. No decoration inside the field. (`mq_gen.c:1137-1150; mq_play.c Build It retell; art studio background`) |
| W1 | medium | school words | Sort Slide, Bubble Pop, feedback | 11 Factor friends; 58 Lines and rays; 59 Sort shapes; 36 Decimals; misconception lines | all | School words reach the child: 'prime', 'composite', 'A factor was missed?', 'Borrowing across a zero?', 'one hundredth', 'parallel', 'segment', 'A digit slid to another place?', 'Converted the wrong way?'. | shots/s11_d0_r1_c_play.png, sheet_ov_d0_d_dud_36.png | Game words: 'one long row only' / 'makes rows' bins with pictures; 'a ten from the next door'; keep vocabulary skills (lines/rays) but introduce the word with a picture once, then use it. (`mq_gen.c:356; mq_curric.c mq_mc_name/mq_mc_help`) |
| F1 | medium | feedback | Treasure Heist, Sort Slide | 43 Measuring stories; 41 Grams and liters; 7; 10 | d0 | Wrong-answer lines do not match the story: 'Hmm, that's only a few! Every group counts.' after 2 x 353 ml; 'Converted the wrong way?' after picking 5 g for a puppy; the dud's value is not shown in the Heist (wheels back to 0000). | shots/s43_d0_r1_d_dud.png, s41_d0_r1_d_dud.png | Classify per story type; show 'You tried 696' next to the friend's why line. (`mq_story.c:602 table; mq_classify()`) |
| P3 | medium | picture | Cloud Catch | 27 Wholes as fractions | all | '3 whole cakes, cut in ones: 3 = ?/1' is drawn as three circles each cut in half; the circles also count the answer (3). | shots/s27_d0_r1_c_play.png | Draw uncut wholes labelled '1 piece each'; vary the picture so the count is not the answer. (`mq_gen.c:732 vis`) |
| P4 | medium | picture/control match | Sort Slide | 14 Bigger parcel; 28 Compare slices; 37 Compare decimals; 68 Thousandths | all | The question asks 'Which slice is bigger?' / 'Which way does the scale tip?' / 'Which cookie jar is fuller?' but the bins are the school symbols < = >; Compare decimals draws one hundred-grid only, no jars. | shots/s37_d0_r1_c_play.png, s14_d0_r1_c_play.png | Bins that answer the question: 'left is heavier' / 'same' / 'right is heavier' with a tipping-scale icon; draw both quantities side by side. (`mq_gen.c cmp_choices(); Sort Slide card drawing`) |
| P5 | medium | clutter | Peekaboo | all Peekaboo orders | all | Each hole shows a faint keypad digit (7 8 9 / 4 5 6 ...) beside the answer numbers on the trays: extra numbers that look like answers; empty holes look like missing options. | shots/s03_d0_r1_c_play.png | Show key hints only on the calculator when a key is pressed, or as small key-cap icons outside the tray; hide empty holes. (`mq_play.c Peekaboo draw`) |
| K1 | medium | locks | finale intro | boss | - | Leftover gate text: the finale intro says 'Win it to open the next place!' although nothing is locked. | mq_main.c:1017 | Replace with 'Win it for a big party!' (a reward, not a gate). (`mq_main.c:1017`) |
| S1 | medium | pacing | all | speed controller | - | With accuracy below 70% the speed level collapses to 1-7 (of 100) within two sessions and stays there; the game is effectively frozen for a struggling child, while the maths stays too hard (D2). | players.json speed field | Floor the speed at the comfortable cruise's 40%; fix the maths first (D2). (`mq_speed_event()`) |
| H1 | medium | help | all | helper friend | - | The helper friend's panel is a large empty cream rectangle with three stars for ~1 s ('I'll help!') before the worked example: on a 320 x 240 screen it reads as a blank/broken screen. | shots/s00_d0_r1_e_dud2.png | Start the worked example immediately behind the friend's entrance. (`mq_guide.c`) |
| X3 | medium | text | Gate Dash, Kart Race, Hop Along, Peekaboo | 13, 42, 27, 33, 6 | all | Grammar: '1 ones', '1 hours = 60 minutes', '1 strawberries', 'Momo baked 1 whole pies', 'Momo has 1 whole pies and 1/3 more'. | orders_table.txt | Pluralise with the story engine's plural helper everywhere. (`mq_gen.c:443, 1003, 721, 822`) |
| X4 | medium | text | Peekaboo | 3 Mystery number; 4 Flip and split; 50 Split the rectangle | all | Bare equations as the task ('The wizard's secret number: 8 / ? = 4', 'Split the 8 by 7 mural: 7 x 8 = 7 x 3 + 7 x ? Two pieces: 21 + 7 x ?') with no picture of the mural/bed; the unknown divisor is the hardest form and appears at d0. | shots/s03_d0_r1_c_play.png, s50_d0_r1_c_play.png | Draw the array split by a line with both parts labelled; d0 unknown product factor (3 x ? = 12), d1 unknown dividend, d2 unknown divisor. (`mq_gen.c:212, 234, 1155`) |
| C3 | medium | controls | Clock Tower | 38, 39, 40 | all | Hands: the minute hand is grabbed only beyond 36 px from the centre, the hour hand inside: grabbing the short hand near its tip moves the minute hand. No tap-on-a-number to point a hand, no flick; the 'Turn the hands!' banner covers 5-7. | mq_play.c:2476-2495; shots/sheet_ov_d0_c_play_36.png (s38) | Grab the nearest hand by angle distance; a tap on a numeral points the selected hand; banner off the face. (`mq_play.c ME_CLOCK`) |
| C4 | medium | controls | Sky Fling | 55, 56, 57 | all | The protractor's double scale at 320 x 240 is unreadable (inner and outer numbers 8 px apart); aim is drag only in the field, keys step 5 degrees, no fine adjust by drag-near-the-rim, no readout of the current angle except the '0' pill. | shots/s56_d0_r1_c_play.png, s55_d0_r1_c_play.png | Single scale per order (the one starting at her side), a live angle readout on the ray, LEFT/RIGHT 1 degree, hold to accelerate. (`mq_play.c ME_LAUNCH; mq_vis.c v_angle`) |
| C5 | medium | controls | all | Mac page | - | Mac: plain scroll wheel/trackpad scroll does nothing anywhere in the games (only ctrl+wheel = pinch); no keyboard focus ring is shown for Sort/Build/Dig; 'r' for a remainder is not mapped (KEYMAP has no R) so Cake Kitchen remainders need the on-screen tile. | web/page/sparkle_page.js:57 KEYMAP, :670 | Forward wheel as a 'scroll' event the module can use for wheels, sliders, the number line, the clock; map r/R; draw focus rings. (`sparkle_page.js`) |
| L5 | medium | ladder | all | all skills | rung change | No explanation when the difficulty steps up (the only worked example is the helper after misses). A child meets regrouping, remainders, zeros or unlike denominators for the first time with no 'new trick' moment. | mq_main.c:550 (diff++ silently) | On the first order of a new rung, the guide shows a one-screen worked example of exactly the new complication (reuse mq_steps.c), then her order. (`mq_main.c, mq_coach.c, mq_steps.c`) |
| C6 | medium | clear what to do | Treasure Heist | all Heist orders | all | The goal reads 'Find the clues, crack it!' but the task is to dial the answer into the wheels; the 'clues' are hidden spots (picture, barrel, jar) whose purpose is never shown; nothing says the wheels are the answer (no 'type the answer' cue, faint 1/9 ghosts only). | shots/mac_heist_wheels.png, s16_d0_r1_c_play.png | Goal: 'Dial the answer, then open!'; arrows on each wheel; the clue spots either removed or labelled ('tap for a hint'). (`mq_play.c goal[] ME_VAULT, spot[]`) |
| Z1 | low | clutter | Kart Race | 17, 42, 71, 83 | all | The '4th' place pill sits on the order board's row and the top pad is half under the board. | sheet_ov_d0_c_play_12.png (s17) | Move the place pill into the HUD line. (`mq_play.c:2724`) |
| Z2 | low | text | Sort Slide | 81 Scaling | all | The card shows only the third line 'or the same number of rabbits?' (the first two lines are in the board's caption). | sheet_ov_d0_c_play_72.png (s81) | Show the whole question on the card. (`Sort Slide card`) |
| Z3 | low | picture | Bubble Pop | 36 Decimals | d0 | '0.94 = ?/100 (Each square is one hundredth.)' with the 94 squares shaded: the model is the answer (VR_MAY) -- fine for the first rung, but it stays at every difficulty. | shots/sheet_ov_d0_c_play_36.png | From d1 show the decimal only. (`mq_gen.c:863`) |

## Systemic issues

1. **No difficulty floor for a new child.** Default start grade 4, placement probes at difficulty 1, one answer per grade moves a strand, grade 5 reachable inside the placement, assumed skills re-checked at difficulty 2 (D1, D2).
2. **The success-rate controller is per skill and only adjusts 0..3 inside a skill.** Nothing lowers the grade of a strand when she is failing; the speed controller freezes the game instead (S1).
3. **Generators are ranges, not rungs.** One complication is not introduced at a time; 33 skills are flat; floors do not rise; no 'new step' explanation; no column workspace (L1-L5; ladder table below).
4. **The board's caption replaces the text.** Any skill whose caption lacks numbers or the question loses its task; the full story is only behind an undiscoverable tap on the board (T1, T2). A single rule (caption must contain every number of the story and the question with its unit, else show the text) plus a test would fix the class.
5. **Overlays drawn over the field.** Goal banner, guide corner, flying coins, place pill: drawn after the game objects, covering them (O1, Z1).
6. **Models chosen without checking they fit.** Base-ten blocks clipped, one of two compared numbers drawn, the Heist chosen for orders whose model it cannot draw (P1, T1).
7. **Choice sets not checked for mathematical uniqueness.** Shape families (G1); the same check is needed for 'which family', 'lines and rays' (a segment is part of a line) and the triangle families.
8. **Controls are hand-written per game.** Tap here, drag there, flick in two games, wheels tap-only; tiny keypads; no scroll wheel on the Mac; no common feedback or focus (C1-C6). Structural fix: the widget library below.

## Per-game rubric scores (1 = poor, 5 = good)

Columns: task obvious to a 9-year-old / picture matches numbers and operation / uncluttered at 320x240 / no give-away / text states the unknown and unit / reading level ~grade 3 / difficulty sensible for the level / right-wrong reaction clear.

| game | obvious | picture | uncluttered | no give-away | unknown stated | reading | difficulty | reaction | note |
|---|---|---|---|---|---|---|---|---|---|
| Bubble Pop | 3 | 3 | 3 | 4 | 2 | 3 | 2 | 4 | numbers missing for caption-only orders (T1); bubbles under guide |
| Cloud Catch | 3 | 3 | 4 | 4 | 2 | 3 | 2 | 4 | grade-5 stars from session 1 |
| Gate Dash | 3 | 2 | 2 | 4 | 3 | 3 | 3 | 3 | base-ten blocks wrong; coins over labels |
| Kart Race | 3 | 3 | 3 | 4 | 3 | 3 | 2 | 3 | unit sizes x1000 at d0 |
| Peekaboo | 3 | 3 | 2 | 4 | 3 | 2 | 2 | 3 | bare equations; key digits |
| Sort Slide | 2 | 2 | 2 | 4 | 3 | 3 | 3 | 3 | symbol bins; one model; grading bug (shapes); labels covered |
| Hop Along | 4 | 3 | 4 | 2 | 4 | 4 | 4 | 4 | start = answer |
| Sky Fling | 3 | 3 | 2 | 4 | 3 | 3 | 3 | 3 | unreadable protractor |
| Pie Shop | 5 | 5 | 4 | 4 | 5 | 4 | 4 | 4 | good |
| Build It | 3 | 3 | 3 | 4 | 3 | 4 | 3 | 4 | same template for 3 skills; decor dots |
| Clock Tower | 4 | 4 | 3 | 4 | 4 | 4 | 4 | 4 | good; hand grab |
| Shop Till / Pet Shop | 3 | 4 | 2 | 4 | 2 | 4 | 3 | 3 | coins hidden by banner; 'surprise gift' caption |
| Treasure Dig | 4 | 4 | 4 | 4 | 4 | 4 | 4 | 4 | small cells |
| Treasure Heist | 2 | 2 | 4 | 4 | 2 | 3 | 1 | 2 | no model drawn; goal unclear; tiny wheels; carries the hardest orders |
| Cake Kitchen | 3 | 3 | 3 | 4 | 3 | 3 | 2 | 3 | tiny pad |

## Placement and the first three sessions (simulated players)

Profiles: *facts strong / fractions weak*, *typical grade 3*, *struggling*; the chance of a right first answer is set by the skill's grade and world (e.g. typical grade 3: grade 2 90 %, grade 3 75 %, grade 4 35 %, grade 5 10 %), minus 7 points a difficulty step. Default settings (no starting grade set). Each session: four rounds at different places.

| profile | seed | session | orders | first-try right | grade-4 orders | grade-5 orders | decimals | numbers >= 1000 | strands after placement |
|---|---|---|---|---|---|---|---|---|---|
| facts_strong_fractions_weak | 1 | 1 | 20 | 60% | 8 | 3 | 2 | 4 | 4 3 2 4 |
| facts_strong_fractions_weak | 1 | 2 | 24 | 67% | 13 | 1 | 4 | 1 |  |
| facts_strong_fractions_weak | 1 | 3 | 23 | 35% | 11 | 2 | 0 | 2 |  |
| facts_strong_fractions_weak | 2 | 1 | 20 | 60% | 6 | 3 | 2 | 2 | 3 3 4 4 |
| facts_strong_fractions_weak | 2 | 2 | 24 | 67% | 5 | 2 | 1 | 1 |  |
| facts_strong_fractions_weak | 2 | 3 | 23 | 57% | 1 | 5 | 1 | 0 |  |
| typical_g3 | 1 | 1 | 24 | 58% | 10 | 3 | 1 | 6 | 4 3 3 4 |
| typical_g3 | 1 | 2 | 24 | 62% | 13 | 0 | 0 | 2 |  |
| typical_g3 | 1 | 3 | 23 | 48% | 6 | 3 | 1 | 2 |  |
| typical_g3 | 2 | 1 | 21 | 57% | 6 | 3 | 1 | 1 | 2 3 4 4 |
| typical_g3 | 2 | 2 | 24 | 46% | 7 | 1 | 2 | 1 |  |
| typical_g3 | 2 | 3 | 23 | 61% | 4 | 0 | 0 | 0 |  |
| struggling | 1 | 1 | 21 | 38% | 7 | 1 | 0 | 2 | 4 3 2 2 |
| struggling | 1 | 2 | 24 | 54% | 3 | 0 | 3 | 1 |  |
| struggling | 1 | 3 | 23 | 30% | 5 | 1 | 0 | 4 |  |
| struggling | 2 | 1 | 21 | 38% | 6 | 3 | 2 | 1 | 2 2 4 4 |
| struggling | 2 | 2 | 24 | 42% | 4 | 2 | 2 | 1 |  |
| struggling | 2 | 3 | 23 | 26% | 6 | 3 | 1 | 3 |  |

The first orders were the same for every profile (the probe order is fixed): `48 seeds is 6 times the pot's. The pot?` (gr 4), `4717 + 2057` (gr 4), `1/6 = ?/12` (gr 4), `8 km = ? m` (gr 4), then after one right answer `1071 stars go into 17 equal clouds` (gr 5). The struggling child's one lucky guess on order 1 placed multiplication at grade 4, after which `6 buses with 86 friends`, `4 x 33` and `Times as many` stayed in her mix at 30-40 % accuracy. The controller moved difficulty only inside skills (mostly staying at 0); the speed level fell to 1-7 of 100.

## Difficulty ladders, skill by skill

Current = what the generator does (observed over 12 samples per skill and the code); 'flat' = difficulty 2 repeats difficulty 0 exactly. Proposed rungs: one new complication per rung (R0 is the first thing a new child sees); every rung gets a one-screen worked example the first time (the guide, `mq_steps.c`), and every multi-digit rung uses the column workspace widget.

| id | skill | gr | current rungs | gaps / too-big jumps / missing | proposed rungs |
|---|---|---|---|---|---|
| 0 | Equal groups | 3 | a x b, a 2..3+d, b 2..4+2d (d0 2x4 .. d3 up to 6x10; floor stays 2x2) | widening only; d3 can be easier than d2; tag gives '2 groups, 4 each' | R0 2-5 groups of 2/5/10 (pictured) | R1 groups of 3/4 | R2 up to 5x9 | R3 6-9 groups of 6-9 | R4 10s |
| 1 | Sharing seeds | 3 | a*b in a rows, Build It; a 2..3+d | same widening; d3 6/2 seen | R0 share <=10 into 2 | R1 into 2/5/10 | R2 into 3/4 | R3 <=50 | R4 <=100 (one new divisor family per rung) |
| 2 | Garden stories | 3 | x and / stories within 100, mixed | mixes x and / and quotative/partitive at every rung | R0 x story, small | R1 / sharing story | R2 / how-many-groups story | R3 mixed, <=100 | R4 with distractor number |
| 3 | Mystery number | 3 | unknown in a x / equation; '8 / ? = 4' at d0 | unknown divisor (hardest form) at d0; bare equation | R0 3 x ? = 12 (picture) | R1 ? x 4 = 20 | R2 ? / 3 = 5 | R3 24 / ? = 6 | R4 any, <=100 |
| 4 | Flip and split | 3 | turn-around / split / associative; flat | flat; equations only; no picture of the bed | R0 turn-around with array | R1 split off 5 (7x6 = 5x6 + 2x6) with array | R2 split off 2 | R3 associative x2 | R4 free split |
| 5 | Divide by thinking x | 3 | Build It rows; '54/9 (Psst: 9 x what?)' | ok sizes; hint text in brackets is wordy | R0 /2 /5 /10 | R1 /3 /4 | R2 /6 /7 | R3 /8 /9 | worked 'think x' example at R1 |
| 6 | Fact garden | 3 | x0/x1 only for a new player at every d; flat | nonsense stories '0 plates with 1 strawberries' | start x2,x5,x10 -> x3,x4 -> x6..x9; x0/x1 as a one-off rule card |
| 7 | Two-step stories | 3 | groups then take/add; numbers small | both steps new at once; caption hides the question | R0 add then add | R1 groups then add | R2 groups then take | R3 share then add | R4 any, check by estimate |
| 8 | Number patterns | 3 | add-k sequences; odd/even rule | odd/even with a hint that does not apply ('Even times anything is even' for 9x3) | R0 +2/+5/+10 | R1 +3/+4 | R2 +6..+9 | R3 odd/even sums | R4 odd/even products |
| 9 | Times as many | 4 | product and inverse at every d | inverse (division) at d0; cryptic text | R0 '3 times as many' with bars, small | R1 bigger | R2 inverse 'is 3 times' with bars | R3 compare words |
| 10 | Leftover stories | 4 | remainder stories, 3 interpretations at every d | all three remainder meanings from d0; caption ambiguous | R0 'how many left over' | R1 'how many full' | R2 'how many needed (round up)' | R3 mixed | worked example for each meaning |
| 11 | Factor friends | 4 | prime/composite, missing factor, multiples; flat | flat; school words; 108 = 9 x ? at d0 | R0 factor pairs of <=24 with Build It | R1 <=50 | R2 multiples | R3 'one long row only' (prime) <=50 | R4 <=100 |
| 12 | Pattern rules | 4 | double / add k, 'which number does the 6th frog wear' | nth term at d0 | R0 continue +k | R1 continue doubling | R2 the 6th term | R3 rule from terms |
| 13 | Hundreds tens ones | 2 | HTO to number, expanded, digit value; flat | flat; blocks drawn do not match | R0 2-digit tens/ones | R1 3-digit no zeros | R2 with a zero (506) | R3 digit value | R4 expanded with zero |
| 14 | Bigger parcel | 2 | compare 3-digit; flat | flat; one model only; symbols | R0 different hundreds | R1 same hundreds | R2 same H,T | R3 equal/zeros |
| 15 | Round to 10 and 100 | 3 | d0-1 nearest 10, d2-3 nearest 100 | ok steps; give-away start position | R0 2-digit to 10 | R1 3-digit to 10 | R2 to 100 | R3 the 5 rule and 95->100 |
| 16 | Add and subtract | 3 | d0 2-digit; across-zero branch at any d | 900-594 at d0; no column workspace | R0 2-digit no regroup | R1 2-digit regroup | R2 3-digit one regroup | R3 3-digit two regroups | R4 across zeros; column workspace from R1 |
| 17 | Times tens | 3 | a x b0; flat | flat | R0 x10 | R1 2-5 x tens | R2 6-9 x tens | R3 answers >= 100 with 0 in the middle (5x80) |
| 18 | Big number places | 4 | 5-6 digit place value, 'x10 sheets' | 6 digits at d2 with no 4-digit rung | R0 4-digit | R1 5-digit | R2 x10 relation | R3 6-digit |
| 19 | Compare big numbers | 4 | compare/glue 4-5 digit; flat | flat | R0 4-digit different thousands | R1 same thousands | R2 5-digit | R3 6-digit with zeros |
| 20 | Round big numbers | 4 | nearest 10..10000 in 5-digit numbers | nearest 10000 at d2 straight after 1000 | R0 4-digit to 100 | R1 to 1000 | R2 5-digit to 1000 | R3 to 10000 |
| 21 | Big add and subtract | 4 | d0-1 4-digit, d2-3 5-digit, zeros | 4-digit with regrouping at d0; no workspace | R0 4-digit no regroup | R1 one regroup | R2 several | R3 zeros in the middle | R4 5-digit; column workspace |
| 22 | Multiply bigger | 4 | 2x1 -> 3x1 -> 4x1 -> 2x2 | 3 rungs each add a digit AND regrouping; 2x2 last but different method | R0 2x1 no regroup (23x3) | R1 2x1 regroup | R2 3x1 | R3 4x1 | R4 2x2 with area model; workspace |
| 23 | Divide parcels | 4 | q 3-12 -> 12-99 -> 100-999 -> 100-1100 | d2 jumps to 4-digit dividends; remainders from d1 | R0 2-digit / 1 exact | R1 with remainder | R2 3-digit / 1 exact | R3 with remainder | R4 4-digit; long-division workspace |
| 24 | Name the slice | 3 | shaded pie a/b | ok | R0 halves/quarters | R1 thirds/sixths | R2 eighths | R3 bars and sets |
| 25 | Fractions on a line | 3 | 0..1 then 0..2 lines | 12/8 at d2 | R0 halves/quarters 0..1 | R1 thirds/sixths | R2 eighths | R3 0..2 beyond 1 |
| 26 | Same amount | 3 | a/b = ?/2b | ok, small | R0 1/2 = ?/4 with bars | R1 x2 any | R2 x3 | R3 find the den |
| 27 | Wholes as fractions | 3 | k/1, n/n, k*n/n; flat | flat; picture mismatch | R0 n/n = 1 | R1 3 = 3/1 | R2 12/4 = 3 | R3 mixed |
| 28 | Compare slices | 3 | same num or same den; flat | flat; symbols | R0 same den | R1 same num unit fractions | R2 same num | R3 mixed |
| 29 | Cut fair shares | 3 | frost a/b of a pie | ok | R0 halves/quarters | R1 thirds/sixths | R2 eighths | R3 a/b > 1 slice counts |
| 30 | Equivalent fractions | 4 | n/d = ?/(k d), k 2..5; ignores d | 5/6 = ?/30 at d0 | R0 x2 halves/quarters | R1 x2..3 | R2 x2..5 | R3 missing den | R4 simplify |
| 31 | Compare any slices | 4 | fixed pair table | numbers not shown on screen (T1) | R0 vs 1/2 benchmark | R1 same num | R2 one den multiple of other | R3 cross |
| 32 | Add and take slices | 4 | like dens, sums >1 at d0 | addends not shown (T1); 11/8 at d0 | R0 sum <= 1 | R1 take away | R2 sum > 1 | R3 tenths/twelfths |
| 33 | Mixed numbers | 4 | both directions; flat | flat | R0 1 and a/b -> improper | R1 improper -> 1 and | R2 2-3 wholes | R3 add mixed |
| 34 | Fraction x whole | 4 | k x n/d; flat | flat | R0 unit fraction x k | R1 non-unit x 2-3 | R2 x 4-5 | R3 as a mixed number |
| 35 | Tenths, hundredths | 4 | a/10 = ?/100; add tenths+hundredths | ok, d1 adds the sum | R0 a/10 = ?/100 | R1 a/100 + b/10 | R2 any order |
| 36 | Decimals | 4 | 0.ab = ?/100 with grid | model gives answer at all d | R0 tenths with grid | R1 hundredths with grid | R2 no grid | R3 money form |
| 37 | Compare decimals | 4 | 0.a vs 0.bc | one grid only; symbols | R0 tenths vs tenths | R1 0.5 vs 0.49 with grids | R2 no grids | R3 trailing zeros |
| 38 | Time to 5 minutes | 2 | flat | flat | R0 o'clock / half past | R1 quarter | R2 5-minute | R3 :55 / :05 tricky |
| 39 | Time to the minute | 3 | flat | flat | R0 :01-:09 near o'clock | R1 any minute first half | R2 any | R3 times near the hour change |
| 40 | How long? | 3 | within hour then across hours, 105 min at d2 | elapsed across 2 hours at d2 with 'how many minutes' | R0 whole hours | R1 within an hour | R2 across one hour | R3 end time | R4 >60 min |
| 41 | Grams and liters | 3 | estimate / x / - stories; flat | flat | R0 estimate | R1 + - | R2 x | R3 / |
| 42 | Unit sizes | 4 | x60, x1000, x16, x100 from d0 | x1000 and x16 at d0 | R0 m->cm, h->min small | R1 kg->g, l->ml | R2 lb->oz | R3 two-step |
| 43 | Measuring stories | 4 | 3-digit ml/m; flat | flat; caption has no question | R0 add same unit | R1 subtract | R2 x | R3 mixed units (m and cm) |
| 44 | Count coins | 2 | coins up to $3.87 at d3; flat | flat; coins hidden by the banner | R0 dimes+pennies | R1 +nickels | R2 +quarters | R3 with a dollar |
| 45 | Making change | 4 | from $1, $5, $10 | ok steps | R0 from $1, 5c multiples | R1 any cents from $1 | R2 from $5 | R3 from $10 |
| 46 | Shop stories | 3 | groups then take; change | two-step from d0 | R0 one step | R1 groups then take | R2 change from $1 |
| 47 | Money decimals | 4 | cents <-> $0.xx | ok | R0 $0.ab -> cents | R1 cents -> $ | R2 under 10c (0.06) | R3 over $1 |
| 48 | Count squares | 3 | Build It rug of N squares, h tall | not counting at all (B1) | R0 count a 2x3 shape | R1 L shapes | R2 half-covered (finish the rows) | R3 compare two shapes |
| 49 | Area by multiplying | 3 | same rug template | same task as 48 | R0 w x h labelled sides | R1 one side missing | R2 bigger |
| 50 | Split the rectangle | 3 | distributive equations; flat | no picture; flat | R0 split drawn, both parts labelled | R1 she chooses the split | R2 7 x 14 as 7x10 + 7x4 |
| 51 | L-shaped rooms | 3 | L area; shape not drawn in Heist | T1 | R0 L drawn on grid, countable | R1 sides labelled | R2 subtraction method |
| 52 | Around the edge | 3 | fence of P, depth h -> width | inverse at d0 | R0 perimeter of drawn rectangle | R1 missing side | R2 same perimeter different area |
| 53 | Shape families | 3 | square/rect/rhombus...; grading bug | G1 | R0 triangle/square/circle-not | R1 rectangles | R2 rhombus | R3 'is a square a rectangle?' |
| 54 | Area & perimeter rules | 4 | flat | flat | R0 area by formula | R1 perimeter by formula | R2 missing side | R3 mixed |
| 55 | Angles in degrees | 4 | 'made of 137 one-degree angles'; flat | flat; abstract | R0 quarter/half turn | R1 tens of degrees | R2 any |
| 56 | Protractor | 4 | 10s then 5s | double scale unreadable | R0 0-90 by 10s | R1 0-180 by 10s | R2 by 5s | R3 inner scale |
| 57 | Adding angles | 4 | right angle split, sum | ok | R0 sum of 2 tens | R1 right-angle complement | R2 straight-angle complement |
| 58 | Lines and rays | 4 | segment/ray/line/parallel | vocabulary only | R0 line vs segment | R1 ray | R2 parallel/perpendicular |
| 59 | Sort shapes | 4 | parallel pairs, triangle families | - | R0 right triangle | R1 acute/obtuse | R2 parallel sides |
| 60 | Mirror lines | 4 | count lines; yes/no | square drawn as rectangle (P2) | R0 yes/no with fold animation | R1 count for rectangle | R2 square | R3 letters |
| 61 | Picture graphs | 2 | flat | flat | R0 read one bar | R1 total of two | R2 how many more |
| 62 | Scaled graphs | 3 | key 2 / 5 / 10 | key hidden by banner | R0 key 2, read | R1 key 2, compare | R2 key 5 | R3 key 10, half symbols |
| 63 | Measure and plot | 3 | ruler to 1/2 then 1/4 | ok | R0 whole inches | R1 halves | R2 quarters |
| 64 | Fraction line plots | 4 | flat; plot not drawn in Heist | T1 | R0 count X at a value | R1 how many more | R2 total length |
| 65 | Read a table | 4 | 2-digit then up to 99 | ok | R0 read | R1 compare | R2 total |
| 66 | Mega multiply | 5 | 3x2 at d0; flat | flat; 288 x 34 first | R0 2x2 | R1 3x2 | R2 4x2; workspace |
| 67 | Unlike slices | 5 | 1/2 + 4/10 etc; flat | flat | R0 one den multiple of the other, sum < 1 | R1 sum > 1 | R2 unrelated dens |
| 68 | Thousandths | 5 | flat | flat | R0 read 0.abc | R1 compare | R2 expanded |
| 69 | Box volume | 5 | l x w x h small | no picture | R0 count cubes in a drawn box | R1 l x w x h | R2 two boxes |
| 70 | Treasure map | 5 | (x, y); flat | flat | R0 x axis only | R1 small grid | R2 full grid |
| 71 | Powers of ten | 5 | x10/x100 of tenths | decimal answers '66.0' formatting | R0 whole x10 | R1 tenths x10 | R2 x100 | R3 divide |
| 72 | Round decimals | 5 | flat | flat | R0 to whole | R1 to tenth | R2 to hundredth |
| 73 | Decimal sums | 5 | flat | flat; no question in caption | R0 tenths + tenths | R1 x whole | R2 hundredths | R3 / |
| 74 | Two-digit sharing | 5 | 442/13 at d0 (and in the placement) | too big a first rung | R0 /10 /20 /30 | R1 2-digit by 11-15 exact | R2 3-digit | R3 4-digit; workspace |
| 75 | Brackets first | 5 | flat | flat | R0 a x (b + c) small | R1 (a + b) x c | R2 two brackets |
| 76 | Expressions | 5 | how many times as big / which spell | expression not shown (T1) | R0 words -> expression | R1 compare without computing |
| 77 | Two patterns | 5 | flat | flat; cryptic caption | R0 table of both patterns | R1 next pair | R2 relation |
| 78 | Unlike stories | 5 | 1/2 + 1/3 at d0 | ok after 67 | R0 halves+quarters | R1 thirds+sixths | R2 unrelated |
| 79 | Fractions as sharing | 5 | flat | flat | R0 1 pizza / n | R1 a < n | R2 a > n |
| 80 | Fraction of a fraction | 5 | flat | flat | R0 1/2 of 1/2 with picture | R1 unit x unit | R2 non-unit |
| 81 | Scaling | 5 | flat | flat; card shows only last line | R0 x2 vs x1/2 | R1 x5/4 | R2 x3/4 |
| 82 | Dividing unit fractions | 5 | flat | flat; caption has no question | R0 2 cups / 1/2 | R1 3 / 1/3 | R2 1/3 / 2 |
| 83 | Converting units | 5 | 1.1 m -> cm at d0; flat | decimals first rung | R0 whole m -> cm | R1 cm -> m with decimal | R2 h min -> min |
| 84 | Fraction plots | 5 | pour fairly | wordy | R0 total of the plot | R1 share fairly |
| 85 | Shape families tree | 5 | riddles; flat | flat | R0 'every square is a rectangle' | R1 'every rectangle a square?' | R2 hierarchy |

Missing everywhere: (a) a worked example at each new rung (only the helper's example after two misses exists); (b) a column workspace for 16, 21, 22, 23, 43, 45, 66, 73, 74 (today: wheels or a keypad, the work in her head); (c) a rising floor (R(q, 2, 3 + d) keeps 2 x 2 at the top rung).

## Controls audit

Panel: 3.2" 320 x 240, about 4.9 logical px per mm, so 10-12 mm is 49-59 px; on the Mac page (1470 x 956 window) the 320 x 240 field is drawn about 4 CSS px per logical px, so 44 CSS px is only 11 logical px: every target is big enough on the Mac and the binding limit is the calculator. 'Mac scroll' = a plain wheel/trackpad scroll: the page forwards only ctrl+wheel (as a pinch), so it does nothing in every game.

| control | games | tap | drag | flick/swipe | calculator keys | Mac click/drag | Mac scroll | Mac pinch | Mac keyboard | hit target | drag feedback | demo | gaps | widget |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Answer bubbles | Bubble Pop | yes (on DOWN) | no (a sweep pops only the first) | no | arrows = focus ring, ENTER pops, digits type | click | no | - | arrows/Enter/digits | r 22 px = 9 mm: ok | focus ring | yes (a sweep -- but sweeping does not pop) | demo gesture not supported; bubbles under the guide/banner | Choice target |
| Cloud catcher | Cloud Catch | yes (moves cloud) | yes | no | LEFT/RIGHT 18 px, ENTER = under focused star, digits | click/drag | no | - | arrows/Enter | whole field: ok | cloud follows | yes | no flick to throw; scroll could steer | Steer |
| Lanes / gates / pads | Gate Dash, Kart Race | yes (lane, gate = go) | no | up/down = lane | UP/DOWN, RIGHT/ENTER go, digits pick lane | click | no | - | arrows/Enter/digits | 49 px lanes: ok | lane highlight | yes | drag between lanes missing | Steer (lanes) |
| Holes | Peekaboo | yes | no | no | keypad digit = hole, arrows, ENTER | click | no | - | digits/arrows | 60x48: ok | focus | yes | faint key digits look like answers | Choice target |
| Card into bins | Sort Slide | yes (bin) | yes (card) | yes (left/right/down) | LEFT/RIGHT, 1-4, ENTER/DOWN | click/drag | no | - | arrows/digits/Enter | bins 312/n x 40 px: ok but labels covered | card follows | yes | banner over bin labels | Draggable item + Drop target |
| Number line (hop) | Hop Along | yes (sets spot) | yes | no | LEFT/RIGHT, ENTER/UP lands | click/drag | no | - | arrows/Enter | ticks 272/n px (34 px at 8ths) | frog moves, value pill | yes | start = answer; ends unlabelled; no scroll | Number line |
| Aim (protractor / power) | Sky Fling | no (drag only) | yes | up = fling | arrows +-5 deg, digits type deg, ENTER | drag | no | - | arrows/digits/Enter | field | ray | yes | no 1-degree step; unreadable double scale | Dial (angle) |
| Pie slices | Pie Shop | yes (toggle) | yes (frost only, cannot un-frost by drag) | no | 1-9 toggle slice, DEL clears, ENTER | click/drag | no | - | digits/Del/Enter | slices of r 66: ok; 12ths small | slice colour | yes | slices 10-12 not on keys | Drop target (multi-select) |
| Bed / rug corner | Build It | yes (DOWN sets size) | yes | no | arrows +-1, ENTER | click/drag | no | - | arrows/Enter | plot | bed grows; no live count | yes | decor dots in grid; no size readout | Draggable handle |
| Clock hands | Clock Tower | no | yes (nearest by radius) | no | LEFT/RIGHT +-step, UP/DOWN +-1 h, digits type time | drag | no | - | arrows/digits/Enter | face r 76 ok; hour-hand zone r < 36 | hand moves | yes | grabs the wrong hand; banner covers 5-7 | Clock hand |
| Coins (drawer, tray) | Shop Till, Pet Shop | yes (drawer adds, tray row removes) | no ('Drop in the coins!' implies drag) | no | digits type amount, DEL removes, ENTER | click | no | - | digits/Del/Enter | drawer 48 px; tray rows 22 px (4.5 mm) | coin flies | yes | no drag; tray rows small | Draggable item + Drop target |
| Till keypad | Shop Till | yes | no | no | digits, DEL, ENTER | click | no | - | digits | 34x26 px (7x5 mm) | key flash | - | small | Keypad |
| Lock wheels | Treasure Heist | yes (top/bottom half) | no | no | LEFT/RIGHT select, UP/DOWN roll, digits shift in, . or R next group, DEL, ENTER | click | no | - | arrows/digits/./Enter (no R) | 20x32 px (4x6.5 mm) | digit changes | yes | no drag/flick/scroll, no arrows drawn, value lost after dud | Wheel |
| Cake pad / digit tiles | Cake Kitchen, made answers | yes | no | no | digits / . R, DEL, ENTER | click | no | - | digits/Del (no R) | 30x20 / 15x17 px (3-6 mm) | key flash | yes | far too small | Keypad |
| Map cell | Treasure Dig | yes | yes | no | arrows, ENTER | click/drag | no | - | arrows/Enter | 22x18 px (4.5x3.7 mm) | marker | yes | small cells, no magnifier | Grid picker |
| Go buttons (land!/set!/open!/ring!) | all calm games | yes | no | no | ENTER | click | no | - | Enter | ~70x30 px (6 mm high) | press | - | short; same spot as the field's bottom-right | Button |
| Merge board | Sparkle Kitchen | yes | yes | no | arrows, ENTER | click/drag | no | - | arrows/Enter | 33 px cells (6.7 mm) | item follows | ? | cells below 10 mm | Draggable item + Drop target |
| Room placement | Home | yes | yes | yes (rooms/pages) | arrows, ENTER, DEL | click/drag | no | pinch (squeeze) | arrows/Enter/Del | items | item follows | ? | ok | Draggable item |
| Rhythm pads / toys | Toy box | yes (pads on DOWN) | kaleidoscope | no | 1/3/7/9 pads, arrows, ENTER | click | no | - | digits | pads | pad flash | ? | ok | Button |
| Squishy | Fun beats | press | stretch | no | - | drag | no | pinch | - | big | deforms | text 'press, stretch or pinch it' | ok | - |

**Recommended standard input set per control type**

- *Choice target* (bubble, hole, gate, bin): tap; drag-and-release onto it; arrows move a visible focus ring, ENTER picks; digits type the value; Mac click, Tab/arrows, Enter. >= 48 px.
- *Wheel / dial* (lock digit, any spinner): tap the drawn up/down arrows or above/below the wheel; drag or flick with snap and inertia; Mac scroll wheel/trackpad; UP/DOWN; digits set the focused wheel and move right; LEFT/RIGHT change wheel; DEL clears.
- *Slider / number line*: tap a spot; drag the handle; flick nudges one tick; scroll = one tick; LEFT/RIGHT one tick (hold repeats, accelerates); both ends labelled; live value readout.
- *Draggable item + drop target* (card, coin, merge item, furniture): drag with the item under the finger and a lifted shadow; targets highlight on hover; tap item then tap target as the accessible alternative; arrows move focus, ENTER picks up / drops; DEL removes the last.
- *Clock hand*: grab the hand nearest by angle; tap a numeral to point the selected hand; flick turns; scroll = 1 or 5 min; LEFT/RIGHT minutes, UP/DOWN hours, digits type the time.
- *Keypad / digit tiles*: >= 48 x 48 px keys; physical digits, DEL, ENTER, '.', '/', R; Mac keyboard incl. r.
- *Steer / catcher* (cloud, lanes): drag; tap a side to step; flick to the far side; LEFT/RIGHT or UP/DOWN; scroll steers.
- *Button* (go buttons): >= 96 x 48 px, pressed state, ENTER.

## Widget library (structural fix)

One module-level library (`mq_widget.c`, used by `mq_play.c`, `mq_home.c`, `mq_merge.c`, `mq_toys.c`) so a convention is implemented once. Every widget: the full gesture set above, a hit area >= 48 x 48 logical px (asserted), pressed and drag feedback, keyboard focus (a ring drawn by the widget; the focus order is the order of creation), and its own first-time demo (a ghost hand path the widget knows, so the guide never hand-codes one).

```c
typedef enum { WG_BUTTON, WG_CHOICE, WG_WHEEL, WG_SLIDER, WG_NUMLINE, WG_DRAG, WG_DROP, WG_KEYPAD, WG_CLOCKHAND, WG_STEER,
               WG_GRIDPICK, WG_COLUMNS } mq_wkind_t;
typedef struct mq_widget {
    uint8_t kind, id, flags;          /* WF_FOCUS, WF_PRESSED, WF_DRAGGING, WF_DISABLED, WF_DEMO_SEEN */
    int16_t x, y, w, h;               /* drawn rect; hit rect = max(drawn, 48x48) centred */
    int32_t value, lo, hi, step;      /* wheel digit, slider value, clock minutes, keypad buffer length ... */
    void (*draw)(mq_t *, const struct mq_widget *);
    void (*changed)(mq_t *, struct mq_widget *, int why); /* WHY_TAP, WHY_DRAG, WHY_FLICK, WHY_KEY, WHY_SCROLL */
} mq_widget_t;
void mq_wg_begin(mq_t *q);                          /* a frame's widget set (max 24) */
mq_widget_t *mq_wg_add(mq_t *q, int kind, int x, int y, int w, int h);
bool mq_wg_touch(mq_t *q, int type, int x, int y);  /* TV_DOWN/DRAG/UP/TAP/PINCH: routed to the widget hit */
bool mq_wg_swipe(mq_t *q, int dir, int speed);      /* flick: wheels spin, sliders nudge, steer jumps */
bool mq_wg_scroll(mq_t *q, int dx, int dy, int x, int y); /* new modx event: Mac wheel/trackpad */
bool mq_wg_key(mq_t *q, int k, int c);              /* arrows, digits, ENTER, DEL, '.', '/', R */
void mq_wg_draw(mq_t *q);                           /* widgets + focus ring + pressed state */
void mq_wg_demo(mq_t *q, int id);                   /* the widget's own ghost-hand path */
```

| ad-hoc code today | replaced by |
|---|---|
| Bubble Pop DOWN hit test, focus ring (mq_play.c:2327, :1920) | WG_CHOICE per bubble |
| Peekaboo holes + hole_of_key (:2362, :1503) | WG_CHOICE with a key binding |
| Gate Dash / Kart lanes (:2352, :1952, swipe :2604) | WG_STEER (lanes) + WG_CHOICE gates |
| Cloud Catch px follow (:2348, :1916) | WG_STEER |
| Sort Slide card/bins/swipe (:2371, :1978, :2597) | WG_DRAG card + WG_DROP bins |
| Hop Along line + land! (:2384, :1995) | WG_NUMLINE + WG_BUTTON |
| Sky Fling aim (:2405, :2020) | WG_SLIDER (angle mode) + WG_BUTTON |
| Pie Shop slices (:2424, :2050) | WG_DROP multi-select (WG_CHOICE toggles) |
| Build It corner (:2442, :2068) | WG_DRAG handle (2-axis slider) |
| Clock Tower hands (:2469, :2084) | WG_CLOCKHAND x2 |
| Shop Till coins + till keys (:2499, :2113) | WG_DRAG coins -> WG_DROP tray; WG_KEYPAD |
| Treasure Heist wheels (:2546, :2158) | WG_WHEEL per digit (+ WG_COLUMNS for heavy orders) |
| Cake Kitchen pad, digit tiles (:2565, :2301, :2219) | WG_KEYPAD |
| Treasure Dig cells (:2536, :2143) | WG_GRIDPICK |
| go buttons land!/set!/open!/ring!/serve!/done! (each case's y > FB-36 && x > 250) | WG_BUTTON (one place, one size) |
| merge board (mq_merge.c), room placement (mq_home.c), toys pads (mq_toys.c) | WG_DRAG / WG_DROP / WG_BUTTON |

**Widget test harness** (`tests/unit/test_mq_widgets.c` + a UI script): for every widget kind, instantiate it in a blank scene and drive it with every gesture -- tap on the centre and on each edge of the hit rect, tap 1 px outside, DOWN-DRAG-UP along a path, a flick (TV_SWIPE at speed), a scroll of +-1 and +-5, a pinch, each key (arrows, digits, ENTER, DEL, '.', '/', R) -- and assert (1) the value changes as specified, (2) the `changed` callback's `why`, (3) the hit rect >= 48 x 48, (4) a pressed/drag state is drawn (pixel diff of the widget rect between idle and pressed), (5) focus moves with arrows and is drawn, (6) the demo path starts and ends inside the hit rect and ends on any input. Then every game is run with `mq mech` for each skill it fits and every widget in its frame is driven the same way (a generic 'gesture fuzz'); the right answer must be reachable by touch only, by keys only, and by Mac mouse + wheel only (web test).

**The calculator app** (`firmware/app`): drag-to-scroll is shared and consistent (`touch_scroll.c`: content follows the finger, one row per row height) -- a good model for the module. Gaps: no fling/inertia anywhere (a long list or the spreadsheet needs many drags); swipes are recognised by the core (`core/touch.c`) but used only by help and Math Quest, so menus, forms and tabs do not page with a swipe; graph sliders (`gr_slider.c`) are 18 px rows with a 7 x 13 px knob (about 3.7 x 2.7 mm: too small) and drag only; pinch is implemented in graph, 3-D and geometry but nowhere a scroll wheel equivalent on the web build (`calc_page.js` forwards none). Recommend the same widget set for the app (sliders >= 48 px tall, fling in `touch_scroll.c`, swipe-to-page in menus/forms, wheel->scroll on the web page).

## Representative screenshots

`shots/` holds 40 single frames (named `sSKILL_dDIFF_rSEED_<frame>.png`: a open, b stage, c play, d one miss, e two misses), the overview contact sheets (`sheet_ov_d0_c_play_*.png`: the play frame of every skill at difficulty 0; `sheet_ov_d3_*`: difficulty 3; `sheet_ov_d0_d_dud_*`: after a miss; `sheet_sk*.png`: one skill's 9 frames; `sheet_locks.png`: map, world, finale, arcade, sticker book) and the Mac page shots (`mac_*.png`). The full set (5,160 + 1,376 frames) was not committed.

