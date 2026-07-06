"""Dumble Steel String Singer (SSS) — white-box, fit to the (trusted) NAM + IR.
It's a single-channel CLEAN amp (no OD circuit, no clipping diodes); "Drive" = pushing
the same clean amp harder (input level up) into preamp/power breakup. Values from the
Colgan #002 LTspice netlist (铁律一): JFET buffer -> 12AX7 (Rp100k/Rk1.5k/Ck5µF) -> FMV
tone stack -> Volume -> 12AX7 (same) -> power/OT. The glassy voicing = bass cut + an
upper-mid/treble presence hump (~1.5-4kHz) + top rolloff.

  in -> V1 (12AX7, bypassed) -> tone stack -> [Volume] -> V2 (12AX7) -> OT resonance
"""
import numpy as np
from tubes import T_12AX7
from mna import LinearNetwork
from princeton import TriodeStageRT
from rectifier import OnePoleLP, BiquadLP


def dumble_tone_stack(fs, treble=0.5, mid=0.5, bass=0.5, volume=0.7, Rsrc=40e3):
    """SSS #002 FMV: TRBL 250k + 360pF, BASS 250k + 0.1µF, MID 100k pot + 0.047µF,
    slope 100k, Volume 1M. Fender-derived with a separate Mid pot (Dumble signature)."""
    RT, RB, RM, RV = 250e3, 250e3, 100e3, 1e6
    cl = lambda p: min(max(p, 1e-3), 0.999) ** 2
    tw, bw, mw, vw = cl(treble), cl(bass), cl(mid), cl(volume)
    net = LinearNetwork(8, fs)
    net.Vsrc(1)
    net.R(1, 2, max(Rsrc, 1.0))              # V1 plate Thevenin source
    net.Cap(2, 3, 360e-12)                   # treble cap 360pF (C1)
    net.R(3, 6, max((1 - tw) * RT, 1.0))
    net.R(6, 4, max(tw * RT, 1.0))
    net.R(2, 4, 100e3)                       # slope R 100k
    net.Cap(4, 8, 0.1e-6)                    # bass cap 0.1µF (C15)
    net.R(8, 5, max(bw * RB, 1.0))
    net.Cap(4, 5, 0.047e-6)                  # mid cap .047µF (C34)
    net.R(5, 0, max(mw * RM, 1.0))           # MID POT 100k to gnd
    net.R(6, 7, max((1 - vw) * RV, 1.0))     # Volume 1M
    net.R(7, 0, max(vw * RV, 1.0))
    return net


class DumbleSSS:
    def __init__(self, fs, drive=0.3, treble=0.6, mid=0.5, bass=0.4, volume=0.7,
                 in_scale=1.0, ot_fc=4200.0, ot_q=1.0):
        # two clean 12AX7 gain stages (bypassed cathodes = full clean gain), #002 values
        self.v1 = TriodeStageRT(T_12AX7, fs, B=325.0, Rp=100e3, Rk=1.5e3, Ck=5e-6,
                                Cc=0.02e-6, RL=1e6)
        self.v2 = TriodeStageRT(T_12AX7, fs, B=340.0, Rp=100e3, Rk=1.5e3, Ck=5e-6,
                                Cc=0.02e-6, RL=1e6)
        self.stack = dumble_tone_stack(fs, treble, mid, bass, volume).compile(7)
        # OT + presence: the glassy upper-mid/treble hump + top rolloff (measured resonance)
        self.ot = BiquadLP(fs, ot_fc, ot_q)
        self.miller = OnePoleLP(fs, 1.0 / (2 * np.pi * 100e3 * 1.6e-12 * 61))  # ~stage HF
        self.drive = drive; self.in_scale = in_scale; self.volume = volume

    def process(self, x):
        out = np.empty(len(x), np.float64)
        v1, v2, stack = self.v1, self.v2, self.stack
        g = self.drive
        for n in range(len(x)):
            y = v1.step(x[n] * self.in_scale)
            y = self.miller.step(y)
            y = stack.process(np.array([y]))[0]
            y = v2.step(y * (0.3 + 1.7 * g))     # Volume/push into 2nd stage
            out[n] = self.ot.step(y)
        return out


if __name__ == "__main__":
    fs = 48000
    a = DumbleSSS(fs, drive=0.3, treble=0.6, mid=0.5, bass=0.4)
    t = np.arange(int(0.2 * fs)) / fs
    x = 0.03 * np.sin(2 * np.pi * 220 * t)
    y = a.process(x)
    c = np.exp(-2j * np.pi * 220 * t); f = abs(np.sum(y * c)) / len(y) * 2
    thd = 100 * np.sqrt(max(np.mean(y ** 2) - f * f / 2, 0) / (f * f / 2))
    print(f"out pk={np.max(np.abs(y)):.2f}  THD@220={thd:.0f}%  (Clean NAM ~ 3-11%)")
