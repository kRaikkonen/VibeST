"""Boss OD-1 (1977) white-box circuit simulation — prototype.

Component values and circuit equations: docs/schematic-od1-1977.md.
Drive/filter stage voltages referenced to VB (4.5 V bias rail);
BJT buffer stages solved in absolute voltages around their DC bias point.

Physics models:
  * diodes   — Shockley equation (1N4148 SPICE card)
  * op-amps  — 2-stage macro model: input-pair tanh saturation feeding the
               compensation integrator => dvo/dt = SR*tanh(wu*vid/SR) - wp*vo.
               Small-signal: exact one-pole (A0, GBW); large-signal: slew limit.
               Slew limiting in real op-amps IS tail-current saturation, so the
               tanh form is physically motivated, not a fudge.
  * BJTs     — Ebers-Moll forward-active (iC = Is*expm1(vBE/VT), iB = iC/beta)

Every real-time stage (trapezoidal discretization + Newton-Raphson) has a
matching continuous-time golden reference solved with scipy Radau.
"""

from dataclasses import dataclass
import numpy as np
from scipy.integrate import solve_ivp


# --- component values (docs/schematic-od1-1977.md) -------------------------

@dataclass
class OD1Params:
    VDD: float = 9.0
    VB: float = 4.5                 # bias rail
    # S1 input buffer (Q1 emitter follower)
    R101: float = 1e3
    C101: float = 47e-9
    R102: float = 470e3
    R103: float = 10e3              # (?) emitter resistor, to verify
    # S2 drive stage
    C102: float = 4.7e-9
    R104: float = 100e3
    R105: float = 33e3
    VR101_max: float = 1e6          # OverDrive pot, log taper
    R106: float = 4.7e3
    C104: float = 47e-9
    C103: float = 100e-12
    # clipping diodes: 1N4148 SPICE card
    diode_Is: float = 2.52e-9
    diode_N: float = 1.752
    VT: float = 0.02585             # kT/q at 300 K
    # S3 filter stage
    R107: float = 10e3
    R108: float = 10e3
    C105: float = 18e-9
    # S4 level
    C108: float = 1e-6
    R110: float = 4.7e3
    VR103: float = 10e3
    # S5 output buffer (Q2 emitter follower)
    C109: float = 100e-9
    R112: float = 470e3
    R113: float = 10e3              # (?) Q2 emitter resistor, to verify
    C110: float = 1e-6
    R115: float = 100e3
    # BJT (2SC732-class small-signal NPN) — generic card, to verify
    bjt_Is: float = 1e-14
    bjt_beta: float = 300.0

    def Rf(self, drive: float) -> float:
        """Feedback resistance for pot position drive in [0,1] (log taper)."""
        taper = (np.expm1(np.log(51.0) * drive)) / 50.0   # ~10% at midpoint
        return self.R105 + self.VR101_max * taper


# --- op-amp macro model ------------------------------------------------------

@dataclass
class OpampMacro:
    name: str
    A0: float       # DC open-loop gain, linear
    GBW: float      # gain-bandwidth product, Hz
    SR: float       # slew rate, V/s
    Vdz: float = 0.0    # class-AB output crossover dead-zone half-width (V);
                        # feedback suppresses it where loop gain is high, so it
                        # shows up as HF fizz — the real 741-class contribution
    Vsat: float = 3.2   # output swing clamp re VB (9 V rail, ~1.3 V headroom)

    @property
    def wu(self):   # unity-gain angular frequency
        return 2.0 * np.pi * self.GBW

    @property
    def wp(self):   # dominant-pole angular frequency
        return self.wu / self.A0

    def dvo_dt(self, vid, vo):
        return self.SR * np.tanh(self.wu * vid / self.SR) - self.wp * vo

    # output-stage crossover: smooth dead zone, slope 0 at origin
    def out(self, vo):
        if self.Vdz <= 0.0:
            return vo
        return vo - self.Vdz * np.tanh(vo / self.Vdz)

    def dout(self, vo):
        if self.Vdz <= 0.0:
            return 1.0
        th = np.tanh(vo / self.Vdz)
        return 1.0 - (1.0 - th * th)


# datasheet-typical parameters (RC3403A Vdz estimated pending measurement)
TL072 = OpampMacro("TL072", A0=2e5, GBW=3e6, SR=13e6, Vdz=0.0)
RC3403A = OpampMacro("RC3403A", A0=2e5, GBW=1e6, SR=0.5e6, Vdz=0.05)
JRC4558 = OpampMacro("JRC4558", A0=1e5, GBW=3e6, SR=1.7e6, Vdz=0.0)


