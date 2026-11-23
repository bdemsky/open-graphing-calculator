"""The invariants the playtest bot checks while it plays (see README.md to add one).

An invariant is a function decorated with @inv("id", severity, when) that looks at an observation
(or a transition: before, action, after) and returns a list of (signature, message) pairs, one per
violation. The signature (a short string, without volatile numbers) dedups the same bug seen again;
the message says what was seen. Severities: S1 crash / lost progress / wrong judging, S2 a kid gets
stuck or cannot do something, S3 visible but survivable, S4 polish.

    when = "obs"   f(ctx, obs)                     every observation
    when = "act"   f(ctx, before, action, after)   after each input (after = a couple of frames later)
"""
import re

INVARIANTS = []


def inv(id_, sev, when):
    def deco(f):
        INVARIANTS.append(dict(id=id_, sev=sev, when=when, f=f, doc=(f.__doc__ or "").strip()))
        return f
    return deco


# state keys that change by themselves (time, sound, perf): not a response to an input
VOLATILE = {"mq_frame_us", "mq_frame_px", "mq_secs", "mq_fun", "cas_mem", "touch_last", "modx_sfx", "modx_music",
            "modx_speak", "modx_speaks", "toast", "session_seq", "session_bytes", "session_dirty", "session_save_ms",
            "session_writes_erases", "session_load_ms", "session_blobs", "session_loaded", "uptime", "mq_beat",
            "modx_pool", "mq_shot", "mq_story", "mq_read"}
ROOTS = {"title", "map"}  # nowhere further back to go
PROGRESS = ["mq_stars", "mq_coins", "mq_stickers", "mq_bosses", "mq_earned"]
BANNED = ["math", "maths", "learn", "learning", "lesson", "lessons", "homework", "grade", "grades", "score", "correct",
          "incorrect", "practice", "quiz", "test", "tests", "fact", "facts", "skill", "skills", "problem", "problems",
          "level", "levels", "standard", "standards", "mistake", "mistakes", "drill", "smart", "worksheet", "review",
          "reviews", "revise", "refresh", "remember", "prime", "composite", "borrowing", "misconception", "cuddle", "cuddles",
          "cuddling"]
BANNED_RE = re.compile(r"\b(" + "|".join(BANNED) + r")\b", re.I)
ADULT_SCENES = {"parent"}
ARROWS = {"LEFT", "RIGHT", "UP", "DOWN"}
# how deep a screen is (going to a smaller number is going back)
DEPTH = {"title": 0, "name": 1, "map": 1, "world": 2, "arcade": 2, "room": 2, "book": 2, "stickers": 2, "parent": 2,
         "merge": 2, "intro": 3, "play": 3, "factgame": 3, "shop": 3, "wardrobe": 3, "showoff": 3, "toys": 3, "fun": 3,
         "action": 3, "preview": 3, "chapter": 3, "done": 3}  # (done: the round's end, not back)


class Obs:
    """what the bot sees at one moment: every state key, the text runs, the tap targets, the 1x pixels"""

    def __init__(self, st, runs, hits, px=None, frame=0):
        self.st, self.runs, self.hits, self.px, self.frame = st, runs, hits, px, frame
        self.extra = None

    @property
    def scene(self):
        return self.st.get("mq_scene", "?")

    @property
    def mech(self):
        return self.st.get("mq_mech", "")

    @property
    def moment(self):
        return self.st.get("mq_moment", "")

    @property
    def busy(self):
        """an overlay owns the round: the acted story, a teaching moment, the notebook"""
        return bool((self.hits.game or {}).get("story") or self.st.get("mq_teach") or self.st.get("mq_notebook"))

    def where(self):
        s = self.scene
        if s == "play":
            s += "/" + (self.mech or "?")
            if self.hits.game and self.hits.game.get("story"):
                s += "+story"
            elif self.st.get("mq_teach"):
                s += "+teaching"
            elif self.st.get("mq_notebook"):
                s += "+notebook"
        return s

    def text(self):
        return " | ".join(r.s for r in self.runs)

    def onscreen(self):
        return [r for r in self.runs if r.x + r.w > 0 and r.x < 320 and r.y + r.h > 0 and r.y < 240]

    def visible_progress(self):
        return tuple(self.st.get(k, "") for k in PROGRESS)


def state_diff(a, b):
    return sorted(k for k in set(a) | set(b) if k not in VOLATILE and a.get(k) != b.get(k))


