# Fit the white-box Python SD-1 tone-shaping to the cached real-SD-1 NAM tone
# sweep (_sd1_tone.npz: T0.35/T0.50/T0.65 at Drive max, low level = linear).
# Usage: python fit_sd1.py [FC] [GDB] [inLevel]
import sys, numpy as np
from nam_curve import build_probe, analyze, FREQS
from od1sim import OD1Params
import sd1sim

FC = float(sys.argv[1]) if len(sys.argv) > 1 else sd1sim.SD1Pedal.TONE_FC
GDB = float(sys.argv[2]) if len(sys.argv) > 2 else sd1sim.SD1Pedal.TONE_GDB
inLevel = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0006

z = np.load("_sd1_tone.npz"); nam = {k: z[k] for k in z.files}
sd1sim.SD1Pedal.TONE_FC = FC
sd1sim.SD1Pedal.TONE_GDB = GDB
p = sd1sim.sd1_params()
x, meta = build_probe(inLevel)

print(f"FC={FC} GDB={GDB} inLevel={inLevel}")
print("      " + " ".join(f"{f:>5}" for f in FREQS))
tones = {"T0.35": 0.35, "T0.50": 0.50, "T0.65": 0.65}
tot = 0.0
for name, tv in tones.items():
    ped = sd1sim.SD1Pedal(p, 48000.0, drive=1.0, tone=tv, level=1.0, buffers=False)
    g, thd = analyze(x, ped.process(x.astype(float)), meta)
    gnam = nam[name]
    band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
    gap = np.mean(np.abs(g[band] - gnam[band])); tot += gap
    print(f"{name} NAM " + " ".join(f"{v:+5.1f}" for v in gnam))
    print(f"{name} me  " + " ".join(f"{v:+5.1f}" for v in g) + f"   gap={gap:.2f}  maxTHD={thd.max():.0f}%")
print(f"MEAN gap = {tot/3:.2f} dB")