# --- diode network: D101+D102 series vs D103 anti-parallel ------------------

class ClipperDiodes:
    """Feedback clipper: nDown series diodes one way, nUp the other. Default
    2/1 = OD-1 / SD-1 asymmetric; 1/1 = TS808/TS9 symmetric."""

    def __init__(self, p: OD1Params, nDown=2, nUp=1):
        self.Is = p.diode_Is
        self.nvt_pair = nDown * p.diode_N * p.VT
        self.nvt_single = nUp * p.diode_N * p.VT

    def i(self, v):
        a = np.clip(v / self.nvt_pair, -80.0, 80.0)
        b = np.clip(-v / self.nvt_single, -80.0, 80.0)
        return self.Is * np.expm1(a) - self.Is * np.expm1(b)

    def di_dv(self, v):
        a = np.clip(v / self.nvt_pair, -80.0, 80.0)
        b = np.clip(-v / self.nvt_single, -80.0, 80.0)
        return (self.Is / self.nvt_pair) * np.exp(a) \
             + (self.Is / self.nvt_single) * np.exp(b)


# --- linear one-pole building blocks (bilinear transform) --------------------

class OnePoleHPExact:
    """C-into-R divider highpass."""

    def __init__(self, R, C, fs):
        wc = 1.0 / (R * C)
        k = 2.0 * fs
        self.b0 = k / (k + wc)
        self.b1 = -self.b0
        self.a1 = (wc - k) / (k + wc)
        self.x1 = 0.0
        self.y1 = 0.0

    def process(self, x):
        y = np.empty_like(x)
        for n in range(len(x)):
            y[n] = self.b0 * x[n] + self.b1 * self.x1 - self.a1 * self.y1
            self.x1, self.y1 = x[n], y[n]
        return y


# --- S2 drive stage -----------------------------------------------------------
#
# States: vC102 (input coupling), vC104 (ground leg), vd (across C103/diodes/Rf,
# = vo - v-), vo (op-amp output, a state once the macro model is in).
#   d(vC102)/dt = (vin - vC102)/(R104*C102);       vp = vin - vC102
#   d(vC104)/dt = (vm - vC104)/(R106*C104);        vm = vo - vd
#   C103*d(vd)/dt = (vm - vC104)/R106 - vd/Rf - iD(vd)
#   d(vo)/dt = SR*tanh(wu*(vp - vm)/SR) - wp*vo    (opamp macro)
# opamp=None reduces to the ideal op-amp (vm = vp, vo = vp + vd).

