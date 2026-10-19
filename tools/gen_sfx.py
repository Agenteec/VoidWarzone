"""Sound effects for Void Warzone: synthesized weapons, explosions and loops plus processed Kenney CC0 sounds.

python tools/gen_sfx.py data/sfx
Needs numpy, scipy and ffmpeg on PATH (to decode the Kenney .ogg files fetched by tools/fetch_assets.py).
Output: 22050 Hz mono 16-bit WAV. Variants are numbered name_0.wav, name_1.wav, ...; loops are seamless.
"""
import os
import subprocess
import sys
import wave

import numpy as np
from scipy import signal

SR = 22050
HERE = os.path.dirname(os.path.abspath(__file__))
KENNEY = os.path.join(HERE, "assets_src", "kenney_sounds")


def t_axis(seconds):
    return np.arange(int(seconds * SR)) / SR


def noise(seconds, seed):
    return np.random.default_rng(seed).standard_normal(int(seconds * SR))


def band(x, lo=None, hi=None, order=2):
    if lo and hi:
        b, a = signal.butter(order, [lo, hi], btype="band", fs=SR)
    elif lo:
        b, a = signal.butter(order, lo, btype="high", fs=SR)
    else:
        b, a = signal.butter(order, hi, btype="low", fs=SR)
    return signal.lfilter(b, a, x)


def sweep_lowpass(x, start, end, steps=24):
    """Low-pass with a cutoff gliding from start to end Hz (processed in blocks, overlap-added)."""
    out = np.zeros_like(x)
    n = len(x)
    block = max(1, n // steps)
    win = np.hanning(block * 2)
    for i in range(0, n, block):
        cut = start * (end / start) ** (i / max(1, n))
        seg = x[max(0, i - block):i + block]
        y = band(seg, hi=min(cut, SR * 0.45))
        w = win[:len(y)] if len(y) == len(win) else np.hanning(len(y))
        out[max(0, i - block):max(0, i - block) + len(y)] += y * w
    return out


def env(seconds, attack=0.002, decay=0.1):
    t = t_axis(seconds)
    e = np.exp(-t / decay)
    a = int(attack * SR)
    if a > 0:
        e[:a] *= np.linspace(0, 1, a)
    return e


def sine(freq0, freq1, seconds):
    t = t_axis(seconds)
    f = freq0 * (freq1 / freq0) ** (t / seconds)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def reverb(x, seconds=0.6, mix=0.25, seed=7):
    ir = noise(seconds, seed) * np.exp(-t_axis(seconds) / (seconds / 5))
    ir = band(ir, hi=4000)
    wet = signal.fftconvolve(x, ir)[: len(x) + len(ir)]
    wet /= np.max(np.abs(wet)) + 1e-9
    dry = np.concatenate([x, np.zeros(len(wet) - len(x))])
    dry /= np.max(np.abs(dry)) + 1e-9
    return dry * (1 - mix) + wet * mix


def fit(x, seconds):
    n = int(seconds * SR)
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])


def finish(x, peak=0.9, fade=0.02):
    x = np.tanh(x / (np.max(np.abs(x)) + 1e-9) * 1.4)
    f = int(fade * SR)
    if f and len(x) > f:
        x[-f:] *= np.linspace(1, 0, f)
    return x / (np.max(np.abs(x)) + 1e-9) * peak


