# Sparkle Town render audit: 1x vs the Mac page (and occlusion)

This audit is independent of the module. It covers `firmware/modules/mathquest` at commit `e9821ff` on the
public page (`web/dist/sparkle.html`) and on the private home build with its theme pack. Nothing in the
module, app, web or CAS code was changed. The harness (`docs/audit/render/tools`, run with `make render-audit`) can
be re-run after every fix.

**Result: 28 findings (9 high, 11 medium, 8 low).** The checks that came back clean are listed at the end.
Machine-readable: `findings_render.json` holds the curated table, all automatic finding groups of the public run,
and the occlusion timelines. Images: `render/shots/` (each is 1x x2 | Mac | the Mac with the finding boxes).
The private pack's images are kept outside the repository.

## Method

The web build draws the module twice from the same calls: the 1x framebuffer, and the drawing-command stream that
the page renders at high resolution (`platform/web/web_sparkle.c`). The harness holds the page's animation loop
(`requestAnimationFrame` is replaced before the page loads), so a scene changes only with console lines. For each
scene it renders three images from one module state:

| | what | how |
|---|---|---|
| **A** | the 1x screen | `sim.framebuffer565()` |
| **C** | the Mac page as it is | the redraw's commands run by the page's own `Renderer`, with the vector art and the atlas registered |
| **B** | the 1x intent at high resolution | the same redraw with no art registered: every hr bracket falls back to the module's own shapes, rectangles and text |

It then compares them:

