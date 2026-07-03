"""Princeton Reverb stage-by-stage validation.

[1] tube models vs datasheet operating points
[2] triode gain stage (V1B config) RT vs Radau (index-1 DAE, nested solve)
[3] cathodyne PI RT vs Radau — balance of the two outputs
[4] 6V6 push-pull + OT + speaker RT vs Radau
[5] tone stack: bilinear vs analytic frequency response
[6] NFB polarity + full-amp smoke test
"""

import numpy as np
from scipy.integrate import solve_ivp

from tubes import T_12AX7, P_6V6, datasheet_sanity
from mna import fender_tone_stack
from princeton import TriodeStageRT, CathodyneRT, PowerStageRT, PrincetonReverb

FS = 48000 * 4       # 192 kHz (amp runs 4x; softer nonlinearity than diodes)
TOL = 1e-3


def rel_rms(y, ref, settle):
    err = y[settle:] - ref[settle:]
    return np.sqrt(np.mean(err ** 2)) / (np.sqrt(np.mean(ref[settle:] ** 2))
                                         + 1e-12)


# ---- [2] triode stage golden reference ---------------------------------------

def triode_stage_reference(tube, B, Rp, Rk, Ck, Cc, RL, vg_fn, t_eval,
                           vck0, q0):
    warm = {"vp": B * 0.6}

    def solve_vp(vg, vck, q):
        vp = warm["vp"]
        for _ in range(200):
            ip = tube.ip(vg - vck, vp - vck)
            f = (B - vp) / Rp - ip - (vp - q) / RL
            e = 1e-5
            ipe = tube.ip(vg - vck, vp + e - vck)
            df = -1.0 / Rp - (ipe - ip) / e - 1.0 / RL
            dv = f / df
            vp -= np.clip(dv, -25, 25)
            if abs(dv) < 1e-11:
                break
        warm["vp"] = vp
        return vp

    def rhs(t, y):
        vck, q = y
        vg = vg_fn(t)
        vp = solve_vp(vg, vck, q)
        ip = tube.ip(vg - vck, vp - vck)
        return ((ip - vck / Rk) / Ck, (vp - q) / (RL * Cc))

    sol = solve_ivp(rhs, (t_eval[0], t_eval[-1]), (vck0, q0), method="Radau",
                    t_eval=t_eval, rtol=1e-8, atol=1e-11)
    out = np.empty_like(sol.t)
    for i, (tt, (vck, q)) in enumerate(zip(sol.t, sol.y.T)):
        vp = solve_vp(vg_fn(tt), vck, q)
        out[i] = vp - q          # vo2 = vp - q = RL*iC
    return out


# ---- [3] cathodyne golden reference -------------------------------------------

def cathodyne_reference(B, vg_fn, t_eval, q_t0, q_b0):
    tube = T_12AX7
    Rp, Rk, RL = 56e3, 57e3, 220e3
    warm = {"vp": 190.0, "vk": 45.0}

    def solve_pk(vg, q_t, q_b):
        vp, vk = warm["vp"], warm["vk"]
        for _ in range(300):
            ip = tube.ip(vg - vk, vp - vk)
            r1 = (B - vp) / Rp - ip - (vp - q_t) / RL
            r2 = ip - vk / Rk - (vk - q_b) / RL
            e = 1e-5
            ipp = tube.ip(vg - vk, vp + e - vk)
            ipk = tube.ip(vg - vk - e, vp - vk - e)
            J11 = -1 / Rp - (ipp - ip) / e - 1 / RL
            J12 = -(ipk - ip) / e
            J21 = (ipp - ip) / e
            J22 = (ipk - ip) / e - 1 / Rk - 1 / RL
            det = J11 * J22 - J12 * J21
            dvp = (r1 * J22 - r2 * J12) / det
            dvk = (J11 * r2 - J21 * r1) / det
            vp -= np.clip(dvp, -20, 20)
            vk -= np.clip(dvk, -8, 8)
            if abs(dvp) < 1e-11 and abs(dvk) < 1e-11:
                break
        warm["vp"], warm["vk"] = vp, vk
        return vp, vk

    def rhs(t, y):
        q_t, q_b = y
        vp, vk = solve_pk(vg_fn(t), q_t, q_b)
        return ((vp - q_t) / (RL * 0.1e-6), (vk - q_b) / (RL * 0.1e-6))

    sol = solve_ivp(rhs, (t_eval[0], t_eval[-1]), (q_t0, q_b0),
                    method="Radau", t_eval=t_eval, rtol=1e-8, atol=1e-11)
    out_t = np.empty_like(sol.t)
    out_b = np.empty_like(sol.t)
    for i, (tt, (q_t, q_b)) in enumerate(zip(sol.t, sol.y.T)):
        vp, vk = solve_pk(vg_fn(tt), q_t, q_b)
        out_t[i] = vp - q_t
        out_b[i] = vk - q_b
    return out_t, out_b