class DriveStageRT:
    def __init__(self, p: OD1Params, fs: float, drive: float = 1.0,
                 opamp: OpampMacro | None = TL072,
                 max_iter: int = 80, tol: float = 1e-11, clipper=None):
        self.p = p
        self.fs = fs
        self.T = 1.0 / fs
        self.Rf = p.Rf(drive)
        self.diodes = clipper if clipper is not None else ClipperDiodes(p)
        self.opamp = opamp
        self.max_iter = max_iter
        self.tol = tol
        # states
        self.vC102 = 0.0
        self.vC104 = 0.0
        self.vd = 0.0
        self.vo = 0.0
        # one-sample memories for trapezoidal rule
        self.vin_prev = 0.0
        self.vm_prev = 0.0
        self.fd_prev = 0.0
        self.fo_prev = 0.0

    # ideal-op-amp scalar solve: g(v) = (2C/T + 1/Rf)*v + iD(v) = b, monotonic
    def _solve_vd_ideal(self, b):
        p, d = self.p, self.diodes
        glin = 2.0 * p.C103 / self.T + 1.0 / self.Rf
        v = self.vd
        lo, hi = -2.0, 2.0
        for _ in range(self.max_iter):
            f = glin * v + d.i(v) - b
            if abs(f) < 1e-14:
                return v
            if f > 0.0:
                hi = min(hi, v)
            else:
                lo = max(lo, v)
            v_new = v - f / (glin + d.di_dv(v))
            if not (lo < v_new < hi):
                v_new = 0.5 * (lo + hi)
            if abs(v_new - v) < 1e-15:
                return v_new
            v = v_new
        return v

    def _step_ideal(self, vp, a104):
        p = self.p
        self.vC104 = (self.vC104 * (1.0 - a104)
                      + a104 * (vp + self.vm_prev)) / (1.0 + a104)
        ileg = (vp - self.vC104) / p.R106
        c = 2.0 * p.C103 / self.T
        b = (c * self.vd
             + (self.fd_prev)          # fd_prev stores full rhs for ideal mode
             + ileg)
        self.vd = self._solve_vd_ideal(b)
        self.fd_prev = ileg - self.vd / self.Rf - self.diodes.i(self.vd)
        self.vm_prev = vp
        return vp + self.vd

    def _nr_solve(self, vp, T, state):
        """One trapezoidal step of size T. state/output tuple:
        (vd, vo, vC104, vm_prev, fd_prev, fo_prev). Damped Newton with
        residual-norm backtracking; returns (converged, new_state)."""
        p, d, oa = self.p, self.diodes, self.opamp
        vd0, vo0, vC104_0, vm_prev, fd_prev, fo_prev = state
        a104 = T / (2.0 * p.R106 * p.C104)
        s4 = a104 / (1.0 + a104)
        cC = 2.0 * p.C103 / T
        base = (vC104_0 * (1.0 - a104) + a104 * vm_prev) / (1.0 + a104)

        def residuals(vd_, vo_):
            vout = oa.out(vo_)
            vm = vout - vd_
            vC104_n = base + s4 * vm
            fd = (vm - vC104_n) / p.R106 - vd_ / self.Rf - d.i(vd_)
            F1 = cC * (vd_ - vd0) - fd - fd_prev
            th = np.tanh(oa.wu * (vp - vm) / oa.SR)
            fo = oa.SR * th - oa.wp * vo_
            F2 = (2.0 / T) * (vo_ - vo0) - fo - fo_prev
            # residual norm scaled to volts^2 (comparable rows)
            nrm = (F1 / cC) ** 2 + (F2 * T / 2.0) ** 2
            return F1, F2, th, nrm

        vd, vo = vd0, vo0
        conv = False
        for _ in range(self.max_iter):
            F1, F2, th, n0 = residuals(vd, vo)
            if n0 < 1e-24:
                conv = True
                break
            Xp = oa.dout(vo)
            dfd_dvm = (1.0 - s4) / p.R106
            J11 = cC + dfd_dvm + 1.0 / self.Rf + d.di_dv(vd)
            J12 = -dfd_dvm * Xp
            sech2 = oa.wu * (1.0 - th * th)
            J21 = -sech2
            J22 = 2.0 / T + oa.wp + sech2 * Xp
            det = J11 * J22 - J12 * J21
            dvd = np.clip((F1 * J22 - F2 * J12) / det, -0.3, 0.3)
            dvo = np.clip((J11 * F2 - J21 * F1) / det, -2.0, 2.0)
            alpha = 1.0
            vd_n, vo_n, n1 = vd, vo, n0
            for _ls in range(7):        # backtracking line search
                cd = vd - alpha * dvd
                co = min(max(vo - alpha * dvo, -oa.Vsat), oa.Vsat)
                _, _, _, nc = residuals(cd, co)
                if nc < n0:
                    vd_n, vo_n, n1 = cd, co, nc
                    break
                alpha *= 0.5
            if n1 >= n0:                # no improvement: smallest step anyway
                vd_n = vd - alpha * dvd
                vo_n = min(max(vo - alpha * dvo, -oa.Vsat), oa.Vsat)
            step = max(abs(vd_n - vd), abs(vo_n - vo))
            vd, vo = vd_n, vo_n
            if step < self.tol:
                _, _, _, nf = residuals(vd, vo)
                conv = nf < 1e-18
                break
        vout = oa.out(vo)
        vm = vout - vd
        vC104 = base + s4 * vm
        fd = (vm - vC104) / p.R106 - vd / self.Rf - d.i(vd)
        fo = oa.SR * np.tanh(oa.wu * (vp - vm) / oa.SR) - oa.wp * vo
        return conv, (vd, vo, vC104, vm, fd, fo)

    def _step_opamp(self, vp, a104):
        state = (self.vd, self.vo, self.vC104,
                 self.vm_prev, self.fd_prev, self.fo_prev)
        conv, new = self._nr_solve(vp, self.T, state)
        if not conv:                     # substep 4x through the fast traverse
            new = state
            for _ in range(4):
                _, new = self._nr_solve(vp, self.T / 4.0, new)
        (self.vd, self.vo, self.vC104,
         self.vm_prev, self.fd_prev, self.fo_prev) = new
        return self.opamp.out(self.vo)

    def process(self, x):
        p = self.p
        T = self.T
        a102 = T / (2.0 * p.R104 * p.C102)
        a104 = T / (2.0 * p.R106 * p.C104)
        y = np.empty_like(x)
        step = self._step_ideal if self.opamp is None else self._step_opamp
        for n in range(len(x)):
            vin = x[n]
            self.vC102 = (self.vC102 * (1.0 - a102)
                          + a102 * (vin + self.vin_prev)) / (1.0 + a102)
            vp = vin - self.vC102
            y[n] = step(vp, a104)
            self.vin_prev = vin
        return y


