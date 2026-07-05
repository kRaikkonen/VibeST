# Fit a white-box pedal (SD-1 / TS808 / OD-1) to its real NAM. At LOW input the
# pedal is below the clipping threshold -> linear tone-shaping FR (tone knob +
# in/out filters), compared normalized to 1kHz. Higher input -> distortion.
# Usage: python fit_pedal.py "<nam>" kind drive tone level namLevel inScale
import sys, os, subprocess, struct, numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from nam_curve import build_probe, run_nam, analyze, FREQS, SR

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

nam = sys.argv[1]
kind, drive, tone, level = (sys.argv + ["1","0.5","0.5","0.7"])[2:6]
namLevel = float(sys.argv[6]) if len(sys.argv) > 6 else 0.003
inScale = sys.argv[7] if len(sys.argv) > 7 else "0.02"

xn, meta = build_probe(namLevel)
gnam, thdn = analyze(xn, run_nam(nam, xn), meta)

xu, _ = build_probe(1.0)
save_wav(os.path.join(VST, "renders", "probe_plexi.wav"), xu)
subprocess.run([os.path.join(VST, "test_pedal.exe"), kind, drive, tone, level, inScale],
               cwd=VST, check=True)
y = load_wav(os.path.join(VST, "renders", "pedal_out.wav"))
gme, thdm = analyze(xu * float(inScale), y, meta)

band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
gap = np.mean(np.abs(gme[band] - gnam[band]))
print(f"NAM {os.path.basename(nam)[:40]}")
print(f"kind={kind} drive={drive} tone={tone} lvl={level}  TONE-SHAPE gap 100-6k = {gap:.2f} dB")
print("freq   NAM   MINE  |thdN thdM")
for f, a, c, tn, tm in zip(FREQS, gnam, gme, thdn, thdm):
    print(f"{f:>5} {a:+6.1f} {c:+6.1f} | {tn:4.0f} {tm:4.0f}")