def px_diff(p0, p1, rect=None):
    """changed pixels (in rect x, y, w, h)"""
    if not p0 or not p1:
        return 0
    if rect is None:
        return sum(1 for i in range(0, len(p0), 2) if p0[i:i + 2] != p1[i:i + 2])
    x, y, w, h = rect
    x0, y0, x1, y1 = max(0, x), max(0, y), min(320, x + w), min(240, y + h)
    n = 0
    for yy in range(y0, y1):
        a = (yy * 320 + x0) * 2
        b = (yy * 320 + x1) * 2
        r0, r1 = p0[a:b], p1[a:b]
        if r0 != r1:
            n += sum(1 for i in range(0, len(r0), 2) if r0[i:i + 2] != r1[i:i + 2])
    return n


def hits_sig(h):
    return sorted((t.src, t.kind, t.id, t.arg, t.x, t.y, t.value, t.label) for t in h) + [str(h.game)]


# ----------------------------------------------------------------- observations
@inv("school-words", "S3", "obs")
def school_words(ctx, o):
    """Kid screens use game words only: no school words (the unit test's list) anywhere a kid sees."""
    if o.scene in ADULT_SCENES:
        return []
    out = []
    for r in o.onscreen():
        m = BANNED_RE.search(r.s)
        if m:
            out.append((f"{o.scene}:{m.group(1).lower()}", f"'{r.s}' on {o.where()} at {r.x},{r.y}"))
    return out


@inv("template-leak", "S2", "obs")
def template_leak(ctx, o):
    """No unfilled template or missing name: '%d', '(null)', a line starting with "'s" or " is"."""
    out = []
    for r in o.onscreen():
        s = r.s
        if "%" in s and re.search(r"%[-0-9.]*[sdlucx]", s) or "(null)" in s or re.match(r"^\s*'s\b", s) or \
                re.match(r"^ (is|has|says|wants|and)\b", s):
            out.append((f"{o.scene}:{re.sub(r'[0-9]+', 'N', s)[:30]}", f"'{s}' on {o.where()}"))
    return out


@inv("left-the-game", "S3", "obs")
def left_the_game(ctx, o):
    """A kid's keys never drop her out of Sparkle Town into the calculator (the sim: CLEAR on the
    title quits the app; the Mac page is locked inside the game)."""
    scr = o.st.get("screen", "mathquest")
    if scr != "mathquest":
        return [(f"{ctx.platform}:{scr}", f"Sparkle Town was left for the calculator's '{scr}' screen (last input: "
                 f"{ctx.d.script[-2:] if len(ctx.d.script) > 1 else '?'})")]
    return []


@inv("text-crammed", "S3", "obs")
def text_crammed(ctx, o):
    """(1x) Words on one line do not run into each other (axis labels, captions): two runs on the
    same row closer than 2 px, or one inside another."""
    rs = sorted([r for r in o.onscreen() if r.s.strip() and r.w > 0], key=lambda r: (r.y, r.x))
    out = []
    for a, b in zip(rs, rs[1:]):
        if abs(a.y - b.y) <= 2 and a.h == b.h and 0 <= b.x - (a.x + a.w) < 2 and not a.s.endswith(" ") and \
                re.match(r"^[A-Za-z]", b.s) and re.search(r"[A-Za-z]$", a.s):
            out.append((f"{o.where()}:{a.s[:12]}/{b.s[:12]}", f"'{a.s}' and '{b.s}' touch at {b.x},{b.y} on {o.where()}"))
    return out


@inv("name-rendered", "S2", "obs")
def name_rendered(ctx, o):
    """The player's name shows on her town (the map's title)."""
    name = o.st.get("mq_player", "")
    if o.scene != "map" or not name:
        return []
    if any(name.lower() in r.s.lower() for r in o.runs):
        return []
    return [("map:name", f"player '{name}' not on the town screen; texts: {o.text()[:200]}")]


@inv("text-clipped", "S3", "obs")
def text_clipped(ctx, o):
    """Text is not cut by the screen edges (scrolling scenes excepted: the town, a world)."""
    if o.scene in ("map", "world", "room", "merge") or o.st.get("screen", "mathquest") != "mathquest":
        return []
    out = []
    for r in o.runs:
        if not r.s.strip() or r.w <= 0:
            continue
        part_x = (r.x < 0 < r.x + r.w) or (r.x < 320 < r.x + r.w)
        part_y = (r.y < 0 < r.y + r.h) or (r.y < 240 < r.y + r.h)
        cx, cy = r.x + r.w // 2, r.y + r.h // 2
        if any(t.kind == "choice" and t.x <= cx <= t.x + t.w and t.y <= cy <= t.y + t.h and
               (t.x < 0 or t.x + t.w > 320) for t in o.hits) and o.mech in ("Kart Race", "Gate Dash", "Bubble Pop", "Cloud Catch"):
            continue  # (a moving item coming in at the edge)
        if part_x or part_y:
            out.append((f"{o.where()}:{re.sub(r'[0-9]+', 'N', r.s)[:24]}", f"'{r.s}' at {r.x},{r.y} {r.w}x{r.h} runs off the screen on {o.where()}"))
    return out


