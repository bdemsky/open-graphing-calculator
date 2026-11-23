# Sparkle Town playtest bot: findings

What the automated playtest bot (`tests/bot/`, `make bot` / `make bot-long`; see `tests/bot/README.md`)
found in Sparkle Town at commit `e9821ff`. Nothing in the module was fixed; the only module change is a
read-only debug console command (`mq hits`), plus `text wh` in the core console.

## How it was run

`make bot-long` with the private theme page: on the simulator 12 seeds x 5 strategies (explorer BFS,
competent kid, struggling kid, masher, monkey) x 9,000 frames (6 simulated minutes each, about 6
simulated hours and about 750,000 inputs in all); on the Mac page in headless Chrome (2940x1912 @2x,
real pointer, wheel, pinch and key events) 2 seeds x 5 strategies on `sparkle.html` and the same on the
private theme page. The explorer reached about 65-70 distinct screens a seed on the simulator.
The run produced **257 distinct signatures** (11 S1, 94 S2, 152 S3). After triage (screenshots, a fresh
replay of each, a direct check of the strongest ones), they come down to **21 bugs: 0 S1, 9 S2, 12 S3**,
listed below. All 11 S1 signatures were the bot's own mistakes, fixed in the bot (see the end).

Clean: no crash, hang, ASan/UBSan report or page exception in any run; every right answer given
with the game's own controls was accepted and every wrong one rejected (about 600 judged orders in
14 of 16 games, by tap, drag, arrows + action button, keypads and lock wheels); no school words; no
unfilled templates; the player's name is always on her town; a power off and on (sim) or a page reload
(Mac) kept stars, coins, stickers, items and the player every time.

Repro scripts are console lines for `build/sim/calcsim` (paste after the setup line) unless they say
Mac. Setup: `fs insert 64`, `fs format`, `module library extra`,
`module install /CALC/MODULES/MATHQ010.MOD`, `key CLEAR`, `app mathquest`. `QUIET` below stands for
`mq scene off`, `mq notebook 2` and `mq teach N` + `mq teach done` for N = 0..11 (no acted story,
notebook or teaching moment over the game). Screenshots are in `bot_shots/`. Longer bot-made
scripts (seed + every input) are in `tests/bot/regressions/` and replay with
`python3 tests/bot/bot.py replay FILE`.

## S2: a child is stuck, blocked or misled

### 1. The 'friend at the door' panel leaks out of the shop and room and covers every screen

