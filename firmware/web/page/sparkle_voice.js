/* Sparkle Town voices: the characters talk (the modx speak hook: app/modx.h, docs/MATHQUEST.md
 * "Talking characters"; the web build calls window.calcSpeak(who, text)).
 *
 *   - Cute babble (the default): an "animalese"-style voice made from the text itself in Web Audio --
 *     a formant blip a syllable (the vowel of the syllable picks the formants), timed by the syllables,
 *     with a pitch contour that follows the punctuation (up on "?", bouncy on "!", down on "."), in the
 *     character's own voice: a base pitch, a formant tint, a speed, a warble and a signature (a squeak, a
 *     deep soft hum, a breathy puff, a chirp, a bell). It never sounds robotic and works offline.
 *   - Spoken words (reading help): the Web Speech API (speechSynthesis), the most child-friendly installed
 *     voice (an Enhanced / Premium one first, then the youthful ones), the pitch raised (1.2..1.5) and the
 *     rate a touch slower per character; without speechSynthesis it falls back to the babble.
 *   - One line at a time: a line marked "now" interrupts, others queue (two at most; older ones drop).
 *     The captions are the game's own (it keeps them on screen while the line types out).
 *
 *   var v = new SparkleVoice({audio}); v.say(who, text, {words, now}); SparkleVoice.plan(text, profile)
 *
 * Profiles per cast slot (0..8: the built-in cast; 9..16 the visitors); a page config may replace them
 * (SPARKLE_CONFIG.voices = {slot: {babble: {...}, tts: {...}}}, e.g. from a theme manifest's
 * per-character "voice" fields: tools/make_theme.py --voices).
 */
