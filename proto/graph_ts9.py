# Overlay white-box Python TS9 vs real TS9 NAM tone-shaping (Drive 0, 3 Tone).
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from nam_curve import build_probe, analyze, FREQS
import ts9sim

z = np.load("_ts9_tone.npz"); nam = {k: z[k] for k in z.files}
p = ts9sim.ts9_params()
x, meta = build_probe(0.01)
cols = {"T0.6": "#1f77b4", "T0.7": "#2ca02c", "T0.9": "#d62728"}
plt.figure(figsize=(11, 5.5))
tot = 0
for name, tv in [("T0.6", 0.6), ("T0.7", 0.7), ("T0.9", 0.9)]:
    ped = ts9sim.TS9Pedal(p, 48000.0, drive=0.0, tone=tv, level=1.0, buffers=False)
    g, _ = analyze(x, ped.process(x.astype(float)), meta)
    band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
    gap = np.mean(np.abs(g[band] - nam[name][band])); tot += gap
    c = cols[name]
    plt.semilogx(FREQS, nam[name], 'o-', color=c, lw=2.2, label=f"NAM {name}")
    plt.semilogx(FREQS, g, 's--', color=c, lw=1.6, alpha=0.8, label=f"mine {name} ({gap:.1f}dB)")
plt.title(f"Ibanez TS9 tone-shaping (Drive 0) — white-box Python vs real NAM  |  mean {tot/3:.2f} dB")
plt.xlabel("Hz"); plt.ylabel("dB rel 1kHz"); plt.grid(True, which='both', alpha=.3)
plt.legend(ncol=3, fontsize=8); plt.xlim(80, 9000); plt.ylim(-25, 5)
plt.xticks([100, 200, 500, 1000, 2000, 5000], ["100", "200", "500", "1k", "2k", "5k"])
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\ts9_fit.png", dpi=100)
print(f"wrote ts9_fit.png, mean gap {tot/3:.2f} dB")