When her fun time runs out in the room or the shop, Pip comes to the door ('The garden show starts
soon!' + a **go!** button). If she leaves before the 9 s hand-over (back, back), `hook_t` stays set:
the panel is then drawn over the title, the town's worlds, the book, the arcade, the shop and **every
game** (it covers the order text and the teaching panel), and its go! button (252,98 60x24) stays in
the tap table on top of answer bubbles, gates and shop items. The hook's 9 s timer only runs in the
fun scenes, so it never clears by itself. This is the 'overlays covering the shop' report.
Invariants: `targets-overlap` (on 9 kinds of screen), screenshots.

    mq seed 4
    mq new Kid 2
    mq coins 300
    mq fun 0
    key F1
    mq step 6
    key F1
    mq step 10
    key CLEAR
    key CLEAR
    mq level 0 0
    mq step 120
    mq hits              -> btn 0 id 116 (the hook's go!) at 252,98 over the game

`bot_shots/hook_overlay_repro.png`, `hook_overlay_title.png`, `hook_overlay_shop.png`, `hook_overlay_bubble_pop.png`

### 2. The grown-up button cannot be opened by touch or with the mouse

The gate's own text says 'Grown-ups: keep holding F5 (or keep a finger on it)', but a finger (or a mouse
button on the Mac) held on 'grown-up' for 2.8 s does nothing; only a held F5 *key* opens it (`held_key`).
On a Mac the F5 key is fn+F5, so the grown-ups' corner is effectively unreachable there. A tap gives no
hint either. Invariants: `kid-gate` (was `hold-gate`), `tap-feedback` (sim and Mac).

    mq new Kid 2
    mq step 4
    touch 294 231 down
    wait 2800
    touch 294 231 up
    state mq_scene       -> map      (expected: parent)
    hold 2500 F5
    wait 2700
    state mq_scene       -> parent

Mac: the `hold` op of `MacDriver` (mouse down 2.8 s on 'grown-up') leaves `mq_scene=map`.
`bot_shots/grownup_hold.png`

### 3. Night mode does nothing in Treasure Heist and Kart Race

Only the 'Night mode!' banner appears; 4.8 s later the screen is identical, pixel for pixel, to the
same moment without the twist (the simulator replays deterministically: same framebuffer CRC).
Shop Till, Cake Kitchen and Bubble Pop do darken. (Once, 'Giant bubbles!' in Bubble Pop also left the
screen unchanged; elsewhere it works -- a candidate, regression `twist-visible_16f51067ca.json`.)
Invariant: `twist-visible` (fork-and-compare).

    mq seed 4
    mq new Kid 2
    QUIET
    mq skill 0 1
    mq mech "Treasure Heist"      (or "Kart Race")
    mq step 40
    mq twist 1                    (run it again with mq twist 0)
    mq step 120
    fbcrc                          -> the same crc both times

`bot_shots/night_treasure_heist_on.png` / `_off.png`, `night_kart_race_on.png` / `_off.png`

### 4. Shop Till: the 'ring!' button covers the keypad's 0

The till's keypad has its 0 key at (246,189 32x25), under the ring! button (246,184 70x36). The 0
cannot be tapped (the tap rings instead), so any amount with a 0 is out of reach by touch. Seen in
1,302 observations. Invariant: `targets-overlap`.

    mq new Kid 2
    QUIET
    mq skill 44 1
    mq mech "Shop Till"
    mq step 60
    mq hits        -> key id 17 arg 48 at 246,189 and button id 1 'ring!' at 246,184

`bot_shots/till_zero_key.png`, `shop_popup_dead_tabs.png`

### 5. Mac: a new building is a blank box on the town ('The town grew!')

On the Mac page, right after 'The town grew!' the new building (Sky Castle) is drawn as an empty
gradient rectangle; the 1x framebuffer has it (8 blocks with detail at 1x, flat on the canvas).
Not seen with `mq grant all` + `mq go map` (the finished town is fine), so it is the growing
animation's picture that has no Mac art. Invariant: `mac-parity`.

Mac repro: `python3 tests/bot/bot.py run --platform mac --strategy competent --seeds 1 --frames 1500`
(the first visit to the town after the third order). `bot_shots/mac_map_sky_castle.png`

### 6. Hop Along: the right pad is under the land! button, the left pad is off the screen

In the two-pad Hop Along (mode 1) the right pad's tap target (266,150 60x60) overlaps 'land!'
(246,184 70x36) and the left pad starts at x = -6. A tap low on the right pad lands instead of
choosing it. Invariants: `targets-overlap`, `target-offscreen`.

    mq new Kid 2
    QUIET
    mq skill 15 1
    mq mech "Hop Along"
    mq step 60
    mq hits        -> choice id 9 arg 0 at -6,150 ; choice id 9 arg 1 at 266,150 ; button 'land!' at 246,184

`bot_shots/hop_along_pad_under_land.png`

### 7. Gate Dash / Kart Race typed answers: the 8 key is under 'ENTER: dash!'

When an order is typed in Gate Dash or Kart Race (money, big numbers), the keypad's 8 (186,198 24x22)
sits under the 'ENTER: dash!' button (196,178 80x46), and the field takes any number of digits (the
masher typed '8438834595'). Invariant: `targets-overlap` (148 observations).
Repro: `python3 tests/bot/bot.py replay tests/bot/regressions/targets-overlap_72425e3ce9.json` (STILL FAILS).
Shot: `bot_shots/gate_dash_keypad_under_dash.png`

### 8. Taps on moving answers are sometimes not taken

In Cloud Catch, Kart Race and Gate Dash a tap on the right answer's own widget (its current box from
the game) was not taken within 4 s in about 1 of 9 direct trials and in 8-30 rounds of the long run
(Cloud Catch: the star the bot tapped had fallen out of view by the time the cloud could get there;
Kart Race / Gate Dash: the kart never changed lane). Matches 'taps on falling items not responding'.
Invariant: `answer-no-response`; a candidate (the bot's tap timing on fast movers may be part of it).
Repro: `python3 tests/bot/bot.py run --platform sim --strategy competent --seeds 1 --frames 9000` (Cloud
Catch, 'Dot's parcel has 9 hundreds' order); the finding carries the full input script. `bot_shots/cloud_catch_tap_not_taken.png`

### 9. A crash or battery pull loses all play since the last idle minute

The calculator writes the session only at power-off or after 60 s without input
(`SESSION_IDLE_MS`, app/session.c). A child playing continuously never idles a minute, so a hard reset
(battery out, a watchdog reset) loses everything since she started. The module itself saves into the
session at most once a second, and a proper power off and on keeps everything.

    mq new Kid 2
    mq coins 77
    mq step 10
    wait 20000
    session info     -> session_seq=0 session_dirty=1 (nothing written in 20 s of play)
    reboot crash
    app mathquest
    state mq_player  -> (empty: the player is gone)

## S3: visible, survivable

10. **Dead buttons during a teaching moment and the acted story.** While 'The big idea' panel is up,
   'help?', 'show me' and the '?' do nothing (no state change, nothing near them changes); during the
   acted story 'show me' does nothing; in the room, 'undo' with nothing to undo gives no sign. Repro: `mq new Kid 2`, `mq level 0 0`, `mq step 80` (teaching
   moment 1), `touch tap 160 231`, `mq step 2`, `state mq_teach` -> unchanged. `bot_shots/teach_softkeys_dead.png`
11. **Shop: the item popup leaves the shop live-looking but dead**: tabs, page arrows and items stay
   visible around 'Bookshelf ... close' but taps on them do nothing; the 'New: the Garden set!'
   banner covers the page counter; LEFT/RIGHT do not change `mq_page`.
   `bot_shots/shop_popup_dead_tabs.png`, `shop_banner_covers_pages.png`
12. **Teaching sentences run off the right edge** (336 px lines at x = 10): 'Fair pieces are all the
   same size: then they get a name!', 'Does the floor count squares inside, or the edge around?',
   'The 1 ten hops next door, into the little box on top!' (318 px). `bot_shots/teach_text_clipped.png`
13. **Toy box hint runs off the screen**: '1, 2, 3 in each row and column' at x 186..366 (4,495 times).
   Repro: `mq new Kid 2`, `mq go arcade`, open the number-puzzle toy. `bot_shots/toys_text_clipped.png`
14. **Clipped labels in games**: Sort Slide number-line labels at x = -10 ('330', '204'), Cloud Catch
   'Super x3!' (217..343), Gate Dash / Kart Race gate values entering at the right edge.
   `bot_shots/sort_slide_label_clipped.png`
15. **Picture-graph orders show no key**: 'Bramble's fruit vote graph: How many votes for plums?' with
   rows of stars and no 'each star = N'. (Bar-graph versions have an axis and are fine.)
   `bot_shots/picture_graph_no_key.png`
16. **Mashing still pays a little**: over 292 mashed rounds (every answer tapped one frame apart) the
   masher earned 7 stars and 10 coins after the guess detector should have stopped rewards; the
   detector zeroes coins only for 3 answers and never stars. `bot_shots/mash_rewards.png`
17. **The town never shows her coins**: its top bar has the week and the stars only, while every game
   pays coins. `bot_shots/town_no_coins.png`
18. **ENTER and the arrows leave screens**: ENTER leaves the wardrobe and the show-off screen for the
   world map; RIGHT leaves a friend's chapter for the town. `bot_shots/wardrobe_enter_exits.png`,
   `chapter_right_exits.png`
19. **CLEAR on the title quits Sparkle Town** into the calculator's home screen (calculator only; the
   Mac page is locked). `bot_shots/left_game_clear_on_title.png`
20. **Mac: text over text** in the shop popup ('Round table' over 'You have 0' / '12 coins') and
   banners over answers ('Night mode!' over a Peekaboo number) -- the renderer draws both.
21. **Mac parity, smaller**: blocks drawn at 1x but flat on the canvas on the title (the two '+' cards),
   the world map's header and during some teaching moments (3-5 blocks); worth a look with the render
   audit, may partly be animation between the two reads.

## Not bugs (the bot's own mistakes, fixed)

- 9 'right answer not accepted' (S1) in Peekaboo: the bot tapped by option index, but Peekaboo's
  widgets are holes and any friend can pop up in any hole; the bot now taps the hole showing the value.
- 2 'answer printed in the scene' (S1): a masher's typed digits and a theme page without the debug hook.
- 'Night mode does nothing' in every game while a teaching moment or the acted story covered the field.
- 'So the a' (template leak): the worked example typing itself out.
- Text overlap on the Mac from two frames' texts captured together (now one drained redraw).
