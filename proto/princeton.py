"""Fender Princeton Reverb AA1164 — white-box circuit simulation.

Circuit transcription: docs/schematic-princeton-aa1164.md.
Tube models: tubes.py (Koren). Linear networks: mna.py (nodal DK core).

Architecture (two passes, chain is feedforward except NFB):
  pass A (per-sample): V1A -> tone stack (MNA) -> V1B;
           reverb send: 500pF HPF -> V2 (12AT7) -> spring tank (dispersive
           IR, FFT conv) -> V3A recovery -> wet buffer
  pass B (per-sample): dry/wet mixer -> V3B (NFB into 47R cathode tail,
           one-sample loop delay) -> cathodyne PI -> 6V6 push-pull + OT +
           speaker load + PSU sag + tremolo bias wiggle

Documented approximations (see docs 'known simplifications'):
  V1A->stack loading via Thevenin Rsrc; PSU sag applied to power/PI nodes
  (preamp node ripple/sag deferred); tremolo as circuit-derived LFO;
  spring tank dispersion parameters typical, to calibrate; 6V6 grid
  conduction as soft clamp.
"""

import numpy as np
from scipy.signal import fftconvolve

from tubes import T_12AX7, T_12AT7, P_6V6
from mna import fender_tone_stack


# --- generic triode gain stage (Rp plate, Rk||Ck cathode, Cc->RL output) -----

class TriodeStageRT:
    def __init__(self, tube, fs, B, Rp, Rk, Ck=None, Cc=None, RL=None,
                 max_iter=40, tol=1e-9):
        self.tube = tube
        self.T = 1.0 / fs
        self.B = B
        self.Rp, self.Rk, self.Ck = Rp, Rk, Ck
        self.Cc, self.RL = Cc, RL
        self.max_iter, self.tol = max_iter, tol
        self.nfb = 0.0            # extra cathode reference (V3B NFB inject)
        # DC operating point (couplings open, cathode cap charged)
        vp, vk = 0.6 * B, 1.0
        for _ in range(200):
            r1, r2 = self._dc_res(vp, vk)
            e = 1e-4
            r1p, r2p = self._dc_res(vp + e, vk)
            r1k, r2k = self._dc_res(vp, vk + e)
            J = np.array([[(r1p - r1) / e, (r1k - r1) / e],
                          [(r2p - r2) / e, (r2k - r2) / e]])
            try:
                d = np.linalg.solve(J, [r1, r2])
            except np.linalg.LinAlgError:
                break
            vp -= np.clip(d[0], -30, 30)
            vk -= np.clip(d[1], -2, 2)
            if abs(d[0]) < 1e-10 and abs(d[1]) < 1e-10:
                break
        self.vp_dc, self.vk_dc = vp, vk
        self.ip_dc = self.tube.ip(0.0 - vk, vp - vk)
        # states
        self.vck = vk             # cathode cap voltage
        self.ik_prev = self.ip_dc
        self.q = vp               # coupling cap charge voltage (vo2_dc = 0)
        self.iC_prev = 0.0
        self.vp = vp
        self.vk = vk
        self.i_supply = (B - vp) / Rp

    def _dc_res(self, vp, vk):
        ip = self.tube.ip(0.0 - vk, vp - vk)
        return (self.B - vp) / self.Rp - ip, vk - self.Rk * ip

    def _residuals(self, vp, vk, vg, B):
        T = self.T
        ip = self.tube.ip(vg - vk, vp - vk)
        # coupling branch (if present)
        if self.Cc is not None:
            c = 2.0 * self.Cc / T
            iC = (c * (vp - self.q) - self.iC_prev) / (1.0 + c * self.RL)
        else:
            iC = 0.0
        r1 = (B - vp) / self.Rp - ip - iC
        if self.Ck is not None:
            ak = T / (2.0 * self.Rk * self.Ck)
            vck = (self.vck * (1.0 - ak)
                   + ak * self.Rk * (ip + self.ik_prev)) / (1.0 + ak)
            r2 = vk - vck - self.nfb
        else:
            r2 = vk - self.Rk * ip - self.nfb
        return r1, r2, ip, iC

    def step(self, vg_ac, B=None):
        """vg_ac: grid voltage (AC, re ground DC=0). Returns output vo2 (AC)."""
        B = self.B if B is None else B
        vg = vg_ac
        vp, vk = self.vp, self.vk
        for _ in range(self.max_iter):
            r1, r2, ip, iC = self._residuals(vp, vk, vg, B)
            e = 1e-5
            r1p, r2p, _, _ = self._residuals(vp + e, vk, vg, B)
            r1k, r2k, _, _ = self._residuals(vp, vk + e, vg, B)
            J11, J12 = (r1p - r1) / e, (r1k - r1) / e
            J21, J22 = (r2p - r2) / e, (r2k - r2) / e
            det = J11 * J22 - J12 * J21
            if det == 0.0:
                break
            dvp = (r1 * J22 - r2 * J12) / det
            dvk = (J11 * r2 - J21 * r1) / det
            vp -= np.clip(dvp, -25.0, 25.0)
            vk -= np.clip(dvk, -2.0, 2.0)
            if abs(dvp) < self.tol and abs(dvk) < self.tol:
                break
        r1, r2, ip, iC = self._residuals(vp, vk, vg, B)
        # commit states
        if self.Ck is not None:
            ak = self.T / (2.0 * self.Rk * self.Ck)
            self.vck = (self.vck * (1.0 - ak)
                        + ak * self.Rk * (ip + self.ik_prev)) / (1.0 + ak)
        self.ik_prev = ip
        if self.Cc is not None:
            vo2 = self.RL * iC
            self.q = vp - vo2
            self.iC_prev = iC
        else:
            vo2 = vp - self.vp_dc      # raw AC plate voltage
        self.vp, self.vk = vp, vk
        self.i_supply = (B - vp) / self.Rp
        return vo2


