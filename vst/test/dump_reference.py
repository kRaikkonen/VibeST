"""Dump a test input and the validated Python engine's output as raw float64,
for sample-by-sample comparison with the C++ port."""

import sys
import numpy as np

sys.path.insert(0, r"D:\sd1\proto")
from od1sim import OD1Params, OD1Pedal, RC3403A   # noqa: E402

FS = 48000 * 8
DUR = 0.25


def main():
    n = int(DUR * FS)
    t = np.arange(n) / FS
    rng = np.random.default_rng(42)
    # mixed content: tone burst + noise floor + level jump
    x = (0.15 * np.sin(2 * np.pi * 220 * t)
         + 0.05 * np.sin(2 * np.pi * 1330 * t)
         + 0.002 * rng.standard_normal(n))
    x[n // 2:] *= 3.0                                # hit the clippers hard
    pedal = OD1Pedal(OD1Params(), FS, drive=0.6, level=0.8, opamp=RC3403A)
    y = pedal.process(x)
    x.astype(np.float64).tofile("od1_input.f64")
    y.astype(np.float64).tofile("od1_ref_py.f64")
    print(f"dumped {n} samples, ref RMS {np.sqrt(np.mean(y**2)):.6f}")


if __name__ == "__main__":
    main()
