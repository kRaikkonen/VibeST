# Overlay white-box Python SD-1 vs real SD-1 NAM tone-shaping (3 Tone settings).
import numpy as np
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from nam_curve import build_probe, analyze, FREQS
import sd1sim

z = np.load("_sd1_tone.npz"); nam = {k: z[k] for k in z.files}
p = sd1sim.sd1_params()
x, meta = build_probe(0.04)
cols = {"T0.35": "#1f77b4", "T0.50": "#2ca02c", "T0.65": "#d62728"}
plt.figure(figsize=(11, 5.5))
tot = 0
for name, tv in [("T0.35", 0.35), ("T0.50", 0.50), ("T0.65", 0.65)]:
    ped = sd1sim.SD1Pedal(p, 48000.0, drive=1.0, tone=tv, level=1.0, buffers=False)
    g, _ = analyze(x, ped.process(x.astype(float)), meta)
    band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
    gap = np.mean(np.abs(g[band] - nam[name][band])); tot += gap
    c = cols[name]
    plt.semilogx(FREQS, nam[name], 'o-', color=c, lw=2.2, label=f"NAM {name}")
    plt.semilogx(FREQS, g, 's--', color=c, lw=1.6, alpha=0.8, label=f"mine {name} ({gap:.1f}dB)")
plt.title(f"Boss SD-1 tone-shaping — white-box Python vs real NAM  |  mean gap {tot/3:.2f} dB")
plt.xlabel("Hz"); plt.ylabel("dB rel 1kHz"); plt.grid(True, which='both', alpha=.3)
plt.legend(ncol=3, fontsize=8); plt.xlim(80, 9000); plt.ylim(-25, 5)
plt.xticks([100, 200, 500, 1000, 2000, 5000], ["100", "200", "500", "1k", "2k", "5k"])
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\sd1_fit.png", dpi=100)
print(f"wrote sd1_fit.png, mean gap {tot/3:.2f} dB")