# ---- [4] power stage golden reference ------------------------------------------

def power_stage_reference(ps: PowerStageRT, vg_fn, t_eval):
    """5-state ODE: vaa (winding cap Cw), iL, spk[i_s, vt, iLc]."""
    tube = ps.tube
    m, Lm, Cw = ps.m, ps.Lm, ps.Cw
    Re, Lvc = 6.4, 0.8e-3
    Rres, Lces = ps._Rres, ps._Lces
    Cmes = ps._Cmes
    vA, vB = 420.0, 410.0
    bias = ps.vbias

    def gclamp(v):
        return v - 0.15 * np.log1p(np.exp((v - 0.5) / 0.15))

    def rhs(t, y):
        vaa, iL, i_s, vt, iLc = y
        g_ac = vg_fn(t)
        g1 = gclamp(bias + g_ac)
        g2 = gclamp(bias - g_ac)
        i1, _ = tube.currents(g1, vB, max(vA - 0.5 * vaa, 0.5))
        i2, _ = tube.currents(g2, vB, max(vA + 0.5 * vaa, 0.5))
        v_s = vaa / (2 * m)
        return (((i1 - i2) - iL - i_s / (2 * m)) / Cw,
                vaa / Lm,
                (v_s - Re * i_s - vt) / Lvc,
                (i_s - vt / Rres - iLc) / Cmes,
                vt / Lces)

    sol = solve_ivp(rhs, (t_eval[0], t_eval[-1]), (0.0,) * 5,
                    method="Radau", t_eval=t_eval, rtol=1e-8, atol=1e-11)
    return sol.y[0] / (2 * m)


