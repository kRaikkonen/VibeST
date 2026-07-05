# Measure any NAM's clean-tone transfer function (voicing) at a given input
# level, plus a distortion proxy (THD = non-fundamental energy in each tone
# segment). Usage: python nam_curve.py "<path.nam>" [level1 level2 ...]
import sys, json, numpy as np, struct, os
import torch
from nam.models._from_nam import init_from_nam

SR = 48000
FREQS = [82, 110, 165, 233, 330, 466, 659, 1031, 1471, 2131, 2971, 4127, 5333, 6469, 8237]

def build_probe(amp):
    segs = []; meta = {}; cur = 0
    for f in FREQS:
        n = int(0.18 * SR); t = np.arange(n) / SR
        env = np.ones(n); r = int(0.02 * SR)
        env[:r] = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r); env[-r:] = env[:r][::-1]
        segs.append((amp * np.sin(2 * np.pi * f * t) * env).astype(np.float32))
        meta[f] = (cur, cur + n); cur += n
        segs.append(np.zeros(int(0.05 * SR), np.float32)); cur += int(0.05 * SR)
    return np.concatenate(segs), meta

def run_nam(path, x):
    d = json.load(open(path)); sm = max(d["config"]["submodels"], key=lambda s: s["max_value"])["model"]
    m = init_from_nam(sm); m.eval()
    with torch.no_grad():
        y = m(torch.from_numpy(x.astype(np.float32))).cpu().numpy().ravel()
    return y[:len(x)]

def analyze(x, y, meta):
    g = []; thd = []
    for f in FREQS:
        a0, a1 = meta[f]
        xs = x[a0 + 1000:a1 - 1000]; ys = y[a0 + 1000:a1 - 1000]
        t = np.arange(len(ys)) / SR; c = np.exp(-2j * np.pi * f * t)
        go = np.abs(np.sum(ys * c)) / len(ys) * 2
        gi = np.abs(np.sum(xs * c)) / len(xs) * 2
        g.append(20 * np.log10(go / gi) if gi > 0 else -120)
        fund = go ** 2 / 2; tot = np.mean(ys ** 2)   # sine mean-power = A^2/2
        thd.append(100 * np.sqrt(max(tot - fund, 0) / fund) if fund > 0 else 0)
    g = np.array(g); g -= g[FREQS.index(1031)]
    return g, np.array(thd)

if __name__ == "__main__":
    path = sys.argv[1]
    levels = [float(a) for a in sys.argv[2:]] or [0.01]
    print("NAM:", os.path.basename(path))
    for lv in levels:
        x, meta = build_probe(lv)
        y = run_nam(path, x)
        g, thd = analyze(x, y, meta)
        print(f"-- level {lv:.4f} --")
        print("  gain: " + " ".join(f"{f}:{v:+.1f}" for f, v in zip(FREQS, g)))
        print("  THD%: " + " ".join(f"{f}:{v:.0f}" for f, v in zip(FREQS, thd)))