# --- cathodyne phase inverter -------------------------------------------------

class CathodyneRT:
    def __init__(self, fs, B, Rp=56e3, Rk_top=1e3, Rk_tail=56e3,
                 Cc=0.1e-6, RL=220e3, max_iter=40, tol=1e-9):
        self.tube = T_12AX7
        self.T = 1.0 / fs
        self.B = B
        self.Rp = Rp
        self.Rk = Rk_top + Rk_tail
        self.Cc, self.RL = Cc, RL
        self.max_iter, self.tol = max_iter, tol
        # DC (grid at 0 via 1M leak)
        vp, vk = 0.7 * B, 40.0
        for _ in range(300):
            ip = self.tube.ip(0.0 - vk, vp - vk)
            r1 = (B - vp) / Rp - ip
            r2 = vk - self.Rk * ip
            e = 1e-4
            ipp = self.tube.ip(0.0 - vk, vp + e - vk)
            ipk = self.tube.ip(0.0 - (vk + e), vp - (vk + e))
            J = np.array([[-1.0 / Rp - (ipp - ip) / e, -(ipk - ip) / e],
                          [-self.Rk * (ipp - ip) / e,
                           1.0 - self.Rk * (ipk - ip) / e]])
            d = np.linalg.solve(J, [r1, r2])
            vp -= np.clip(d[0], -30, 30)
            vk -= np.clip(d[1], -10, 10)
            if max(abs(d)) < 1e-10:
                break
        self.vp_dc, self.vk_dc = vp, vk
        self.vp, self.vk = vp, vk
        self.ip_prev = self.tube.ip(-vk, vp - vk)
        self.q_top = vp           # top coupling (out_dc = 0)
        self.iC_top_prev = 0.0
        self.q_bot = vk
        self.iC_bot_prev = 0.0
        self.i_supply = (B - vp) / Rp

    def _residuals(self, vp, vk, vg, B):
        T = self.T
        ip = self.tube.ip(vg - vk, vp - vk)
        c = 2.0 * self.Cc / T
        iC_t = (c * (vp - self.q_top) - self.iC_top_prev) / (1.0 + c * self.RL)
        iC_b = (c * (vk - self.q_bot) - self.iC_bot_prev) / (1.0 + c * self.RL)
        r1 = (B - vp) / self.Rp - ip - iC_t
        r2 = ip - vk / self.Rk - iC_b
        return r1, r2, ip, iC_t, iC_b

    def step(self, vg_ac, B=None):
        B = self.B if B is None else B
        vg = vg_ac                  # grid rides at DC 0 through 1M leak
        vp, vk = self.vp, self.vk
        for _ in range(self.max_iter):
            r1, r2, *_ = self._residuals(vp, vk, vg, B)
            e = 1e-5
            r1p, r2p, *_ = self._residuals(vp + e, vk, vg, B)
            r1k, r2k, *_ = self._residuals(vp, vk + e, vg, B)
            J11, J12 = (r1p - r1) / e, (r1k - r1) / e
            J21, J22 = (r2p - r2) / e, (r2k - r2) / e
            det = J11 * J22 - J12 * J21
            if det == 0.0:
                break
            dvp = (r1 * J22 - r2 * J12) / det
            dvk = (J11 * r2 - J21 * r1) / det
            vp -= np.clip(dvp, -25.0, 25.0)
            vk -= np.clip(dvk, -10.0, 10.0)
            if abs(dvp) < self.tol and abs(dvk) < self.tol:
                break
        r1, r2, ip, iC_t, iC_b = self._residuals(vp, vk, vg, B)
        out_t = self.RL * iC_t
        out_b = self.RL * iC_b
        self.q_top = vp - out_t
        self.iC_top_prev = iC_t
        self.q_bot = vk - out_b
        self.iC_bot_prev = iC_b
        self.vp, self.vk = vp, vk
        self.i_supply = (B - vp) / self.Rp
        # plate output inverts, cathode output follows: they are anti-phase
        return out_t, out_b


