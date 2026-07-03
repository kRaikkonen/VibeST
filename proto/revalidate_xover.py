import numpy as np
from od1sim import OD1Params, DriveStageRT, drive_stage_reference, TL072, RC3403A

FS = 48000 * 8
F0 = 220.0
DUR = 0.03
p = OD1Params()
n = int(DUR * FS)
t = np.arange(n) / FS
settle = n // 2

worst = 0.0
for drive in (0.0, 1.0):
    for amp in (0.05, 0.5):
        vf = lambda tt: amp * np.sin(2 * np.pi * F0 * tt)
        x = vf(t)
        y = DriveStageRT(p, FS, drive, opamp=RC3403A).process(x)
        _, ref = drive_stage_reference(p, drive, vf, t, opamp=RC3403A)
        r = (np.sqrt(np.mean((y[settle:] - ref[settle:]) ** 2))
             / np.sqrt(np.mean(ref[settle:] ** 2)))
        worst = max(worst, r)
        print(f"drive={drive} amp={amp}: rel RMS {r:.2e} "
              + ("PASS" if r < 1e-3 else "FAIL"))


def hl(y, ks):
    tt = np.arange(len(y)) / FS
    w = np.hanning(len(y))
    yw = (y - y.mean()) * w
    s = 2 / np.sum(w)
    return np.array([abs(np.sum(yw * np.exp(-2j * np.pi * k * F0 * tt)) * s)
                     for k in ks])


x = 0.05 * np.sin(2 * np.pi * F0 * t)
ya = DriveStageRT(p, FS, 0.5, opamp=RC3403A).process(x)[settle:]
yb = DriveStageRT(p, FS, 0.5, opamp=TL072).process(x)[settle:]
ha = hl(ya, range(1, 10))
hb = hl(yb, range(1, 10))
print("xover fizz (RC3403A vs TL072), H3/H5/H7/H9 delta dB:",
      np.round(20 * np.log10(ha[[2, 4, 6, 8]] / hb[[2, 4, 6, 8]]), 1))
print(f"worst: {worst:.2e}")
