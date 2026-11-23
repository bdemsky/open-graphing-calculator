/* Sparkle Town sound: every effect and every music loop synthesised with Web Audio (no audio
 * files). The game calls the modx sound hooks (app/modx.h: MODX_SFX_*, MODX_MUS_*; docs/MATHQUEST.md
 * "Sound hooks"); the web build forwards them to window.calcSound(kind, id, param), which this
 * file answers.
 *
 *   var a = new SparkleAudio({store});  a.unlock() on the first user gesture (Safari's autoplay rule)
 *   a.handle("sfx", 2, 0); a.handle("music", 2, 1); a.setVolume(0.7); a.setMuted(true)
 *
 * Effects: short envelopes on oscillators and filtered noise. Music: a step sequencer (16ths,
 * scheduled 120 ms ahead) per area -- title, map, cafe, arcade, boss, wind-down, play, rhythm --
 * each with four layers that intensity 0..3 switches on (pad and bass; melody; drums; sparkle
 * arpeggios). The rhythm track (Bongo Beats) runs at 90 / 110 / 130 beats a minute from the moment
 * the game asks for it, like the game's beat clock, so the hits line up without a position coming
 * back. Without Web Audio (node tests) everything is only logged (a.log).
 */
(function (root) {
  "use strict";
  var SFX_NAMES = ["", "pop", "coin", "combo", "whoosh", "squish", "unlock", "gift", "cheer", "dud", "step", "jump",
    "boss", "tap", "launch", "land", "boost", "bongo", "bongo2", "cowbell", "clap", "voice", "merge", "fanfare", "click",
    "mew", "trill", "meow", "paw", "bass", "chord", "tamb", "giggle", "sigh"];
  /* each friend's voice (MODX_SFX_VOICE, param: cast slot x 16 + syllables): a pitch (Hz), a timbre, a
   * filter, how it moves (vib: vibrato Hz; step: ms a syllable; glide: the pitch slides, x per syllable;
   * spread: semitones the syllables wander) -- Pip, Dot, Momo, Olli, Tango, Dabble, Bramble, Shelly, Nova,
   * then the visitors (slots 9..16): Sir Sprinkles, Luna, Captain Puff, Coco, Biscuit, Ziggy, Pearl, Bao */
  var VOICES = [
    { f: 560, type: "triangle", lp: 2600, step: 82, spread: 5, up: true },
    { f: 470, type: "square", lp: 1500, step: 70, spread: 4, chirp: true },
    { f: 620, type: "sine", lp: 3000, step: 95, spread: 3, vib: 9 },
    { f: 290, type: "triangle", lp: 1200, step: 120, spread: 2, glide: 0.85 },
    { f: 500, type: "sawtooth", lp: 1300, step: 68, spread: 6 },
    { f: 780, type: "triangle", lp: 3400, step: 60, spread: 7, chirp: true },
    { f: 340, type: "square", lp: 850, step: 105, spread: 2, soft: true },
    { f: 410, type: "sine", lp: 2000, step: 115, spread: 3, glide: 1.12 },
    { f: 900, type: "sine", lp: 5000, step: 88, spread: 5, bell: true },
    { f: 660, type: "triangle", lp: 3600, step: 90, spread: 4, bell: true },
    { f: 720, type: "sine", lp: 4200, step: 100, spread: 3, vib: 6 },
    { f: 250, type: "square", lp: 900, step: 125, spread: 2, glide: 0.9 },
    { f: 380, type: "triangle", lp: 1600, step: 110, spread: 2, soft: true },
    { f: 640, type: "square", lp: 2200, step: 58, spread: 6, chirp: true },
    { f: 430, type: "sawtooth", lp: 1400, step: 75, spread: 5, up: true },
    { f: 820, type: "sine", lp: 5200, step: 105, spread: 4, vib: 11 },
    { f: 320, type: "triangle", lp: 1300, step: 118, spread: 2, glide: 1.05 }
  ];
  var MUS_NAMES = ["off", "title", "map", "cafe", "arcade", "boss", "winddown", "play", "rhythm"];

  function mtof(m) { return 440 * Math.pow(2, (m - 69) / 12); }
  function rng(seed) { var s = seed >>> 0; return function () { s = (s * 1664525 + 1013904223) >>> 0; return s / 4294967296; }; }

  /* ---------------------------------------------------------------- tracks
   * root: MIDI note of the key; scale: semitones; prog: chord degrees (0-based, one a bar), seventh:
   * add the 7th; lead: oscillator of the melody; swing: 0..0.3 of a 16th; drums: "pop" | "lofi" |
   * "chip" | "drive" | "soft" | "beat"; arp: oscillator of the sparkle layer. */
  var MAJOR = [0, 2, 4, 5, 7, 9, 11], MINOR = [0, 2, 3, 5, 7, 8, 10];
  var TRACKS = {
    1: { bpm: 100, root: 60, scale: MAJOR, prog: [0, 4, 5, 3], lead: "triangle", bell: true, drums: "pop", arp: "sine", seed: 11 },
    2: { bpm: 108, root: 65, scale: MAJOR, prog: [0, 5, 3, 4], lead: "square", drums: "pop", arp: "triangle", seed: 23, pluck: true },
    3: { bpm: 84, root: 62, scale: MAJOR, prog: [0, 5, 1, 4], seventh: true, lead: "sine", rhodes: true, swing: 0.22, drums: "lofi", arp: "sine", seed: 37, crackle: true },
    4: { bpm: 138, root: 57, scale: MINOR, prog: [0, 5, 2, 6], lead: "square", drums: "chip", arp: "square", seed: 41, fast: true },
    5: { bpm: 150, root: 50, scale: MINOR, prog: [0, 0, 5, 4], lead: "sawtooth", drums: "drive", arp: "square", seed: 53, bass8: true },
    6: { bpm: 70, root: 67, scale: MAJOR, prog: [0, 3, 0, 4], lead: "sine", bell: true, drums: "none", arp: "sine", seed: 61, gentle: true },
    7: { bpm: 116, root: 58, scale: MAJOR, prog: [0, 3, 4, 3], lead: "square", drums: "pop", arp: "triangle", seed: 71, pluck: true },
    8: { bpm: 90, root: 60, scale: MAJOR, prog: [0, 3, 4, 0], lead: "triangle", drums: "beat", arp: "sine", seed: 83, rhythm: true }
  };

  function SparkleAudio(opts) {
    opts = opts || {};
    this.store = opts.store || null;
    this.ctx = null;
    this.log = [];            /* [kind, id, param] of every call (tests, the debug console) */
    this.volume = 0.7;
    this.muted = false;
    this.want = { track: 0, intensity: 0, since: 0 }; /* the music the game asked for (perf ms) */
    this.playing = null;      /* the sequencer of the track playing */
    this._noise = null;
    var v = this._get("sparkle_volume"), m = this._get("sparkle_muted");
    if (v !== null && !isNaN(parseFloat(v))) this.volume = Math.max(0, Math.min(1, parseFloat(v)));
    if (m === "1") this.muted = true;
  }
  SparkleAudio.SFX_NAMES = SFX_NAMES;
  SparkleAudio.MUS_NAMES = MUS_NAMES;
  SparkleAudio.TRACKS = TRACKS;
  SparkleAudio.VOICES = VOICES;

  SparkleAudio.prototype._get = function (k) { try { return this.store ? this.store.getItem(k) : null; } catch (e) { return null; } };
  SparkleAudio.prototype._set = function (k, v) { try { if (this.store) this.store.setItem(k, v); } catch (e) { /* blocked */ } };
  function now() { return (typeof performance !== "undefined" && performance.now) ? performance.now() : Date.now(); }

  /* The first click / key: make (or resume) the AudioContext. Safe to call again. */
  SparkleAudio.prototype.unlock = function () {
    var AC = root.AudioContext || root.webkitAudioContext;
    if (!AC) return false;
    if (!this.ctx) {
      var c = this.ctx = new AC();
      this.master = c.createGain();
      var comp = c.createDynamicsCompressor();
      comp.threshold.value = -14; comp.knee.value = 12; comp.ratio.value = 4;
      comp.attack.value = 0.004; comp.release.value = 0.2;
      this.master.connect(comp); comp.connect(c.destination);
      this.sfxBus = c.createGain(); this.sfxBus.gain.value = 0.8; this.sfxBus.connect(this.master);
      this.musBus = c.createGain(); this.musBus.gain.value = 0.32; this.musBus.connect(this.master);
      /* a soft room: a short feedback delay, quietly */
      var d = c.createDelay(0.5), fb = c.createGain(), wet = c.createGain(), lp = c.createBiquadFilter();
      d.delayTime.value = 0.13; fb.gain.value = 0.28; wet.gain.value = 0.16; lp.type = "lowpass"; lp.frequency.value = 2600;
      this.verb = c.createGain(); this.verb.connect(d); d.connect(lp); lp.connect(fb); fb.connect(d); lp.connect(wet); wet.connect(this.master);
      this._applyVolume();
      var self = this;
      this._timer = setInterval(function () { self._schedule(); }, 25);
    }
    if (this.ctx.state === "suspended" && this.ctx.resume) this.ctx.resume();
    if (this.want.track && !this.playing) this._startMusic();
    return true;
  };

  SparkleAudio.prototype._applyVolume = function () {
    if (!this.master) return;
    var g = this.muted ? 0 : this.volume * this.volume; /* a gentle curve */
    this.master.gain.setTargetAtTime(g, this.ctx.currentTime, 0.03);
  };
  SparkleAudio.prototype.setVolume = function (v) {
    this.volume = Math.max(0, Math.min(1, +v || 0));
    this._set("sparkle_volume", String(this.volume));
    this._applyVolume();
  };
  SparkleAudio.prototype.setMuted = function (m) {
    this.muted = !!m;
    this._set("sparkle_muted", this.muted ? "1" : "0");
    this._applyVolume();
  };

  /* window.calcSound(kind, id, param) */
  SparkleAudio.prototype.handle = function (kind, id, param) {
    this.log.push([kind, id, param]);
    if (this.log.length > 2000) this.log.splice(0, 1000);
    if (kind === "sfx") this.sfx(id, param);
    else if (kind === "music") this.music(id, param);
  };

  /* ---------------------------------------------------------------- building blocks */
  SparkleAudio.prototype._env = function (g, t, a, peak, d, sus, r, len) {
    var p = g.gain;
    p.setValueAtTime(0.0001, t);
    p.linearRampToValueAtTime(peak, t + a);
    p.setTargetAtTime(peak * sus, t + a, d / 3);
    p.setTargetAtTime(0.0001, t + len, r / 4);
  };
  /* one oscillator note: f (Hz) or [f0, f1] (a slide), to bus */
  SparkleAudio.prototype.tone = function (f, t, len, o) {
    o = o || {};
    var c = this.ctx, osc = c.createOscillator(), g = c.createGain();
    osc.type = o.type || "sine";
    var f0 = Array.isArray(f) ? f[0] : f, f1 = Array.isArray(f) ? f[1] : f;
    osc.frequency.setValueAtTime(f0, t);
    if (f1 !== f0) osc.frequency.exponentialRampToValueAtTime(Math.max(20, f1), t + (o.slide || len));
    if (o.detune) osc.detune.value = o.detune;
    if (o.vib) { /* a little vibrato */
      var l = c.createOscillator(), lg = c.createGain();
      l.frequency.value = o.vib; lg.gain.value = f0 * 0.012; l.connect(lg); lg.connect(osc.frequency);
      l.start(t); l.stop(t + len + (o.r || 0.1) + 0.05);
    }
    var node = osc;
    if (o.lp) { var bq = c.createBiquadFilter(); bq.type = "lowpass"; bq.frequency.value = o.lp; bq.Q.value = o.q || 0.7; osc.connect(bq); node = bq; }
    node.connect(g);
    g.connect(o.bus || this.sfxBus);
    if (o.verb) g.connect(this.verb);
    var a = o.a === undefined ? 0.005 : o.a, r = o.r === undefined ? 0.08 : o.r;
    this._env(g, t, a, o.gain === undefined ? 0.3 : o.gain, o.d || len, o.sus === undefined ? 0.6 : o.sus, r, len);
    osc.start(t);
    osc.stop(t + len + r + 0.05);
    return osc;
  };
  SparkleAudio.prototype._noiseBuf = function () {
    if (this._noise) return this._noise;
    var c = this.ctx, n = c.sampleRate, b = c.createBuffer(1, n, n), d = b.getChannelData(0), r = rng(7);
    for (var i = 0; i < n; i++) d[i] = r() * 2 - 1;
    return (this._noise = b);
  };
  /* filtered noise: o.type (bandpass/lowpass/highpass), o.f or [f0, f1], o.q */
  SparkleAudio.prototype.noise = function (t, len, o) {
    o = o || {};
    var c = this.ctx, s = c.createBufferSource(), f = c.createBiquadFilter(), g = c.createGain();
    s.buffer = this._noiseBuf();
    s.loop = true;
    f.type = o.type || "bandpass";
    var f0 = Array.isArray(o.f) ? o.f[0] : (o.f || 1000), f1 = Array.isArray(o.f) ? o.f[1] : f0;
    f.frequency.setValueAtTime(f0, t);
    if (f1 !== f0) f.frequency.exponentialRampToValueAtTime(f1, t + len);
    f.Q.value = o.q || 1;
    s.connect(f); f.connect(g); g.connect(o.bus || this.sfxBus);
    if (o.verb) g.connect(this.verb);
    this._env(g, t, o.a || 0.002, o.gain === undefined ? 0.3 : o.gain, o.d || len, o.sus === undefined ? 0.4 : o.sus, o.r || 0.05, len);
    s.start(t, Math.random() * 0.5);
    s.stop(t + len + (o.r || 0.05) + 0.05);
  };

  /* a singing cat: a buzzy source (a sawtooth and a triangle) at f, through a formant (bandpass) that
   * glides m -> e -> ow (closed, open bright, rounder), a little scoop up into the pitch and a light
   * vibrato; kind 1 holds the vowel ("meeeow"), 2 is a rolled "mrrp"; soft and rounded (low-passed) */
  SparkleAudio.prototype.meow = function (f, t, len, kind, loud) {
    var c = this.ctx, g = c.createGain(), bp = c.createBiquadFilter(), lp = c.createBiquadFilter();
    var o1 = c.createOscillator(), o2 = c.createOscillator(), vib = c.createOscillator(), vg = c.createGain();
    o1.type = "sawtooth"; o2.type = "triangle";
    [o1, o2].forEach(function (o, i) {
      var ff = f * (i ? 2 : 1);
      o.frequency.setValueAtTime(ff * (kind === 2 ? 0.8 : 0.92), t);
      o.frequency.exponentialRampToValueAtTime(ff, t + Math.min(0.06, len * 0.4));
      if (kind !== 2) o.frequency.setValueAtTime(ff, t + len * 0.7), o.frequency.exponentialRampToValueAtTime(ff * 0.94, t + len);
    });
    vib.frequency.value = kind === 2 ? 28 : 6; vg.gain.value = f * (kind === 2 ? 0.05 : 0.012);
    vib.connect(vg); vg.connect(o1.frequency);
    bp.type = "bandpass"; bp.Q.value = 3;
    bp.frequency.setValueAtTime(500, t);                                          /* m */
    bp.frequency.exponentialRampToValueAtTime(1700, t + Math.max(0.04, len * 0.3)); /* e */
    bp.frequency.exponentialRampToValueAtTime(750, t + len);                      /* ow */
    lp.type = "lowpass"; lp.frequency.value = 2600;
    o1.connect(bp); o2.connect(lp); bp.connect(lp); lp.connect(g); g.connect(this.sfxBus); g.connect(this.verb);
    this._env(g, t, 0.02, 0.16 * (loud || 1), len, 0.8, 0.08, len);
    [o1, o2, vib].forEach(function (o) { o.start(t); o.stop(t + len + 0.2); });
  };

  /* ---------------------------------------------------------------- effects */
  var NOTE = function (n) { return mtof(n); };
  SparkleAudio.prototype.sfx = function (id, param) {
    if (!this.ctx || this.ctx.state !== "running") return;
    var t = this.ctx.currentTime + 0.005, self = this, p = param | 0;
    function chime(notes, step, o) { notes.forEach(function (n, i) { self.tone(NOTE(n), t + i * step, (o && o.len) || 0.12, o); }); }
    switch (id) {
    case 1: /* pop: a bubble */
      this.tone([520, 1250], t, 0.06, { type: "sine", gain: 0.35, slide: 0.05, r: 0.05 });
      this.noise(t, 0.02, { type: "highpass", f: 3000, gain: 0.12 });
      break;
    case 2: /* coin */
      this.tone(NOTE(83), t, 0.06, { type: "square", gain: 0.12, lp: 5000 });
      this.tone(NOTE(88), t + 0.06, 0.22, { type: "square", gain: 0.12, lp: 5000, sus: 0.5, r: 0.15, verb: true });
      break;
    case 3: { /* combo n: rising */
      var base = 72 + Math.min(p, 16);
      chime([base, base + 4, base + 7], 0.045, { type: "triangle", gain: 0.22, len: 0.09, verb: true });
      if (p >= 5) this.tone(NOTE(base + 12), t + 0.14, 0.15, { type: "sine", gain: 0.15, verb: true });
      break;
    }
    case 4: /* whoosh */
      this.noise(t, 0.32, { type: "bandpass", f: [350, 2800], q: 1.4, gain: 0.32, a: 0.06, sus: 0.7, r: 0.12 });
      break;
    case 5: /* squish */
      this.tone([300, 110], t, 0.16, { type: "sine", gain: 0.35, slide: 0.14, vib: 30 });
      this.noise(t, 0.1, { type: "lowpass", f: [900, 300], gain: 0.2 });
      break;
    case 6: /* unlock: a sparkly arpeggio */
      chime([72, 76, 79, 84, 88], 0.06, { type: "triangle", gain: 0.2, len: 0.14, verb: true });
      this.tone(NOTE(96), t + 0.32, 0.4, { type: "sine", gain: 0.1, r: 0.4, verb: true, vib: 7 });
      break;
    case 7: /* gift: the paper, then a chord of bells */
      this.noise(t, 0.12, { type: "highpass", f: [1500, 5000], gain: 0.2 });
      [72, 76, 79, 83, 86].forEach(function (n, i) {
        self.tone(NOTE(n), t + 0.12 + i * 0.03, 0.5, { type: "sine", gain: 0.12, r: 0.6, sus: 0.3, verb: true });
        self.tone(NOTE(n + 12), t + 0.12 + i * 0.03, 0.2, { type: "triangle", gain: 0.05, r: 0.3 });
      });
      break;
    case 8: /* cheer: a happy leap and a twinkle */
      chime([72, 76, 79], 0.07, { type: "square", gain: 0.1, len: 0.1, lp: 3500 });
      this.tone(NOTE(84), t + 0.21, 0.25, { type: "triangle", gain: 0.22, verb: true, vib: 6 });
      this.tone(NOTE(96), t + 0.3, 0.12, { type: "sine", gain: 0.08, verb: true });
      break;
    case 9: /* dud: a soft "bwomp", never harsh */
      this.tone([233, 175], t, 0.16, { type: "triangle", gain: 0.22, slide: 0.15 });
      this.tone([175, 131], t + 0.16, 0.22, { type: "triangle", gain: 0.2, slide: 0.2, r: 0.12 });
      break;
    case 10: /* step: a light tick */
      this.tone(900 + (p % 4) * 60, t, 0.025, { type: "sine", gain: 0.18, r: 0.03 });
      this.noise(t, 0.015, { type: "highpass", f: 4000, gain: 0.06 });
      break;
    case 11: /* jump */
      this.tone([280, 820], t, 0.14, { type: "square", gain: 0.1, lp: 2500, slide: 0.12 });
      break;
    case 12: /* boss: drums and a brassy minor chord */
      [0, 0.18, 0.36].forEach(function (d) {
        self.tone([110, 45], t + d, 0.18, { type: "sine", gain: 0.5, slide: 0.15 });
        self.noise(t + d, 0.06, { type: "lowpass", f: 600, gain: 0.25 });
      });
      [45, 52, 57, 60].forEach(function (n) { self.tone(NOTE(n), t + 0.5, 0.8, { type: "sawtooth", gain: 0.07, lp: 1200, a: 0.05, r: 0.4 }); });
      break;
    case 13: /* tap */
      this.tone(1500, t, 0.02, { type: "sine", gain: 0.2, r: 0.02 });
      break;
    case 14: /* launch (param: the angle) */
      var hi = 700 + Math.max(0, Math.min(90, p)) * 6;
      this.tone([220, hi], t, 0.22, { type: "triangle", gain: 0.22, slide: 0.2 });
      this.noise(t, 0.2, { type: "bandpass", f: [600, 2400], gain: 0.12 });
      break;
    case 15: /* land / dig: a thud */
      this.tone([160, 55], t, 0.14, { type: "sine", gain: 0.45, slide: 0.12 });
      this.noise(t, 0.08, { type: "lowpass", f: 700, gain: 0.25 });
      break;
    case 16: /* boost */
      this.tone([200, 1300], t, 0.35, { type: "sawtooth", gain: 0.08, lp: 3000, slide: 0.3 });
      this.noise(t, 0.35, { type: "bandpass", f: [800, 4000], gain: 0.1 });
      break;
    case 17: case 18: { /* bongos (param: the judgement: 2 perfect, 1 good, 0 a miss, 3 no note) */
      var miss = p === 0 || p === 3, f = id === 18 ? 330 : 205;
      this.tone([f * 1.35, f], t, 0.12, { type: "sine", gain: miss ? 0.2 : 0.55, slide: 0.03, r: 0.1, sus: 0.2 });
      this.noise(t, 0.012, { type: "bandpass", f: 2200, q: 0.8, gain: 0.18 });
      break;
    }
    case 19: /* cowbell */
      [540, 800].forEach(function (fq) { self.tone(fq, t, 0.08, { type: "square", gain: p === 9 ? 0.035 : 0.07, lp: 2600, sus: 0.3, r: 0.2 }); });
      break;
    case 20: /* clap */
      [0, 0.011, 0.023].forEach(function (d) { self.noise(t + d, 0.012, { type: "bandpass", f: 1200, q: 0.9, gain: p === 9 ? 0.14 : 0.35 }); });
      this.noise(t + 0.03, 0.12, { type: "bandpass", f: 1300, q: 0.8, gain: 0.18, r: 0.08 });
      break;
    case 21: { /* a friend's voice: a blip a syllable, in their own pitch and timbre */
      var v = VOICES[((p >> 4) % VOICES.length + VOICES.length) % VOICES.length], n = Math.max(1, Math.min(10, p & 15)), r = rng(p * 977 + 13);
      var k = 0;
      for (var i = 0; i < n; i++) {
        var semi = Math.round((r() - 0.5) * 2 * v.spread) + (v.up && i === n - 1 ? 4 : 0);
        var fq = v.f * Math.pow(2, semi / 12) * (v.glide ? Math.pow(v.glide, i) : 1), tt = t + k / 1000;
        var len = (v.step * (0.62 + r() * 0.3)) / 1000;
        this.tone(v.chirp ? [fq * 1.25, fq] : fq, tt, len, { type: v.type, gain: v.soft ? 0.08 : 0.12, lp: v.lp, slide: len * 0.7,
          vib: v.vib, a: 0.008, r: 0.05 });
        if (v.bell) this.tone(fq * 2, tt, len * 0.8, { type: "sine", gain: 0.04, r: 0.15 });
        k += v.step * (0.85 + r() * 0.3);
      }
      break;
    }
    case 22: { /* merge: a squishy whoosh up and a pop, higher for higher levels */
      var lv = Math.max(1, Math.min(8, p));
      this.tone([300 + lv * 40, 900 + lv * 120], t, 0.12, { type: "sine", gain: 0.25, slide: 0.1 });
      this.tone(1200 + lv * 150, t + 0.1, 0.08, { type: "triangle", gain: 0.18, r: 0.12, verb: true });
      this.noise(t, 0.06, { type: "bandpass", f: 1800, gain: 0.1 });
      break;
    }
    case 23: /* fanfare: a little brassy arpeggio */
      [0, 4, 7, 12, 16].forEach(function (s, i) {
        self.tone(mtof(67 + s), t + i * 0.09, i === 4 ? 0.5 : 0.12, { type: "sawtooth", gain: 0.1, lp: 2400, r: 0.2, verb: true });
      });
      break;
    case 24: /* a dial's click (param: the digit) */
      this.noise(t, 0.008, { type: "highpass", f: 3000, gain: 0.25 });
      this.tone(900 + (p % 10) * 40, t, 0.02, { type: "square", gain: 0.05, lp: 3000 });
      break;
    /* the cat drummer: soft, round voices (triangle through a low-pass, a little vibrato; never shrill) */
    case 25: { /* the singing cat: a meow at the note's pitch (param: the MIDI note | eighths << 7 | kind << 12:
                0 a short "mew", 1 a held "meeeow", 2 a pickup "mrrp"); a formant glides m -> e -> ow */
      var nt = p & 127, el = (p >> 7) & 31, kd = (p >> 12) & 3;
      this.meow(mtof(nt || 76), t, kd === 1 ? Math.min(0.75, 0.12 * Math.max(3, el)) : kd === 2 ? 0.1 : 0.15, kd);
      break;
    }
    case 26: /* a happy trill: a purring roll up */
      for (var tr = 0; tr < 6; tr++)
        this.tone([520 + tr * 40, 600 + tr * 45], t + tr * 0.035, 0.04, { type: "triangle", gain: 0.07, lp: 1700, r: 0.03 });
      this.tone([700, 880], t + 0.21, 0.18, { type: "triangle", gain: 0.09, lp: 1800, vib: 7, slide: 0.15, r: 0.12 });
      break;
    case 27: /* the finale's big happy "miaaaow!" */
      this.meow(560, t, 0.7, 1, 1.4);
      break;
    case 28: /* a paw's soft tap (param: 0 low, 1 high) */
      this.noise(t, 0.02, { type: "bandpass", f: p ? 1600 : 900, q: 1.2, gain: 0.16 });
      this.tone([p ? 260 : 170, p ? 200 : 120], t, 0.06, { type: "sine", gain: 0.2, slide: 0.05, sus: 0.2 });
      break;
    case 29: { /* the bass: a round plucked note (param: the MIDI note | eighths << 7) */
      var bl = Math.min(0.5, 0.11 * Math.max(1, (p >> 7) & 31));
      this.tone(mtof(p & 127), t, bl, { type: "triangle", gain: 0.16, lp: 600, sus: 0.5, r: 0.06 });
      break;
    }
    case 30: { /* a chord: a soft pad under the cats (param: the root | eighths << 7 | minor << 14) */
      var root = p & 127, cl = Math.min(1.6, 0.12 * Math.max(2, (p >> 7) & 127)), third = (p >> 14) & 1 ? 3 : 4;
      [0, third, 7].forEach(function (iv) {
        self.tone(mtof(root + iv - 12), t, cl, { type: "sine", gain: 0.035, a: 0.04, sus: 0.7, r: 0.15 });
      });
      break;
    }
    case 32: { /* a squishy's ticklish giggle (param: the kind | the squeeze 0..3 << 4 | 1 << 6 building): "hee-hee",
                "kyaa", "teehee" -- each kind its own voice: the mochi high and soft, the bun lower and puffier,
                the peach bubbly; soft and round (triangles through a low-pass), never shrill */
      var GV = [{ f: 520, step: 0.11, wob: 1.0 }, { f: 700, step: 0.085, wob: 1.4 }, { f: 640, step: 0.1, wob: 1.1 },
        { f: 560, step: 0.12, wob: 0.9 }, { f: 820, step: 0.1, wob: 1.0 }, { f: 470, step: 0.13, wob: 0.8 },
        { f: 760, step: 0.09, wob: 1.3 }, { f: 680, step: 0.1, wob: 1.2 }];
      var gv = GV[(p & 15) % GV.length], lvl = (p >> 4) & 3, build = (p >> 6) & 1, n = 2 + lvl * 2;
      for (var gi = 0; gi < n; gi++) {
        var up = build ? gi * 0.06 : (gi % 2 ? -0.05 : 0.05), gf = gv.f * (1 + up) * (1 + lvl * 0.04);
        var tt = t + gi * gv.step;
        this.tone([gf * 1.25, gf], tt, gv.step * 0.6, { type: "triangle", gain: 0.06 + lvl * 0.012, lp: 2200, slide: gv.step * 0.4,
          vib: 9 * gv.wob, a: 0.006, r: 0.04 });
        this.noise(tt, 0.02, { type: "bandpass", f: 3000, q: 2, gain: 0.015 }); /* the breathy "h" */
      }
      break;
    }
    case 33: { /* letting go: a happy sigh "mmm" (param: the kind | 1 << 4 a wobble-boing) */
      var sf = 300 + ((p & 15) % 8) * 30;
      if ((p >> 4) & 1)
        this.tone([sf * 1.6, sf * 0.8], t, 0.35, { type: "sine", gain: 0.08, slide: 0.3, vib: 14, r: 0.12 });
      else
        this.tone([sf * 1.2, sf], t, 0.4, { type: "triangle", gain: 0.05, lp: 900, slide: 0.35, vib: 5, a: 0.04, r: 0.15 });
      break;
    }
    case 31: /* a tambourine: a shimmer (param 9: soft) */
      this.noise(t, 0.05, { type: "highpass", f: 6500, gain: p === 9 ? 0.07 : 0.14, r: 0.06 });
      break;
    default:
      break;
    }
  };

  /* ---------------------------------------------------------------- music */
  SparkleAudio.prototype.music = function (track, intensity) {
    track = track | 0; intensity = Math.max(0, Math.min(3, intensity | 0));
    var w = this.want;
    if (w.track === track && w.intensity === intensity) return;
    var same = w.track === track && track !== 8; /* the rhythm track restarts its clock */
    w.track = track; w.intensity = intensity; if (!same) w.since = now();
    if (!this.ctx) return;
    if (same && this.playing) { this.playing.intensity = intensity; return; }
    this._stopMusic();
    if (track) this._startMusic();
  };

  SparkleAudio.prototype._stopMusic = function () {
    var p = this.playing;
    if (!p) return;
    this.playing = null;
    var t = this.ctx.currentTime;
    p.bus.gain.setTargetAtTime(0.0001, t, 0.12);
    setTimeout(function () { try { p.bus.disconnect(); } catch (e) { /* gone */ } }, 1200);
  };

  SparkleAudio.prototype._startMusic = function () {
    var w = this.want, spec = TRACKS[w.track];
    if (!spec || !this.ctx) return;
    var c = this.ctx, bus = c.createGain();
    bus.gain.value = 1;
    bus.connect(this.musBus);
    var bpm = spec.rhythm ? [90, 110, 130][Math.min(2, w.intensity)] : spec.bpm;
    var stepDur = 60 / bpm / 4;
    /* the rhythm track keeps the game's clock: its beat 0 is when it was asked for */
    var start = c.currentTime + 0.05, step = 0;
    if (spec.rhythm) {
      var el = (now() - w.since) / 1000, steps = Math.ceil(el / stepDur);
      start = c.currentTime + (steps * stepDur - el);
      step = steps;
    }
    this.playing = { spec: spec, track: w.track, bus: bus, step: step, next: start, stepDur: stepDur,
      intensity: spec.rhythm ? 2 : w.intensity, melody: makeMelody(spec), bpm: bpm };
  };

  /* a 4-bar phrase of the track (16ths: a scale degree or null), deterministic per track */
  function makeMelody(spec) {
    var r = rng(spec.seed), out = [], deg = 4;
    for (var bar = 0; bar < 4; bar++) {
      var chord = spec.prog[bar % spec.prog.length];
      for (var s = 0; s < 16; s++) {
        var on = s % 4 === 0 ? r() < 0.85 : s % 2 === 0 ? r() < (spec.fast ? 0.6 : 0.35) : r() < (spec.fast ? 0.3 : 0.08);
        if (bar === 3 && s >= 12) on = s === 12;
        if (!on) { out.push(null); continue; }
        if (s % 4 === 0) { /* on the beat: a chord tone */
          var tones = [chord, chord + 2, chord + 4];
          deg = tones[Math.floor(r() * 3)] + (r() < 0.3 ? 7 : 0);
        } else {
          deg += r() < 0.5 ? 1 : -1;
        }
        if (bar === 3 && s === 12) deg = 7; /* home */
        out.push(deg);
      }
    }
    return out;
  }
  function degNote(spec, d) {
    var sc = spec.scale, o = Math.floor(d / 7), i = ((d % 7) + 7) % 7;
    return spec.root + sc[i] + 12 * o;
  }

  SparkleAudio.prototype._schedule = function () {
    var p = this.playing, c = this.ctx;
    if (!p || !c || c.state !== "running") return;
    if (p.next < c.currentTime - 0.5) { /* the tab slept: skip ahead, keep the grid */
      var lost = Math.ceil((c.currentTime - p.next) / p.stepDur);
      p.next += lost * p.stepDur, p.step += lost;
    }
    while (p.next < c.currentTime + 0.12) {
      this._step(p, p.step, p.next);
      p.step++;
      p.next += p.stepDur;
    }
  };

  SparkleAudio.prototype._step = function (p, step, t) {
    var spec = p.spec, s = step % 16, bar = Math.floor(step / 16), lv = p.intensity, self = this, bus = p.bus;
    var chordDeg = spec.prog[bar % spec.prog.length];
    if (spec.swing && s % 2 === 1) t += spec.swing * p.stepDur;
    var beat = 60 / p.bpm;
    /* layer 0: the pad (each bar) and the bass */
    if (s === 0) {
      var notes = [0, 2, 4].concat(spec.seventh ? [6] : []);
      notes.forEach(function (k, i) {
        var n = degNote(spec, chordDeg + k);
        self.tone(mtof(n), t + i * (spec.rhodes ? 0.015 : 0), beat * 3.8, {
          type: spec.rhodes ? "sine" : "triangle", gain: spec.gentle ? 0.05 : 0.045, a: spec.rhodes ? 0.01 : 0.12,
          sus: spec.rhodes ? 0.35 : 0.8, r: 0.5, bus: bus, vib: spec.rhodes ? 5 : 0, lp: 2400 });
      });
      if (spec.crackle) this.noise(t, beat * 4, { type: "highpass", f: 5000, gain: 0.012, bus: bus, sus: 1 });
    }
    var bassOn = spec.bass8 ? s % 2 === 0 : (s === 0 || s === 8 || (s === 11 && !spec.gentle) || (spec.rhythm && s % 4 === 0));
    if (bassOn) {
      var bn = degNote(spec, chordDeg) - 24 + (spec.bass8 && s % 8 === 4 ? 12 : 0);
      this.tone(mtof(bn), t, p.stepDur * (spec.bass8 ? 1.6 : 3), { type: spec.fast || spec.bass8 ? "square" : "triangle",
        gain: spec.fast || spec.bass8 ? 0.07 : 0.16, lp: 900, sus: 0.7, r: 0.06, bus: bus });
    }
    /* layer 1: the melody */
    if (lv >= 1 && !spec.rhythm) {
      var m = p.melody[step % p.melody.length];
      if (m !== null && m !== undefined) {
        var mn = degNote(spec, m) + 12;
        if (spec.bell) {
          this.tone(mtof(mn), t, 0.35, { type: "sine", gain: 0.1, sus: 0.25, r: 0.5, bus: bus, verb: true });
          this.tone(mtof(mn + 12), t, 0.12, { type: "triangle", gain: 0.03, sus: 0.1, r: 0.2, bus: bus });
        } else {
          this.tone(mtof(mn), t, p.stepDur * (spec.pluck ? 1.2 : 1.8), { type: spec.lead, gain: spec.lead === "sine" ? 0.11 : 0.05,
            lp: spec.lead === "sawtooth" ? 1800 : 3200, sus: spec.pluck ? 0.2 : 0.6, r: 0.08, bus: bus, vib: spec.lead === "sine" ? 5 : 0, verb: true });
        }
      }
    }
    /* layer 2: drums */
    var dr = spec.drums;
    if (lv >= 2 && dr !== "none" && !(spec.gentle)) {
      var kick = dr === "drive" ? s % 4 === 0 : (s === 0 || s === 8 || (dr === "chip" && s === 10) || (dr === "beat" && s % 4 === 0));
      var snare = s === 4 || s === 12;
      var hat = dr === "lofi" ? s % 4 === 2 : s % 2 === 0;
      if (kick) this.tone([dr === "chip" ? 180 : 130, 45], t, 0.12, { type: "sine", gain: dr === "lofi" ? 0.3 : 0.4, slide: 0.1, r: 0.05, bus: bus });
      if (snare) {
        if (dr === "chip") this.noise(t, 0.06, { type: "highpass", f: 1800, gain: 0.12, bus: bus });
        else if (dr === "beat") { /* claps stay the player's: a soft rim */ this.tone(1800, t, 0.02, { type: "triangle", gain: 0.06, bus: bus }); }
        else this.noise(t, 0.1, { type: "bandpass", f: dr === "lofi" ? 1500 : 1900, q: 0.7, gain: dr === "lofi" ? 0.1 : 0.15, bus: bus });
      }
      if (hat) this.noise(t, 0.025, { type: "highpass", f: 7000, gain: s % 4 === 0 ? 0.05 : 0.03, bus: bus });
      if (dr === "beat" && s === 0) this.tone(1320, t, 0.03, { type: "sine", gain: 0.08, bus: bus }); /* bar one, clearly */
    }
    if (spec.rhythm && s % 4 === 0) /* the count: a soft woodblock on every beat */
      this.tone(s === 0 ? 1050 : 880, t, 0.03, { type: "sine", gain: s === 0 ? 0.12 : 0.07, r: 0.03, bus: bus });
    /* layer 3: sparkle arpeggio */
    if (lv >= 3 && !spec.gentle) {
      var arp = [0, 2, 4, 7][s % 4];
      this.tone(mtof(degNote(spec, chordDeg + arp) + 24), t, p.stepDur * 0.8, { type: spec.arp, gain: 0.025, sus: 0.3, r: 0.05, bus: bus, lp: 5000 });
    } else if (lv >= 3 && spec.gentle && s % 4 === 2) {
      this.tone(mtof(degNote(spec, chordDeg + 4) + 24), t, 0.3, { type: "sine", gain: 0.03, sus: 0.2, r: 0.4, bus: bus, verb: true });
    }
  };

  if (typeof module !== "undefined" && module.exports) module.exports = SparkleAudio;
  else root.SparkleAudio = SparkleAudio;
})(typeof window !== "undefined" ? window : this);
