"""Measure the NFB loop gain T(f) with the loop OPEN: inject a probe sine at
the V3B cathode, measure what the feedback network would return."""

import numpy as np
from princeton import PrincetonReverb

FS = 48000 * 4
Rt, Rn = 47.0, 2700.0
beta_fac = (Rt / Rn) / (1.0 + Rt / Rn)

print(f"{'f/Hz':>7} {'|T|':>7} {'phase/deg':>10}")
for f in (220, 1000, 5000, 10000, 20000, 35000, 48000, 70000):
    dur = max(0.06, 40.0 / f)
    n = int(dur * FS)
    t = np.arange(n) / FS
    amp = PrincetonReverb(FS, volume=0.5, reverb=0.0)
    amp.R_nfb = 1e12                       # open the loop
    amp.probe = 0.002 * np.sin(2 * np.pi * f * t)
    y = amp.process(np.zeros(n))
    seg = slice(n // 2, n)
    tt = t[seg]
    w = np.hanning(len(tt))
    c = np.sum((y[seg] - y[seg].mean()) * w
               * np.exp(-2j * np.pi * f * tt)) * 2 / np.sum(w)
    # returned feedback voltage phasor at the cathode (with averaging filter)
    ret = c * beta_fac * 0.5 * (1 + np.exp(-2j * np.pi * f / FS))
    probe_ph = -1j          # sin = (e^{jwt} - e^{-jwt})/2j -> phasor of sin
    T = ret / (0.002 * probe_ph * -1)      # relative to probe sine phasor
    print(f"{f:>7} {abs(T):>7.2f} {np.degrees(np.angle(T)):>10.1f}")