def drive_stage_reference(p: OD1Params, drive: float, vin_fn, t_eval,
                          opamp: OpampMacro | None = TL072,
                          rtol=1e-9, atol=1e-12):
    """Continuous-time golden reference (scipy Radau) of the same equations."""
    Rf = p.Rf(drive)
    d = ClipperDiodes(p)

    if opamp is None:
        def rhs(t, y):
            vC102, vC104, vd = y
            vin = vin_fn(t)
            vp = vin - vC102
            return ((vin - vC102) / (p.R104 * p.C102),
                    (vp - vC104) / (p.R106 * p.C104),
                    ((vp - vC104) / p.R106 - vd / Rf - d.i(vd)) / p.C103)
        y0 = (0.0, 0.0, 0.0)
    else:
        def rhs(t, y):
            vC102, vC104, vd, vo = y
            vin = vin_fn(t)
            vp = vin - vC102
            vm = opamp.out(vo) - vd
            return ((vin - vC102) / (p.R104 * p.C102),
                    (vm - vC104) / (p.R106 * p.C104),
                    ((vm - vC104) / p.R106 - vd / Rf - d.i(vd)) / p.C103,
                    opamp.dvo_dt(vp - vm, vo))
        y0 = (0.0, 0.0, 0.0, 0.0)

    sol = solve_ivp(rhs, (t_eval[0], t_eval[-1]), y0, method="Radau",
                    t_eval=t_eval, rtol=rtol, atol=atol)
    vin = np.array([vin_fn(t) for t in sol.t])
    vp = vin - sol.y[0]
    vo = vp + sol.y[2] if opamp is None else opamp.out(sol.y[3])
    return sol.t, vo


# --- S3 filter stage (inverting one-pole LP with op-amp macro) ---------------
#
# States: vC105 (= vo - vm across feedback cap), vo.
#   C105*d(vC105)/dt = -(x - vm)/R108 - (vo - vm)/R107;   vm = vo - vC105
#   d(vo)/dt = SR*tanh(wu*(0 - vm)/SR) - wp*vo

class FilterStageRT:
    def __init__(self, p: OD1Params, fs, opamp: OpampMacro = TL072,
                 max_iter: int = 30, tol: float = 1e-12):
        self.p = p
        self.T = 1.0 / fs
        self.opamp = opamp
        self.max_iter = max_iter
        self.tol = tol
        self.vC105 = 0.0
        self.vo = 0.0
        self.fc_prev = 0.0
        self.fo_prev = 0.0

    def process(self, x):
        p, oa, T = self.p, self.opamp, self.T
        cC = 2.0 * p.C105 / T
        gsum = 1.0 / p.R108 + 1.0 / p.R107
        y = np.empty_like(x)
        for n in range(len(x)):
            xn = x[n]
            vc, vo = self.vC105, self.vo
            for _ in range(self.max_iter):
                vout = oa.out(vo)
                Xp = oa.dout(vo)
                vm = vout - vc
                fc = -(xn - vm) / p.R108 - (vout - vm) / p.R107
                F1 = cC * (vc - self.vC105) - fc - self.fc_prev
                arg = -oa.wu * vm / oa.SR
                th = np.tanh(arg)
                fo = oa.SR * th - oa.wp * vo
                F2 = (2.0 / T) * (vo - self.vo) - fo - self.fo_prev
                J11 = cC + gsum
                J12 = -Xp / p.R108
                sech2 = oa.wu * (1.0 - th * th)
                J21 = -sech2
                J22 = 2.0 / T + oa.wp + sech2 * Xp
                det = J11 * J22 - J12 * J21
                dvc = (F1 * J22 - F2 * J12) / det
                dvo = (J11 * F2 - J21 * F1) / det
                vc -= dvc
                vo -= dvo
                vo = np.clip(vo, -oa.Vsat, oa.Vsat)
                if abs(dvc) < self.tol and abs(dvo) < self.tol:
                    break
            vout = oa.out(vo)
            vm = vout - vc
            self.fc_prev = -(xn - vm) / p.R108 - (vout - vm) / p.R107
            self.fo_prev = oa.SR * np.tanh(-oa.wu * vm / oa.SR) - oa.wp * vo
            self.vC105, self.vo = vc, vo
            y[n] = vout
        return y