def main():
    print("[1] tube models vs datasheet typical operating points")
    ok = True
    for name, got, want, reltol in datasheet_sanity():
        dev = abs(got - want) / want
        ok &= dev < reltol
        print(f"    {name}: {got:6.2f} mA vs {want} mA "
              f"(dev {dev*100:4.0f}%, allow {reltol*100:.0f}%)  "
              + ("PASS" if dev < reltol else "FAIL"))

    n = int(0.02 * FS)
    t = np.arange(n) / FS
    settle = n // 2

    print("\n[2] triode stage (V1B config) RT vs Radau")
    for amp in (0.1, 2.0, 8.0):
        vg_fn = lambda tt: amp * np.sin(2 * np.pi * 220 * tt)
        st = TriodeStageRT(T_12AX7, FS, B=240.0, Rp=100e3, Rk=1500.0,
                           Ck=25e-6, Cc=0.02e-6, RL=1e6)
        y = np.array([st.step(v) for v in vg_fn(t)])
        ref = triode_stage_reference(T_12AX7, 240.0, 100e3, 1500.0, 25e-6,
                                     0.02e-6, 1e6, vg_fn, t,
                                     st.vk_dc, st.vp_dc)
        r = rel_rms(y, ref, settle)
        g = np.max(np.abs(ref[settle:])) / amp
        print(f"    amp={amp:4.1f}V: gain x{g:5.1f}, rel RMS {r:.2e}  "
              + ("PASS" if r < TOL else "FAIL"))

    print("\n[3] cathodyne PI RT vs Radau + output balance")
    for amp in (1.0, 10.0):
        vg_fn = lambda tt: amp * np.sin(2 * np.pi * 220 * tt)
        pi = CathodyneRT(FS, B=240.0)
        yt = np.empty(n)
        yb = np.empty(n)
        for i, v in enumerate(vg_fn(t)):
            yt[i], yb[i] = pi.step(v)
        rt_, rb_ = cathodyne_reference(240.0, vg_fn, t, pi.vp_dc, pi.vk_dc)
        r1 = rel_rms(yt, rt_, settle)
        r2 = rel_rms(yb, rb_, settle)
        bal = np.max(np.abs(yt[settle:])) / np.max(np.abs(yb[settle:]))
        print(f"    amp={amp:4.1f}V: relRMS top {r1:.2e} bot {r2:.2e}, "
              f"balance {bal:.3f}  "
              + ("PASS" if max(r1, r2) < TOL else "FAIL"))

    print("\n[4] 6V6 push-pull + OT + speaker RT vs Radau")
    for amp in (5.0, 12.0):
        nn = int(0.02 * FS)
        tc = np.arange(nn) / FS
        stl = nn // 2
        vg_fn = lambda tt: amp * np.sin(2 * np.pi * 110 * tt)
        ps = PowerStageRT(FS)
        y = np.empty(nn)
        for i, v in enumerate(vg_fn(tc)):
            y[i] = ps.step(v, -v, 420.0, 410.0)
        ref = power_stage_reference(PowerStageRT(FS), vg_fn, tc)
        # apply the same leakage one-pole the RT stage applies at its output
        b, a = ps.lk_b, ps.lk_a
        rf = np.empty_like(ref)
        x1 = y1 = 0.0
        for i, v in enumerate(ref):
            y1 = b * (v + x1) + a * y1
            x1 = v
            rf[i] = y1
        r = rel_rms(y, rf, stl)
        pk = np.max(np.abs(rf[stl:]))
        tol = TOL if amp <= 5.0 else 5 * TOL
        print(f"    amp={amp:4.1f}V @ {FS/1000:.0f}kHz: peak {pk:5.1f}V "
              f"({pk**2/2/8:5.1f}W/8ohm), rel RMS {r:.2e}  "
              + ("PASS" if r < tol else "FAIL"))
    # deep clip: Radau is stiffness-limited by the 125 ns Cw timescale, so
    # use RT self-convergence across rates as the criterion instead
    amp = 25.0
    ys = {}
    for fs_case in (FS, 2 * FS):
        nn = int(0.02 * fs_case)
        tc = np.arange(nn) / fs_case
        ps = PowerStageRT(fs_case)
        yy = np.empty(nn)
        for i, v in enumerate(amp * np.sin(2 * np.pi * 110 * tc)):
            yy[i] = ps.step(v, -v, 420.0, 410.0)
        ys[fs_case] = yy
    y1_, y2_ = ys[FS], ys[2 * FS][::2]
    stl = len(y1_) // 2
    r = rel_rms(y1_, y2_, stl)
    print(f"    amp=25.0V self-convergence 192k vs 384k: rel RMS {r:.2e}  "
          + ("PASS" if r < 5 * TOL else "FAIL"))

    print("\n[5] tone stack bilinear vs analytic (3 settings, 6 freqs)")
    freqs = np.array([60, 200, 500, 1000, 3000, 8000], dtype=float)
    for (tr, ba, vo) in ((0.5, 0.5, 0.7), (0.9, 0.2, 1.0), (0.2, 0.9, 0.3)):
        net = fender_tone_stack(FS, tr, ba, vo)
        H = np.abs(net.freq_response(freqs, 7))
        worst = 0.0
        for f, hmag in zip(freqs, H):
            dur = max(0.05, 20.0 / f)
            nn = int(dur * FS)
            tt = np.arange(nn) / FS
            x = np.sin(2 * np.pi * f * tt)
            net2 = fender_tone_stack(FS, tr, ba, vo).compile(7)
            y = net2.process(x)[nn // 2:]
            g = np.max(np.abs(y))
            worst = max(worst, abs(g - hmag) / hmag)
        print(f"    T={tr} B={ba} V={vo}: worst dev {worst*100:.2f}%  "
              + ("PASS" if worst < 0.02 else "FAIL"))

    print("\n[6] NFB polarity + full amp smoke test (0.25 s)")
    fs = FS
    nn = int(0.25 * fs)
    tt = np.arange(nn) / fs
    x = 0.02 * np.sin(2 * np.pi * 220 * tt) * np.minimum(tt / 0.02, 1.0)

    def h220(y):
        seg = y[nn // 2:]
        ts = np.arange(len(seg)) / fs
        w = np.hanning(len(seg))
        return abs(np.sum((seg - seg.mean()) * w
                          * np.exp(-2j * np.pi * 220 * ts)) * 2 / np.sum(w))

    amp_on = PrincetonReverb(fs, volume=0.5, reverb=0.0)
    y_on = amp_on.process(x)
    amp_off = PrincetonReverb(fs, volume=0.5, reverb=0.0)
    amp_off.R_nfb = 1e12          # open the NFB loop
    y_off = amp_off.process(x)
    g_on, g_off = h220(y_on), h220(y_off)
    print(f"    H(220Hz): NFB on {g_on:.3f}V, off {g_off:.3f}V, "
          f"reduction {g_off/g_on:.2f}x "
          + ("PASS" if g_on < g_off else "FAIL — polarity!"))
    print(f"    finite: {np.all(np.isfinite(y_on))}, "
          f"PSU A node after signal: {amp_on.psu.vA:.0f}V (sag from 420V)")


if __name__ == "__main__":
    main()