# --- 6V6 push-pull + output transformer + speaker load ------------------------

class PowerStageRT:
    def __init__(self, fs, Raa=8000.0, Lm=15.0, m_half=15.8,
                 idle_ma=30.0, max_iter=60, tol=1e-9):
        self.tube = P_6V6
        self.T = 1.0 / fs
        self.Lm = Lm
        self.m = m_half
        self.max_iter, self.tol = max_iter, tol
        # speaker model (8 ohm 10-inch, typical params, to calibrate)
        Re, Lvc = 6.4, 0.8e-3
        Rres, Lces = 20.0, 30e-3
        Cmes = 1.0 / ((2 * np.pi * 95.0) ** 2 * Lces)
        T = self.T
        self.Re = Re
        A = np.array([[2 * Lvc / T + Re, 1.0, 0.0],
                      [-1.0, 2 * Cmes / T + 1.0 / Rres, 1.0],
                      [0.0, -1.0, 2 * Lces / T]])
        self.Ainv = np.linalg.inv(A)
        self.geq = self.Ainv[0, 0]
        self.spk = np.zeros(3)           # [i_s, vt, iLc]
        self.spk_hist = np.zeros(3)
        self.v_s_prev = 0.0
        self._Cmes, self._Lces, self._Rres, self._Lvc = Cmes, Lces, Rres, Lvc
        # bias: calibrate to idle current (schematic -34V nominal; Koren
        # parameter sets need the operating-point match, docs 'known simpl.')
        vg = -34.0
        for _ in range(200):
            ip, _ = self.tube.currents(vg, 400.0, 410.0)
            f = ip - idle_ma * 1e-3
            ip2, _ = self.tube.currents(vg + 1e-4, 400.0, 410.0)
            vg -= f / ((ip2 - ip) / 1e-4)
        self.vbias = vg
        self.iL = 0.0                    # OT magnetizing current
        self.vaa = 0.0
        self.vaa_prev = 0.0
        self.i_plates = 2 * idle_ma * 1e-3
        self.i_screens = 0.0
        # OT primary winding (distributed) capacitance: caps the HF plate
        # load a pentode sees into a rising speaker impedance — without it
        # loop gain RISES with f (measured, loop_probe.py) and the NFB loop
        # oscillates. 500 pF is a typical small-OT value (to measure).
        self.Cw = 500e-12
        self.iCw_prev = 0.0
        # OT leakage inductance + stray C: one-pole rolloff ~20 kHz on the
        # secondary. Second real HF mechanism, kept from the leakage model.
        wl = 2 * np.pi * 20e3
        self.lk_b = wl * self.T / (2.0 + wl * self.T)
        self.lk_a = (2.0 - wl * self.T) / (2.0 + wl * self.T)
        self.lk_x1 = 0.0
        self.lk_y1 = 0.0

    def _spk_hist_update(self):
        T = self.T
        i_s, vt, iLc = self.spk
        c1 = (2 * self._Lvc / T) * i_s + (self.v_s_prev - self.Re * i_s - vt)
        c2 = (2 * self._Cmes / T) * vt + (i_s - vt / self._Rres - iLc)
        c3 = (2 * self._Lces / T) * iLc + vt
        self.spk_hist = np.array([c1, c2, c3])

    def step(self, vg1_ac, vg2_ac, vA, vB, bias_mod=0.0):
        """vg1/vg2: PI outputs (AC across 220k grid leaks). Returns speaker V."""
        tube, T = self.tube, self.T
        bias = self.vbias + bias_mod
        # grid conduction soft clamp (AB2 boundary, crude — see docs)
        def gclamp(v):
            return v - 0.15 * np.log1p(np.exp((v - 0.5) / 0.15))
        g1 = gclamp(bias + vg1_ac)
        g2 = gclamp(bias + vg2_ac)
        self._spk_hist_update()
        ih = self.Ainv @ self.spk_hist   # affine speaker: i_s = geq*v_s + ih[0]
        vaa_old = self.vaa               # vaa[n-1] for the trapezoid
        vaa = vaa_old
        lo, hi = -2.2 * vA, 2.2 * vA     # F is strictly decreasing in vaa

        def Fres(v):
            i1_, _ = tube.currents(g1, vB, max(vA - 0.5 * v, 0.5))
            i2_, _ = tube.currents(g2, vB, max(vA + 0.5 * v, 0.5))
            iLn_ = self.iL + (T / (2 * self.Lm)) * (v + vaa_old)
            i_s_ = self.geq * v / (2 * self.m) + ih[0]
            iCw_ = (2 * self.Cw / T) * (v - vaa_old) - self.iCw_prev
            return (i1_ - i2_) - iLn_ - i_s_ / (2 * self.m) - iCw_

        for _ in range(self.max_iter):
            F = Fres(vaa)
            if F > 0.0:
                lo = max(lo, vaa)
            else:
                hi = min(hi, vaa)
            e = 1e-3
            dF = (Fres(vaa + e) - F) / e
            v_new = vaa - F / dF if dF < 0.0 else 0.5 * (lo + hi)
            if not (lo < v_new < hi):
                v_new = 0.5 * (lo + hi)
            if abs(v_new - vaa) < 1e-8:
                vaa = v_new
                break
            vaa = v_new
        # commit
        vp1 = max(vA - 0.5 * vaa, 0.5)
        vp2 = max(vA + 0.5 * vaa, 0.5)
        i1, s1 = tube.currents(g1, vB, vp1)
        i2, s2 = tube.currents(g2, vB, vp2)
        self.iL = self.iL + (T / (2 * self.Lm)) * (vaa + vaa_old)
        self.iCw_prev = (2 * self.Cw / T) * (vaa - vaa_old) - self.iCw_prev
        v_s = vaa / (2 * self.m)
        b = np.array([v_s + self.spk_hist[0], self.spk_hist[1],
                      self.spk_hist[2]])
        self.spk = self.Ainv @ b
        self.v_s_prev = v_s
        self.vaa = vaa
        self.i_plates = i1 + i2
        self.i_screens = s1 + s2
        # leakage-inductance rolloff (bilinear one-pole LP)
        v_out = self.lk_b * (v_s + self.lk_x1) + self.lk_a * self.lk_y1
        self.lk_x1, self.lk_y1 = v_s, v_out
        return v_out


