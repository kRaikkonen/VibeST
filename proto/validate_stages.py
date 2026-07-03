"""Stage-by-stage validation against Radau golden references, plus the
TL072 vs RC3403A (original 1977 chip) A/B comparison.

Pass criterion per stage: relative RMS error < 0.1% at 8x oversampling.
"""

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from od1sim import (OD1Params, DriveStageRT, drive_stage_reference,
                    EmitterFollowerRT, buffer_reference,
                    FilterStageRT, TL072, RC3403A)

FS = 48000 * 8
F0 = 220.0
DUR = 0.03
TOL = 1e-3


def rel_rms(y, ref, settle):
    err = y[settle:] - ref[settle:]
    return np.sqrt(np.mean(err ** 2)) / np.sqrt(np.mean(ref[settle:] ** 2))


def harmonic_levels(y, fs, f0, nharm=8):
    n = len(y)
    t = np.arange(n) / fs
    w = np.hanning(n)
    yw = (y - np.mean(y)) * w
    scale = 2.0 / np.sum(w)
    return np.array([abs(np.sum(yw * np.exp(-2j * np.pi * k * f0 * t)) * scale)
                     for k in range(1, nharm + 1)])


def main():
    p = OD1Params()
    n = int(DUR * FS)
    t = np.arange(n) / FS
    settle = n // 2
    print(f"fs = {FS/1000:.0f} kHz, tone = {F0} Hz, pass tol = {TOL:.0e}\n")

    # ---- 1) drive stage with op-amp macro vs Radau --------------------------
    print("[1] drive stage + TL072 macro vs Radau reference")
    print(f"{'drive':>6} {'amp/V':>6} {'RMSerr/sig':>11}  verdict")
    worst = 0.0
    for drive in (0.0, 0.5, 1.0):
        for amp in (0.05, 0.5):
            vin_fn = lambda tt: amp * np.sin(2 * np.pi * F0 * tt)
            x = vin_fn(t)
            y = DriveStageRT(p, FS, drive, opamp=TL072).process(x)
            _, ref = drive_stage_reference(p, drive, vin_fn, t, opamp=TL072)
            r = rel_rms(y, ref, settle)
            worst = max(worst, r)
            print(f"{drive:>6.1f} {amp:>6.2f} {r:>11.2e}  "
                  f"{'PASS' if r < TOL else 'FAIL'}")

    # ---- 2) input buffer (Ebers-Moll) vs Radau DAE ---------------------------
    print("\n[2] emitter follower Q1 (Ebers-Moll) vs Radau reference")
    print(f"{'amp/V':>6} {'gain':>7} {'RMSerr/sig':>11}  verdict")
    for amp in (0.05, 0.5, 2.0):
        vin_fn = lambda tt: amp * np.sin(2 * np.pi * F0 * tt)
        x = vin_fn(t)
        buf = EmitterFollowerRT(p, FS, Rs=p.R101, Cc=p.C101,
                                Rb=p.R102, Re=p.R103)
        y = buf.process(x)
        _, ref = buffer_reference(p, vin_fn, t, Rs=p.R101, Cc=p.C101,
                                  Rb=p.R102, Re=p.R103)
        r = rel_rms(y, ref, settle)
        worst = max(worst, r)
        g = np.max(np.abs(ref[settle:])) / amp
        print(f"{amp:>6.2f} {g:>7.4f} {r:>11.2e}  "
              f"{'PASS' if r < TOL else 'FAIL'}")

    # buffer harmonic content at hot level (should be small but nonzero)
    amp = 0.5
    x = amp * np.sin(2 * np.pi * F0 * t)
    buf = EmitterFollowerRT(p, FS, Rs=p.R101, Cc=p.C101, Rb=p.R102, Re=p.R103)
    h = harmonic_levels(buf.process(x)[settle:], FS, F0)
    print(f"    Q1 THD @ 500 mVpk: H2 {20*np.log10(h[1]/h[0]):+.1f} dB, "
          f"H3 {20*np.log10(h[2]/h[0]):+.1f} dB "
          f"(the 'transparent but not sterile' BOSS front end)")

    # ---- 3) filter stage sanity (weakly nonlinear,小信号应匹配理想一阶低通) ----
    print("\n[3] filter stage + macro op-amp, small-signal vs analytic LPF")
    amp = 0.1
    fcheck = [(F0, None)]
    errs = []
    for f, _ in fcheck:
        x = amp * np.sin(2 * np.pi * f * t)
        y = FilterStageRT(p, FS, opamp=TL072).process(x)[settle:]
        # analytic inverting one-pole: gain -R107/R108 / sqrt(1+(f/fc)^2)
        fc = 1.0 / (2 * np.pi * p.R107 * p.C105)
        g_num = np.max(np.abs(y)) / amp
        g_ana = (p.R107 / p.R108) / np.sqrt(1 + (f / fc) ** 2)
        errs.append(abs(g_num - g_ana) / g_ana)
        print(f"    {f:6.0f} Hz: sim gain {g_num:.4f}, analytic {g_ana:.4f}, "
              f"dev {errs[-1]*100:.3f}%  "
              f"{'PASS' if errs[-1] < 5e-3 else 'FAIL'}")

    # ---- 4) A/B: ideal op-amp vs TL072 vs RC3403A (original 1977 chip) -------
    print("\n[4] chip A/B at drive=max, 500 mVpk, 220 Hz")
    amp, drive = 0.5, 1.0
    x = amp * np.sin(2 * np.pi * F0 * t)
    results = {}
    for oa, name in ((None, "ideal"), (TL072, "TL072"), (RC3403A, "RC3403A")):
        y = DriveStageRT(p, FS, drive, opamp=oa).process(x)
        results[name] = y
        h = harmonic_levels(y[settle:], FS, F0)
        hdb = 20 * np.log10(h[1:] / h[0])
        print(f"    {name:>8}: H2 {hdb[0]:+6.1f}  H3 {hdb[1]:+6.1f}  "
              f"H5 {hdb[3]:+6.1f}  H7 {hdb[5]:+6.1f} dB")
    # closed-loop bandwidth from GBW at max gain (white-box prediction)
    gmax = 1 + p.Rf(1.0) / p.R106
    print(f"    white-box prediction: max gain x{gmax:.0f} -> closed-loop BW "
          f"TL072 {TL072.GBW/gmax/1e3:.1f} kHz vs RC3403A "
          f"{RC3403A.GBW/gmax/1e3:.1f} kHz (audible treble difference)")

    seg = slice(settle, settle + int(2 * FS / F0))
    fig, ax = plt.subplots(figsize=(11, 4.5))
    for name, y in results.items():
        ax.plot(t[seg] * 1e3, y[seg], label=name,
                lw=1.2 if name != "ideal" else 0.8,
                alpha=0.9 if name != "ideal" else 0.5)
    ax.set_title("OD-1 drive stage, drive=max: ideal vs TL072 vs RC3403A "
                 "(slew-limited clipping edges)")
    ax.set_xlabel("t / ms"); ax.set_ylabel("V (re VB)")
    ax.legend(); ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig("validate_stages.png", dpi=110)
    print("\nplot -> proto/validate_stages.png")
    print(f"worst stage-vs-reference RMS error: {worst:.2e}  "
          f"({'ALL PASS' if worst < TOL else 'FAILURES PRESENT'})")


if __name__ == "__main__":
    main()
