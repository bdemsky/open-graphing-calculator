"""How a kid answers in each game, with real inputs only (taps, drags, the wheel, arrow keys).

The target comes from the engine (the debug line of `mq hits`: the answer `ans` in the game's own
encoding, a wrong one `wrongv`, the right option); the bot then works the game's controls until
the game shows that value and presses the game's action button -- exactly what a child does. If the
controls cannot reach the value, that is reported by the caller (the answer could not be given by
hand), never papered over.

answer_by_hand(s, o, right) -> (obs_after, how) or (None, why)
"""
import re

from drivers import Target


def _act(s, kind, *a, frames=1, label=""):
    from bot import Action
    if kind == "key":
        return s.do(Action("key", a[0]), frames=frames, check=False)
    if kind == "tap":
        return s.do(Action("tap", label, a[0], a[1]), frames=frames, check=False)
    if kind == "drag":
        return s.do(Action("drag", label, extra=a), frames=frames, check=False)
    if kind == "wheel":
        return s.do(Action("wheel", label, a[0], a[1], a[2]), frames=frames, check=False)


def answer_widgets(o):
    """the widgets that are the answer options (not the decoration bubbles of the same kind): the id
    whose args cover 0..nopt-1, the one with the fewest extras"""
    g = o.hits.game or {}
    n = g.get("nopt", 0)
    groups = {}
    for t in o.hits:
        if t.kind == "choice":
            groups.setdefault(t.id, []).append(t)
    best = None
    for id_, ts in groups.items():
        args = {t.arg for t in ts}
        if n and not set(range(n)) <= args:
            continue
        if best is None or len(ts) < len(groups[best]):
            best = id_
    return groups.get(best, [])


def _game(s):
    return s.d.hits().game or {}


def _button(o, label_re=None):
    for t in o.hits:
        if t.src == "wg" and t.kind == "button" and t.id == 1 and (not label_re or re.search(label_re, t.label)):
            return t
    return None


def climb(s, field, target, inc, dec, n=40):
    """press inc / dec until the game's field shows target (cyclic fields too)"""
    g = _game(s)
    last_dir = None
    stuck = 0
    for _ in range(n):
        v = g.get(field)
        if v is None:
            return False
        if v == target:
            return True
        k = inc if v < target else dec
        if last_dir and k != last_dir and stuck > 1:  # cyclic: keep going the same way
            k = last_dir
        _act(s, "key", k)
        g2 = _game(s)
        if g2.get(field) == v:
            stuck += 1
            if stuck > 3:
                k = dec if k == inc else inc
                _act(s, "key", k)
                g2 = _game(s)
                if g2.get(field) == v:
                    return False
        last_dir = k
        g = g2
    return g.get(field) == target