# --- power supply with sag ----------------------------------------------------

class PSU:
    """5U4GB full-wave rectifier + 4-node RC filter chain (A/B/C/D).
    Forward Euler (dt << all time constants). Rect params estimated."""

    def __init__(self, fs, mains_hz=60.0):
        self.T = 1.0 / fs
        self.w = 2 * np.pi * mains_hz
        self.t = 0.0
        self.Vpk = 480.0
        self.Vth, self.Rs = 20.0, 180.0
        self.C = 20e-6
        self.vA, self.vB, self.vC, self.vD = 420.0, 410.0, 320.0, 240.0

    def step(self, iA, iB, iC_load=0.002, iD_load=0.0025):
        T, C = self.T, self.C
        vr = self.Vpk * abs(np.sin(self.w * self.t))
        self.t += T
        irect = max(0.0, (vr - self.Vth - self.vA) / self.Rs)
        i_ab = (self.vA - self.vB) / 1e3
        i_bc = (self.vB - self.vC) / 18e3
        i_cd = (self.vC - self.vD) / 18e3
        self.vA += T / C * (irect - i_ab - iA)
        self.vB += T / C * (i_ab - i_bc - iB)
        self.vC += T / C * (i_bc - i_cd - iC_load)
        self.vD += T / C * (i_cd - iD_load)
        return self.vA, self.vB, self.vC, self.vD


