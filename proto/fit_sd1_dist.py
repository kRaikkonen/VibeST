# SD-1 distortion-domain fit: drive a 220Hz tone at several input levels through
# the real SD-1 NAM and the white-box model; measure harmonic magnitudes H1..H6
# (dB rel H1) + THD. Asymmetric 2+1 clipping -> strong EVEN (2nd) harmonic.
# Find the input calibration that matches THD, then compare the harmonic shape.
import sys, os, json, numpy as np
SR = 48000; F0 = 220.0
NAM = r"C:\Users\Ziyu.Liu25\Documents\xwechat_files\wxid_h7gd25u6tiug12_70c5\msg\file\2026-07\newpack\newpack\Boss SD-1 - Level500 Tone1200 Drive500.nam"

def tone(amp, n=int(0.4 * SR)):
    t = np.arange(n) / SR
    env = np.ones(n); r = int(0.03 * SR)
    env[:r] = 0.5 - 0.5 * np.cos(np.pi * np.arange(r) / r); env[-r:] = env[:r][::-1]
    return (amp * np.sin(2 * np.pi * F0 * t) * env).astype(np.float32)

def harm(y):
    s = y[3000:-3000]; w = np.hanning(len(s)); sp = np.abs(np.fft.rfft(s * w)); fr = np.fft.rfftfreq(len(s), 1 / SR)
    H = []
    for k in range(1, 8):
        i = np.argmin(np.abs(fr - k * F0)); H.append(sp[max(i-2,0):i+3].max())
    H = np.array(H); thd = np.sqrt(np.sum(H[1:] ** 2)) / H[0] * 100
    return 20 * np.log10(H / H[0] + 1e-9), thd

def run_nam(path, x):
    import torch
    from nam.models._from_nam import init_from_nam
    d = json.load(open(path)); sm = max(d["config"]["submodels"], key=lambda s: s["max_value"])["model"]
    m = init_from_nam(sm); m.eval()
    return m(torch.from_numpy(x)).detach().cpu().numpy().ravel()[:len(x)]

if __name__ == "__main__":
    cache = "_sd1_dist_nam.npz"
    namlv = [0.03, 0.06, 0.12, 0.25]
    if os.path.exists(cache):
        z = np.load(cache); NAMH = z["H"]; NAMT = z["thd"]
    else:
        NAMH = []; NAMT = []
        for a in namlv:
            H, thd = harm(run_nam(NAM, tone(a))); NAMH.append(H); NAMT.append(thd)
        NAMH = np.array(NAMH); NAMT = np.array(NAMT); np.savez(cache, H=NAMH, thd=NAMT)
    print("== real SD-1 NAM (220Hz, noon, max drive) ==")
    print("in     THD   H2   H3   H4   H5   H6  (dB rel H1)")
    for a, H, t in zip(namlv, NAMH, NAMT):
        print(f"{a:.3f} {t:5.0f}% " + " ".join(f"{v:+4.0f}" for v in H[1:6]))

    import sd1sim
    from od1sim import OD1Params
    p = sd1sim.sd1_params()
    print("== my white-box SD-1 ==")
    print("in     THD   H2   H3   H4   H5   H6")
    for a in [float(x) for x in sys.argv[1:]] or [0.03, 0.06, 0.12, 0.25, 0.5]:
        ped = sd1sim.SD1Pedal(p, SR, drive=1.0, tone=0.5, level=1.0, buffers=False)
        H, t = harm(ped.process(tone(a).astype(float)))
        print(f"{a:.3f} {t:5.0f}% " + " ".join(f"{v:+4.0f}" for v in H[1:6]))
