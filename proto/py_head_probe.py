# Get it right in Python first: drive the ORIGINAL validated Python Princeton
# head with clean tones and compare its frequency response to the real-amp NAM.
# Diagnoses whether the too-dark/jagged HF is the MODEL (present in Python too)
# or a C++ divergence.
import sys, os, numpy as np, json, struct
sys.path.insert(0, os.path.dirname(__file__))
from princeton import PrincetonReverb, TriodeStageRT
from tubes import T_12AX7
from mna import fender_tone_stack

SR = 48000
freqs = [233, 467, 1031, 1471, 2131, 2971, 3529, 4127, 4729, 5333, 5927, 6469, 7333, 8237]
amp = 0.04
segs = []; meta = {}; cur = 0
for f in freqs:
    n = int(0.15 * SR); t = np.arange(n) / SR
    env = np.ones(n); r = int(0.02 * SR)
    env[:r] = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r); env[-r:] = env[:r][::-1]
    s = amp * np.sin(2 * np.pi * f * t) * env
    meta[f] = (cur, cur + n); segs.append(s); cur += n
    segs.append(np.zeros(int(0.05 * SR))); cur += int(0.05 * SR)
x = np.concatenate(segs).astype(np.float32)
# save the EXACT probe so the C++ engine renders the identical signal
import wave as _w
def _wav(p, v):
    d = v.astype(np.float32).tobytes()
    with open(p, 'wb') as f:
        f.write(b'RIFF'); f.write(struct.pack('<I', 36+len(d))); f.write(b'WAVE')
        f.write(b'fmt '); f.write(struct.pack('<IHHIIHH', 16, 3, 1, SR, SR*4, 4, 32))
        f.write(b'data'); f.write(struct.pack('<I', len(d))); f.write(d)
_wav(r"D:\sd1\vst\renders\probe.wav", x)

def gain_curve(y, label):
    g = []
    for f in freqs:
        a0, a1 = meta[f]
        def A(sig):
            s = sig[a0 + 1500:a1 - 1500]; t = np.arange(len(s)) / SR
            return np.abs(np.sum(s * np.exp(-2j * np.pi * f * t))) / len(s) * 2
        gi = A(x); go = A(y); g.append(20 * np.log10(go / gi) if gi > 0 else -120)
    g = np.array(g); k1 = freqs.index(1031); g -= g[k1]
    print(f"{label}: " + " ".join(f"{f}:{gg:+.1f}" for f, gg in zip(freqs, g)))
    return g

# --- Python PREAMP only (V1A -> tone stack -> V1B): the voicing ---
amp_py = PrincetonReverb(SR, volume=0.4, treble=0.4, bass=0.4, reverb=0.0)
v1a_out = np.array([amp_py.v1a.step(v) for v in x])
vtone = amp_py.stack.process(v1a_out)
v1b_out = np.array([amp_py.v1b.step(v) for v in vtone])
print("=== Python head, tone=0.4, level %.3f V ===" % amp)
gain_curve(v1a_out, "  v1a      ")
gain_curve(v1b_out, "  preamp(v1b)")
# full Python head into a FLAT LOAD (load box), like the NAM capture.
# Sweep the NFB loop corner: 250Hz (old stability hack) cuts only lows;
# broadband is physically correct and evens the balance.
for nfb in (250.0, 2000.0, 8000.0):
    a = PrincetonReverb(SR, volume=0.4, treble=0.4, bass=0.4, reverb=0.0,
                        flat_load=True)
    a.nfb_hz = nfb
    yf = a.process(x.astype(np.float64))
    if nfb == 8000.0:
        _wav(r"D:\sd1\vst\renders\py_head.wav", yf.astype(np.float32))
    gain_curve(yf, f"  head(flat,nfb{int(nfb)})")

# --- NAM reference on the same probe ---
try:
    import torch
    from nam.models._from_nam import init_from_nam
    namf = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\1964 Fender Princeton Head NAM\64 Princeton Clean - Vol 4, Tone 4.nam"
    d = json.load(open(namf)); sm = max(d["config"]["submodels"], key=lambda s: s["max_value"])["model"]
    m = init_from_nam(sm); m.eval()
    with torch.no_grad():
        yn = m(torch.from_numpy(x.astype(np.float32))).cpu().numpy().ravel()
    gain_curve(yn[:len(x)], "  NAM (real)  ")
except Exception as e:
    print("NAM skipped:", e)