@inv("text-squeezed", "S4", "obs")
def text_squeezed(ctx, o):
    """(Mac) The web font fits the 1x box: text squeezed under 80 % of its width is unreadable."""
    out = []
    for r in o.runs:
        if r.squeeze < 0.8 and len(r.s) > 2:
            out.append((f"{o.where()}:{re.sub(r'[0-9]+', 'N', r.s)[:24]}", f"'{r.s}' squeezed to {r.squeeze:.2f} of its width on {o.where()}"))
    return out


@inv("text-overlap", "S3", "obs")
def text_overlap(ctx, o):
    """(Mac: the renderer keeps every text call) Two different texts do not overlap each other."""
    if ctx.platform != "mac":
        return []
    rs = [r for r in o.onscreen() if r.s.strip() and r.w > 0 and r.h > 0]
    out = []
    for i in range(len(rs)):
        for j in range(i + 1, len(rs)):
            a, b = rs[i], rs[j]
            if a.s == b.s:
                continue
            ix = min(a.x + a.w, b.x + b.w) - max(a.x, b.x)
            iy = min(a.y + a.h, b.y + b.h) - max(a.y, b.y)
            if ix > 2 and iy > 3 and ix * iy > 0.25 * min(a.w * a.h, b.w * b.h):
                k = sorted([re.sub(r"[0-9]+", "N", a.s)[:16], re.sub(r"[0-9]+", "N", b.s)[:16]])
                out.append((f"{o.where()}:{k[0]}/{k[1]}", f"'{a.s}' at {a.x},{a.y} overlaps '{b.s}' at {b.x},{b.y} on {o.where()}"))
    return out


@inv("targets-overlap", "S2", "obs")
def targets_overlap(ctx, o):
    """Two tap targets (buttons, answers) do not cover each other: a tap must have one meaning.
    (Big 'area' widgets are backgrounds; they are allowed under the others.)"""
    ts = [t for t in o.hits if t.kind not in ("area", "steer") and t.w > 0 and t.h > 0]
    out = []
    for i in range(len(ts)):
        for j in range(i + 1, len(ts)):
            a, b = ts[i], ts[j]
            ix = min(a.x + a.w, b.x + b.w) - max(a.x, b.x)
            iy = min(a.y + a.h, b.y + b.h) - max(a.y, b.y)
            if ix > 4 and iy > 4 and ix * iy > 0.3 * min(a.w * a.h, b.w * b.h):
                if a.src == b.src == "wg" and a.kind == b.kind == "choice" and o.st.get("mq_mech") in ("Bubble Pop", "Cloud Catch"):
                    continue  # (moving items may cross for a moment)
                if a.kind == b.kind == "clockhand" or (a.kind, b.kind) in (("drag", "drop"), ("drop", "drag")):
                    continue  # (the two hands share the face; a drag starts over its drop)
                out.append((f"{o.where()}:{a.src}{a.kind}{a.id}/{b.src}{b.kind}{b.id}",
                            f"{a} and {b} overlap on {o.where()}"))
    return out


@inv("target-offscreen", "S2", "obs")
def target_offscreen(ctx, o):
    """Every tap target is on the screen (at least its centre)."""
    out = []
    for t in o.hits:
        if t.kind == "area":
            continue
        cx, cy = t.x + t.w // 2, t.y + t.h // 2
        if not (0 <= cx < 320 and 0 <= cy < 240):
            out.append((f"{o.where()}:{t.src}{t.kind}{t.id}", f"{t} has its centre off the screen on {o.where()}"))
    return out


@inv("answer-shown", "S1", "obs")
def answer_shown(ctx, o):
    """While an order waits for her answer, the answer is not printed in the scene (outside the
    answer choices and her own typing)."""
    if o.scene != "play" or o.moment != "play" or o.busy or not o.hits.game:
        return []
    ans = o.st.get("mq_answer", "")
    if len(ans) < 2 or not re.search(r"\d", ans):
        return []
    q = o.st.get("mq_q", "") + " " + o.st.get("mq_q_more", "") + " " + ctx.problem_text
    if re.search(r"(?<![\d/.:])" + re.escape(ans) + r"(?![\d/.:])", q):
        return []  # the number is part of the story itself
    typed = o.st.get("mq_input", "")
    opts = [t for t in o.hits if t.kind in ("choice", "button", "wheel", "key")]
    out = []
    for r in o.onscreen():
        if r.s.strip() != ans or re.sub(r"\D", "", r.s) == re.sub(r"\D", "", typed) and typed:
            continue
        cx, cy = r.x + r.w // 2, r.y + r.h // 2
        m = 34 if o.mech in ("Bubble Pop", "Cloud Catch", "Kart Race", "Gate Dash", "Peekaboo") else 6
        if any(t.x - m <= cx <= t.x + t.w + m and t.y - m <= cy <= t.y + t.h + m for t in opts):
            continue
        out.append((f"{o.where()}:answer", f"the answer '{ans}' is printed at {r.x},{r.y} on {o.where()} (q: {o.st.get('mq_q', '')[:60]})"))
    return out


