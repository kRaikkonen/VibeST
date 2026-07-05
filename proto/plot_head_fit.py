# Visual proof: overlay my white-box Princeton head (into a flat load, like the
# NAM capture) against the real 1964 Princeton NAM head, clean-tone transfer fn.
import sys, os, json, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
from princeton import PrincetonReverb

SR = 48000
FR = [82, 110, 147, 196, 262, 330, 440, 587, 784, 1047, 1319, 1568,
      2093, 2637, 3136, 3729, 4186, 4978, 5588, 6272, 7040, 7902, 9000]
amp = 0.04
segs = []; meta = {}; cur = 0
for f in FR:
    n = int(0.13 * SR); t = np.arange(n) / SR
    env = np.ones(n); r = int(0.02 * SR)
    env[:r] = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r); env[-r:] = env[:r][::-1]
    segs.append(amp * np.sin(2 * np.pi * f * t) * env)
    meta[f] = (cur, cur + n); cur += n
    segs.append(np.zeros(int(0.04 * SR))); cur += int(0.04 * SR)
x = np.concatenate(segs).astype(np.float32)

def curve(y):
    g = []
    for f in FR:
        a0, a1 = meta[f]
        def A(s):
            ss = s[a0 + 1200:a1 - 1200]; t = np.arange(len(ss)) / SR
            return np.abs(np.sum(ss * np.exp(-2j * np.pi * f * t))) / len(ss) * 2
        g.append(20 * np.log10(A(y) / A(x)))
    g = np.array(g); k = int(np.argmin([abs(f - 1047) for f in FR])); return g - g[k]

# my head: flat load (like NAM), calibrated defaults, light NFB -> off (see notes)
a = PrincetonReverb(SR, volume=0.4, treble=0.4, bass=0.2, reverb=0.0, flat_load=True)
a.R_nfb = 1e12
gme = curve(a.process(x.astype(np.float64)))

# NAM real head
import torch
from nam.models._from_nam import init_from_nam
namf = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\1964 Fender Princeton Head NAM\64 Princeton Clean - Vol 4, Tone 4.nam"
d = json.load(open(namf)); sm = max(d["config"]["submodels"], key=lambda s: s["max_value"])["model"]
m = init_from_nam(sm); m.eval()
with torch.no_grad():
    yn = m(torch.from_numpy(x)).cpu().numpy().ravel()
gnam = curve(yn[:len(x)])

gap = np.mean(np.abs(gme - gnam))
print(f"HEAD clean-tone gap 82Hz-9kHz = {gap:.2f} dB")
plt.figure(figsize=(11, 5.5))
plt.semilogx(FR, gnam, 'o-', color='#1f77b4', lw=2.4, ms=6, label="REAL 1964 Princeton (NAM head)")
plt.semilogx(FR, gme, 's--', color='#d62728', lw=2.0, ms=5, label="My white-box head (flat load)")
plt.title(f"Princeton head — clean-tone transfer function (dB rel 1kHz)   |   mean gap = {gap:.2f} dB")
plt.xlabel("Hz"); plt.ylabel("dB"); plt.grid(True, which='both', alpha=.3); plt.legend(loc='upper left')
plt.xlim(80, 9500); plt.ylim(-6, 9)
plt.xticks([100, 200, 500, 1000, 2000, 5000, 9000], ["100", "200", "500", "1k", "2k", "5k", "9k"])
plt.tight_layout()
out = r"D:\sd1\vst\renders\princeton_head_fit.png"
plt.savefig(out, dpi=100); print("wrote", out)