# --- spring reverb tank (dispersive IR, typical 2-spring pan) ------------------

def spring_tank_ir(fs, dur=1.4, springs=((0.028, 0.62), (0.033, 0.58)),
                   fc=4200.0, damp_f=4500.0):
    """Frequency-domain construction: each spring = dispersive delay with
    per-trip damping, reflections as geometric series H = G/(1-G)."""
    n = int(dur * fs)
    nfft = 1 << int(np.ceil(np.log2(2 * n)))
    f = np.fft.rfftfreq(nfft, 1.0 / fs)
    w = 2 * np.pi * f
    h = np.zeros(nfft)
    for Td, r in springs:
        wc = 2 * np.pi * fc
        phi = Td * (w + 0.3 * w * w / wc)        # dispersive phase
        L = np.exp(-(f / damp_f) ** 2)           # per-trip HF damping
        G = r * L * np.exp(-1j * phi)
        # reflections: geometric series of round trips (down + back = G^2)
        H = G / (1.0 - (r * L) ** 2 * np.exp(-2j * phi))
        h += np.fft.irfft(H)
    h = h[:n]
    h /= np.max(np.abs(h)) + 1e-12
    return h


# --- reverb mixer (dry 3.3M||10pF, wet 470k, grid leak 220k) --------------------

class ReverbMixerRT:
    def __init__(self, fs, Rdry=3.3e6, Cbright=10e-12, Rwet=470e3,
                 Rleak=220e3):
        self.g1, self.g2, self.g3 = 1 / Rdry, 1 / Rwet, 1 / Rleak
        self.Cb = Cbright
        self.k = 2.0 * fs
        self.x1 = 0.0
        self.y1 = 0.0
        self.w1 = 0.0

    def step(self, vdry, vwet):
        # node eq: (g1 + g2 + g3 + sCb)*vg = (g1 + sCb)*vdry + g2*vwet
        gs = self.g1 + self.g2 + self.g3
        k, Cb = self.k, self.Cb
        num = (self.g1 * (vdry + self.x1) + k * Cb * (vdry - self.x1)
               + self.g2 * (vwet + self.w1) - (gs - k * Cb) * self.y1)
        y = num / (gs + k * Cb)
        self.x1, self.w1, self.y1 = vdry, vwet, y
        return y


# --- the amp -------------------------------------------------------------------