@inv("no-right-option", "S1", "obs")
def no_right_option(ctx, o):
    """A round with answer options always has exactly one right option, and no option twice."""
    g = o.hits.game
    if not g or o.scene != "play" or o.moment != "play" or g["nopt"] < 2:
        return []
    out = []
    if g["right"] < 0:
        out.append((f"{o.where()}:noright", f"no right option among {g['opts']} (answer {o.st.get('mq_answer')}, q '{o.st.get('mq_q')}')"))
    if len(set(g["opts"])) < len(g["opts"]):
        out.append((f"{o.where()}:dupopt", f"an option twice: {g['opts']} (answer {o.st.get('mq_answer')})"))
    return out


@inv("hud-coins-town", "S3", "obs")
def hud_coins_town(ctx, o):
    """The town shows her purse in its top bar (she earns coins in every game; the shop wants them)."""
    if o.scene != "map" or not o.st.get("mq_player"):
        return []
    coins = o.st.get("mq_coins", "")
    shown = o.st.get("mq_purse_shown", coins)  # (the purse: "55" and a drawn cent sign, or "$3.00")
    if any(r.s.strip().rstrip("\u00a2c") in (coins, shown.rstrip("\u00a2c")) and r.y < 34 for r in o.onscreen()):
        return []
    return [("map:coins", f"coins ({coins}) not in the town's top bar ({ctx.platform}); top texts: "
             + " | ".join(f"{r.s}@{r.x},{r.y}" for r in o.onscreen() if r.y < 34))]


@inv("hud-coins", "S2", "obs")
def hud_coins(ctx, o):
    """Screens with the title line (the header: title, her purse) show the purse's balance -- also on the Mac."""
    if o.scene not in ("shop", "room", "wardrobe", "book", "stickers", "arcade") or not o.st.get("mq_player"):
        return []
    coins = o.st.get("mq_coins", "")
    shown = o.st.get("mq_purse_shown", coins)
    if any(r.s.strip().rstrip("\u00a2c") in (coins, shown.rstrip("\u00a2c")) and r.y < 34 for r in o.onscreen()):
        return []
    return [(f"{o.scene}:coins", f"coins ({coins}) not in the top bar of {o.scene} ({ctx.platform}); top texts: "
             + " | ".join(f"{r.s}@{r.x},{r.y}" for r in o.onscreen() if r.y < 34))]


W_FUN = 3  # (mq_play.c: the game's extras -- a bubble to pop, a star or a coin to grab -- as a widget id)


@inv("only-answers-tappable", "S2", "obs")
def only_answers_tappable(ctx, o):
    """While an order waits, the only things to tap are its answers and the game's controls: no extra bubble, star,
    coin or power-up to pop or grab (the user: 'rising bubbles you can click on that are not the maths')"""
    if o.scene != "play" or o.moment != "play" or o.busy:
        return []
    extra = [t for t in o.hits if t.src == "wg" and t.kind == "choice" and t.id == W_FUN]
    if not extra:
        return []
    return [(f"{o.where()}:extra", f"{len(extra)} tappable extra(s) that are not answers while the order waits on {o.where()}: "
             + " ".join(f"{t.x},{t.y}" for t in extra[:4]))]


@inv("picture-shown", "S2", "obs")
def picture_shown(ctx, o):
    """An order whose question needs its picture (a graph, a clock, coins, 'what did Dab paint?') shows it: in the
    picking games, on the card beside the field (state mq_pic; the user: 'it doesn't show me what they painted')"""
    if o.scene != "play" or o.moment != "play" or o.busy or o.st.get("mq_pic") != "missing":
        return []
    return [(f"{o.where()}:{o.st.get('mq_skill')}", f"the picture of '{o.st.get('mq_q', '')[:60]}' is not on screen in {o.where()}")]


@inv("graph-key", "S2", "obs")
def graph_key(ctx, o):
    """A picture graph or bar graph order shows its key ('each ... = N')."""
    sk = o.st.get("mq_skill", "").lower()
    if o.scene != "play" or o.moment != "play" or not re.search(r"graph|chart|plot", sk):
        return []
    axis = [r for r in o.onscreen() if re.fullmatch(r"\d+", r.s.strip()) and r.x < 40]
    if any(re.search(r"=|each|key", r.s, re.I) for r in o.onscreen()) or len(axis) >= 3:
        return []
    return [(f"{o.where()}:{sk}", f"'{o.st.get('mq_skill')}' order with no key on screen: {o.text()[:200]}")]