(function (root) {
  "use strict";
  /* babble: f0 Hz, tint (formants x), step ms a syllable, warble (vibrato depth, semitones), sig */
  var PROFILES = [
    { babble: { f0: 520, tint: 1.08, step: 92, warble: 0.4, sig: "bounce" }, tts: { pitch: 1.4, rate: 0.95 } },  /* Pip */
    { babble: { f0: 470, tint: 1.0, step: 80, warble: 0.2, sig: "chirp" }, tts: { pitch: 1.25, rate: 1.0 } },   /* Dot */
    { babble: { f0: 600, tint: 1.15, step: 104, warble: 0.6, sig: "breathy" }, tts: { pitch: 1.45, rate: 0.9 } },/* Momo */
    { babble: { f0: 300, tint: 0.86, step: 128, warble: 0.3, sig: "deep" }, tts: { pitch: 1.1, rate: 0.85 } },  /* Olli */
    { babble: { f0: 510, tint: 1.02, step: 76, warble: 0.3, sig: "chirp" }, tts: { pitch: 1.3, rate: 1.05 } },  /* Tango */
    { babble: { f0: 760, tint: 1.25, step: 70, warble: 0.5, sig: "squeak" }, tts: { pitch: 1.5, rate: 1.05 } }, /* Dabble */
    { babble: { f0: 380, tint: 0.95, step: 112, warble: 0.2, sig: "breathy" }, tts: { pitch: 1.2, rate: 0.9 } },/* Bramble */
    { babble: { f0: 420, tint: 1.0, step: 120, warble: 0.4, sig: "deep" }, tts: { pitch: 1.2, rate: 0.85 } },   /* Shelly */
    { babble: { f0: 860, tint: 1.3, step: 96, warble: 0.7, sig: "bell" }, tts: { pitch: 1.5, rate: 0.95 } },    /* Nova */
    { babble: { f0: 640, tint: 1.12, step: 96, warble: 0.4, sig: "bell" }, tts: { pitch: 1.4, rate: 0.95 } },   /* the visitors */
    { babble: { f0: 700, tint: 1.2, step: 104, warble: 0.6, sig: "breathy" }, tts: { pitch: 1.45, rate: 0.9 } },
    { babble: { f0: 260, tint: 0.85, step: 130, warble: 0.2, sig: "deep" }, tts: { pitch: 1.1, rate: 0.85 } },
    { babble: { f0: 380, tint: 0.95, step: 116, warble: 0.3, sig: "breathy" }, tts: { pitch: 1.2, rate: 0.9 } },
    { babble: { f0: 620, tint: 1.1, step: 66, warble: 0.4, sig: "chirp" }, tts: { pitch: 1.35, rate: 1.05 } },
    { babble: { f0: 430, tint: 1.0, step: 82, warble: 0.3, sig: "bounce" }, tts: { pitch: 1.25, rate: 1.0 } },
    { babble: { f0: 800, tint: 1.28, step: 106, warble: 0.8, sig: "squeak" }, tts: { pitch: 1.5, rate: 0.95 } },
    { babble: { f0: 330, tint: 0.9, step: 120, warble: 0.3, sig: "deep" }, tts: { pitch: 1.15, rate: 0.85 } }
  ];
  /* the formants of a vowel (Hz): a child's, roughly */
  var VOWEL = { a: [850, 1600], e: [600, 2300], i: [400, 2800], o: [600, 1100], u: [450, 1200], y: [420, 2600] };
  /* the youthful, child-friendly macOS voices, the best first */
  var TTS_PREFER = ["Samantha", "Karen", "Moira", "Tessa", "Allison", "Ava", "Susan", "Kathy", "Victoria", "Fiona", "Serena"];

  function hash(s) { var h = 2166136261 >>> 0; for (var i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619) >>> 0; return h; }

  /* The babble of a line: one blip a syllable -- {t ms, dur ms, f0 Hz, glide (x at the end), f1, f2,
   * gain} -- deterministic for a text and a profile (tests). A syllable is a vowel group of a word;
   * a word of none (a number, "hmm") counts one; punctuation adds a pause and bends the last syllable. */
  function plan(text, prof) {
    var b = (prof && prof.babble) || prof || PROFILES[0].babble, out = [], t = 0;
    var words = String(text || "").match(/[A-Za-z']+|[0-9]+|[?!.,]/g) || [];
    var k = 0, h = hash(String(text || "")), bounce = 0;
    for (var w = 0; w < words.length; w++) {
      var wd = words[w];
      if (/^[?!.,]$/.test(wd)) { /* the contour of the phrase's end, then a little pause */
        var last = out[out.length - 1];
        if (last) {
          if (wd === "?") last.glide = 1.32, last.dur *= 1.25;
          else if (wd === "!") last.glide = 1.18, last.gain *= 1.15;
          else if (wd === ".") last.glide = 0.84;
        }
        t += wd === "," ? b.step * 0.9 : b.step * 1.6;
        continue;
      }
      var syl = /^[0-9]+$/.test(wd) ? Math.min(4, wd.length) : Math.max(1, (wd.toLowerCase().match(/[aeiouy]+/g) || []).length);
      syl = Math.min(syl, 4);
      var vowels = wd.toLowerCase().match(/[aeiouy]/g) || ["a"];
      var excl = words.indexOf("!", w) >= 0 && words.indexOf("!", w) - w <= 3;
      for (var s = 0; s < syl; s++, k++) {
        var hh = (h ^ Math.imul(k + 1, 2654435761)) >>> 0;
        var semi = ((hh >>> 8) % 7) - 3; /* a little melody from the letters */
        if (excl) bounce = 1 - bounce, semi += bounce ? 3 : -1;
        var v = VOWEL[vowels[s % vowels.length]] || VOWEL.a;
        var dur = b.step * (0.72 + ((hh >>> 16) % 30) / 100);
        out.push({ t: Math.round(t), dur: Math.round(dur), f0: Math.round(b.f0 * Math.pow(2, semi / 12)), glide: 1,
          f1: Math.round(v[0] * b.tint), f2: Math.round(v[1] * b.tint), gain: 1 });
        t += b.step * (0.92 + ((hh >>> 4) % 16) / 100);
      }
      t += b.step * 0.25; /* between words */
      if (out.length >= 28) break; /* lines are short; a long one babbles its start */
    }
    return out;
  }

  function SparkleVoice(opts) {
    opts = opts || {};
    this.audio = opts.audio || null;         /* the SparkleAudio (its context and buses) */
    this.synth = opts.synth !== undefined ? opts.synth : (root.speechSynthesis || null);
    this.profiles = PROFILES.slice();
    var cfg = (root.SPARKLE_CONFIG && root.SPARKLE_CONFIG.voices) || opts.voices || null;
    if (cfg) for (var k in cfg) if (cfg.hasOwnProperty(k)) this.profiles[k | 0] = merge(this.profiles[k | 0] || PROFILES[0], cfg[k]);
    this.queue = [];
    this.busyUntil = 0;
    this.log = [];                          /* [who, mode, text] (tests) */
    this.lastSay = -1e9;
    this._voices = null;
  }
  function merge(a, b) {
    var o = { babble: {}, tts: {} }, k;
    for (k in a.babble) o.babble[k] = a.babble[k];
    for (k in a.tts) o.tts[k] = a.tts[k];
    if (b && b.babble) for (k in b.babble) o.babble[k] = b.babble[k];
    if (b && b.tts) for (k in b.tts) o.tts[k] = b.tts[k];
    return o;
  }
  SparkleVoice.plan = plan;
  SparkleVoice.PROFILES = PROFILES;
  SparkleVoice.TTS_PREFER = TTS_PREFER;
  SparkleVoice.prototype.profile = function (who) { return this.profiles[who] || this.profiles[who % 9] || PROFILES[0]; };
  function nowMs() { return (typeof performance !== "undefined" && performance.now) ? performance.now() : Date.now(); }

  /* the installed voice to read words with: a profile's own name if installed, else an Enhanced /
   * Premium voice of the preferred ones, else a preferred one, else any English voice */
  SparkleVoice.prototype.pickVoice = function (prof) {
    var list = this.synth && this.synth.getVoices ? this.synth.getVoices() : [];
    if (!list || !list.length) return null;
    var want = prof && prof.tts && prof.tts.voice, en = list.filter(function (v) { return /^en/i.test(v.lang || "en"); });
    if (want) for (var i = 0; i < list.length; i++) if (list[i].name === want || list[i].name.indexOf(want) === 0) return list[i];
    for (var p = 0; p < TTS_PREFER.length; p++)
      for (var j = 0; j < en.length; j++) if (en[j].name.indexOf(TTS_PREFER[p]) === 0 && /Enhanced|Premium/.test(en[j].name)) return en[j];
    for (p = 0; p < TTS_PREFER.length; p++)
      for (j = 0; j < en.length; j++) if (en[j].name.indexOf(TTS_PREFER[p]) === 0) return en[j];
    return en[0] || list[0];
  };

  /* a character says text: words (reading help) or babble; now: interrupt what is being said */
  SparkleVoice.prototype.say = function (who, text, o) {
    o = o || {};
    text = String(text || "").slice(0, 160);
    if (!text) return;
    this.lastSay = nowMs();
    var words = !!o.words && !!this.synth && typeof root.SpeechSynthesisUtterance === "function";
    this.log.push([who, words ? "words" : "babble", text]);
    if (this.log.length > 200) this.log.shift();
    if (o.now) this.stop();
    else if (nowMs() < this.busyUntil || (this.synth && this.synth.speaking)) {
      this.queue.push([who, text, o]);
      if (this.queue.length > 2) this.queue.shift();
      this._kick();
      return;
    }
    if (words) this._speakWords(who, text);
    else this._babble(who, text);
  };
  SparkleVoice.prototype.stop = function () {
    this.queue.length = 0;
    this.busyUntil = 0;
    if (this.synth && this.synth.cancel) try { this.synth.cancel(); } catch (e) { /* */ }
    if (this._nodes) this._nodes.forEach(function (n) { try { n.stop(); } catch (e) { /* */ } });
    this._nodes = [];
  };
  SparkleVoice.prototype._kick = function () {
    var self = this;
    if (this._timer) return;
    this._timer = setTimeout(function () {
      self._timer = null;
      if (!self.queue.length) return;
      if (nowMs() < self.busyUntil || (self.synth && self.synth.speaking)) return self._kick();
      var q = self.queue.shift();
      q[2].now = false;
      if (q[2].words && self.synth) self._speakWords(q[0], q[1]); else self._babble(q[0], q[1]);
      self._kick();
    }, 120);
  };
  SparkleVoice.prototype._speakWords = function (who, text) {
    var prof = this.profile(who), u = new root.SpeechSynthesisUtterance(text.replace(/[×]/g, " times ").replace(/[÷]/g, " shared by "));
    var v = this.pickVoice(prof);
    if (v) u.voice = v;
    u.pitch = Math.max(1, Math.min(1.6, prof.tts.pitch || 1.3));
    u.rate = Math.max(0.7, Math.min(1.1, prof.tts.rate || 0.95));
    u.volume = this.audio && this.audio.muted ? 0 : (this.audio ? this.audio.volume : 0.8);
    try { this.synth.speak(u); } catch (e) { this._babble(who, text); }
  };
  SparkleVoice.prototype._babble = function (who, text) {
    var prof = this.profile(who), b = prof.babble, p = plan(text, prof), a = this.audio;
    var end = p.length ? p[p.length - 1].t + p[p.length - 1].dur : 0;
    this.busyUntil = nowMs() + end + 80;
    if (!a || !a.ctx) return; /* (no Web Audio: logged only) */
    var c = a.ctx, t0 = c.currentTime + 0.02, out = a.sfxBus || a.master, self = this;
    this._nodes = [];
    p.forEach(function (s) {
      var t = t0 + s.t / 1000, d = s.dur / 1000;
      var o = c.createOscillator(), g = c.createGain(), f1 = c.createBiquadFilter(), f2 = c.createBiquadFilter(), mix = c.createGain();
      o.type = b.sig === "deep" ? "triangle" : b.sig === "bell" ? "sine" : "sawtooth";
      var f = s.f0;
      o.frequency.setValueAtTime(b.sig === "chirp" ? f * 1.18 : f, t);
      o.frequency.exponentialRampToValueAtTime(Math.max(60, f * s.glide), t + d);
      if (b.warble) { /* a little vibrato */
        var lfo = c.createOscillator(), lg = c.createGain();
        lfo.frequency.value = 7 + (b.warble * 3); lg.gain.value = f * (Math.pow(2, b.warble / 12) - 1);
        lfo.connect(lg); lg.connect(o.frequency); lfo.start(t); lfo.stop(t + d + 0.05); self._nodes.push(lfo);
      }
      f1.type = "bandpass"; f1.frequency.value = s.f1; f1.Q.value = 6;
      f2.type = "bandpass"; f2.frequency.value = s.f2; f2.Q.value = 9;
      o.connect(f1); o.connect(f2); f1.connect(mix); f2.connect(mix);
      mix.gain.value = b.sig === "deep" ? 2.2 : 3.2;
      var peak = 0.16 * s.gain * (b.sig === "breathy" ? 0.7 : 1);
      g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(peak, t + 0.012);
      g.gain.setTargetAtTime(peak * 0.6, t + 0.02, d * 0.4); g.gain.setTargetAtTime(0, t + d * 0.8, 0.02);
      mix.connect(g); g.connect(out);
      o.start(t); o.stop(t + d + 0.08); self._nodes.push(o);
      if (b.sig === "squeak" || b.sig === "bell") { /* a squeaky or chiming overtone */
        var o2 = c.createOscillator(), g2 = c.createGain();
        o2.type = "sine"; o2.frequency.setValueAtTime(f * (b.sig === "bell" ? 3 : 2), t);
        g2.gain.setValueAtTime(0.03, t); g2.gain.setTargetAtTime(0, t + d * 0.5, 0.03);
        o2.connect(g2); g2.connect(out); o2.start(t); o2.stop(t + d + 0.05); self._nodes.push(o2);
      }
      if (b.sig === "breathy" && a.noise) a.noise(t, d * 0.8, { type: "bandpass", f: s.f2, q: 1.5, gain: 0.025 });
    });
  };
  root.SparkleVoice = SparkleVoice;
})(typeof window !== "undefined" ? window : globalThis);