def answer_by_hand(s, o, right):
    g = o.hits.game or {}
    mech = o.mech
    if not g:
        return None, "nogame"
    target = g.get("ans") if right else g.get("wrongv")
    if target is None:
        return None, "noans"
    nopt = g.get("nopt", 0)
    # 1. games with answer options: tap (or drag to) the option
    if nopt:
        r = g["right"]
        if r < 0:
            return None, "noright"
        idx = [r] if right else [i for i in range(nopt) if i != r and not (g["gone"] >> i) & 1][:1]
        if not idx:
            return None, "nocand"
        i = idx[0]
        drags = [t for t in o.hits if t.kind == "drag"]
        drops = [t for t in o.hits if t.kind == "drop" and t.arg == i]
        if drags and drops and mech == "Sort Slide":
            a, b = drags[0].center, drops[0].center
            return _act(s, "drag", a[0], a[1], b[0], b[1], frames=2, label="drag to the answer"), "drag"
        # the kid taps the thing that shows the value (holes, gates and bubbles may hold any option)
        val = str(g["opts"][i])
        aw = answer_widgets(o)
        def label_of(t):  # the text nearest the widget's centre, inside it (not a key hint at its corner)
            cx, cy = t.center
            ins = [r for r in o.runs if t.x <= r.x + r.w // 2 <= t.x + t.w and t.y <= r.y + r.h // 2 <= t.y + t.h
                   and abs(r.x + r.w / 2 - cx) < t.w / 3]
            return min(ins, key=lambda r: abs(r.x + r.w / 2 - cx) + abs(r.y + r.h / 2 - cy)).s.strip() if ins else None
        shows = [t for t in aw if label_of(t) == val]
        if mech == "Peekaboo":  # (holes, not options: wait until a friend holding the value pops up)
            for _ in range(30):
                if len(shows) == 1:
                    break
                o = s.wait(4)
                if o.scene != "play" or o.moment != "play" or o.busy:
                    return None, "interrupted"
                aw = answer_widgets(o)
                shows = [t for t in aw if label_of(t) == val]
            if len(shows) != 1:
                return None, "notshown"
        by_arg = [t for t in aw if t.arg == i]
        ch = shows if len(shows) == 1 else by_arg
        if not ch:
            return None, "nochoice"
        x, y = ch[0].center
        return _act(s, "tap", x, y, frames=2, label="the right answer" if right else "a wrong answer"), "tap"
    # 2. games set with the arrows, then their action button
    ok = None
    if mech == "Hop Along":
        if g.get("mode") == 1:
            o4 = g.get("opt4", [])
            want = 0 if (o4 and o4[0] == target) else 1
            ok = climb(s, "aim", want, "RIGHT", "LEFT")
        else:
            ok = climb(s, "aim", target, "RIGHT", "LEFT", n=80)
    elif mech == "Sky Fling":
        ok = climb(s, "aim", target, "RIGHT", "LEFT", n=80)
    elif mech == "Build It":
        ok = climb(s, "bw" if g.get("fixh") else "bh", target, "RIGHT" if g.get("fixh") else "UP",
                   "LEFT" if g.get("fixh") else "DOWN")
        if not ok:  # (the other pair of arrows)
            ok = climb(s, "bw" if g.get("fixh") else "bh", target, "UP" if g.get("fixh") else "RIGHT",
                       "DOWN" if g.get("fixh") else "LEFT")
    elif mech == "Clock Tower":
        hh, mm = target // 100, target % 100
        okm = False
        for _ in range(70):
            v = _game(s).get("value", 0)
            if v % 100 == mm:
                okm = True
                break
            _act(s, "key", "RIGHT" if (mm - v % 100) % 60 <= 30 else "LEFT")
        okh = False
        for _ in range(14):
            v = _game(s).get("value", 0)
            if v // 100 == hh:
                okh = True
                break
            _act(s, "key", "UP")
        ok = okm and okh and _game(s).get("value") == target
    elif mech == "Treasure Dig":
        ok = climb(s, "px", target // 100, "RIGHT", "LEFT") and climb(s, "py", target % 100, "UP", "DOWN")
    elif mech in ("Shop Till", "Cake Kitchen"):
        keys = {t.label: t for t in o.hits if t.kind == "key"}
        digits = str(abs(target))
        for ch in digits:
            if ch not in keys:
                return None, "nokey"
            x, y = keys[ch].center
            _act(s, "tap", x, y, label=f"key {ch}")
        ok = True
    elif mech == "Treasure Heist":
        wheels = sorted([t for t in o.hits if t.kind == "wheel"], key=lambda t: t.x)
        want = str(abs(target)).zfill(len(wheels))
        if len(want) > len(wheels):
            return None, "nowheels"
        ok = True
        for i, w in enumerate(wheels):
            for _ in range(11):
                st = s.d.state()
                if st.get("mq_notebook") or st.get("mq_teach") or st.get("mq_moment") != "play":
                    return None, "interrupted"
                lock = st.get("mq_lock", "")
                if i < len(lock) and lock[i] == want[i]:
                    break
                x, y = w.center
                _act(s, "wheel", x, y, -1, label="roll")
            lock = s.d.state().get("mq_lock", "")
            ok = ok and i < len(lock) and lock[i] == want[i]
    else:
        return None, "unsupported"
    o2 = s.observe()
    if not ok:
        if o2.busy or o2.moment != "play" or o2.scene != "play":
            return None, "interrupted"
        return None, f"cannot-set:{mech}"
    b = _button(o2)
    if not b:
        return _act(s, "key", "ENTER", frames=2), "keys+enter"
    x, y = b.center
    return _act(s, "tap", x, y, frames=2, label=b.label), "set+" + b.label
