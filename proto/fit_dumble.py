"""Overlay the white-box Dumble SSS against the (trusted) NAM. Clean amp -> match the
glassy voicing (bass cut + upper-mid/treble hump + top rolloff). Princeton-style probe.
Usage: python fit_dumble.py [drive treble mid bass in_scale level]
"""
import sys, os, numpy as np
from nam_curve import build_probe, analyze, FREQS, run_nam
from dumble import DumbleSSS

NAM = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\z\Dumble Steel SS Clean.nam"
FS = 48000
drive   = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
treble  = float(sys.argv[2]) if len(sys.argv) > 2 else 0.6
mid     = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
bass    = float(sys.argv[4]) if len(sys.argv) > 4 else 0.4
inscale = float(sys.argv[5]) if len(sys.argv) > 5 else 1.0
level   = float(sys.argv[6]) if len(sys.argv) > 6 else 0.01

xn, meta = build_probe(level)
cache = os.path.join(os.path.dirname(__file__), f"_dumble_nam_{level:.4f}.npz")
if os.path.exists(cache):
    z = np.load(cache); gnam = z["g"]
else:
    gnam, _ = analyze(xn, run_nam(NAM, xn), meta); np.savez(cache, g=gnam)

ot_fc = float(sys.argv[7]) if len(sys.argv) > 7 else 4200.0
ot_q  = float(sys.argv[8]) if len(sys.argv) > 8 else 1.0
amp = DumbleSSS(FS, drive=drive, treble=treble, mid=mid, bass=bass, in_scale=inscale,
                ot_fc=ot_fc, ot_q=ot_q)
gme, thd = analyze(xn, amp.process(xn), meta)
print(f"drive={drive} t={treble} m={mid} b={bass} in={inscale}")
print(f"{'freq':>5} {'NAM':>6} {'MINE':>6} {'d':>6}")
for f, a, c in zip(FREQS, gnam, gme):
    print(f"{f:>5} {a:+6.1f} {c:+6.1f} {c-a:+6.1f}")
band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6469]
print(f"VOICING gap 110-6k = {np.mean(np.abs(gme[band]-gnam[band])):.2f} dB")