@inv("mac-parity", "S2", "obs")
def mac_parity(ctx, o):
    """(Mac) Whatever the 1x screen draws, the Mac canvas draws too (16x16 blocks with detail in the
    1x framebuffer and flat on the Mac), and every 1x text is a Mac text."""
    if ctx.platform != "mac" or o.extra is None:
        return []
    out = []
    miss = o.extra.get("missing", [])
    # (a block on a 1x text is the two fonts' widths, not a missing picture: the pixel font runs wider than the
    #  Mac's; the texts are compared one by one below)
    t1 = o.extra.get("t1x", [])
    miss = [b for b in miss if not any(r.x - 4 < b[0] + 16 and b[0] < r.x + r.w + 4 and r.y - 4 < b[1] + 16 and b[1] < r.y + r.h + 4
                                       for r in t1)]
    if len(miss) >= 6 or any(b[1] < 32 for b in miss):  # (a missing picture is 6+ blocks; fewer: a footprint, below)
        cells = " ".join(f"{b[0]},{b[1]}" for b in miss[:8])
        reg = "top" if all(b[1] < 34 for b in miss) else "bottom" if all(b[1] >= 208 for b in miss) else "middle"
        out.append((f"{o.where()}:blocks-{reg}", f"{len(miss)} 16x16 block(s) drawn at 1x but blank on the Mac on {o.where()}: {cells}"))
    mac = {r.s.strip() for r in (o.extra.get("mac_runs") or o.runs)}
    for r in t1:
        s = r.s.strip()
        cents = s + "\u00a2" in mac or (s == "c" and any(m.endswith("\u00a2") for m in mac))  # (a cent sign: drawn at 1x, a glyph on the Mac)
        if s and 0 <= r.x < 320 and 0 <= r.y < 240 and r.x + r.w <= 320 and s not in mac and not cents:
            out.append((f"{o.where()}:text:{re.sub(r'[0-9]+', 'N', s)[:20]}", f"1x text '{s}' at {r.x},{r.y} not drawn on the Mac ({o.where()})"))
    return out


@inv("mac-footprint", "S3", "obs")
def mac_footprint(ctx, o):
    """(Mac) A picture's vector twin covers about what its 1x pixel art covers: two to five 16x16 blocks of the
    1x art outside the Mac's (a sprite drawn a little larger or offset in its box; the render audit's
    substitution check measures it per sprite) -- seen, never blocking."""
    if ctx.platform != "mac" or o.extra is None:
        return []
    t1 = o.extra.get("t1x", [])
    miss = [b for b in o.extra.get("missing", []) if b[1] >= 32 and not any(
        r.x - 4 < b[0] + 16 and b[0] < r.x + r.w + 4 and r.y - 4 < b[1] + 16 and b[1] < r.y + r.h + 4 for r in t1)]
    if 2 <= len(miss) <= 5:
        return [(f"{o.where()}:footprint", f"{len(miss)} 16x16 block(s) of 1x art outside the Mac's picture on {o.where()}: "
                 + " ".join(f"{b[0]},{b[1]}" for b in miss))]
    return []


# ----------------------------------------------------------------- transitions
# screens that stay put on their own (nothing times out of them): a scroll there must leave them as they are
WHEEL_STILL = ("title", "map", "world", "room", "stickers", "parent", "shop", "book", "showoff", "wardrobe", "arcade",
               "chapter")


@inv("wheel-acts", "S2", "act")
def wheel_acts(ctx, b, a, o):
    """The scroll wheel (a mouse's or a trackpad's) only scrolls or adjusts the widget under the pointer: it
    never changes the screen -- no arrows, no swipe, no "back", no walking into the town's buildings (the
    user: "the mouse scroll button does weird stuff")."""
    if a.kind != "wheel" or b.scene == o.scene or b.scene not in WHEEL_STILL:
        return []
    return [(f"{b.scene}->{o.scene}:wheel", f"a scroll of {a.extra} at {a.x},{a.y} left {b.where()} for {o.where()}")]


@inv("arrow-exits", "S2", "act")
def arrow_exits(ctx, b, a, o):
    """Arrow keys move within a screen; they never leave it."""
    if a.kind != "key" or a.arg not in ARROWS or b.scene == o.scene:
        return []
    if DEPTH.get(o.scene, 2) > DEPTH.get(b.scene, 2):
        return []  # (walking into a place with the arrows is moving, not leaving)
    if b.scene == "name":
        return []
    return [(f"{b.scene}->{o.scene}:{a.arg}", f"key {a.arg} left {b.where()} for {o.where()}")]


