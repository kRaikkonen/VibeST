"""M2 validation: DriveStageRT (real-time trapezoidal+NR) vs Radau golden ref.

Pass criteria: RMS error of the real-time solver relative to the stiff-ODE
reference < 0.1% of signal RMS at 8x oversampling (fs = 384 kHz).
Also reports harmonic structure to confirm asymmetric clipping (even harmonics).
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from od1sim import OD1Params, DriveStageRT, drive_stage_reference

FS = 48000 * 8          # 8x oversampled rate, the target VST configuration
F0 = 220.0              # test tone (A3, guitar range)
DUR = 0.05              # 50 ms
AMP_CASES = [0.05, 0.5]         # 50 mVpk (soft pickup) and 500 mVpk (hot)
DRIVE_CASES = [0.0, 0.5, 1.0]   # OverDrive pot min / mid / max


def harmonic_levels(y, fs, f0, nharm=6):
    """Amplitude of harmonics 1..nharm via windowed DFT projection."""
    n = len(y)
    t = np.arange(n) / fs
    w = np.hanning(n)
    yw = (y - np.mean(y)) * w
    scale = 2.0 / np.sum(w)
    out = []
    for k in range(1, nharm + 1):
        c = np.sum(yw * np.exp(-2j * np.pi * k * f0 * t)) * scale
        out.append(abs(c))
    return np.array(out)


def main():
    p = OD1Params()
    n = int(DUR * FS)
    t = np.arange(n) / FS
    settle = n // 2  # discard first half (bias/coupling transients)

    print(f"fs = {FS/1000:.0f} kHz, tone = {F0} Hz, dur = {DUR*1e3:.0f} ms")
    print(f"{'drive':>6} {'amp/V':>6} {'gain(lin)':>9} "
          f"{'maxerr/V':>10} {'RMSerr/sig':>10}  verdict")

    worst = 0.0
    for drive in DRIVE_CASES:
        for amp in AMP_CASES:
            vin_fn = lambda tt: amp * np.sin(2 * np.pi * F0 * tt)
            x = vin_fn(t)

            rt = DriveStageRT(p, FS, drive)
            y_rt = rt.process(x)

            _, y_ref = drive_stage_reference(p, drive, vin_fn, t)

            err = y_rt[settle:] - y_ref[settle:]
            sig_rms = np.sqrt(np.mean(y_ref[settle:] ** 2))
            rel = np.sqrt(np.mean(err ** 2)) / sig_rms
            worst = max(worst, rel)
            g = np.max(np.abs(y_ref[settle:])) / amp
            print(f"{drive:>6.1f} {amp:>6.2f} {g:>9.2f} "
                  f"{np.max(np.abs(err)):>10.2e} {rel:>10.2e}  "
                  f"{'PASS' if rel < 1e-3 else 'FAIL'}")

    # harmonic structure at drive=1.0, hot input: expect strong EVEN harmonics
    amp, drive = 0.5, 1.0
    vin_fn = lambda tt: amp * np.sin(2 * np.pi * F0 * tt)
    x = vin_fn(t)
    y = DriveStageRT(p, FS, drive).process(x)[settle:]
    h = harmonic_levels(y, FS, F0)
    hdb = 20 * np.log10(h / h[0])
    print("\nHarmonics re H1 (drive=max, 500 mVpk):")
    for k, db in enumerate(hdb, start=1):
        tag = "even" if k % 2 == 0 else "odd"
        print(f"  H{k} ({tag}): {db:+6.1f} dB")

    # plots
    fig, axes = plt.subplots(2, 1, figsize=(11, 7))
    seg = slice(settle, settle + int(3 * FS / F0))  # 3 cycles
    _, y_ref = drive_stage_reference(p, drive, vin_fn, t)
    y_rt = DriveStageRT(p, FS, drive).process(x)
    axes[0].plot(t[seg] * 1e3, x[seg], label="input", lw=0.8)
    axes[0].plot(t[seg] * 1e3, y_ref[seg], label="Radau reference", lw=2, alpha=0.6)
    axes[0].plot(t[seg] * 1e3, y_rt[seg], "--", label="real-time solver", lw=1)
    axes[0].set_title(f"OD-1 drive stage, drive=max, {amp*1e3:.0f} mVpk @ {F0:.0f} Hz "
                      f"(asymmetric clipping: +1.2 V / -0.6 V)")
    axes[0].set_xlabel("t / ms"); axes[0].set_ylabel("V (re VB)")
    axes[0].legend(); axes[0].grid(alpha=0.3)

    axes[1].plot(t[seg] * 1e3, (y_rt - y_ref)[seg] * 1e6)
    axes[1].set_title("error: real-time solver minus reference")
    axes[1].set_xlabel("t / ms"); axes[1].set_ylabel("µV")
    axes[1].grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig("validate_drive.png", dpi=110)
    print("\nplot -> proto/validate_drive.png")
    print(f"worst relative RMS error: {worst:.2e}  "
          f"({'ALL PASS' if worst < 1e-3 else 'FAILURES PRESENT'})")


if __name__ == "__main__":
    main()