# --- S1/S5 emitter-follower buffers (Ebers-Moll) ------------------------------
#
# Topology: vin -Rs- vx -Cc- vb(base);  Rb from vb to VBB;  emitter -> Re -> GND
# Ebers-Moll forward-active: iC = Is*expm1(vBE/VT), iB = iC/beta,
# iE = (1+1/beta)*iC.  Absolute voltages; output = ve - ve_dc (AC).
# Scalar reduction: parametrize by u = vBE:
#   ve(u) = Re*(1+1/beta)*iC(u),  vb(u) = ve(u) + u  -> monotone base-KCL in u.

class EmitterFollowerRT:
    def __init__(self, p: OD1Params, fs, Rs, Cc, Rb, Re,
                 max_iter: int = 60):
        self.p = p
        self.T = 1.0 / fs
        self.Rs, self.Cc, self.Rb, self.Re = Rs, Cc, Rb, Re
        self.Is, self.beta, self.VT = p.bjt_Is, p.bjt_beta, p.VT
        self.VBB = p.VB
        self.max_iter = max_iter
        # DC operating point: no current through Cc =>
        #   (VBB - vb)/Rb = iC/beta,  vb = ve(u) + u
        u = 0.6
        for _ in range(200):
            iC = self.Is * np.expm1(u / self.VT)
            ve = Re * (1.0 + 1.0 / self.beta) * iC
            vb = ve + u
            f = (self.VBB - vb) / Rb - iC / self.beta
            diC = (self.Is / self.VT) * np.exp(u / self.VT)
            dvb = Re * (1.0 + 1.0 / self.beta) * diC + 1.0
            df = -dvb / Rb - diC / self.beta
            du = f / df
            u -= np.clip(du, -0.05, 0.05)
            if abs(du) < 1e-14:
                break
        self.u = u
        self.ve_dc = Re * (1.0 + 1.0 / self.beta) * self.Is * np.expm1(u / self.VT)
        vb_dc = self.ve_dc + u
        self.vC101 = 0.0 - vb_dc          # vin_dc = 0 (guitar side at ground)
        self.dvc_prev = 0.0

    def _ic(self, u):
        return self.Is * np.expm1(np.clip(u / self.VT, -200.0, 200.0))

    def _dic(self, u):
        return (self.Is / self.VT) * np.exp(np.clip(u / self.VT, -200.0, 200.0))

    def process(self, x):
        T, Rs, Cc, Rb, Re = self.T, self.Rs, self.Cc, self.Rb, self.Re
        beta, VBB = self.beta, self.VBB
        kap = T / (2.0 * Rs * Cc)
        y = np.empty_like(x)
        for n in range(len(x)):
            vin = x[n]
            # trapezoid on vC101 folded into base KCL (vC101 linear in vb):
            c0 = self.vC101 + (T / 2.0) * self.dvc_prev
            u = self.u
            lo, hi = -2.0, 0.9
            for _ in range(self.max_iter):
                iC = self._ic(u)
                ve = Re * (1.0 + 1.0 / beta) * iC
                vb = ve + u
                vC101_n = (c0 + kap * (vin - vb)) / (1.0 + kap)
                h = (vin - vb - vC101_n) / Rs - (vb - VBB) / Rb - iC / beta
                if h > 0.0:
                    lo = max(lo, u)
                else:
                    hi = min(hi, u)
                diC = self._dic(u)
                dvb = Re * (1.0 + 1.0 / beta) * diC + 1.0
                dh = (-dvb * (1.0 - kap / (1.0 + kap))) / Rs \
                     - dvb / Rb - diC / beta
                u_new = u - h / dh
                if not (lo < u_new < hi):
                    u_new = 0.5 * (lo + hi)
                if abs(u_new - u) < 1e-13:
                    u = u_new
                    break
                u = u_new
            iC = self._ic(u)
            ve = Re * (1.0 + 1.0 / beta) * iC
            vb = ve + u
            vC101_n = (c0 + kap * (vin - vb)) / (1.0 + kap)
            self.dvc_prev = (vin - vb - vC101_n) / (Rs * Cc)
            self.vC101 = vC101_n
            self.u = u
            y[n] = ve - self.ve_dc
        return y


