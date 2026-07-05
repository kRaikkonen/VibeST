# Fit white-box TS9 tone-shaping to cached TS9 NAM tone sweep (_ts9_tone.npz:
# T0.6/T0.7/T0.9 at Drive 0). Usage: python fit_ts9.py [FC] [GLO] [GHI] [inLvl]
import sys, numpy as np
from nam_curve import build_probe, analyze, FREQS
import ts9sim

FC = float(sys.argv[1]) if len(sys.argv) > 1 else ts9sim.TS9Pedal.TONE_FC
GLO = float(sys.argv[2]) if len(sys.argv) > 2 else ts9sim.TS9Pedal.TONE_GLO
GHI = float(sys.argv[3]) if len(sys.argv) > 3 else ts9sim.TS9Pedal.TONE_GHI
inLevel = float(sys.argv[4]) if len(sys.argv) > 4 else 0.01

z = np.load("_ts9_tone.npz"); nam = {k: z[k] for k in z.files}
ts9sim.TS9Pedal.TONE_FC = FC; ts9sim.TS9Pedal.TONE_GLO = GLO; ts9sim.TS9Pedal.TONE_GHI = GHI
p = ts9sim.ts9_params()
x, meta = build_probe(inLevel)
print(f"FC={FC} GLO={GLO} GHI={GHI} inLevel={inLevel}")
print("      " + " ".join(f"{f:>5}" for f in FREQS))
tot = 0.0
for name, tv in {"T0.6": 0.6, "T0.7": 0.7, "T0.9": 0.9}.items():
    ped = ts9sim.TS9Pedal(p, 48000.0, drive=0.0, tone=tv, level=1.0, buffers=False)
    g, thd = analyze(x, ped.process(x.astype(float)), meta)
    band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
    gap = np.mean(np.abs(g[band] - nam[name][band])); tot += gap
    print(f"{name} NAM " + " ".join(f"{v:+5.1f}" for v in nam[name]))
    print(f"{name} me  " + " ".join(f"{v:+5.1f}" for v in g) + f"   gap={gap:.2f}  maxTHD={thd.max():.0f}%")
print(f"MEAN gap = {tot/3:.2f} dB")