@inv("back-only-via-back", "S2", "act")
def back_only_via_back(ctx, b, a, o):
    """Only back (CLEAR, or the soft key labelled back) goes back: a digit, ENTER or DEL never exits
    a screen to the town or the title."""
    if a.kind != "key" or b.scene == o.scene or DEPTH.get(o.scene, 2) >= DEPTH.get(b.scene, 2):
        return []
    if a.arg in ("CLEAR",) or a.arg.startswith("F"):
        return []
    if b.scene in ("done", "feedback", "example", "intro", "surprise", "complete", "chapter", "break", "offer", "name"):
        return []  # (ENTER continues out of these on purpose)
    return [(f"{b.scene}->{o.scene}:{a.arg}", f"key {a.arg} went back from {b.where()} to {o.where()}")]


@inv("crash-scene", "S1", "act")
def bad_scene(ctx, b, a, o):
    """The module is always on a known scene, with a player once one is chosen."""
    if o.scene in ("?", ""):
        return [("noscene", f"no mq_scene after {a}")]
    if b.st.get("mq_player") and not o.st.get("mq_player") and o.scene not in ("title", "name"):
        return [("lost-player", f"the player vanished after {a} on {b.where()}")]
    return []


@inv("rewards-monotone", "S1", "act")
def rewards_monotone(ctx, b, a, o):
    """Stars, stickers and bosses never go down (coins only by spending: in the shop or wardrobe)."""
    out = []
    if b.st.get("mq_player") != o.st.get("mq_player") or a.kind in ("setup",):
        return []
    for k in ("mq_stars", "mq_stickers", "mq_bosses"):
        try:
            if int(o.st.get(k, 0)) < int(b.st.get(k, 0)):
                out.append((f"{k}-down", f"{k} {b.st.get(k)} -> {o.st.get(k)} after {a} on {b.where()}"))
        except ValueError:
            pass
    return out


# the town's street: the buildings stand above this row (logical y); the street and the pavement below it
TOWN_STREET_Y = 18 + 122 + 14
SOFTKEY_Y = 18 + 206  # the soft-key bar (MQ_Y0 + MQ_H)


def _explicit_enter(a):
    """an input that asks to go in: ENTER / UP, a tap on a building (above the street), or a tap on the
    "go in!" bubble beside her (the bot names it by its label)"""
    if a.kind == "key":  # (DOWN opens a friend's news: the chapter, asked for)
        return a.arg in ("ENTER", "UP", "DOWN")
    if a.kind == "tap":  # (the soft keys under the screen -- home, book, arcade -- are asked-for places)
        return a.y < TOWN_STREET_Y or a.y >= SOFTKEY_Y or "go in" in str(a.arg or "")
    return False


def screen_key(st):
    """what she is looking at: the scene, the game's moment, the order, an overlay over it, a friend's line"""
    return (st.get("mq_scene"), st.get("mq_moment"), st.get("mq_q"), st.get("mq_run"), st.get("mq_teach"),
            st.get("mq_notebook"), st.get("mq_bulb"), st.get("mq_chapter"))


@inv("press-two-acts", "S2", "press")
def press_two_acts(ctx, b, mid, a, o):
    """One press acts once: when the finger landing changed the screen (a pick on DOWN, a beat ending, a timer), its
    release on the new screen does nothing there -- not 'next', not 'go on', not an answer (the user: 'it flips
    through a screen and jumps to another without giving me a chance to do the first question')"""
    k0, k1, k2 = screen_key(b.st), screen_key(mid), screen_key(o.st)
    if k0 == k1:
        return []
    acted = k1 != k2 or any(o.st.get(k) != mid.get(k) for k in ("mq_combo", "mq_input", "mq_stars"))
    if not acted:
        return []
    def w(st):
        return f"{st.get('mq_scene')}/{st.get('mq_moment') or '-'}" + ("+teach" if st.get("mq_teach") else "")
    return [(f"{w(b.st)}>{w(mid)}>{w(o.st)}",
             f"one press acted twice after {a!r}: its DOWN went {w(b.st)} -> {w(mid)}, its release then {w(mid)} -> {w(o.st)} "
             f"('{mid.get('mq_q', '')[:30]}' -> '{o.st.get('mq_q', '')[:30]}')")]


@inv("order-shown-kept", "S2", "session")
def order_shown_kept(ctx, *args):
    """An order that reached her screen stays until an input begun on it (an answer, a deliberate back or exit, help
    that comes back to it): never replaced by a timer, a beat, an event, or the release of a press begun before it
    (bot.py Session.track_shown checks it at every look, the moment a finger is down too)"""
    return []


