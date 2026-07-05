# Fit the white-box Plexi voicing to the real Plexi 51 NAM head. Compares the
# LINEAR voicing (both amps normalized to 1kHz in their linear region, so the
# absolute gain/level cancels). Runs test_plexi.exe (bare plexi::Amp, flat load)
# and overlays vs the NAM. Usage: python fit_plexi.py gain treble bass mid inScale
import sys, os, subprocess, struct, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from nam_curve import build_probe, run_nam, analyze, FREQS, SR

NAM = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\pp(1)\pp\Plexi Head\Plexi 51.nam"
VST = r"D:\sd1\vst"

def save_wav(p, v):
    d = v.astype(np.float32).tobytes()
    with open(p, "wb") as f:
        f.write(b"RIFF"); f.write(struct.pack("<I", 36 + len(d))); f.write(b"WAVE")
        f.write(b"fmt "); f.write(struct.pack("<IHHIIHH", 16, 3, 1, SR, SR * 4, 4, 32))
        f.write(b"data"); f.write(struct.pack("<I", len(d))); f.write(d)

def load_wav(p):
    b = open(p, "rb").read(); i = b.find(b"data"); n = struct.unpack("<I", b[i+4:i+8])[0]
    ch = struct.unpack("<H", b[22:24])[0]
    v = np.frombuffer(b[i+8:i+8+n], "<f4").astype(float)
    return v[::ch] if ch > 1 else v

# NAM voicing (cache; measured in its linear region ~0.0008)
NAMLV = 0.0008
xn, meta = build_probe(NAMLV)
cache = os.path.join(os.path.dirname(__file__), "_plexi_nam_curve.npy")
if os.path.exists(cache):
    gnam = np.load(cache)
else:
    gnam, _ = analyze(xn, run_nam(NAM, xn), meta); np.save(cache, gnam)

# unit probe for test_plexi (level 1.0; harness scales by inScale)
xu, _ = build_probe(1.0)
save_wav(os.path.join(VST, "renders", "probe_plexi.wav"), xu)

args = (sys.argv + ["0.6","0.6","0.4","0.5","0.001","0","0.5","0","0"])[1:10]
gain, treble, bass, mid, inScale, bright, presence, otbw, nfb = args
subprocess.run([os.path.join(VST, "test_plexi.exe"), gain, treble, bass, mid,
                inScale, bright, presence, otbw, nfb], cwd=VST, check=True)
y = load_wav(os.path.join(VST, "renders", "plexi_head.wav"))
gme, thd = analyze(xu * float(inScale), y, meta)

band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
gap = np.mean(np.abs(gme[band] - gnam[band]))
print(f"gain={gain} t={treble} b={bass} m={mid} in={inScale}  VOICING gap 100-6k = {gap:.2f} dB")
print("freq   NAM    MINE   THD%")
for f, a, c, t in zip(FREQS, gnam, gme, thd):
    print(f"{f:>5} {a:+6.1f} {c:+6.1f}  {t:4.0f}")
