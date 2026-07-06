"""Overlay the white-box Dual Rectifier (Rhythm/Orange) against the real-amp NAM,
Princeton-style: clean-tone probe -> gain curve (voicing) + THD (distortion), compare.
It's a CLIPPING match (no clean region), so we align on BOTH the voicing tilt and the
THD-vs-frequency shape at a matched drive. NAM curve cached to disk.

Usage: python fit_recti.py [gain treble mid bass in_scale level]
"""
import sys, os, numpy as np
from nam_curve import build_probe, analyze, FREQS, run_nam
from rectifier import DualRectifier

NAM = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\newpack\newpack\1. MESA DUAL RECTIFIER 2025 _ CRUNCH _ RHYTHM #1.nam"
FS = 48000

gain     = float(sys.argv[1]) if len(sys.argv) > 1 else 0.5
treble   = float(sys.argv[2]) if len(sys.argv) > 2 else 0.5
mid      = float(sys.argv[3]) if len(sys.argv) > 3 else 0.4
bass     = float(sys.argv[4]) if len(sys.argv) > 4 else 0.4
in_scale = float(sys.argv[5]) if len(sys.argv) > 5 else 3.0
level    = float(sys.argv[6]) if len(sys.argv) > 6 else 0.01

# --- NAM reference (cached per level) ---
xn, meta = build_probe(level)
cache = os.path.join(os.path.dirname(__file__), f"_recti_nam_{level:.4f}.npz")
if os.path.exists(cache):
    z = np.load(cache); gnam, tnam = z["g"], z["t"]
else:
    yn = run_nam(NAM, xn); gnam, tnam = analyze(xn, yn, meta)
    np.savez(cache, g=gnam, t=tnam)

# --- white-box ---
amp = DualRectifier(FS, gain=gain, treble=treble, mid=mid, bass=bass, in_scale=in_scale)
ym = amp.process(xn)
gme, tme = analyze(xn, ym, meta)

# --- overlay ---
print(f"gain={gain} treble={treble} mid={mid} bass={bass} in_scale={in_scale} level={level}")
print(f"{'freq':>6} {'NAM_g':>7} {'MINE_g':>7} {'d':>6} | {'NAM_thd':>7} {'MINE_thd':>8}")
for f, a, c, ta, tc in zip(FREQS, gnam, gme, tnam, tme):
    print(f"{f:>6} {a:>+6.1f} {c:>+6.1f} {c-a:>+6.1f} | {ta:>6.0f}% {tc:>7.0f}%")
band = [i for i, f in enumerate(FREQS) if 110 <= f <= 6469]
vgap = np.mean(np.abs(gme[band] - gnam[band]))
tgap = np.mean(np.abs(tme[band] - tnam[band]))
print(f"voicing gap = {vgap:.1f} dB | THD gap = {tgap:.0f}%  (Princeton std: voicing <~1.5 dB)")