@inv("order-identity", "S2", "act")
def order_identity(ctx, b, a, o):
    """An order stays until she answers it, skips it or leaves: nothing else -- a 'show me', a hint, a beat, a toast
    -- swaps it for another (the user: 'it shows me a problem, then flips to another one I had not answered')"""
    if b.scene != "play" or o.scene != "play" or a.kind == "setup":
        return []
    if b.st.get("mq_run") != o.st.get("mq_run") or not b.st.get("mq_q") or not o.st.get("mq_q"):
        return []  # (answered: the run moved on)
    if (b.st.get("mq_q"), b.st.get("mq_answer")) == (o.st.get("mq_q"), o.st.get("mq_answer")):
        return []
    if o.st.get("mq_combo") != b.st.get("mq_combo") or o.moment in ("why", "cheer") or b.moment in ("why", "cheer"):
        return []  # (an answer judged)
    return [(f"{o.where()}:flip", f"the order changed without an answer after {a!r}: '{b.st.get('mq_q', '')[:40]}' -> "
             f"'{o.st.get('mq_q', '')[:40]}' on {o.where()}")]


@inv("starts-in-town", "S2", "act")
def starts_in_town(ctx, b, a, o):
    """After the title (a player picked) and after the name of a new one, she is in her town (the daily gift box
    first is fine), never dropped into a game or a world (the user: 'it should always start outside')"""
    if b.scene not in ("title", "name") or o.scene in (b.scene, "map", "surprise", "name", "parent", "title") or a.kind == "setup":
        return []
    return [(f"{b.scene}->{o.scene}", f"from the {b.scene} screen she landed in {o.where()}, not her town")]


@inv("read-time", "S2", "act")
def read_time(ctx, b, a, o):
    """A text that goes (or moves the game on) by itself is shown at least its reading time unless she tapped
    (mq_read.c: the module counts every such text against mq_read_ms; `state mq_read` = too soon, all, the last
    too-soon one's ms shown and needed)"""
    try:
        before, after = int(str(b.st.get("mq_read", "0")).split()[0]), str(o.st.get("mq_read", "0")).split()
    except (ValueError, IndexError):
        return []
    if not after or int(after[0]) <= before:
        return []
    got, need = (after[2], after[3]) if len(after) >= 4 else ("?", "?")
    return [(f"{o.scene}:{o.moment}", f"a text on {o.where()} went by itself after {got} ms, its reading time is {need} ms")]


@inv("dismiss-stays", "S2", "act")
def dismiss_stays(ctx, b, a, o):
    """Closing a message (done, close, ok, got it, x) only closes it: she stays exactly where she was -- the scene
    does not change (the user: 'a star fell from the sky ... if I click on it, I end up leaving the shop')"""
    if a.kind != "tap" or a.y >= SOFTKEY_Y or b.scene == o.scene or b.scene in ("name", "title", "quiz", "parent"):
        return []
    if not re.fullmatch(r"\s*(done|close|ok!?|got it!?|x|later|not now)\s*", str(a.arg or ""), re.I):
        return []
    return [(f"{b.scene}->{o.scene}:{a.arg}", f"'{a.arg}' on {b.where()} closed a message and took her to {o.where()}")]


@inv("shop-stays", "S2", "act")
def shop_stays(ctx, b, a, o):
    """The shop is hers: entering it reaches it (nothing over it), and nothing but her own way out leaves it -- a tap
    inside it (closing a message, picking a page, buying) keeps her in the shop (the user: 'a star fell from the sky;
    I click it and end up leaving the shop')"""
    if b.scene != "shop" or o.scene == "shop" or a.kind != "tap" or o.scene in ("title", "parent"):
        return []
    if a.y >= SOFTKEY_Y or re.search(r"back|home|town|close", str(a.arg or ""), re.I):
        return []
    return [(f"shop->{o.scene}", f"a tap at {a.x},{a.y} ({a.arg}) in the shop took her to {o.where()}")]


@inv("walk-enters", "S2", "act")
def walk_enters(ctx, b, a, o):
    """Walking past buildings never changes the scene: a tap on the street or the pavement, the arrows (held:
    the keyboard's repeat), a drag or a swipe only walk or look; she goes in only by an explicit action -- a
    tap on a building or its "go in!" bubble, or ENTER / UP at a door (the user's report). The town's last
    input decides: an entry that comes when she arrives counts as asked only after an explicit one."""
    if b.scene == "map" and a.kind != "setup":
        ctx.__dict__["_town_enter_asked"] = _explicit_enter(a)
    if b.scene != "map" or o.scene == "map" or o.scene in ("title", "parent") or a.kind == "setup":
        return []
    if a.kind == "key" and (a.arg == "CLEAR" or a.arg.startswith("F")):
        return []
    if ctx.__dict__.get("_town_enter_asked"):
        return []
    return [(f"map->{o.scene}:{a.kind}", f"{a!r} on the street took her into {o.where()} without asking")]