- **hook (A vs B).** Do the hr hooks describe what the module drew? Text is masked out, and each pixel gets a 3x3 tolerance.
- **substitution (C vs B), per sprite or picture.** Is the element present, in the right place, with the right tint? The C and
  B streams are aligned, so every substitution gets the exact list of module commands it *swallowed* (text inside a
  bracket, or content outside the sprite's box).
- **text.** Every `fillText` the page makes is logged: squeezed by `maxWidth`, out of its clip band or the screen,
  overlapping another text, or showing less ink on the Mac than in B (`text-hidden-mac`).
- **blank.** The canvas is cleared to magenta first, and any magenta left is reported.
- **occlusion.** Each text, button or picture that something later in the stream covers is checked by geometry, then
  *verified* by re-rendering the stream without the occluder. Occluders are classed as panel (a dialog), particle,
  or overlay (a banner, toast, guide, hand or coin pop-up). Durations come from timelines driven by real frames:
  `sim.frame(40)`, because `mq step` hides coins in flight and curtains.
- **animation.** The scene is captured again 3 frames later. Something that moves in 1x but not on the Mac is reported as frozen.
- **simulator.** Every generated scene is written out as a simulator script. The native `calcsim` (scale 1, no hr hooks)
  screenshots are compared with the page's 1x (`sim_compare.py`).
- **page overlays.** The corner menu button and the cheat sheet are placed in logical coordinates at 8 Mac window sizes.

**Coverage.** 2,059 captures on each page:

- all 86 skills at every rung (the lowest and highest with seeds 1 and 2, the others with seed 1), each with its stage, play and dud frames;
- all 16 games with up to 4 fitting skills each, and all 5 twists in 12 games;
- the town at day, dusk and night across the street, and 10 festivals and visitors;
- every scene the console reaches: title, name entry, world, arcade, stickers, the grown-up tabs, rooms, shop, book, show-off,
  wardrobe, the merge board, the toy box and its 9 toys (Bongo Beats too), 10 fun beats (squishies), 3 action games and
  4 fact games, the helper, the lightbulb, the notebook, and teaching moments;
- the 186 screenshots of the 13 Math Quest UI-test scripts;
- 17 occlusion timelines, from 0 to 12 s.

The two pages were rendered in Chrome (1280x960, scale 4).

## Findings, ranked by severity

"Both" in the platform column means the bug is in the module's drawing, so 1x shows it too.

| ID | Sev | Scene | Platform | Theme | Element | Symptom | Evidence | Suspected cause |
|---|---|---|---|---|---|---|---|---|
| H1 | high | any game at night (the Night mode twist) | Mac | both | game backdrops `bg/garden`, `bg/post`, `bg/clock`, `stage/cookie`; `game/bubble` | Night is not applied. The backdrop stays daylight (+45..+70 luma against the module's colours) while 1x is deep blue. `bg/bakery` is tinted, so it depends on the rig | r03, r04 | **tint not applied**: the rigs paint literal colours (`fillSoft` white, `#fff3a0`, `INK`, gradients) and raw parameter colours. Only some calls go through `T(spr, ...)` |
| H2 | high | night and dusk: town, games, rooms | Mac | both | every **picture** (cast, items, props) | Pictures are never tinted. Items in the night town are +33..+38 luma, the cast in a night game +56..+59, the pack's cast and items +53..+73 | r04, r02 | **tint not carried**: the IMAGE command has no tint word (`mq_hr_image` sends only the silhouette colour). `Renderer._image` draws vector art with tint 0 and atlas art as is |
| H3 | high | the town street, Sky Castle (all times of day) | Mac | both | the Sky Castle building | A blank pale sky panel: no castle, no beanstalk, and light at night | r01, r02 | **ID collision**: `mq_town.c` `building()` names world 7's building `"town/sky"`, the same id as the sky backdrop (`mq_town.c:615`). The page runs the backdrop rig `R["town/sky"]` in the building's box |
| H4 | high | scaled picture graphs (skill 62; any scale > 1) | both | both | graph key | Reads `key: each star ` with **no value**, so 5 stars for an answer of 10 looks wrong. This is the reported "bar graph shows 5 stars" bug | r05 | **layout/buffer**: `mq_vis.c` `v_bargraph` formats `"key: each star = %d"` into `char s[16]` and cuts it to 15 characters |
| H5 | high | line plots (skills 63, 64, 84) | both | both | axis tick labels | The fractions run together into `4/45/46/47/48/49/4`. The axis title leaves its panel and goes under the bubbles | r06 | **layout**: fixed tick spacing for labels wider than the spacing |
| H6 | high | Read a table (skill 65) with the theme pack | both | private | name column | The pack's longest cast name overruns the column and **overwrites that friend's number** | private (outside the repository) | **layout**: column width sized for the built-in names; no fit or ellipsis |
| H7 | high | **shop**, on entering after an unlock | both | both | toasts over the tab row | Toasts follow one another over the Pets / Walls tabs: "New! Bouncy ball" 0.24-2 s, "*friend* moved into town!" 3-4 s, "New! A coin bag" about 6 s. **Tabs covered for about 6 s** | r08, r09 | **occlusion**: the toast has one fixed spot in the header row (y 40) on every screen, and the queue plays toasts back to back |
| H8 | high | **shop**, after buying | both | both | "You got a *item*!" | The message stays over the tab row as long as the item card is open (**still there at 12 s**) | r10 | **occlusion**: no timeout while the modal is up |
| H9 | high | any game when a toast fires (Hop Along, Sky Fling) | both | both | the game's instruction pill | The toast covers the instruction and cuts it to "Hop to the r". It is wider with the pack's long names | r12 | the same fixed toast spot as H7 |
| M1 | med | Clock Tower (skills 38-40) | Mac | both | `game/clock_ticks` over the numerals | The vector 5-minute ticks are long bars drawn **after** the numerals, so 1, 3, 6, 9, 11 and 12 look struck through. In 1x the ticks are dots at the rim | r07, r07b | **vector twin / z-order**: the tick art reaches the numeral ring. The numerals are drawn between the face and the ticks |
| M2 | med | the grown-up screens, two-line orders, tables | Mac | both | text placement | Every string is **centred** in its 1x box, so left-aligned columns get ragged edges | r13, r04 | **text**: `Renderer._text` uses `bx = x + (w - dw) / 2`. The TEXT command has no alignment |
| M3 | med | the start of a twisted round | both | both | "Night mode!" / "Mirror world!" banners | They cover the answer bubbles and order numbers for 0.24-1.0 s | r03 | occlusion (a banner over the play field) |
| M4 | med | **shop**, every page | both | both | "New: the Garden set!" | It permanently covers the page count ("1/7") | r11 | occlusion (banner and page count share the footer spot) |
| M5 | med | a right answer | both | both | the cheering friend; the +coins pop-up | They cover answer numbers for 0-1 s | r17 | occlusion |
| M6 | med | the first order of a game | both | both | the demo finger | It covers the answer bubble's number (28-99% hidden; 361 captures) | r18 | occlusion (the hand points at the label it covers) |
| M7 | med | name entry; toy keypads | Mac | both | single-letter keys | Wide letters are squashed: W to 63%, M 71%, G/O/Q 75% | r14 | **text**: font fitting skips one-character strings, so `maxWidth` squeezes them |
| M8 | med | Star Harbor backdrop (`bg/star`) | Mac | both | lighthouse beam | It sweeps in 1x and is **frozen** on the Mac (11 captures) | r16 | the rig ignores the sweep |
| M9 | med | picture graphs (61, 62) | both | both | star rows | A long row (8 stars) leaves the white panel and goes under the bubbles | r05 | layout (star spacing not fitted) |
| M10 | med | the Mac page in a **4:3 window** (1024x768, 800x600, full screen on a 4:3 display) | Mac | both | the corner menu button | The 46 px button sits over the play area's top-right (logical x 296-315, y 4-24): the empty status band at 1024x768, the header's star count at 800x600. In 16:10 windows it is beside the play area | r21 | `#menubtn` is fixed to the window corner, not to the side area |
| M11 | med | Treasure Dig (70) | both | both | map axis labels | The x "10" sits under the dig! button. On the Mac the friend's picture also hides 31-36% of the axis numbers' ink | r20 | layout / occlusion |
| L1 | low | Peekaboo | Mac | both | `game/hole` | The vector holes are dark pits where 1x shows brown mounds (240 captures). The game still reads | r15 | vector twin differs from the 1x design |
| L2 | low | squishies | Mac | both | squishy | The vector squishy has no blush or cheeks | r19 | vector twin differs from the 1x design |
| L3 | low | the grown-up skill list | 1x | both | standard lines | Lines are cut mid-phrase ("ones in") | r13 | truncated to the 1x width |
| L4 | low | the shop grid | both | both | item pictures | Some pictures reach past their card's edge (12-24%) | r08 | picture larger than the card art |
| L5 | low | stars, rings, small polygons | Mac | both | hr shapes | 10-56 px differences between the hr shape and 1x (7 groups) | - | hook rounding |
| L6 | low | first start | Mac | both | the cheat sheet | It covers the game until a click or key (by design) | - | - |
| L7 | low | rooms | both | both | the walking guide; overlapping furniture | The guide walks in front of furniture for 0.24-4 s | - | expected |
| L8 | low | the town while it scrolls | both | both | building labels | Labels are cut at the screen edge (expected) | - | - |

Images (`render/shots/`):

- r01 `r01_town_skycastle_id_collision.png`
- r02 `r02_town_night_untinted.png`
- r03 `r03_game_night_untinted.png`
- r04 `r04_gatedash_night_cast_items.png`
- r05 `r05_scaled_graph_key_truncated.png`
- r06 `r06_lineplot_axis_labels_collide.png`
- r07 `r07_clock_ticks_over_numerals.png`, with `r07b_clock_zoom_mac.png`
- r08 `r08_shop_unlock_toast_over_tabs.png`
- r09 `r09_shop_moved_in_toast_over_tabs.png`
- r10 `r10_shop_bought_msg_persists.png`
- r11 `r11_shop_banner_over_page_count.png`
- r12 `r12_hop_toast_over_instruction.png`
- r13 `r13_parent_text_centred_ragged.png`
- r14 `r14_name_keys_squeezed.png`
- r15 `r15_peekaboo_holes.png`
- r16 `r16_lighthouse_beam_frozen.png`
- r17 `r17_cheer_coins_over_bubble.png`
- r18 `r18_finger_over_answer.png`
- r19 `r19_squishy_art.png`
- r20 `r20_treasure_dig_axis.png`
- r21 `r21_menu_button_4x3_window.png`

### Occlusion: what covers what, and for how long

Measured from real frames after entering each screen or acting on it (`findings_render.json` →
`occlusion_timelines_public`).

| Screen | Occluder | Covers | Duration |
|---|---|---|---|
| Shop | unlock toasts ("New! ...", "... moved into town!") | the Pets / Walls tabs (tap targets) | 0.24 s to about 6 s, back to back |
| Shop | "You got a *item*!" | the tab row | the whole time the card is open (≥ 12 s) |
| Shop | "New: the Garden set!" | the page count "1/7" | always |
| Arcade, book, room, wardrobe | the same toast; the +coins pop-up at the coin counter | the guide's picture; the coin count "315"; "?" | 0.24-6 s |
| Games | the twist banner | answer bubbles and the order's numbers | 0.24-1.0 s |
| Games | a toast in the header | the instruction pill | about 2 s each |
| Games | the cheering friend, the +coins pop-up | answer numbers | 0-1 s |
| Games | the demo finger | the answer bubble's label | until the demo ends |
| Streak | the "Amazing x5!" banner and the streak bar | the score "x5" and a bubble's number | 0.48-1.0 s |
| Room | the walking guide | furniture | 0.24-4 s at a time (expected) |
| Town | the pet | building labels | under 0.5 s |
| Mac page | the corner menu button | the header's top-right, in 4:3 windows only | always |
| Mac page | the cheat sheet | everything (by design) | until the first click or key |

## Systemic causes

1. **Tint is applied per colour by the module, and the page's substitutes go around it.** `mq_tint` runs in
   `mq_fill` and in the hr `head()`. There are three ways around it:
   - the IMAGE command carries no tint at all (H2);
   - rigs paint literal colours and raw parameter colours (H1);
   - only the plain `V.draw` path tints rasters.

   The fix is a tint word on IMAGE, and `T(spr, ...)` on every rig colour, or one tint pass over each substitute.
2. **Sprite ids are one flat string namespace with no uniqueness check.** `town/sky` is both a building and the
   backdrop (H3). Add a unit check that every `mq_hr_sprite` id names exactly one thing. The harness's
   `inventory` per scene lists the ids actually drawn.
3. **Fixed-width 1x layouts with no fit, skip or ellipsis.** Examples are the graph key buffer (`char s[16]`), line-plot tick
   labels, table columns and star rows (H4-H6, M9). A theme pack's longer names break them first.
4. **Toasts and banners have no reserved area.** The toast always lands in the header row, which holds the shop tabs
   and the game instruction. The queue plays toasts back to back, and the purchase message never times out under a
   modal (H7-H9, M3, M4). Options are a toast lane that never covers tap targets, or toasts held while a screen with
   tabs is up.
5. **Mac text placement loses the 1x intent.** The TEXT command has no alignment, so everything is centred (M2).
   Font fitting skips one-character strings (M7). The page was audited in Chrome, but the Mac page is meant for
   Safari, where `ui-rounded` resolves to SF Pro Rounded, so widths and squeeze ratios will differ there.
6. **Vector twins are not held to the 1x footprint or z-order.** Clock ticks over the numerals (M1), the holes (L1)
   and the squishy (L2) are examples. The harness's C-vs-B diff per sprite id now flags drift like this.
7. **Some rigs ignore the animation parameter** (the lighthouse beam, M8).

## Checks that came back clean

- **Swallowed text: 0.** No substitution hides text the module drew inside its bracket. The clock's numerals and the coins are drawn
  outside the sprite brackets, so the earlier "clock / coins not shown" class does not recur at this commit.
- **Blank pixels on the Mac: 0.** **Stream desyncs: 0.**
- **Theme id collisions: 0.** The pack's vector ids are all namespaced, and no hash overlaps the built-in vector or atlas art.
- **1x is independent of scale.** All 1,683 generated scenes are pixel-identical between the native simulator and the page's
  1x (status and soft-key bars masked). The 111 UI-script scenes that differ are script state drift from taps and timing,
  not drawing.

## Running it

```
make render-audit                                    # public page: all jobs, about 4 min; + the simulator comparison
make render-audit RENDER_AUDIT_JOBS=twists,town RENDER_AUDIT_ONLY=night
make render-audit RENDER_AUDIT_PRIVATE=/path/to/private_page.html RENDER_AUDIT_PRIVATE_OUT=/path/outside/the/repo
```

The output goes to `build/render_audit/`:

- `groups.txt` and `groups.json`: the findings grouped by check and element, with an example image, and the occlusion
  durations;
- `public/results.jsonl`: one line per capture;
- `public/shots/<job>/<capture>.png`: the side-by-side images;
- `sim/sim_compare.json`: the simulator comparison.

The tools are in `docs/audit/render/tools`:

- `render_audit.js`: the driver; jobs `ui,games,mechs,twists,town,events,screens,occl,overlays`;
- `inpage.js`: capture and comparisons;
- `aggregate.py`;
- `sim_compare.py`;
- `summary.py`: one line per capture, or one check.

It needs Google Chrome and node ≥ 22.