def write(out, name, x):
    data = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(os.path.join(out, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


def kenney(pack, name):
    path = None
    for root, _, files in list(os.walk(os.path.join(KENNEY, pack))) + list(os.walk(KENNEY)):
        if name + ".ogg" in files:
            path = os.path.join(root, name + ".ogg")
            break
    if not path:
        raise FileNotFoundError(name)
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def pitch(x, factor):
    """Resample to shift pitch (and length) by factor."""
    n = int(len(x) / factor)
    return np.interp(np.linspace(0, len(x) - 1, n), np.arange(len(x)), x)


def loop_crossfade(x, fade):
    f = int(fade * SR)
    body = x[:-f].copy()
    body[:f] = body[:f] * np.linspace(0, 1, f) + x[-f:] * np.linspace(1, 0, f)
    return body


# ---------------------------------------------------------------- weapons

def gun(seed, length, crack_decay, lo, hi, thump_hz, thump_gain, tail, verb=0.3):
    crack = band(noise(length, seed), lo, hi) * env(length, 0.0005, crack_decay)
    thump = sine(thump_hz * 1.6, thump_hz, length) * env(length, 0.001, crack_decay * 2.5) * thump_gain
    click = np.zeros(int(length * SR))
    click[: int(0.003 * SR)] = np.random.default_rng(seed + 1).uniform(-1, 1, int(0.003 * SR))
    x = crack + thump + click * 0.6
    return finish(fit(reverb(x, tail, verb, seed), length + tail * 0.6))


def cannon(seed, sub_hz, length, lp, tail):
    boom = band(noise(length, seed), hi=lp) * env(length, 0.002, length * 0.18)
    sub = sine(sub_hz * 2.2, sub_hz, length) * env(length, 0.002, length * 0.3) * 1.4
    crack = band(noise(length, seed + 3), 1500, 7000) * env(length, 0.0005, 0.012) * 0.9
    return finish(fit(reverb(boom + sub + crack, tail, 0.35, seed), length + tail * 0.5))


def rocket(seed, length, bright):
    n = noise(length, seed)
    hiss = sweep_lowpass(n, 1200 * bright, 5000 * bright) * env(length, 0.03, length * 0.45)
    roar = band(noise(length, seed + 1), hi=500) * env(length, 0.01, length * 0.3) * 1.2
    pop = band(noise(length, seed + 2), 200, 3000) * env(length, 0.0005, 0.02)
    fade = np.clip((length - t_axis(length)) / (length * 0.4), 0, 1) ** 1.5
    return finish(fit(reverb((hiss + roar) * fade + pop * 1.5, 0.8, 0.2, seed), length + 0.4))


def mortar(seed):
    length = 0.6
    x = sine(190, 80, length) * env(length, 0.001, 0.08) * 1.2 + band(noise(length, seed), hi=900) * env(length, 0.002, 0.12)
    return finish(fit(reverb(x, 0.7, 0.25, seed), length + 0.4))


def explosion(seed, size):
    length = 0.7 + size * 0.9
    n = noise(length, seed)
    body = sweep_lowpass(n, 3500, 260) * env(length, 0.004, length * 0.35)
    sub = sine(70, 32, length) * env(length, 0.003, length * 0.4) * (0.8 + size * 0.6)
    rng = np.random.default_rng(seed + 9)
    crackle = np.zeros(len(n))
    for _ in range(int(40 * size + 10)):
        i = rng.integers(0, len(n) // 2)
        crackle[i: i + 30] += rng.uniform(-1, 1, len(crackle[i: i + 30])) * rng.uniform(0.2, 1.0)
    crackle = band(crackle, 800, 6000) * env(length, 0.001, length * 0.25) * 0.5
    return finish(fit(reverb(body + sub + crackle, 1.0 + size, 0.3, seed), length + 0.6 + size * 0.5))


# ---------------------------------------------------------------- loops

def rotor_loop():
    length = 1.0
    t = t_axis(length)
    beats = 0.5 + 0.5 * np.cos(2 * np.pi * 17 * t)
    chop = band(noise(length + 0.2, 21), hi=700)[: len(t)] * beats ** 6
    hum = np.sin(2 * np.pi * 34 * t) * 0.35 + np.sin(2 * np.pi * 68 * t) * 0.15
    whine = band(noise(length + 0.2, 22), 1800, 3200)[: len(t)] * 0.06
    x = np.concatenate([chop + hum + whine, (chop + hum + whine)[: int(0.1 * SR)]])
    return finish(loop_crossfade(x, 0.1), 0.8, 0)


def engine_loop():
    length = 2.0
    t = t_axis(length)
    f = 36
    saw = signal.sawtooth(2 * np.pi * f * t) + 0.5 * signal.sawtooth(2 * np.pi * f * 2 * t)
    knock = 0.75 + 0.25 * np.cos(2 * np.pi * 9 * t)
    rumble = band(saw, hi=320) * knock + band(noise(length, 31), hi=400) * 0.35
    x = np.concatenate([rumble, rumble[: int(0.1 * SR)]])
    return finish(loop_crossfade(x, 0.1), 0.7, 0)


def wind_loop():
    length = 8.0
    n = noise(length + 1, 41)
    x = sweep_lowpass(n, 300, 900, 32) * 0.6 + band(n, 300, 900) * 0.4
    x *= 0.6 + 0.4 * np.sin(2 * np.pi * t_axis(length + 1) / 4.0) ** 2
    return finish(loop_crossfade(x, 1.0), 0.6, 0)


def fire_loop():
    length = 3.0
    n = noise(length + 0.5, 51)
    base = band(n, hi=600) * 0.5
    rng = np.random.default_rng(52)
    crackle = np.zeros(len(n))
    for _ in range(90):
        i = rng.integers(0, len(n) - 60)
        crackle[i: i + 40] += rng.uniform(-1, 1, 40) * rng.uniform(0.3, 1.0)
    x = base + band(crackle, 1500, 6000) * 0.6
    return finish(loop_crossfade(x, 0.5), 0.6, 0)


# ---------------------------------------------------------------- stingers

def tone(freq, seconds, kind="saw", vibrato=0.0):
    t = t_axis(seconds)
    ph = 2 * np.pi * freq * t + vibrato * np.sin(2 * np.pi * 5 * t)
    if kind == "saw":
        return signal.sawtooth(ph)
    if kind == "tri":
        return signal.sawtooth(ph, 0.5)
    return np.sin(ph)


def horn():
    length = 2.2
    x = (tone(110, length, vibrato=0.6) + tone(164.8, length, vibrato=0.6) * 0.7 + tone(220, length) * 0.3)
    x = band(x, hi=900) * np.minimum(1, t_axis(length) / 0.25) * np.minimum(1, (length - t_axis(length)) / 0.6)
    return finish(reverb(x, 1.2, 0.3), 0.75)


def arpeggio(notes, step, kind, tail, decay):
    total = step * len(notes) + tail
    out = np.zeros(int(total * SR))
    for i, f in enumerate(notes):
        seg = tone(f, tail, kind) * env(tail, 0.005, decay)
        s = int(i * step * SR)
        out[s: s + len(seg)] += seg
    return finish(reverb(band(out, hi=5000), 0.9, 0.25), 0.7)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data/sfx"
    os.makedirs(out, exist_ok=True)
    sounds = {}
    for k in range(3):
        sounds["shot_mg_%d" % k] = gun(100 + k, 0.12, 0.012, 900, 7000, 160, 0.4, 0.35, 0.2)
        sounds["shot_heavy_%d" % k] = gun(110 + k, 0.2, 0.025, 400, 5000, 95, 0.9, 0.6)
        sounds["shot_auto_%d" % k] = gun(120 + k, 0.25, 0.035, 250, 4000, 70, 1.2, 0.8)
    for k in range(2):
        sounds["shot_cannon_%d" % k] = cannon(200 + k, 55, 0.9, 1100, 1.4)
        sounds["shot_artillery_%d" % k] = cannon(210 + k, 40, 1.4, 700, 2.2)
        sounds["shot_rocket_%d" % k] = rocket(220 + k, 0.9, 1.0)
        sounds["shot_missile_%d" % k] = rocket(230 + k, 1.2, 1.4)
        sounds["shot_mortar_%d" % k] = mortar(240 + k)
    for k in range(3):
        sounds["expl_small_%d" % k] = explosion(300 + k, 0.3)
        sounds["expl_medium_%d" % k] = explosion(310 + k, 0.8)
    for k in range(2):
        sounds["expl_large_%d" % k] = explosion(320 + k, 1.6)
    sounds["loop_rotor"] = rotor_loop()
    sounds["loop_engine"] = engine_loop()
    sounds["loop_wind"] = wind_loop()
    sounds["loop_fire"] = fire_loop()
    sounds["wave_horn"] = horn()
    sounds["victory"] = arpeggio([261.6, 329.6, 392.0, 523.3, 659.3], 0.16, "saw", 1.4, 0.5)
    sounds["defeat"] = arpeggio([392.0, 311.1, 261.6, 196.0], 0.3, "saw", 1.8, 0.7)
    sounds["level_up"] = arpeggio([523.3, 659.3, 784.0, 1046.5], 0.07, "tri", 0.6, 0.18)
    sounds["research"] = arpeggio([659.3, 987.8], 0.12, "sine", 1.0, 0.35)

    # processed Kenney CC0 sounds
    k = lambda pack, name: kenney(pack, name)  # noqa: E731
    for i in range(3):
        sounds["shot_void_%d" % i] = finish(reverb(pitch(k("scifi", "laserLarge_00%d" % i), 0.7), 0.6, 0.3))
        sounds["shot_frost_%d" % i] = finish(reverb(pitch(k("scifi", "forceField_00%d" % i), 1.3), 0.5, 0.3))
        sounds["hit_metal_%d" % i] = finish(k("impact", "impactMetal_light_00%d" % i), 0.6)
        sounds["build_%d" % i] = finish(k("impact", ["impactMetal_medium_000", "impactPlate_medium_001", "impactMining_002"][i]), 0.7)
        slime = pitch(k("scifi", "slime_00%d" % (i % 2)), 0.6)
        glitch = pitch(k("scifi", "glitch_00%d" % (i + 1)), 0.8)
        n = max(len(slime), len(glitch))
        sounds["void_death_%d" % i] = finish(reverb(fit(slime, n / SR) + fit(glitch, n / SR) * 0.6, 1.0, 0.4))
    sounds["expl_small_3"] = finish(k("scifi", "explosionCrunch_000"))
    sounds["ui_click"] = finish(k("interface", "click_002"), 0.6)
    sounds["ui_select"] = finish(k("interface", "select_002"), 0.55)
    sounds["ui_confirm"] = finish(k("interface", "confirmation_002"), 0.5)
    sounds["ui_error"] = finish(k("interface", "error_004"), 0.6)
    sounds["ui_open"] = finish(k("interface", "open_001"), 0.5)
    sounds["ui_close"] = finish(k("interface", "close_001"), 0.5)
    sounds["ui_build"] = finish(k("impact", "impactPlank_medium_001"), 0.6)
    sounds["point"] = finish(k("interface", "bong_001"), 0.6)
    sounds["promote"] = finish(k("interface", "maximize_006"), 0.6)
    for name, x in sounds.items():
        write(out, name, x)
    with open(os.path.join(out, "index.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sorted(sounds)) + "\n")
    print(len(sounds), "sounds written to", out)


if __name__ == "__main__":
    main()