def _cards(st):
    """the cards of this screen (state mq_cards: "ID:ARG=Name;..."): {(id, arg): name}"""
    out = {}
    for c in (st.get("mq_cards") or "").split(";"):
        if "=" in c and ":" in c:
            k, name = c.split("=", 1)
            i, a = k.split(":", 1)
            try:
                out[(int(i), int(a))] = name
            except ValueError:
                pass
    return out


@inv("card-lands", "S1", "act")
def card_lands(ctx, b, a, o):
    """A card lands where it says: a world's "What shall we play?" card starts the game it names (the user's
    report: "Shop Till" and "Pet Shop" in the Shop started Bubble Pop), the finale card the finale, an arcade
    card its game. Checked on the tap that hit the card (the screen's tap targets before it)."""
    if a.kind != "tap" or b.scene not in ("world", "arcade") or o.scene == b.scene:
        return []
    cards = _cards(b.st)
    hit = [t for t in b.hits if t.src == "btn" and (t.id, t.arg) in cards and t.x <= a.x < t.x + t.w and t.y <= a.y < t.y + t.h]
    if not hit:
        return []
    want, got = cards[(hit[0].id, hit[0].arg)], o.st.get("mq_here", "")
    if o.scene == "play" and want != "finale" and o.mech and o.mech != want:
        got = o.mech
    if o.scene in ("play", "factgame", "action") and got != want:
        return [(f"{b.scene}:{want}", f"the {want!r} card on {b.where()} (world {b.st.get('mq_world')}) landed on "
                                      f"{o.where()} ({got or 'not the game she picked'})")]
    return []


@inv("round-game", "S1", "obs")
def round_game(ctx, o):
    """A round she picked on the world screen stays in that game: every order of it is played there (the engine
    picks only maths the game plays)."""
    here = o.st.get("mq_here", "")
    if o.st.get("mq_together") == "1":
        return []  # (help together after misses: a game that cannot be set to the answer retells this one order as a bubble)
    if o.scene == "play" and here and here != "finale" and o.mech and o.mech != here:
        return [(f"round:{here}", f"a round of {here} she picked shows an order in {o.mech}")]
    return []


HOME_STORY = ("room", "wardrobe", "showoff")
STORY_GO_ID = 119  # HB_STORY (mq.h): arg 1 "go see!" / "go!", arg 0 "later"
OUTSIDE = ("play", "quiz", "intro", "world", "feedback", "example", "offer", "factgame", "action")


def story_of(st):
    """state mq_story: stage, home seconds, the home time (s, 0 off), the errand (world + 1), the card"""
    try:
        return [int(v) for v in (st.get("mq_story") or "").split()] + [0] * 5
    except ValueError:
        return [0] * 5


@inv("pay-uninterrupted", "S1", "act")
def pay_uninterrupted(ctx, b, a, o):
    """Paying at a counter (the store's or dress-up's) is never taken away: while she pays the scene stays (CLEAR
    puts the coins back first, then closes the counter -- the user's rule and CR-12: no friend at the door, no home
    story mid-purchase)"""
    if b.st.get("mq_pay", "off") in ("off", "") or o.scene == b.scene or o.scene in ("title", "parent"):
        return []
    if a.kind == "setup":  # (a test hook of the strategy)
        return []
    return [(f"pay:{b.scene}->{o.scene}", f"paying on {b.where()} ({b.st.get('mq_pay')}), {a.kind} {a.arg!r} took her to "
                                          f"{o.where()}")]


@inv("story-calm", "S2", "act")
def story_calm(ctx, b, a, o):
    """The home story takes her outside only at a calm moment (mq_homestory.c): her own touch, drag or key calls the
    walk-out off, so an input of hers in her room or dress-up never lands her outside -- except a tap on the
    friend's card's own "go see!" or "go!" button"""
    if b.scene not in HOME_STORY or o.scene not in OUTSIDE or a.kind == "setup" or story_of(b.st)[0] < 2:
        return []
    if a.kind == "tap" and any(t.src == "btn" and (t.id, t.arg) == (STORY_GO_ID, 1) and t.x <= a.x < t.x + t.w and
                               t.y <= a.y < t.y + t.h for t in b.hits):
        return []  # ("go see!" / "go!" on the friend's card)
    return [(f"{b.scene}:{a.kind}", f"a {a.kind} ({a.arg!r}) in {b.where()} -- the home story at stage "
                                    f"{story_of(b.st)[0]} -- took her to {o.where()}: she was busy, not calm")]
