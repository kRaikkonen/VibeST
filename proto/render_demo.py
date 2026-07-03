"""Render a demo: synthetic guitar pluck through the full OD-1 chain.

Runs the pedal at 8x oversampling (384 kHz), decimates to 48 kHz, writes
dry/wet wav files side by side.
"""

import numpy as np
from scipy.signal import resample_poly
from scipy.io import wavfile

from od1sim import OD1Params, OD1Pedal, RC3403A

FS_BASE = 48000
OS = 8
FS = FS_BASE * OS
DUR = 2.0


def pluck(f0, t, decay=3.0, nharm=12):
    """Crude Karplus-ish pluck: decaying harmonics, slight inharmonicity."""
    y = np.zeros_like(t)
    rng = np.random.default_rng(1977)
    for k in range(1, nharm + 1):
        fk = f0 * k * np.sqrt(1 + 3e-4 * k * k)
        if fk > 0.45 * FS_BASE:
            break
        a = 1.0 / k ** 1.2
        y += a * np.exp(-decay * k ** 0.5 * t) * np.sin(2 * np.pi * fk * t
                                                        + rng.uniform(0, np.pi))
    return y


def main():
    t = np.arange(int(DUR * FS)) / FS
    # A-power-chord-ish: A2 + E3 + A3, ~150 mVpk typical humbucker level
    x = pluck(110.0, t) + pluck(164.8, t) + pluck(220.0, t)
    x *= 0.15 / np.max(np.abs(x))

    for drive, tag in [(0.35, "drive35"), (1.0, "drivemax")]:
        pedal = OD1Pedal(OD1Params(), FS, drive=drive, level=1.0,
                         opamp=RC3403A)   # original 1977 chip
        y = pedal.process(x)
        y48 = resample_poly(y, 1, OS)
        y48 = 0.9 * y48 / np.max(np.abs(y48))
        wavfile.write(f"demo_od1_{tag}.wav", FS_BASE,
                      (y48 * 32767).astype(np.int16))
        print(f"demo_od1_{tag}.wav written "
              f"(drive={drive}, peak drive-stage swing on real diodes)")

    x48 = resample_poly(x, 1, OS)
    x48 = 0.9 * x48 / np.max(np.abs(x48))
    wavfile.write("demo_dry.wav", FS_BASE, (x48 * 32767).astype(np.int16))
    print("demo_dry.wav written")


if __name__ == "__main__":
    main()