def buffer_reference(p: OD1Params, vin_fn, t_eval, Rs, Cc, Rb, Re,
                     rtol=1e-10, atol=1e-13):
    """Golden reference for the emitter follower: index-1 DAE, algebraic
    part (base KCL in u = vBE) solved by nested Newton inside rhs."""
    Is, beta, VT, VBB = p.bjt_Is, p.bjt_beta, p.VT, p.VB
    warm = {"u": 0.6}

    def solve_u(vin, vC):
        u = warm["u"]
        lo, hi = -2.0, 0.9
        for _ in range(200):
            iC = Is * np.expm1(np.clip(u / VT, -200, 200))
            ve = Re * (1.0 + 1.0 / beta) * iC
            vb = ve + u
            h = (vin - vb - vC) / Rs - (vb - VBB) / Rb - iC / beta
            if h > 0.0:
                lo = max(lo, u)
            else:
                hi = min(hi, u)
            diC = (Is / VT) * np.exp(np.clip(u / VT, -200, 200))
            dvb = Re * (1.0 + 1.0 / beta) * diC + 1.0
            dh = -dvb / Rs - dvb / Rb - diC / beta
            u_new = u - h / dh
            if not (lo < u_new < hi):
                u_new = 0.5 * (lo + hi)
            if abs(u_new - u) < 1e-14:
                u = u_new
                break
            u = u_new
        warm["u"] = u
        return u

    # DC init identical to RT class
    tmp = EmitterFollowerRT(p, 48000.0, Rs, Cc, Rb, Re)
    vC0 = tmp.vC101
    ve_dc = tmp.ve_dc

    def rhs(t, y):
        vC = y[0]
        vin = vin_fn(t)
        u = solve_u(vin, vC)
        iC = Is * np.expm1(np.clip(u / VT, -200, 200))
        vb = Re * (1.0 + 1.0 / beta) * iC + u
        return ((vin - vb - vC) / (Rs * Cc),)

    sol = solve_ivp(rhs, (t_eval[0], t_eval[-1]), (vC0,), method="Radau",
                    t_eval=t_eval, rtol=rtol, atol=atol)
    ve = np.empty_like(sol.t)
    for i, (t, vC) in enumerate(zip(sol.t, sol.y[0])):
        u = solve_u(vin_fn(t), vC)
        ve[i] = Re * (1.0 + 1.0 / beta) * Is * np.expm1(u / VT) - ve_dc
    return sol.t, ve


# --- full pedal chain ---------------------------------------------------------

class OD1Pedal:
    """Full OD-1 chain, all-white-box: BJT buffers (Ebers-Moll), op-amp macro
    drive + filter stages, level network, output coupling."""

    def __init__(self, p: OD1Params, fs, drive=0.5, level=1.0,
                 opamp: OpampMacro = TL072):
        self.p = p
        self.buf_in = EmitterFollowerRT(p, fs, Rs=p.R101, Cc=p.C101,
                                        Rb=p.R102, Re=p.R103)       # S1
        self.drive = DriveStageRT(p, fs, drive, opamp=opamp)         # S2
        self.filt = FilterStageRT(p, fs, opamp=opamp)                # S3
        self.hp_lvl = OnePoleHPExact(p.R110 + p.VR103, p.C108, fs)   # S4
        self.k_level = level * p.VR103 / (p.R110 + p.VR103)
        self.buf_out = EmitterFollowerRT(p, fs, Rs=1.0, Cc=p.C109,
                                         Rb=p.R112, Re=p.R113)      # S5
        self.hp_out = OnePoleHPExact(p.R115, p.C110, fs)             # S5

    def process(self, x):
        y = self.buf_in.process(x)
        y = self.drive.process(y)
        y = self.filt.process(y)
        y = self.hp_lvl.process(y) * self.k_level
        y = self.buf_out.process(y)
        y = self.hp_out.process(y)
        return y
