# Fast Princeton head-fit harness: drive the full Python head (flat load, like
# the NAM capture) with a short clean-tone probe and print its gain curve next
# to the real-amp NAM. Iterate white-box params until the two lines overlap.
import sys, os, json, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from princeton import PrincetonReverb

SR = 48000
FREQS = [117, 233, 467, 1031, 1471, 2131, 2971, 4127, 5333, 6469, 8237]
amp = 0.04
segs = []; meta = {}; cur = 0
for f in FREQS:
    n = int(0.13 * SR); t = np.arange(n) / SR
    env = np.ones(n); r = int(0.02 * SR)
    env[:r] = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r); env[-r:] = env[:r][::-1]
    segs.append(amp * np.sin(2 * np.pi * f * t) * env)
    meta[f] = (cur, cur + n); cur += n
    segs.append(np.zeros(int(0.04 * SR))); cur += int(0.04 * SR)
x = np.concatenate(segs)

def curve(y):
    g = []
    for f in FREQS:
        a0, a1 = meta[f]
        def A(s):
            ss = s[a0 + 1200:a1 - 1200]; t = np.arange(len(ss)) / SR
            return np.abs(np.sum(ss * np.exp(-2j * np.pi * f * t))) / len(ss) * 2
        g.append(20 * np.log10(A(y) / A(x)))
    g = np.array(g); g -= g[FREQS.index(1031)]
    return g

def show(g, lab):
    print(lab.ljust(20) + " ".join(f"{f}:{v:+.1f}" for f, v in zip(FREQS, g)), flush=True)

# NAM reference (cached to disk so we don't reload torch every run)
NAMCACHE = os.path.join(os.path.dirname(__file__), "_nam_curve.json")
if os.path.exists(NAMCACHE):
    gnam = np.array(json.load(open(NAMCACHE)))
else:
    import torch
    from nam.models._from_nam import init_from_nam
    namf = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\1964 Fender Princeton Head NAM\64 Princeton Clean - Vol 4, Tone 4.nam"
    d = json.load(open(namf)); sm = max(d["config"]["submodels"], key=lambda s: s["max_value"])["model"]
    m = init_from_nam(sm); m.eval()
    with torch.no_grad():
        yn = m(torch.from_numpy(x.astype(np.float32))).cpu().numpy().ravel()
    gnam = curve(yn[:len(x)])
    json.dump(gnam.tolist(), open(NAMCACHE, "w"))
show(gnam, "NAM(real)")

# args: treble bass [nfb|off] [leak_khz]
treble = float(sys.argv[1]) if len(sys.argv) > 1 else 0.4
bass = float(sys.argv[2]) if len(sys.argv) > 2 else 0.2
nfbarg = sys.argv[3] if len(sys.argv) > 3 else "off"
leak = float(sys.argv[4]) * 1e3 if len(sys.argv) > 4 else 20e3
Lm = float(sys.argv[5]) if len(sys.argv) > 5 else 15.0
a = PrincetonReverb(SR, volume=0.4, treble=treble, bass=bass, reverb=0.0, flat_load=True)
a.power.Lm = Lm
if nfbarg == "off":
    a.R_nfb = 1e12
else:
    a.nfb_hz = float(nfbarg)
# retune OT leakage rolloff corner
import numpy as _np
wl = 2 * _np.pi * leak; T = a.power.T
a.power.lk_b = wl * T / (2.0 + wl * T); a.power.lk_a = (2.0 - wl * T) / (2.0 + wl * T)
g = curve(a.process(x.astype(np.float64)))
band = [i for i, f in enumerate(FREQS) if 100 <= f <= 8300]
gap = np.mean(np.abs(g[band] - gnam[band]))
show(g, f"t{treble} b{bass} nfb{nfbarg} lk{int(leak/1e3)}k Lm{Lm:g} gap{gap:.2f}")
