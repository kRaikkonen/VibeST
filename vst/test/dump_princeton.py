import sys
import numpy as np

sys.path.insert(0, r"D:\sd1\proto")
from princeton import PrincetonReverb   # noqa: E402

FS = 48000 * 4
DUR = 0.2


def main():
    n = int(DUR * FS)
    t = np.arange(n) / FS
    rng = np.random.default_rng(7)
    x = (0.1 * np.sin(2 * np.pi * 220 * t)
         + 0.03 * np.sin(2 * np.pi * 923 * t)
         + 0.001 * rng.standard_normal(n))
    x *= np.minimum(t / 0.01, 1.0)
    amp = PrincetonReverb(FS, volume=0.5, treble=0.55, bass=0.5,
                          reverb=0.25, trem_intensity=0.0)
    y = amp.process(x)
    x.astype(np.float64).tofile("pr_input.f64")
    y.astype(np.float64).tofile("pr_ref_py.f64")
    amp.tank_ir.astype(np.float64).tofile("pr_tank_ir.f64")
    print(f"dumped {n} samples + IR {len(amp.tank_ir)}, "
          f"ref RMS {np.sqrt(np.mean(y**2)):.6f}")


if __name__ == "__main__":
    main()
