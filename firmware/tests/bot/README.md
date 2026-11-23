# Sparkle Town playtest bot

Plays Sparkle Town (modules/mathquest) like children do, on the simulator and on the Mac page, and
checks invariants the whole time. Findings land in `build/bot/findings.json` with a screenshot and
a repro script each.

    make bot          # quick: 2 seeds x 5 strategies on the sim, 1 seed each on the Mac (~6 min)
    make bot-long     # 12 seeds x 9000 frames (6 simulated minutes) each on the sim, 2 seeds on the Mac;
                      # SPARKLE_PRIVATE_PAGE=/path/to/your/private/page.html also plays a private theme
    python3 tests/bot/bot.py run --platform sim --strategy monkey --seeds 1-20 --frames 20000
    python3 tests/bot/bot.py replay tests/bot/regressions/<file>.json    # STILL FAILS / passes now (a tap or twist
                                                                         # probe is re-run at that moment)
    python3 tests/bot/bot.py minimise build/bot/findings.json --save-regressions   # after a run with --minimise 0
    python3 tests/bot/bot.py list                                        # every invariant and probe

## Pieces

| File | What |
|---|---|
| `drivers.py` | `SimDriver` (build/sim/calcsim over its console: `touch`, `key`, `mq step`, `screen`, `text wh`, `mq hits`; ASan reports and exits are crashes, a silent command is a hang) and `MacDriver` (the page in headless Chrome through `mac_bridge.js`: real pointer, wheel, ctrl+wheel pinch and key events; the renderer's text calls with their boxes; the canvas vs the 1x framebuffer). Every input is recorded in `d.script`, the repro. |
| `mac_bridge.js` | the Chrome DevTools side of `MacDriver` (one JSON request a line). |
| `hands.py` | how a kid answers in each of the 16 games with real inputs: tap the option that shows the value, drag in Sort Slide, arrows then the action button in Hop Along / Sky Fling / Build It / Clock Tower / Treasure Dig, the on-screen keypads, the Heist wheels (the scroll wheel). The target value is the engine's (`mq hits` line `game ... ans ... wrongv ...`). |
| `bot.py` | the strategies (explorer BFS, competent, struggling, masher, monkey), the multi-step probes (tap feedback, way back, frozen items, twists, saves, stuck), the runner and the ddmin minimiser. |
| `invariants.py` | the per-observation and per-transition invariants. |
| `regressions/` | minimised repros of S1/S2 sim findings (`--save-regressions`), replayable. |

Debug hooks this needs (read-only, both in the harness build and the calculator build):
`mq hits` (modules/mathquest/mq_main.c: the button table, the game's widgets, and one `game ...` line with
the right option, the answer in the game's encoding, the controls' values, the acted-story flag) and
`text wh` (core/console.c: text runs with their boxes). The Mac runs use `build/bot/sparkle.html`, a
harness build of the page (`make bot-page`); with `web/dist/sparkle.html` the hooks are missing and the
bot sees no tap targets there.

## Adding an invariant

An invariant is a function in `invariants.py`:

```python
@inv("my-id", "S2", "obs")            # or "act" for f(ctx, before, action, after)
def my_check(ctx, o):
    """One line: the rule (it is what `bot.py list` and the report print)."""
    if o.scene != "shop":
        return []
    return [(signature, message)]      # signature: short, stable (no counts / positions) -> dedup key
```

`o` is an `Obs`: `o.st` (every state key), `o.runs` (text runs with x, y, w, h; on the Mac also
`squeeze`), `o.hits` (tap targets: `src` btn|wg, `kind`, `id`, `arg`, box, `label`; `o.hits.game`: the
game line), `o.scene`, `o.mech`, `o.moment`, `o.busy` (an acted story, a teaching moment or the notebook
owns the round), `o.where()`. `ctx` is the `Session` (`ctx.platform`, `ctx.d` the driver).
Severities: S1 crash / lost progress / wrong judging, S2 a kid is stuck or cannot do something, S3
visible but survivable, S4 polish.

Something that needs several steps (do X, wait, compare) is a probe: a `Session.probe_*` method that
calls `self.report(id, sev, sig, msg)`, called from a strategy. On the sim, `self.fork()` replays the
session into a second simulator (it is deterministic), for with/without comparisons (see the twists).

New state for the bot belongs in the `mq hits` debug line or a new `state` key -- read-only, never a
behaviour change.

## Notes (2026-10-22)

- `mq hits` lists a twisted field's targets where they are drawn (`mq_game_screen_box`: mirror world, gravity
  flip); the bot taps those, the module mirrors the touch back.
- The notebook: `state mq_nb_want` is the digit of the box she is on; the competent kid writes it, the
  struggling kid slips now and then (the notebook stalls).
- `mac-parity` compares the canvas and the 1x framebuffer of ONE redraw of the module as it is (`Sparkle.redraw`
  on the page; no frame in between), with the texts of that redraw; blocks on a 1x text are the fonts' widths
  (the texts are compared one by one). Two to five blocks below the header are a picture's footprint (`mac-footprint`, S3); a missing picture is six or more.
- The Mac boot retries a stalled navigation twice before it is a hang.
- A private harness page: `build_sparkle_home.py --harness` (outside the repository) builds the home theme
  page from `build/bot/sparkle.html` with its own progress key; pass it as SPARKLE_PRIVATE_PAGE.

## Known limits

- The Mac page runs in real time: its scripts are recorded but not minimised (not deterministic).
- `text-overlap` on the Mac sees text covered by an opaque panel as overlapping; check the screenshot.
- `mac-parity` compares the canvas and the 1x framebuffer read a few ms apart: single blocks during
  animation are ignored, but motion can still show up.
- Pet Shop and Pie Shop have no hand model yet (`hands.py` "unsupported"): the bot answers there with
  `mq correct`, so the truth checks do not cover them.
