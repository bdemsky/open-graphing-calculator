#!/usr/bin/env python3
"""Sparkle Town playtest bot: player strategies + invariants on the simulator and the Mac page.

    python3 tests/bot/bot.py quick                 (make bot)       a few seeds, every strategy, both platforms
    python3 tests/bot/bot.py long                  (make bot-long)  many seeds, minutes of play each
    python3 tests/bot/bot.py run --strategy monkey --platform sim --seeds 1-5 --frames 6000
    python3 tests/bot/bot.py replay tests/bot/regressions/XXXX.json   re-run a saved repro
    python3 tests/bot/bot.py list                  the invariants and strategies

Findings go to --out (default build/bot): findings.json, a screenshot and a repro script per bug
(seed + inputs, minimised on the simulator), and the regression candidates in tests/bot/regressions/
when --save-regressions is given.
"""
import argparse
import hashlib
import json
import math
import os
import random
import re
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from drivers import FW, Crash, MacDriver, SimDriver, Target  # noqa: E402
import invariants as I  # noqa: E402
import hands  # noqa: E402

SEV_ORDER = {"S1": 0, "S2": 1, "S3": 2, "S4": 3}
HERE = os.path.dirname(os.path.abspath(__file__))


class Action:
    __slots__ = ("kind", "arg", "x", "y", "extra")

    def __init__(self, kind, arg="", x=0, y=0, extra=None):
        self.kind, self.arg, self.x, self.y, self.extra = kind, arg, x, y, extra

    def __repr__(self):
        if self.kind in ("tap", "hold"):
            return f"{self.kind} {self.x},{self.y}" + (f" ({self.arg})" if self.arg else "")
        return f"{self.kind} {self.arg}".strip()


class Finding:
    def __init__(self, inv, sev, sig, msg, platform, strategy, seed, script, shot):
        self.inv, self.sev, self.sig, self.msg = inv, sev, sig, msg
        self.platform, self.strategy, self.seed, self.script, self.shot = platform, strategy, seed, list(script), shot
        self.count, self.min_script, self.platforms, self.examples = 1, None, {platform}, [msg]

    def key(self):
        return f"{self.inv}|{self.sig}"

    def to_json(self):
        return dict(inv=self.inv, sev=self.sev, sig=self.sig, msg=self.msg, platform=self.platform, platforms=sorted(self.platforms),
                    strategy=self.strategy, seed=self.seed, count=self.count, shot=self.shot, script=self.script,
                    min_script=self.min_script, examples=self.examples[:5])


class Findings:
    def __init__(self, out):
        self.out = out
        self.by_key = {}
        os.makedirs(os.path.join(out, "shots"), exist_ok=True)

    def add(self, sess, inv, sev, sig, msg):
        k = f"{inv}|{sig}"
        f = self.by_key.get(k)
        if f:
            f.count += 1
            f.platforms.add(sess.d.platform)
            if len(f.examples) < 5 and msg not in f.examples:
                f.examples.append(msg)
            return f, False
        h = hashlib.sha1(k.encode()).hexdigest()[:10]
        shot = os.path.join(self.out, "shots", f"{sess.d.platform}_{h}.png")
        try:
            sess.d.shot(shot)
        except Exception:
            shot = None
        f = Finding(inv, sev, sig, msg, sess.d.platform, sess.strategy, sess.d.seed, sess.d.script, shot)
        self.by_key[k] = f
        print(f"  [{sev}] {inv}: {msg[:160]}", flush=True)
        return f, True

    def save(self):
        fs = sorted(self.by_key.values(), key=lambda f: (SEV_ORDER[f.sev], -f.count, f.inv))
        json.dump([f.to_json() for f in fs], open(os.path.join(self.out, "findings.json"), "w"), indent=1)
        return fs


