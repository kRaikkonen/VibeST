"""Find the Plexi output 'voicing' filters (bass HP + presence peak + top rolloff) that
turn the STABLE (nfb=0) plexi render into the real NAM voicing. The +12dB @2-4kHz peak
is the NFB+OT presence resonance, which self-oscillates in the discretized feedback loop
(THD blew to 1000s%); we model that real resonance as a stable peaking biquad instead
(fc/Q/gain identified from the NAM = measuring the resonance, like the Recti OT biquad).
Tune here in Python, then bake the winning values into plexi.hpp.

Usage: python fit_plexi_post.py hp_fc peak_fc peak_gaindB peak_Q lp_fc
"""
import sys, os, subprocess, struct, numpy as np
from scipy.signal import lfilter
sys.path.insert(0, os.path.dirname(__file__))
from nam_curve import build_probe, analyze, FREQS, SR

VST = r"D:\sd1\vst"
gnam = np.load(os.path.join(os.path.dirname(__file__), "_plexi_nam_curve.npy"))

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
hp_fc   = float(sys.argv[1]) if len(sys.argv) > 1 else 130.0
pk_fc   = float(sys.argv[2]) if len(sys.argv) > 2 else 3000.0
pk_gain = float(sys.argv[3]) if len(sys.argv) > 3 else 11.0
pk_Q    = float(sys.argv[4]) if len(sys.argv) > 4 else 0.7
lp_fc   = float(sys.argv[5]) if len(sys.argv) > 5 else 7000.0

# render the STABLE plexi (nfb=0) once
xu, _ = build_probe(1.0)
save_wav(os.path.join(VST, "renders", "probe_plexi.wav"), xu)
subprocess.run([os.path.join(VST, "test_plexi.exe"), "0.6", "0.7", "0.25", "0.4",
                "0.001", "0", "0", "0", "0"], cwd=VST, check=True)
y = load_wav(os.path.join(VST, "renders", "plexi_head.wav"))

shelf_g = float(sys.argv[6]) if len(sys.argv) > 6 else 8.0
def rbj(kind, fc, Q, gdb=0.0):
    w0 = 2*np.pi*fc/SR; c, s = np.cos(w0), np.sin(w0); al = s/(2*Q); A = 10**(gdb/40)
    if kind == "hp":
        b = [(1+c)/2, -(1+c), (1+c)/2]; a = [1+al, -2*c, 1-al]
    elif kind == "lp":
        b = [(1-c)/2, 1-c, (1-c)/2]; a = [1+al, -2*c, 1-al]
    elif kind == "hs":                       # high-shelf (RBJ), slope S=1
        al = s/2*np.sqrt(2); sA = np.sqrt(A)
        b = [A*((A+1)+(A-1)*c+2*sA*al), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-2*sA*al)]
        a = [(A+1)-(A-1)*c+2*sA*al, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-2*sA*al]
    else:  # peak
        b = [1+al*A, -2*c, 1-al*A]; a = [1+al/A, -2*c, 1-al/A]
    return np.array(b)/a[0], np.array(a)/a[0]

for k, fc, Q, g in [("hp", hp_fc, 0.9, 0), ("hs", 600.0, 0.7, shelf_g),
                    ("peak", pk_fc, pk_Q, pk_gain), ("lp", lp_fc, 0.8, 0)]:
    b, a = rbj(k, fc, Q, g); y = lfilter(b, a, y)

gme, thd = analyze(xu * 0.001, y, meta := build_probe(1.0)[1])
band = [i for i, f in enumerate(FREQS) if 100 <= f <= 6000]
gap = np.mean(np.abs(gme[band] - gnam[band]))
print(f"hp={hp_fc} peak={pk_fc}/{pk_gain}dB/Q{pk_Q} lp={lp_fc}  VOICING gap = {gap:.2f} dB")
print("freq   NAM   MINE   d")
for f, a2, c in zip(FREQS, gnam, gme):
    print(f"{f:>5} {a2:+6.1f} {c:+6.1f} {c-a2:+6.1f}")