class PrincetonReverb:
    def __init__(self, fs, volume=0.5, treble=0.5, bass=0.5,
                 reverb=0.3, trem_speed=0.5, trem_intensity=0.0):
        self.fs = fs
        p12 = T_12AX7
        self.v1a = TriodeStageRT(p12, fs, B=240.0, Rp=100e3, Rk=1500.0,
                                 Ck=25e-6)
        self.stack = fender_tone_stack(fs, treble, bass, volume).compile(7)
        self.v1b = TriodeStageRT(p12, fs, B=240.0, Rp=100e3, Rk=1500.0,
                                 Ck=25e-6, Cc=0.02e-6, RL=1e6)
        # reverb branch
        self.rev_hp_a = 1.0 / (2 * fs * 500e-12 * 1e6)   # 500pF/1M send HPF
        self._rev_hp_state = 0.0
        self._rev_hp_x1 = 0.0
        self.v2 = TriodeStageRT(T_12AT7, fs, B=410.0, Rp=10e3, Rk=2200.0,
                                Ck=25e-6)                # TR3 simplified R load
        self.tank_ir = spring_tank_ir(fs)
        self.v3a = TriodeStageRT(p12, fs, B=240.0, Rp=220e3, Rk=1500.0,
                                 Ck=25e-6, Cc=0.003e-6, RL=100e3)
        self.k_reverb = reverb
        self.mixer = ReverbMixerRT(fs)
        # V3B with NFB tail
        self.v3b = TriodeStageRT(p12, fs, B=240.0, Rp=100e3, Rk=1500.0,
                                 Ck=25e-6, Cc=0.02e-6, RL=1e6)
        self.R_nfb, self.R_tail = 2700.0, 47.0
        self.pi = CathodyneRT(fs, B=240.0)
        self.power = PowerStageRT(fs)
        self.psu = PSU(fs)
        self.trem_hz = 3.0 + 4.0 * trem_speed     # circuit-derived range
        self.trem_depth = 4.0 * trem_intensity    # volts of bias wiggle
        self.tank_send_k = 8.0e-3   # TR3 stepdown to tank coil (calibrate)
        self.tank_ret_k = 0.4       # PRODUCT VOICING (user preference, hot
                                    # return, V3A saturates, wet dominates);
                                    # physically-calibrated value is 4.0e-4
                                    # (test/calib_rev.cpp grid search),
                                    # pending real-pan measurement
        self.probe = None           # optional per-sample NFB injection (debug)

    def _rev_hp(self, x):
        y = np.empty_like(x)
        a = self.rev_hp_a
        s, x1 = self._rev_hp_state, self._rev_hp_x1
        for i in range(len(x)):
            s = (s * (1 - a) + a * (x[i] + x1)) / (1 + a)
            y[i] = x[i] - s
            x1 = x[i]
        self._rev_hp_state, self._rev_hp_x1 = s, x1
        return y

    def process(self, x):
        fs = self.fs
        n = len(x)
        # ---- pass A: front end ------------------------------------------
        v1a_out = np.empty(n)
        for i in range(n):
            v1a_out[i] = self.v1a.step(x[i])
        vtone = self.stack.process(v1a_out)
        v1b_out = np.empty(n)
        for i in range(n):
            v1b_out[i] = self.v1b.step(vtone[i])
        # ---- reverb branch ----------------------------------------------
        send = self._rev_hp(v1b_out)
        drv = np.empty(n)
        for i in range(n):
            drv[i] = self.v2.step(send[i]) * self.tank_send_k
        wet_raw = fftconvolve(drv, self.tank_ir)[:n] * self.tank_ret_k
        wet = np.empty(n)
        for i in range(n):
            wet[i] = self.v3a.step(wet_raw[i])
        wet *= self.k_reverb          # Reverb pot (wiper into 470k)
        # ---- pass B: V3B + PI + power + PSU + tremolo --------------------
        y = np.empty(n)
        vspk = 0.0
        vspk2 = 0.0   # NFB uses 2-sample average: zero at Nyquist, kills the
                      # numerical loop oscillation the real OT's leakage
                      # inductance prevents (leakage model TODO in PLAN)
        t = np.arange(n) / fs
        trem = self.trem_depth * np.sin(2 * np.pi * self.trem_hz * t) \
            if self.trem_depth > 0 else np.zeros(n)
        for i in range(n):
            vg = self.mixer.step(v1b_out[i], wet[i])
            # NFB injection at V3B cathode tail (one-sample delayed speaker V)
            # node J: vJ = (Rt*ik + Rt*vspk/Rn) / (1 + Rt/Rn)
            Rt, Rn = self.R_tail, self.R_nfb
            v_fb = 0.5 * (vspk + vspk2)
            vJ = (Rt * self.v3b.ik_prev + Rt * v_fb / Rn) / (1.0 + Rt / Rn)
            if self.probe is not None:        # loop-gain measurement hook
                vJ += self.probe[i]
            self.v3b.nfb = vJ
            v3b_out = self.v3b.step(vg)
            out_t, out_b = self.pi.step(v3b_out)
            vA, vB, vC, vD = self.psu.step(self.power.i_plates,
                                           self.power.i_screens)
            # grid assignment sets NFB polarity: speaker V must arrive at the
            # V3B cathode in phase with its grid signal (negative feedback);
            # verified empirically in validate_princeton [6]
            vspk2 = vspk
            vspk = self.power.step(out_t, out_b, vA, vB, bias_mod=trem[i])
            y[i] = vspk
        return y