# ------------------------------------------------------------------ a session: one player on one driver
class Session:
    def __init__(self, d, findings, strategy, check_every=1):
        self.d, self.F, self.strategy = d, findings, strategy
        self.platform = d.platform
        self.frame = 0
        self.problem_text = ""
        self.last_q = None
        self.obs = None
        self.check_every = check_every
        self.n_act = 0
        self.last_change_frame = 0
        self.last_sig = None
        self.moved = {}  # mech -> True when its items were seen moving
        self.mash = dict(rounds=0, right=0, expect=0.0, coins=0, stars=0, late_coins=0)
        self.mac_scene_seen = set()
        # real presses: a tap is often a finger down, a moment, the finger up (the bot looks in between)
        self.split_p = 0.5
        self.srng = random.Random(f"split-{strategy}-{getattr(d, 'seed', 0)}")
        self.shown = None  # the order on her screen: [key, inputs begun on it, mech, text]

    # the order she can see (and answer): a play moment, nothing over it
    @staticmethod
    def shown_key(st):
        if st.get("mq_scene") != "play" or st.get("mq_moment") != "play" or not st.get("mq_q"):
            return None
        if st.get("mq_teach") or st.get("mq_notebook") or st.get("mq_bulb") == "1":
            return None
        return (st.get("mq_q"), st.get("mq_q_more"), st.get("mq_answer"), st.get("mq_run"))

    def track_shown(self, st, how):
        """order-shown-kept: an order that reached the screen stays until an input begun on it (an answer, back, help)"""
        k = self.shown_key(st)
        if self.shown and self.shown[5] is not self.d.script:
            self.shown = None  # (a new boot: the explorer starts its path again)
        if k is None or (self.shown and k == self.shown[0]):
            return
        bot_lines = self.shown and any(l.split(" ", 1)[0] in ("con", "reload", "power_cycle", "hold", "boot")
                                       for l in self.d.script[self.shown[4]:])
        if self.shown and self.shown[1] == 0 and not bot_lines:  # (a console line of the bot's own: not hers)
            old = self.shown
            self.report("order-shown-kept", "S2", f"{old[2]}->{st.get('mq_mech', '?')}:{how}",
                        f"'{old[3][:40]}' was on the screen and was replaced by '{st.get('mq_q', '')[:40]}' with no input "
                        f"begun on it ({how}; {old[2]} -> {st.get('mq_mech', '?')})")
        self.shown = [k, 0, st.get("mq_mech", "?"), st.get("mq_q", ""), len(self.d.script), self.d.script]

    # observing
    def observe(self, with_px=False, parity=False):
        d = self.d
        d.rec("obs")  # (`mq hits` lays out the game's widgets: a replay must look when the run looked)
        st = d.state()
        runs = d.texts()
        hits = d.hits()
        o = I.Obs(st, runs, hits, d.pixels() if with_px else None, self.frame)
        o.extra = None
        if st.get("mq_scene") == "play" and st.get("mq_q") != self.last_q:
            self.last_q = st.get("mq_q")
            info = d.con("mq info")
            self.problem_text = info[0] if info else ""
        if parity and d.platform == "mac":
            o.extra = d.parity()
            D = __import__("drivers")
            if o.extra.get("t1x") is not None:  # (both from one redraw of the module as it is: no frame in between)
                o.extra["t1x"] = D.parse_runs(str(o.extra["t1x"]).splitlines())
                o.extra["mac_runs"] = [D.Run(t["x"], t["y"], t["w"], t["h"], t["s"]) for t in o.extra.get("macTexts") or []]
            else:
                o.extra["t1x"] = [r for r in D.parse_runs(d.con("text wh"))]
        self.obs = o
        self.track_shown(st, "seen")
        sig = (st.get("mq_scene"), st.get("mq_moment"), st.get("mq_q"), st.get("mq_run"), st.get("mq_input"), st.get("mq_combo"),
               st.get("mq_coins"), st.get("mq_stars"), st.get("mq_toy"), st.get("mq_sel"), st.get("mq_page"))
        if sig != self.last_sig:
            self.last_sig, self.last_change_frame = sig, self.frame
        return o

    def check_obs(self, o):
        for v in I.INVARIANTS:
            if v["when"] != "obs":
                continue
            try:
                res = v["f"](self, o)
            except Exception as e:  # an invariant's own bug: report, do not stop
                res = [("checker-error", f"{v['id']}: {e!r}")]
            for sig, msg in res:
                self.F.add(self, v["id"], v["sev"], sig, msg)

    def check_act(self, b, a, o):
        for v in I.INVARIANTS:
            if v["when"] != "act":
                continue
            for sig, msg in v["f"](self, b, a, o):
                self.F.add(self, v["id"], v["sev"], sig, msg)

    def report(self, inv, sev, sig, msg):
        return self.F.add(self, inv, sev, sig, msg)

    # acting
    def do(self, a, frames=2, check=True):
        """perform an action, let `frames` frames pass, observe, check"""
        b = self.obs or self.observe()
        d = self.d
        mid = None
        if a.kind == "setup":
            self.shown = None  # (the bot's own console line: a new order is not a flip)
        elif self.shown and self.shown_key(b.st) == self.shown[0]:
            self.shown[1] += 1  # an input begun on the order she sees
        if a.kind == "tap" and self.split_p and self.srng.random() < self.split_p:
            how = "touch" if self.platform == "mac" and self.srng.random() < 0.3 else "mouse"
            g = self.srng.randint(1, 4)  # (40-160 ms: a kid's tap)
            d.down(a.x, a.y, how)
            d.rec(f"gap {g}")
            d.gap(g)
            if self.platform == "sim":
                self.frame += g
            mid = d.state()
            self.track_shown(mid, "while a finger was down")
            d.up(a.x, a.y, how)
        elif a.kind == "tap":
            d.tap(a.x, a.y)
        elif a.kind == "hold":
            d.rec(f"hold {a.x} {a.y} {a.extra}")
            d.hold(a.x, a.y, a.extra)
        elif a.kind == "drag":
            x1, y1, x2, y2 = a.extra
            d.drag(x1, y1, x2, y2)
        elif a.kind == "key":
            d.key(a.arg)
        elif a.kind == "wheel":
            d.wheel(a.x, a.y, a.extra)
        elif a.kind == "pinch":
            d.pinch(a.x, a.y, a.extra[0], a.extra[1])
        elif a.kind == "setup":
            d.setup(a.arg)
        if frames:
            d.step(frames)
            self.frame += frames
        self.n_act += 1
        o = self.observe()
        if check:
            self.check_act(b, a, o)
            if mid is not None:
                for v in I.INVARIANTS:
                    if v["when"] == "press":
                        for sig, msg in v["f"](self, b, mid, a, o):
                            self.F.add(self, v["id"], v["sev"], sig, msg)
            if self.n_act % self.check_every == 0 or o.scene != b.scene:
                self.check_obs(o)
                if self.platform == "mac" and o.where() not in self.mac_scene_seen:
                    self.mac_scene_seen.add(o.where())
                    o2 = self.observe(parity=True)
                    self.check_obs_only(o2, "mac-parity")
        if o.st.get("screen", "mathquest") != "mathquest":  # (out in the calculator: back into the game)
            if check:
                self.check_obs_only(o, "left-the-game")
            d.setup("app mathquest")
            d.step(2)
            o = self.observe()
        return o

    def check_obs_only(self, o, inv_id):
        for v in I.INVARIANTS:
            if v["id"] == inv_id:
                for sig, msg in v["f"](self, o):
                    self.F.add(self, v["id"], v["sev"], sig, msg)

    def wait(self, frames):
        self.d.step(frames)
        self.frame += frames
        return self.observe()

    # ------------------------------------------------------------ probes (multi-step invariants)
    # the grown-ups' gate (mq_gate.c): a tap on "grown-up" shows it; a child's taps all over its pad, guesses with
    # ENTER, a held finger and keys never open the grown-ups' screen (only a grown-up's typed answer does)
    def kid_at_gate(self, o, label):
        o2 = self.observe()
        if o2.st.get("mq_gate") not in ("up", "wait"):
            self.report("kid-gate", "S2", f"{o.scene}:{label}", f"a tap on '{label}' on {o.where()} does not show the grown-ups' gate")
            return o2
        for i, (x, y) in enumerate([(216, 45), (254, 79), (292, 113), (216, 147), (254, 181), (292, 45), (100, 100),
                                    (254, 147), (216, 79), (254, 181)]):
            o2 = self.do(Action("tap", "gate pad", x, y), frames=2)
            if i % 3 == 2:
                o2 = self.do(Action("key", "ENTER"), frames=2)
        o2 = self.do(Action("hold", "gate pad", 254, 181, extra=2800), frames=4)
        if o2.scene == "parent":
            self.report("kid-gate", "S1", f"{o.scene}:{label}", f"a child's taps and guesses at the gate on {o.where()} "
                        f"opened the grown-ups' screen")
        o2 = self.do(Action("key", "CLEAR"), frames=2)
        return o2

    def probe_tap_feedback(self, t, what=None):
        """Tapping an interactive element gives visible feedback within 2 frames (pixels in or near
        it change more than its own animation did, or the state / the targets change)."""
        d = self.d
        o0 = self.observe(with_px=True)
        # (the Mac runs in real time: a path replayed to reach a screen can land elsewhere; a target that is not
        #  on the screen now is not this screen's -- tapping it says nothing about this screen's feedback)
        if isinstance(t, Target):
            if not any(u.kind == t.kind and u.id == t.id and u.arg == t.arg for u in o0.hits):
                return True, o0
        elif what and not any(r.s.strip() == what.strip() for r in o0.onscreen()):
            return True, o0
        o1 = self.wait(2)
        p1 = d.pixels()
        x, y = t.center if isinstance(t, Target) else t
        rect = (t.x - 6, t.y - 6, t.w + 12, t.h + 12) if isinstance(t, Target) else (x - 30, y - 14, 60, 28)
        base = I.px_diff(o0.px, p1, rect)
        b = I.Obs(o1.st, o1.runs, o1.hits, p1, self.frame)
        o2 = self.do(Action("tap", what or "", x, y), frames=2)
        p2 = d.pixels()
        resp = I.px_diff(p1, p2, rect)
        whole = I.px_diff(p1, p2)
        sd = I.state_diff(b.st, o2.st)
        hd = [(u.kind, u.id, u.arg, u.value, u.label) for u in b.hits] != [(u.kind, u.id, u.arg, u.value, u.label) for u in o2.hits]
        ok = bool(sd) or hd or resp > max(10, 1.5 * base + 6) or (whole > 3000 and whole > 3 * I.px_diff(o0.px, p1))
        if not ok:
            label = what or (f"{t.src} {t.kind} id {t.id} arg {t.arg}" if isinstance(t, Target) else "?")
            self.report("tap-feedback", "S2" if y >= 218 or (b.scene == "play" and not b.busy) else "S3", f"{b.where()}:{re.sub(r'[0-9]+', 'N', label)[:30]}",
                        f"tap on {label} at {x},{y} on {b.where()}: nothing visible within 2 frames "
                        f"(pixels near it {resp}, its own motion {base}, whole screen {whole}, no state change)")
        return ok, o2

    def probe_way_back(self):
        """Every screen has a way back: CLEAR (up to three times: an overlay first) leaves it."""
        o = self.observe()
        if o.scene in I.ROOTS:
            return True
        s0 = o.scene
        for i in range(3):
            o2 = self.do(Action("key", "CLEAR"), frames=4)
            if o2.scene != s0:
                return True
        self.report("way-back", "S2", f"{s0}", f"no way back from {o.where()}: CLEAR x3 stays on {o2.where()}")
        return False

    def probe_frozen(self, frames=100, every=10):
        """Items that move keep moving: in a game where answers float, fall or drive, none of them
        freezes while the others move, and none sits stuck at the top edge."""
        o = self.observe()
        if o.scene != "play" or o.moment != "play":
            return
        mech = o.mech
        tracks = {}
        f = 0
        while f < frames:
            o = self.wait(every)
            f += every
            if o.scene != "play" or o.moment != "play" or o.busy:
                return
            g = o.hits.game or {}
            gone = g.get("gone", 0)
            for t in hands.answer_widgets(o):
                if (gone >> t.arg) & 1:
                    continue
                tracks.setdefault(t.arg, []).append((t.x, t.y))
        moving = [k for k, v in tracks.items() if len(set(v)) > 2]
        still = [k for k, v in tracks.items() if len(set(v)) == 1 and len(v) >= frames // every - 1]
        if moving:
            self.moved[mech] = True
        if moving and still:
            for k in still:
                x, y = tracks[k][0]
                edge = " at the top edge" if y < 40 else ""
                self.report("frozen-item", "S2", f"{mech}:{'top' if edge else 'mid'}",
                            f"{mech}: answer {k} frozen at {x},{y}{edge} for {frames * 40 // 1000} s while {len(moving)} other(s) move")
        elif not moving and self.moved.get(mech) and tracks:
            self.report("frozen-item", "S2", f"{mech}:all", f"{mech}: no answer moved for {frames * 40 // 1000} s (they do in other rounds)")

    def probe_twists(self):
        """A twist visibly changes the round (night mode darkens it, mirror flips it, ...)."""
        o = self.observe()
        if o.scene != "play" or o.moment != "play" or o.busy:
            return
        d = self.d
        mech = o.mech
        fits = {1: True, 2: mech == "Bubble Pop", 3: mech in ("Bubble Pop", "Cloud Catch"),
                4: mech in ("Bubble Pop", "Gate Dash", "Kart Race", "Peekaboo", "Cloud Catch")}
        if d.platform == "sim":
            return self._twists_by_fork(mech, fits)
        thumb = (lambda: d.thumb()) if d.platform == "mac" else (lambda: d.pixels())
        diff = (lambda a, b: sum(1 for i in range(len(a)) if abs(a[i] - b[i]) > 12) * 4) if d.platform == "mac" else I.px_diff
        d.setup("mq twist 0")
        d.step(2)
        a0 = thumb()
        d.step(2)
        a1 = thumb()
        base = diff(a0, a1)
        for m, ok in fits.items():
            if not ok:
                continue
            d.setup(f"mq twist {m}")
            d.step(2)
            st = d.state()
            if st.get("mq_scene") != "play":
                return
            t = thumb()
            ch = diff(a1, t)
            name = st.get("mq_mod", str(m))
            if ch < max(600, 2 * base):
                self.report("twist-visible", "S2", f"{mech}:{name}",
                            f"twist '{name}' in {mech}: only {ch} px change on {d.platform} (animation alone {base})")
            d.setup("mq twist 0")
            d.step(2)
            a1 = thumb()
        self.observe()

    def fork(self):
        """a second simulator replaying this session's inputs to here (the simulator is deterministic)"""
        f = SimDriver(os.path.join(self.F.out, "fork"))
        f.boot(self.d.seed)
        for line in self.d.script[1:]:
            f.replay(line)
        return f

    def _twists_by_fork(self, mech, fits, frames=120):
        """sim: the same moment twice, with and without the twist, 2.4 s later (the banner gone):
        the twisted screen must differ from the plain one (beyond the banner's band)"""
        def run(m):
            f = self.fork()
            try:
                f.con(f"mq twist {m}")
                f.con(f"mq step {frames}")
                st = f.state()
                return f.pixels(), st
            finally:
                f.close()
        try:
            p0, _ = run(0)
            p0b, _ = run(0)
        except Crash:
            return
        noise = I.px_diff(p0, p0b)
        for m, ok in fits.items():
            if not ok:
                continue
            p1, st = run(m)
            if st.get("mq_scene") != "play":
                continue
            ch = I.px_diff(p0, p1)
            name = st.get("mq_mod", str(m)) or str(m)
            if ch < max(800, 3 * noise):
                self.report("twist-visible", "S2", f"{mech}:{name}",
                            f"twist '{name}' in {mech}: the screen 4.8 s into the twist differs from the same moment without it "
                            f"by only {ch} px (replay noise {noise}) on sim")

    SAVE_KEYS = ["mq_player", "mq_stars", "mq_coins", "mq_stickers", "mq_bosses", "mq_found", "mq_placed", "mq_fluent",
                 "mq_standards", "mq_squishies", "mq_chapters"]

    def _back_in(self, before):
        """after a restart: Sparkle Town again, the same player"""
        d = self.d
        st = d.state()
        if st.get("screen") != "mathquest":
            d.setup("app mathquest")
        d.step(4)
        o2 = self.observe()
        for slot in (0, 1):
            if o2.st.get("mq_player") == before["mq_player"]:
                break
            if o2.scene in ("title", "name") or not o2.st.get("mq_player"):
                d.setup(f"mq player {slot}")
                d.step(2)
                o2 = self.observe()
        return o2

    def probe_save(self):
        """Progress survives a power cycle mid-anything: the calculator's power off and on (sim) or a
        page reload (Mac) keeps everything (S1); a hard reset 3 s after the last input keeps what
        was earned (S2: the session is only written at power-off or after 60 s without input)."""
        d = self.d
        o = self.observe()
        if not o.st.get("mq_player"):
            return
        d.step(10)
        if d.platform == "sim":
            d.setup("wait 2000")  # (the module's own once-a-second save into the session)
        else:
            time.sleep(1.2)
        o = self.observe()
        before = {k: o.st.get(k) for k in self.SAVE_KEYS}
        where = o.where()
        if d.platform == "sim":
            d.setup("power off")
            d.setup("wait 300")
            d.setup("key ON")
            d.setup("wait 300")
            how = "power off and on"
        else:
            d.reload()
            how = "a page reload"
        o2 = self._back_in(before)
        lost = [f"{k} {before[k]} -> {o2.st.get(k)}" for k in self.SAVE_KEYS if before[k] != o2.st.get(k)]
        if lost:
            self.report("save-survives", "S1", f"{how}:{','.join(sorted(l.split()[0] for l in lost))}",
                        f"{how} on {where} lost: " + "; ".join(lost))
        if d.platform != "sim" or self.__dict__.get("_hard_done"):
            return
        self._hard_done = True  # (once a session: it is the same mechanism every time)
        o = self.observe()
        if o.scene != "play":
            return
        # a hard reset (battery out, a crash) after real progress and 3 s without input
        mo = None
        for _ in range(3):
            mo = self.observe()
            if mo.scene == "play" and mo.moment == "play":
                answer_now(self, mo, random.Random(1), True, prefer_tap=1.0)
                self.wait(20)
        d.setup("wait 3000")
        o = self.observe()
        before = {k: o.st.get(k) for k in self.SAVE_KEYS}
        d.setup("reboot crash")
        o2 = self._back_in(before)
        lost = [f"{k} {before[k]} -> {o2.st.get(k)}" for k in self.SAVE_KEYS if before[k] != o2.st.get(k)]
        if lost:
            self.report("save-survives", "S2", "hard-reset:" + ",".join(sorted(l.split()[0] for l in lost)),
                        f"a hard reset 3 s after play on {o.where()} lost: " + "; ".join(lost))

    def probe_town_walk(self):
        """Walking past buildings never goes in (the user's report): on the street, tap-to-walk on the pavement in
        front of every door, the arrows held down (the keyboard's repeat), a drag-walk and a swipe along it --
        the scene stays the town (S2 'walk-enters'). Then, stopped at a door, ENTER goes in (the explicit way)."""
        d = self.d
        o = self.observe()
        if not o.st.get("mq_player") or self.__dict__.get("_walk_done"):
            return
        self._walk_done = True
        d.setup("mq go map")
        d.setup("mq town 140")
        self.wait(4)

        def stays(how):
            o = self.wait(90)  # (long enough to arrive anywhere on screen)
            if o.scene != "map":
                self.report("walk-enters", "S2", how, f"{how} on the street took her into {o.where()}")
                d.setup("mq go map")
                d.setup("mq town 140")
                self.wait(4)

        for x in (40, 100, 160, 220, 280):  # the pavement in front of the doors
            d.tap(x, 200)
            stays("tap-walk")
        for name in ("RIGHT", "LEFT"):  # held: the keyboard's repeat, a frame apart
            for _ in range(25):
                d.key(name)
                d.step(2)
            stays("arrow-repeat")
        d.drag(280, 200, 40, 200, steps=8)  # drag-walk / look along the street
        stays("drag")
        d.drag(300, 120, 20, 120, steps=2)  # a swipe
        stays("swipe")
        d.key("ENTER")  # explicit: in
        self.wait(60)
        d.setup("mq go map")
        self.wait(4)

    def probe_stuck(self, limit_frames=750):
        """Something responds: a strategy that keeps acting never sees 30 s with nothing changing."""
        if self.strategy == "masher":  # (mashing that changes nothing is the game doing its job)
            return False
        if self.frame - self.last_change_frame > limit_frames:
            o = self.obs
            self.report("stuck", "S2", f"{o.where()}:{o.moment}", f"nothing changed for {(self.frame - self.last_change_frame) * 40 // 1000} s "
                        f"of {self.strategy} play on {o.where()} (moment {o.moment}, run {o.st.get('mq_run')})")
            self.last_change_frame = self.frame
            return True
        return False


# ------------------------------------------------------------------ helpers for strategies
KEY_FOR = {"/": ["DIV"], ".": ["DOT"], "-": ["SUB"], "R": ["ALPHA", "9"], "r": ["ALPHA", "9"]}


def keys_for_answer(ans):
    ks = []
    for ch in ans:
        if ch.isdigit():
            ks.append(ch)
        elif ch in KEY_FOR:
            ks += KEY_FOR[ch]
        elif ch in " :":
            continue
        else:
            return None
    return ks


def new_player_by_touch(s, name="Mia"):
    """the first start as a kid does it: the card, a letter or three, ok"""
    o = s.observe()
    if o.scene == "title":
        t = [t for t in o.hits if t.id == 1]
        if t:
            s.do(Action("tap", "new player", *t[0].center), frames=4)
    o = s.observe()
    if o.scene == "name":
        for ch in name.upper():
            r = [r for r in o.runs if r.s == ch]
            if r:
                s.do(Action("tap", ch, r[0].x + r[0].w // 2, r[0].y + r[0].h // 2), frames=2, check=False)
        s.do(Action("key", "ENTER"), frames=6)
    return s.observe()


def rich_player(s, name="Kid"):
    """a player with everything open (for the explorer and the monkey): via test hooks"""
    for line in [f"mq new {name} 2", "mq grant all", "mq coins 900", "mq fun 36000", "mq opengames", "mq day 5"]:
        s.d.setup(line)
    s.d.step(4)
    return s.observe()


def option_target(o, i):
    for t in hands.answer_widgets(o):
        if t.arg == i and t in hands.answer_widgets(o):
            return t
    return None


def round_result(b, o):
    """'right' / 'wrong' / None from two observations around an answer"""
    if o.scene != "play":
        return "right" if o.scene in ("done", "complete") else "left"
    if o.moment == "cheer" or int(o.st.get("mq_combo", 0) or 0) > int(b.st.get("mq_combo", 0) or 0):
        return "right"
    if o.moment in ("why", "help", "offer") or (o.st.get("mq_run") != b.st.get("mq_run") and o.moment == "play"):
        return "wrong" if o.moment != "cheer" else "right"
    if o.st.get("mq_combo") == "0" and b.st.get("mq_combo") not in ("0", None):
        return "wrong"
    return None


def answer_now(s, o, rng, right, prefer_tap=1.0, gap=2):
    """answer the waiting order with the game's own controls (hands.py)"""
    return hands.answer_by_hand(s, o, right)


# ------------------------------------------------------------------ strategies
def play_rounds(s, rng, frames, mode):
    """the kid strategies: start orders (a debug key: `mq level W L`, recorded in the repro) and
    play them with real taps and keys. mode: competent | struggling | masher"""
    places = [(0, 0), (0, 1), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (6, 0), (0, 2), (1, 1), (2, 1), (3, 1), (7, 0), (8, 0)]
    end = s.frame + frames
    judged = dict(right_ok=0, right_bad=0, wrong_ok=0, wrong_bad=0)
    twist_done, frozen_done, save_at = set(), set(), s.frame + rng.randint(800, 2500)
    while s.frame < end:
        w, l = rng.choice(places)
        s.d.setup(f"mq level {w} {l}")
        o = s.wait(rng.randint(20, 80))
        guard = 0
        while o.scene == "play" and s.frame < end and guard < 120:
            guard += 1
            s.probe_stuck()
            if o.moment != "play" or o.busy:
                # cheer / why / help / offer / a teaching moment / the notebook: carry on as a kid would
                want = o.st.get("mq_nb_want") or ""
                if o.st.get("mq_notebook") and want.isdigit() and mode != "masher":
                    # the notebook: she writes the box's digit (a struggling kid slips now and then)
                    dg = int(want) if mode == "competent" or rng.random() < 0.7 else (int(want) + rng.randint(1, 9)) % 10
                    o = s.do(Action("key", str(dg)), frames=rng.randint(6, 20))
                    continue
                k = rng.choice(["ENTER", "ENTER", "tap"]) if mode != "masher" else "tap"
                if k == "tap":
                    nxt = [t for t in o.hits if t.kind == "button"] or list(o.hits)
                    runs = [r for r in o.onscreen() if r.s.strip().lower() in ("next", "ok", "ok!", "done", "go!", "skip", "yes", "no", "continue", "play!")]
                    if runs:
                        r = rng.choice(runs)
                        o = s.do(Action("tap", r.s, r.x + r.w // 2, r.y + r.h // 2), frames=rng.randint(4, 20))
                    elif nxt:
                        o = s.do(Action("tap", "a button", *rng.choice(nxt).center), frames=rng.randint(4, 20))
                    else:
                        o = s.do(Action("key", "ENTER"), frames=10)
                else:
                    o = s.do(Action("key", "ENTER"), frames=rng.randint(6, 30))
                continue
            # an order waits
            if o.mech not in frozen_done and mode == "competent":
                frozen_done.add(o.mech)
                s.probe_frozen()
                o = s.observe()
                if o.scene != "play" or o.moment != "play":
                    continue
            if o.mech not in twist_done and mode == "competent" and rng.random() < 0.5:
                twist_done.add(o.mech)
                s.probe_twists()
                o = s.observe()
                if o.scene != "play" or o.moment != "play":
                    continue
            if s.frame >= save_at:
                save_at = s.frame + rng.randint(1500, 4000)
                s.probe_save()
                if mode == "competent":
                    s.probe_town_walk()
                o = s.observe()
                continue
            if mode == "masher":
                o = mash_round(s, o, rng)
                continue
            think = rng.randint(30, 90) if mode == "competent" else rng.randint(60, 400)
            o = s.wait(think)
            if o.scene != "play" or o.moment != "play" or o.busy:
                continue
            right = True if mode == "competent" else rng.random() < 0.35
            b = o
            o2, how = answer_now(s, o, rng, right, gap=2)
            if o2 is None:
                if how == "noright":
                    s.report("no-right-option", "S1", f"{o.where()}:noright", f"no right option to tap ({o.hits.game}) answer {o.st.get('mq_answer')}")
                elif how.startswith("cannot-set") and right:
                    s.report("controls-reach", "S2", f"{o.where()}", f"{o.mech}: the game's controls never showed the answer "
                             f"'{o.st.get('mq_answer')}' (q '{o.st.get('mq_q')[:50]}'; game {o.hits.game})")
                s.d.setup("mq correct" if right else "mq wrong")  # (cannot be answered by hand: let the hook do it)
                o = s.wait(10)
                continue
            res = None
            for _ in range(10):  # (a caught star or a kart takes a moment to arrive: up to 4 s)
                o = s.wait(10)
                res = round_result(b, o)
                if res:
                    break
            what = f"{how} {'right' if right else 'wrong'} answer '{b.st.get('mq_answer')}' in {b.where()} (q '{b.st.get('mq_q')[:50]}')"
            if right:
                if res == "right":
                    judged["right_ok"] += 1
                elif res == "wrong":
                    judged["right_bad"] += 1
                    s.report("truth-right-accepted", "S1", f"{b.where()}:{how}:{b.st.get('mq_skill')}",
                             f"the right answer was not accepted: {what}; moment {o.moment}, combo {b.st.get('mq_combo')}->{o.st.get('mq_combo')}")
                elif res is None:
                    s.report("answer-no-response", "S2", f"{b.where()}:{how}",
                             f"the game did not take the answer within 4 s: {what}; moment {o.moment}, game {o.hits.game and {k: o.hits.game.get(k) for k in ('gone', 'px', 'aim', 'value')}}")
            else:
                if res == "right":
                    judged["wrong_bad"] += 1
                    s.report("truth-wrong-rejected", "S1", f"{b.where()}:{how}:{b.st.get('mq_skill')}",
                             f"a wrong answer was accepted: {what}")
                elif res == "wrong":
                    judged["wrong_ok"] += 1
            # (hesitation: the struggling kid sometimes just waits, or wanders off with back)
            if mode == "struggling" and rng.random() < 0.1:
                o = s.wait(rng.randint(300, 900))
            if mode == "struggling" and rng.random() < 0.05:
                o = s.do(Action("key", "CLEAR"), frames=6)
        # out of the round: done screens etc.
        o = s.observe()
        for _ in range(6):
            if o.scene in ("map", "world", "title"):
                break
            o = s.do(Action("key", rng.choice(["ENTER", "CLEAR"])), frames=10)
    return judged


def mash_round(s, o, rng):
    """the masher: every answer as fast as it can, without reading (one frame apart)"""
    g = o.hits.game or {}
    b = o
    coins0, stars0 = int(o.st.get("mq_coins", 0) or 0), int(o.st.get("mq_stars", 0) or 0)
    if g.get("nopt"):
        order = list(range(g["nopt"]))
        rng.shuffle(order)
        tries = 0
        for i in order:
            t = option_target(o, i)
            if t:
                o = s.do(Action("tap", "mash", *t.center), frames=1, check=False)
                tries += 1
                if o.scene != "play" or o.moment != "play":
                    break
        s.mash["expect"] += min(1.0, 2.0 / max(1, g["nopt"]))
    else:
        for _ in range(rng.randint(3, 8)):
            o = s.do(Action("key", str(rng.randint(0, 9))), frames=0, check=False)
        o = s.do(Action("key", "ENTER"), frames=1)
    o = s.wait(12)
    res = round_result(b, o)
    s.mash["rounds"] += 1
    gained_c = int(o.st.get("mq_coins", 0) or 0) - coins0
    gained_s = int(o.st.get("mq_stars", 0) or 0) - stars0
    if res == "right":
        s.mash["right"] += 1
    s.mash["coins"] += max(0, gained_c)
    s.mash["stars"] += max(0, gained_s)
    if s.mash["rounds"] > 3 and gained_c > 0:
        s.mash["late_coins"] += gained_c
    return o


def masher_verdict(s):
    m = s.mash
    if m["rounds"] < 6:
        return
    exp, n = m["expect"], m["rounds"]
    if m["late_coins"] > 0:
        s.report("mash-no-reward", "S2", "coins", f"mashing kept earning coins after the guess detector should kick in: "
                 f"{m['late_coins']} coins in rounds 4..{n} ({m['right']}/{n} rounds 'right', {m['stars']} stars)")
    if m["stars"] > 0 and m["right"] > exp + 2 * math.sqrt(max(1.0, exp * 0.5)) + 1:
        s.report("mash-no-reward", "S2", "rate", f"mashing wins more than chance: {m['right']}/{n} rounds (chance about {exp:.1f}), {m['stars']} stars")
    if m["stars"] > 0:
        s.report("mash-no-reward", "S3", "stars", f"mashing earns stars: {m['stars']} stars and {m['coins']} coins over {n} mashed rounds "
                 f"({m['right']} 'right'; chance about {exp:.1f})")


def strat_competent(s, rng, frames):
    new_player_by_touch(s, rng.choice(["Mia", "Zoe", "Ava"]))
    j = play_rounds(s, rng, frames, "competent")
    return j


def strat_struggling(s, rng, frames):
    new_player_by_touch(s, "Lu")
    return play_rounds(s, rng, frames, "struggling")


def strat_masher(s, rng, frames):
    new_player_by_touch(s, "Max")
    play_rounds(s, rng, frames // 2, "masher")
    masher_verdict(s)
    # and random mashing anywhere: rapid hits all over
    end = s.frame + frames // 2
    while s.frame < end:
        for _ in range(rng.randint(5, 15)):
            s.do(Action("tap", "mash", rng.randint(0, 319), rng.randint(0, 239)), frames=0, check=False)
        s.do(Action("key", rng.choice(["ENTER", "1", "2", "F1", "F5", "CLEAR", "LEFT", "RIGHT"])), frames=1)
        s.probe_stuck()
        if s.obs.scene == "parent":
            s.do(Action("key", "CLEAR"), frames=4)
    return {}


MONKEY_KEYS = ["ENTER", "CLEAR", "DEL", "LEFT", "RIGHT", "UP", "DOWN", "F1", "F2", "F3", "F4", "F5", "1", "2", "3", "4", "5", "6",
               "7", "8", "9", "0", "DOT", "SUB", "DIV"]


def strat_monkey(s, rng, frames):
    if rng.random() < 0.5:
        new_player_by_touch(s, "Mo")
    else:
        rich_player(s)
    end = s.frame + frames
    while s.frame < end:
        o = s.obs or s.observe()
        r = rng.random()
        if o.scene == "parent" and rng.random() < 0.7:  # (the grown-ups' corner: leave again soon; no erasing)
            a = Action("key", "CLEAR")
        elif r < 0.30:
            a = Action("tap", "", rng.randint(0, 319), rng.randint(0, 239))
        elif r < 0.50 and len(o.hits):
            t = rng.choice(list(o.hits))
            a = Action("tap", "", *t.center)
        elif r < 0.60:
            x, y = rng.randint(0, 319), rng.randint(20, 220)
            a = Action("drag", "", extra=(x, y, max(0, min(319, x + rng.randint(-120, 120))), max(0, min(239, y + rng.randint(-80, 80)))))
        elif r < 0.85:
            a = Action("key", rng.choice(MONKEY_KEYS))
        elif r < 0.93:
            a = Action("wheel", "", rng.randint(0, 319), rng.randint(20, 220), rng.choice([-3, -1, 1, 2]))
        else:
            a = Action("pinch", "", rng.randint(80, 240), rng.randint(60, 180), (rng.randint(20, 60), rng.randint(60, 200)))
        s.do(a, frames=rng.randint(1, 8))
        s.probe_stuck(1500)
    return {}


def strat_explorer(s, rng, frames, budget=None):
    """BFS over the navigation: every screen reached by tapping every target and labelled text and
    pressing the soft keys; on each new screen the tap-feedback, way-back, arrow and twist probes."""
    seen = {}
    queued = set()
    queue = [[]]
    boots = 0
    budget = budget or max(60, frames // 15)
    d = s.d
    root = rng.choice(["fresh", "rich"]) if budget < 60 else "both"
    roots = ["fresh", "rich"] if root == "both" else [root]

    def screen_key(o):
        return (o.scene, o.mech, o.moment, bool(o.st.get("mq_teach")), bool(o.st.get("mq_notebook")), o.st.get("mq_toy", "")[:6])

    def candidates(o):
        c = []
        for t in o.hits:
            if t.kind in ("area",):
                continue
            c.append(("tap", t))
        for r in o.onscreen():
            if not r.s.strip() or len(r.s) > 18:
                continue
            inside = any(t.x <= r.x + r.w // 2 <= t.x + t.w and t.y <= r.y + r.h // 2 <= t.y + t.h for t in o.hits)
            if r.y >= 218 or inside:
                c.append(("label", r))  # (looks like a button: a soft key, a label on a target)
            elif len(r.s) <= 10:
                c.append(("text", r))  # (might be one: tried, but no feedback is required)
        for k in ["F1", "F2", "F3", "F4", "F5", "ENTER", "LEFT", "RIGHT", "UP", "DOWN"]:
            c.append(("key", k))
        return c

    def replay(root_kind, path):
        nonlocal boots
        d.boot(s.d.seed)
        boots += 1
        s.obs = None
        s.frame = 0
        if root_kind == "fresh":
            new_player_by_touch(s, "Mia")
        else:
            rich_player(s)
        o = s.observe()
        for a in path:
            o = s.do(a, frames=6, check=False)
        return o

    for rk in roots:
        queue = [[]]
        while queue and boots < budget:
            path = queue.pop(0)
            o = replay(rk, path)
            k0 = screen_key(o)
            if k0 in seen and path:
                continue
            seen[k0] = path
            s.check_obs(o)
            if o.scene == "play" and o.moment == "play":
                s.probe_twists()
                o = replay(rk, path)
            cands = candidates(o)
            rng.shuffle(cands)
            changed = False
            for kind, x in cands[:40]:
                if boots >= budget:
                    break
                if changed:
                    o = replay(rk, path)
                    changed = False
                if kind == "key":
                    a = Action("key", x)
                    o2 = s.do(a, frames=4)
                else:
                    label = x.s if kind in ("text", "label") else ""
                    if kind in ("text", "label"):
                        t = Target("text", "label", 0, 0, x.x, x.y, x.w, x.h, label=x.s)
                    else:
                        t = x
                    if "reset" in label.lower() or "erase" in label.lower() or "delete" in label.lower():
                        continue
                    if kind == "text":
                        a = Action("tap", label, *t.center)
                        o2 = s.do(a, frames=4)
                    else:
                        ok, o2 = s.probe_tap_feedback(t, label or None)
                        a = Action("tap", label, *t.center)
                        if "grown" in label.lower() and o2.scene == o.scene:  # (the grown-ups' gate: shown, and a kid stays out)
                            o2 = s.kid_at_gate(o, label)
                k = screen_key(o2)
                if k != k0:
                    changed = True
                    if k not in seen and k not in queued:
                        queued.add(k)
                        queue.append(path + [a])
            # the way back from here
            if o.scene not in I.ROOTS:
                o = replay(rk, path)
                s.probe_way_back()
    s.explored = {f"{k[0]}{'/' + k[1] if k[1] else ''}{'/' + k[2] if k[2] else ''}": len(v or []) for k, v in seen.items()}
    return {"screens": len(seen), "boots": boots}


DN_NAMES = ["penny", "nickel", "dime", "quarter", "dollar", "five"]
DN_VALUE = [1, 5, 10, 25, 100, 500]
COUNTER_DROP = (130, 95)  # (the counter's middle: where a dragged coin lands)


def _exact_ways(price, hand, fewest):
    """coins of hand (counts per DN) making exactly price: the fewest, or the most small ones (None: no way)"""
    best = [None]

    def rec(k, rem, cur):
        if rem == 0:
            n = sum(cur)
            if best[0] is None or (n < sum(best[0]) if fewest else n > sum(best[0])):
                best[0] = list(cur)
            return
        if k < 0:
            return
        for n in range(min(hand[k], rem // DN_VALUE[k]), -1, -1):
            cur[k] = n
            rec(k - 1, rem - n * DN_VALUE[k], cur)
        cur[k] = 0

    rec(5, price, [0] * 6)
    return best[0]


def _int_list(v, n=6):
    try:
        xs = [int(x) for x in (v or "").split()]
    except ValueError:
        return [0] * n
    return (xs + [0] * n)[:n]


def strat_shopper(s, rng, frames):
    """a kid at the shop, paying with coins at the counter (mq_purse.c) the way she would: cards and "buy!",
    then coins tapped or dragged from her purse onto the counter -- mostly the right ones, sometimes too few
    or too many, "help me pay" now and then -- the change typed on the keys, the picky cashier's swap. Her
    level moves between purchases (a test hook). Checks: her purse and the counter always make her balance,
    a purchase costs exactly its price, and the counter never gets stuck."""
    for line in ["mq new Penny 2", "mq coins 900", "mq fun 36000", "mq opengames"]:
        s.d.setup(line)
    s.d.step(4)
    s.observe()
    end = s.frame + frames
    bought, opened, idle, last, tried = 0, 0, 0, None, False
    bal0, price0 = None, 0
    while s.frame < end:
        o = s.obs or s.observe()
        st = o.st
        pay = st.get("mq_pay", "off")
        if pay == "off":
            bal0 = None
            if o.scene == "map":
                s.do(Action("key", "F1"), frames=4)
                continue
            if o.scene == "room":
                s.do(Action("key", "F1"), frames=4)
                continue
            if o.scene == "title":  # (her player again)
                s.d.setup("mq player 0")
                s.d.step(2)
                s.observe()
                continue
            if o.scene != "shop":
                s.do(Action("key", "CLEAR"), frames=4)
                continue
            if int(st.get("mq_coins", 0) or 0) < 60:
                s.d.setup("mq coins 900")
            if rng.random() < 0.15:  # (her level moves: before coins, counting, change, the picky cashier)
                lv = rng.choice(["0 0", "1 0", "2 0 0", "2 0 1", "3 5 -1 1", "3 5 0 0", "2 5 1 1"])
                s.d.setup(f"mq pay setup {lv}")
            buy = [t for t in o.hits if t.src == "btn" and t.id == 102]
            if buy and not tried:  # (once: one she has enough of says so, and the card is closed)
                s.do(Action("tap", "buy!", *buy[0].center), frames=3)
                opened, tried = opened + 1, True
                continue
            cards = [t for t in o.hits if t.src == "btn" and t.id == 101]
            close = [t for t in o.hits if t.src == "btn" and t.id == 103]
            tried = False
            if close:
                s.do(Action("tap", "close", *close[0].center), frames=2)
            elif cards:
                c = rng.choice(cards)
                s.do(Action("tap", "card", *c.center), frames=2)
            else:  # (something over the shop: out and in again)
                s.do(Action("key", "CLEAR"), frames=4)
            continue
        mode, phase = (pay.split() + ["", ""])[:2]
        hand, counter = _int_list(st.get("mq_pay_hand")), _int_list(st.get("mq_pay_counter"))
        price, paid, _ = _int_list(st.get("mq_pay_price"), 3)
        coins = int(st.get("mq_coins", 0) or 0)
        if bal0 is None:
            bal0, price0 = coins, price
        if phase == "pay" and sum(n * v for n, v in zip(hand, DN_VALUE)) + \
                sum(n * v for n, v in zip(counter, DN_VALUE)) != coins:
            s.report("purse-conserved", "S1", "counter", f"her purse {hand} and the counter {counter} do not make her balance "
                     f"{coins} on {o.where()} ({pay})")
        if phase == "thanks" and bal0 is not None and coins != bal0 - price0:
            s.report("purse-conserved", "S1", "price", f"a {price0}c purchase took her purse from {bal0} to {coins} ({mode})")
            bal0 = coins + price0
        key = (pay, tuple(hand), tuple(counter), st.get("mq_pay_price"))
        idle = idle + 1 if key == last else 0
        last = key
        if idle > 25:
            s.report("pay-stuck", "S2", f"{mode}/{phase}", f"the counter did not move for 25 inputs ({pay}, hand {hand}, "
                     f"counter {counter}, price {price})")
            s.do(Action("key", "CLEAR"), frames=2)
            s.do(Action("key", "CLEAR"), frames=2)
            idle = 0
            continue

        def label(text):
            return [t for t in o.hits if t.src == "wg" and t.label == text]

        def tap_label(text, frames=2):
            t = label(text)
            if t:
                return s.do(Action("tap", text, *t[0].center), frames=frames)
            return s.do(Action("key", "ENTER"), frames=frames)

        if phase == "pay":
            if sum(counter) == 0:
                r = rng.random()
                if mode == "change" and r < 0.6:
                    over = [k for k in range(6) if hand[k] and DN_VALUE[k] > price]
                    plan = [0] * 6
                    if over:
                        plan[min(over)] = 1
                elif r < 0.85:
                    plan = _exact_ways(price, hand, fewest=(mode == "picky" and rng.random() < 0.7) or rng.random() < 0.5)
                else:  # a kid's slip: a few coins at random
                    plan = [0] * 6
                    for _ in range(rng.randint(1, 4)):
                        k = rng.randrange(6)
                        if hand[k] > plan[k]:
                            plan[k] += 1
                if plan:
                    for k in range(6):
                        for _ in range(plan[k]):
                            o = s.obs or s.observe()
                            t = [t for t in o.hits if t.src == "wg" and t.label == DN_NAMES[k] and t.y > 120]
                            if not t:
                                break
                            if rng.random() < 0.3:
                                cx, cy = t[0].center
                                s.do(Action("drag", DN_NAMES[k], extra=(cx, cy, COUNTER_DROP[0], COUNTER_DROP[1])), frames=1)
                            else:
                                s.do(Action("tap", DN_NAMES[k], *t[0].center), frames=1)
                o = s.obs or s.observe()
                tap_label("pay!")
            elif rng.random() < 0.4 and not any(_int_list(st.get("mq_pay_help"))):
                tap_label("help me pay")
            else:
                helpset = _int_list(st.get("mq_pay_help"))
                for k in range(6):
                    for _ in range(helpset[k]):
                        o = s.obs or s.observe()
                        t = [t for t in o.hits if t.src == "wg" and t.label == DN_NAMES[k] and t.y > 120]
                        if t:
                            s.do(Action("tap", DN_NAMES[k], *t[0].center), frames=1)
                o = s.obs or s.observe()
                tap_label("pay!")
        elif phase == "change":
            want = paid - price if rng.random() < 0.8 else max(0, paid - price + rng.choice([-10, -1, 1, 5]))
            for ch in str(want):
                s.do(Action("key", ch), frames=1)
            s.do(Action("key", "ENTER"), frames=2)
        elif phase == "swap":
            tap_label("swap!" if rng.random() < 0.7 else "no thanks")
        elif phase == "thanks":
            bought += 1
            tap_label("yay!")
        else:
            s.do(Action("key", "CLEAR"), frames=2)
        s.probe_stuck(1500)
    return {"opened": opened, "bought": bought}


def strat_cards(s, rng, frames):
    """every card of every world's "What shall we play?" (and the arcade's) tapped the way she would, each
    round's orders answered for a while: card-lands checks the tap landed in the game the card names,
    round-game that the round stays there (the user's report: Shop Till and Pet Shop started Bubble Pop)."""
    grade = rng.choice([2, 3, 4, 5])
    for line in [f"mq new Cards {grade}", "mq fun 36000", "mq opengames"]:
        s.d.setup(line)
    s.d.step(2)
    s.observe()
    landed = 0
    end = s.frame + frames
    for w in list(range(9)) + ["arcade"]:
        if s.frame >= end:
            break
        s.d.setup("mq go arcade" if w == "arcade" else f"mq world {w}")
        s.d.step(2)
        o = s.observe()
        cards = I._cards(o.st)
        for (cid, carg), name in cards.items():
            if s.frame >= end:
                break
            s.d.setup("mq go arcade" if w == "arcade" else f"mq world {w}")
            s.d.step(2)
            o = s.observe()
            t = [t for t in o.hits if t.src == "btn" and t.id == cid and t.arg == carg]
            if not t:
                continue
            o = s.do(Action("tap", name, *t[0].center), frames=4)
            landed += 1
            for _ in range(rng.randint(2, 6)):  # a few orders of the round
                if o.scene != "play":
                    break
                s.d.setup("mq correct" if rng.random() < 0.8 else "mq wrong")
                s.d.step(30)
                s.d.setup("mq next")
                s.d.step(30)
                o = s.observe()
                s.check_obs(o)
            s.d.setup("mq go map")
            s.d.step(2)
    return {"cards": landed}


GAME_SCENES = ("play", "quiz", "intro", "world", "feedback", "example", "offer")


def _story(st):
    return I.story_of(st)


def _btn(o, id_, arg=None):
    return [t for t in o.hits if t.src == "btn" and t.id == id_ and (arg is None or t.arg == arg)]


def _run(o, text):
    return [r for r in o.onscreen() if r.s.strip().lower() == text]


def strat_homebody(s, rng, frames):
    """a kid who would rather stay home: she moves furniture about and dresses up, again and again (the home time
    is set short: `mq story min 1`). Checks (the home story, mq_homestory.c): it never moves her while a finger is
    down or a thing is dragged (a finger held on the bed for six seconds), never while she pays in dress-up (a
    coin on the counter for eight seconds), and it does lead her outside at a calm moment (S2 'story-never-out' if
    not); the errand's round brings a thank-you and the home time back ('errand-reward'), and "home" on the done
    screen takes her straight back ('errand-home')."""
    for line in ["mq new Hana 2", "mq coins 300", "mq fun off", "mq story min 1", "mq pay setup 1 0", "mq go wardrobe",
                 "mq story time 170"]:
        s.d.setup(line)
    s.d.step(4)
    o = s.observe()
    # dress-up: a rack piece up close, "buy!", a coin on the counter -- then her hands still for eight seconds
    cards = [t for t in o.hits if t.src == "btn" and t.id == 101]
    if cards:
        o = s.do(Action("tap", "rack piece", *cards[0].center), frames=3)
        buy = _btn(o, 102)  # HB_BUY
        if buy:
            o = s.do(Action("tap", "buy!", *buy[0].center), frames=3)
            if o.st.get("mq_pay", "off") != "off":
                s.do(Action("setup", "mq pay put p"), frames=2)
                o = s.wait(200)
                if o.scene != "wardrobe" or o.st.get("mq_pay", "off") == "off" or _story(o.st)[4]:
                    s.report("story-mid-pay", "S1", "wardrobe", f"paying in dress-up with the home story due, after 8 s "
                             f"she was on {o.where()} (pay {o.st.get('mq_pay')}, story {o.st.get('mq_story')})")
        for _ in range(3):
            o = s.do(Action("key", "CLEAR"), frames=3)
    # the room: a finger held down on the bed (dragging it) for six seconds with the walk-out due
    if o.scene != "room":
        s.d.setup("mq go room")
        s.d.step(2)
        o = s.observe()
    if s.platform == "sim":
        s.d.setup("touch 24 142 down")
        s.d.setup("mq step 150")
        s.d.setup("touch 60 142 move")
        s.d.setup("mq step 30")
        s.frame += 180
        o = s.observe()
        if o.scene != "room" or _story(o.st)[4] == 2:
            s.report("story-mid-drag", "S1", "room", f"a finger down dragging the bed, the home story moved: "
                     f"{o.where()} (story {o.st.get('mq_story')})")
        s.d.setup("touch 60 142 up")
        s.d.step(2)
        o = s.observe()
    end = s.frame + frames
    calm_home = 0  # frames spent calm at home with the walk-out due
    outs = 0
    while s.frame < end:
        o = s.obs or s.observe()
        st = o.st
        if o.scene in GAME_SCENES:  # the errand: the friend's round, played (a debug answer: the truth is elsewhere)
            calm_home = 0
            if o.scene == "play" and o.moment == "play" and not o.busy:
                s.do(Action("setup", "mq correct"), frames=30)
                continue
            # a cheer, an acted story, a teaching moment: carry on as a kid would (as play_rounds does)
            runs = [r for r in o.onscreen() if r.s.strip().lower() in ("next", "ok", "ok!", "done", "go!", "skip", "yes",
                                                                       "continue", "play!", "try one", "got it!")]
            btns = [t for t in o.hits if t.kind == "button"]
            if runs:
                r = rng.choice(runs)
                s.do(Action("tap", r.s, r.x + r.w // 2, r.y + r.h // 2), frames=rng.randint(6, 20))
            elif btns and rng.random() < 0.5:
                s.do(Action("tap", "a button", *rng.choice(btns).center), frames=rng.randint(6, 20))
            else:
                s.do(Action("key", "ENTER"), frames=rng.randint(6, 20))
            continue
        if o.scene == "done":
            if outs == 0 or st.get("mq_errand_gift", "0") != "0":
                if st.get("mq_errand_gift", "0") == "0" or _story(st)[1] > 60:
                    s.report("errand-reward", "S2", "done", f"the errand's round ended with no thank-you or the home "
                             f"time not back (gift {st.get('mq_errand_gift')}, story {st.get('mq_story')})")
            outs += 1
            home = _run(o, "home")
            if home:
                o = s.do(Action("tap", "home", home[0].x + home[0].w // 2, home[0].y + home[0].h // 2), frames=4)
                if o.scene != "room":
                    s.report("errand-home", "S2", o.scene, f"'home' on the done screen took her to {o.where()}")
            else:
                s.do(Action("key", "CLEAR"), frames=4)
            s.d.setup("mq story time 170")  # (and she stays home again)
            continue
        if o.scene not in ("room", "wardrobe"):
            s.d.setup("mq go room")
            s.d.step(2)
            s.observe()
            continue
        r = rng.random()
        due = _story(st)[0] >= 3
        if r < 0.45 and o.scene == "room":  # furniture about: a drag from the floor or the wall
            x1, y1 = rng.randint(10, 300), rng.randint(50, 180)
            s.do(Action("drag", "furniture", x1, y1, extra=(x1, y1, rng.randint(10, 300), rng.randint(50, 180))),
                 frames=rng.randint(2, 6))
            calm_home = 0
        elif r < 0.6:
            tr = [t for t in o.hits if t.src == "btn" and t.id in (105, 107, 111, 101, 103)]  # tray, tools, wear, rack, close
            if tr:
                t = rng.choice(tr)
                s.do(Action("tap", t.label or "a thing", *t.center), frames=rng.randint(2, 6))
            calm_home = 0
        elif r < 0.68:
            s.do(Action("key", "F1" if o.scene == "room" else "CLEAR"), frames=4)  # (the store; then dress-up)
            if s.obs.scene == "shop":
                s.do(Action("key", "F1"), frames=4)
            calm_home = 0
        elif r < 0.72 and _btn(o, I.STORY_GO_ID, 0):
            s.do(Action("tap", "later", *_btn(o, I.STORY_GO_ID, 0)[0].center), frames=3)
            calm_home = 0
        else:  # a pause: her hands still for a moment (now and then a long one: she looks at her room)
            n = rng.randint(30, 120) if rng.random() < 0.6 else rng.randint(150, 220)
            b = o
            o = s.wait(n)
            if b.scene in I.HOME_STORY and o.scene in GAME_SCENES and _story(b.st)[0] < 3:
                s.report("story-early", "S2", b.scene, f"the home story took her out before its time "
                         f"(story {b.st.get('mq_story')})")
            if due and n >= 150 and o.scene in I.HOME_STORY and b.st.get("mq_pay", "off") == "off":
                calm_home += 1
                s.report("story-never-out", "S2", o.scene, f"{n * 40 // 1000} s calm at home with the walk-out due, "
                         f"still on {o.where()} (story {o.st.get('mq_story')})")
    return {"errands": outs}


STRATEGIES = {"explorer": strat_explorer, "competent": strat_competent, "struggling": strat_struggling, "masher": strat_masher,
              "monkey": strat_monkey, "shopper": strat_shopper, "cards": strat_cards, "homebody": strat_homebody}


# ------------------------------------------------------------------ running
def make_driver(platform, out, page=None):
    if platform == "sim":
        return SimDriver(os.path.join(out, "sim"))
    return MacDriver(os.path.join(out, "mac"), page=page)


def run_one(platform, strategy, seed, frames, F, out, page=None, driver=None):
    d = driver or make_driver(platform, out, page)
    s = Session(d, F, strategy, check_every=1 if platform == "sim" else 3)
    rng = random.Random(f"{strategy}-{seed}")
    t0 = time.time()
    res = {}
    try:
        d.boot(seed)
        s.observe()
        res = STRATEGIES[strategy](s, rng, frames) or {}
    except Crash as e:
        msg = str(e)
        sig = re.sub(r"0x[0-9a-f]+|\d+", "N", msg)[:80]
        F.add(s, "crash", "S1", sig, f"{msg} (after {len(d.script)} inputs, {strategy} seed {seed})")
        res["crash"] = msg
    except Exception as e:
        tb = traceback.format_exc().strip().splitlines()[-3:]
        print("  bot error:", " | ".join(tb), flush=True)
        res["bot_error"] = repr(e)
    finally:
        if driver is None:
            d.close()
    res.update(platform=platform, strategy=strategy, seed=seed, frames=s.frame, inputs=len(d.script), secs=round(time.time() - t0, 1))
    if hasattr(s, "explored"):
        res["explored"] = s.explored
    print(f"{platform:4} {strategy:10} seed {seed:3}: {s.frame:6} frames ({s.frame * 40 / 60000:.1f} min), {len(d.script):5} inputs, "
          f"{time.time() - t0:5.1f} s  {json.dumps({k: v for k, v in res.items() if k not in ('platform', 'strategy', 'seed', 'frames', 'inputs', 'secs', 'explored')})}",
          flush=True)
    return res


# ------------------------------------------------------------------ minimising a repro (simulator)
def reproduces(script, inv, sig, out):
    F = Findings(os.path.join(out, "min"))
    d = SimDriver(os.path.join(out, "min"))
    s = Session(d, F, "replay")
    seed = int(script[0].split()[1]) if script and script[0].startswith("boot") else 1
    try:
        d.boot(seed)
        for line in script[1:]:
            d.replay(line)
        o = s.observe()
        s.check_obs(o)
        if inv in ("save-survives", "way-back", "tap-feedback", "frozen-item", "twist-visible", "stuck", "mash-no-reward",
                   "truth-right-accepted", "truth-wrong-rejected", "answer-no-response", "order-shown-kept",
                   "press-two-acts"):
            return False  # (a probe's verdict: not re-judged from one observation; keep the full script)
    except Crash as e:
        return inv == "crash"
    finally:
        d.close()
    return f"{inv}|{sig}" in F.by_key


def minimise(f, out, max_runs=40):
    """ddmin over the input lines of the repro (the boot line kept)"""
    if f.platform != "sim":
        return None
    script = f.script
    if not reproduces(script, f.inv, f.sig, out):
        return None
    head, body = script[:1], script[1:]
    n = 2
    runs = 1
    while len(body) >= 2 and runs < max_runs:
        chunk = max(1, len(body) // n)
        reduced = False
        for i in range(0, len(body), chunk):
            cand = body[:i] + body[i + chunk:]
            runs += 1
            if reproduces(head + cand, f.inv, f.sig, out):
                body, n, reduced = cand, max(n - 1, 2), True
                break
            if runs >= max_runs:
                break
        if not reduced:
            if n >= len(body):
                break
            n = min(len(body), n * 2)
    return head + body


def parse_seeds(s):
    out = []
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += list(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["quick", "long", "run", "replay", "list", "minimise"])
    ap.add_argument("file", nargs="?")
    ap.add_argument("--platform", default=None, help="sim, mac or sim,mac")
    ap.add_argument("--strategy", default=None, help="comma list (default: all)")
    ap.add_argument("--seeds", default=None)
    ap.add_argument("--frames", type=int, default=None, help="simulated frames (40 ms) per run")
    ap.add_argument("--out", default=os.path.join(FW, "build", "bot"))
    ap.add_argument("--page", default=None, help="the Mac page (default: build/bot/sparkle.html if built, else web/dist/sparkle.html)")
    ap.add_argument("--theme-page", default=os.environ.get("SPARKLE_PRIVATE_PAGE", ""),
                    help="a private theme page (a local file, never committed): Mac runs also on it")
    ap.add_argument("--minimise", type=int, default=1)
    ap.add_argument("--save-regressions", action="store_true")
    a = ap.parse_args()

    if a.mode == "list":
        for v in I.INVARIANTS:
            print(f"{v['sev']} {v['id']:22} {v['doc'].splitlines()[0] if v['doc'] else ''}")
        for name in ("probe_tap_feedback", "probe_way_back", "probe_frozen", "probe_twists", "probe_save", "probe_stuck"):
            print(f"   {name:22} {getattr(Session, name).__doc__.strip().splitlines()[0]}")
        print("strategies:", ", ".join(STRATEGIES))
        return 0
    if a.mode == "minimise":  # bot.py minimise OUT/findings.json: minimise and save regressions after a run
        F = Findings(os.path.dirname(os.path.abspath(a.file)))
        for j in json.load(open(a.file)):
            f = Finding(j["inv"], j["sev"], j["sig"], j["msg"], j["platform"], j["strategy"], j["seed"], j["script"], j["shot"])
            f.count, f.platforms, f.examples, f.min_script = j["count"], set(j["platforms"]), j["examples"], j.get("min_script")
            F.by_key[f.key()] = f
        a.minimise = 1
        return finish(a, F, [], time.time(), load_runs=True)
    if a.mode == "replay":
        reg = json.load(open(a.file))
        F = Findings(os.path.join(a.out, "replay"))
        d = make_driver(reg.get("platform", "sim"), a.out, a.page)
        s = Session(d, F, "replay")
        script = reg.get("min_script") or reg["script"]
        inv = reg["inv"]
        taps = [i for i, l in enumerate(script) if l.startswith("tap ")]
        upto = taps[-1] if inv in ("tap-feedback", "kid-gate") and taps else len(script)
        try:
            d.boot(int(script[0].split()[1]))
            for line in script[1:upto]:
                d.replay(line)
            o = s.observe()
            s.check_obs(o)
            if upto < len(script):  # a probe's verdict: the probe again, at the same moment
                x, y = map(int, script[upto].split()[1:3])
                label = reg["sig"].split(":")[-1]
                ok, o2 = s.probe_tap_feedback((x, y), label)
                if inv == "kid-gate" and o2.scene == o.scene:
                    s.kid_at_gate(o, label)
            elif inv == "twist-visible":
                s.probe_twists()
            elif inv in ("stuck", "answer-no-response", "mash-no-reward", "frozen-item", "save-survives", "way-back",
                         "controls-reach", "truth-right-accepted", "truth-wrong-rejected"):
                print("replayed to the moment (a strategy's verdict, not re-judged here): see the screenshot")
                return 2
        except Crash as e:
            print("crash:", e)
            return 1
        finally:
            d.close()
        hit = f"{reg['inv']}|{reg['sig']}" in F.by_key
        print(("STILL FAILS: " if hit else "passes now: ") + reg["inv"] + " " + reg["sig"])
        return 1 if hit else 0

    page = a.page
    if not page:
        b = os.path.join(FW, "build", "bot", "sparkle.html")
        page = b if os.path.exists(b) else os.path.join(FW, "web", "dist", "sparkle.html")
    plats = (a.platform or "sim,mac").split(",")
    strats = (a.strategy or ",".join(STRATEGIES)).split(",")
    if a.mode == "quick":
        seeds = parse_seeds(a.seeds or "1-2")
        frames = a.frames or 1500
    elif a.mode == "long":
        seeds = parse_seeds(a.seeds or "1-12")
        frames = a.frames or 9000
    else:
        seeds = parse_seeds(a.seeds or "1")
        frames = a.frames or 3000
    os.makedirs(a.out, exist_ok=True)
    F = Findings(a.out)
    runs = []
    t0 = time.time()
    for plat in plats:
        pages = [page] + ([a.theme_page] if plat == "mac" and a.theme_page and os.path.exists(a.theme_page) else [])
        for pg in pages:
            mac = make_driver("mac", a.out, pg) if plat == "mac" else None  # (one browser for the page's runs)
            try:
                for strat in strats:
                    for seed in (seeds if plat == "sim" else seeds[: max(1, len(seeds) // 6)]):
                        fr = frames if plat == "sim" else max(600, frames // 6)
                        if mac and mac.p.poll() is not None:  # (a run that crashed the bridge: a new one)
                            mac = make_driver("mac", a.out, pg)
                        r = run_one(plat, strat, seed, fr, F, a.out, pg, driver=mac)
                        r["page"] = os.path.basename(pg) if plat == "mac" else ""
                        if pg == a.theme_page:
                            r["page"] = "private-theme"
                        runs.append(r)
            finally:
                if mac:
                    mac.close()
    return finish(a, F, runs, t0)


def finish(a, F, runs, t0, load_runs=False):
    fs = F.save()
    if a.minimise:
        for f in fs:
            if f.platform == "sim" and 3 < len(f.script) <= 3000 and f.sev in ("S1", "S2"):
                try:
                    print("  minimising", f.inv, f.sig[:50], len(f.script), "lines", flush=True)
                    f.min_script = minimise(f, a.out)
                except Exception as e:
                    print("  minimise:", f.inv, repr(e))
    fs = F.save()
    if not load_runs:
        json.dump(runs, open(os.path.join(a.out, "runs.json"), "w"), indent=1)
    if a.save_regressions:
        rd = os.path.join(HERE, "regressions")
        os.makedirs(rd, exist_ok=True)
        for f in fs:
            if f.sev in ("S1", "S2") and f.platform == "sim" and (f.min_script or len(f.script) <= 3000):  # (deterministic; never private material)
                h = hashlib.sha1(f.key().encode()).hexdigest()[:10]
                json.dump(dict(f.to_json(), examples=None, shot=None, script=f.min_script or f.script, min_script=None, minimised=bool(f.min_script)), open(os.path.join(rd, f"{f.inv}_{h}.json"), "w"), indent=1)
    by = {}
    for f in fs:
        by[f.sev] = by.get(f.sev, 0) + 1
    print(f"\n{len(fs)} distinct findings ({', '.join(f'{k}: {by[k]}' for k in sorted(by))}) in {time.time() - t0:.0f} s; "
          f"{sum(r.get('frames', 0) for r in runs) * 40 / 60000:.1f} simulated minutes, {sum(r.get('inputs', 0) for r in runs)} inputs. "
          f"Details: {os.path.relpath(os.path.join(a.out, 'findings.json'), FW)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
