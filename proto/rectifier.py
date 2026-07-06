"""Mesa/Boogie Dual Rectifier — RHYTHM (Orange) channel, white-box.
Circuit values transcribed from the factory Rev-F 2-channel schematic (RF-1F, 6-93,
schematicheaven.net). Every number below is from that sheet (铁律一) — see the values
in-line. Shared 4-stage cascade + cathode follower + FMV tone stack, exactly as the
real amp routes the Orange voicing.

  V1A -> [OR GAIN 1M] -> V2A -> V2B(unbypassed 39K) -> V3A -> V3B(follower) -> tone -> [OR MSTR]

The Recto's tight/aggressive bass is NOT from tiny coupling caps (those are .02µF here)
but from (a) the UNBYPASSED 39K cathode on V2B (heavy local degeneration) and (b) small
bright/gain-injection caps. Bright caps deferred to the refine pass; this is the gross
gain-staging + voicing skeleton to overlay against the NAM first (Princeton flow).
"""
import numpy as np
from tubes import T_12AX7
from mna import LinearNetwork
from princeton import TriodeStageRT


class BiquadLP:
    """Resonant 2nd-order lowpass (RBJ) — models the OT leakage-inductance + winding-
    capacitance resonance: a presence peak then a 12 dB/oct rolloff. fc/Q identify the
    real OT's HF resonance (not on any datasheet; measured, like the OT primary Z)."""
    def __init__(self, fs, fc, Q):
        import math
        w0 = 2 * math.pi * fc / fs; c, s = math.cos(w0), math.sin(w0); al = s / (2 * Q)
        a0 = 1 + al
        self.b0 = (1 - c) / 2 / a0; self.b1 = (1 - c) / a0; self.b2 = self.b0
        self.a1 = -2 * c / a0; self.a2 = (1 - al) / a0
        self.x1 = self.x2 = self.y1 = self.y2 = 0.0
    def step(self, x):
        y = (self.b0 * x + self.b1 * self.x1 + self.b2 * self.x2
             - self.a1 * self.y1 - self.a2 * self.y2)
        self.x2, self.x1 = self.x1, x; self.y2, self.y1 = self.y1, y
        return y


class OnePoleLP:
    """Miller/grid-stopper HF rolloff: corner from real R_stopper x C_miller,
    C_miller = Cgp(1.6pF)*(1+A_stage). Bilinear one-pole. 铁律一: fc is computed
    from schematic R and the tube's Miller capacitance, not tuned."""
    def __init__(self, fs, fc):
        wc = 2 * np.pi * fc; k = 2 * fs
        self.b = wc / (k + wc); self.a = (wc - k) / (k + wc); self.x1 = self.y1 = 0.0
    def step(self, x):
        y = self.b * (x + self.x1) - self.a * self.y1
        self.x1 = x; self.y1 = y
        return y


def recti_tone_stack(fs, treble=0.5, mid=0.5, bass=0.5, master=0.7, Rsrc=1e3):
    """Marshall/FMV stack, Orange values: TRBL 250K+500pF, BASS 1M, MID 25K pot,
    slope 47K, bass/mid caps .02µF. Driven by the cathode follower -> stiff Rsrc."""
    RT, RB, RM, RV = 250e3, 1e6, 25e3, 1e6
    cl = lambda p: min(max(p, 1e-3), 0.999) ** 2
    tw, bw, mw, vw = cl(treble), cl(bass), cl(mid), cl(master)
    net = LinearNetwork(8, fs)
    net.Vsrc(1)
    net.R(1, 2, max(Rsrc, 1.0))              # cathode-follower output impedance (stiff)
    net.Cap(2, 3, 500e-12)                   # treble cap 500pF (Orange C5)
    net.R(3, 6, max((1 - tw) * RT, 1.0))     # treble pot upper (wiper = 6)
    net.R(6, 4, max(tw * RT, 1.0))           # treble pot lower -> slope node 4
    net.R(2, 4, 47e3)                        # slope R 47K (R274)
    net.Cap(4, 8, 0.02e-6)                   # bass cap .02µF (C25)
    net.R(8, 5, max(bw * RB, 1.0))           # bass pot 1M -> mid node 5
    net.Cap(4, 5, 0.02e-6)                   # mid cap .02µF (C26)
    net.R(5, 0, max(mw * RM, 1.0))           # MID POT 25K to gnd (Marshall-style)
    net.R(6, 7, max((1 - vw) * RV, 1.0))     # master (OR MSTR 1M) upper
    net.R(7, 0, max(vw * RV, 1.0))           # master lower (wiper = 7 = out)
    return net


class DualRectifier:
    """Rhythm/Orange channel head, DI (flat-load) output at the tone-stack out —
    matches how the NAM head captures were taken (pre power amp for now; power
    stage + OT added in the refine pass once the preamp voicing overlaps)."""
    def __init__(self, fs, gain=0.7, treble=0.5, mid=0.5, bass=0.5, master=0.7,
                 in_scale=3.0):
        # plate rails from the B+ sheet (local plate node voltages, printed):
        #   V1A 200V, V2A 280V, V2B 384V, V3A 213V.  Rp/Rk/Cc all schematic values.
        # Ck=None on V1A/V2A/V3A: their 1µF bypass is in SERIES with 47K (LDR-switched);
        # in crunch/rhythm mode 47K>>Rk 1.8K so the cap barely shunts -> ~unbypassed
        # (tight, no bass boost). Only fully bypasses in the bright/high-gain LDR state.
        self.v1a = TriodeStageRT(T_12AX7, fs, B=200.0, Rp=220e3, Rk=1.8e3, Ck=None,
                                 Cc=0.001e-6, RL=1e6)          # OR GAIN pot = 1M load
        self.v2a = TriodeStageRT(T_12AX7, fs, B=280.0, Rp=100e3, Rk=1.8e3, Ck=None,
                                 Cc=0.001e-6, RL=1e6)
        self.v2b = TriodeStageRT(T_12AX7, fs, B=384.0, Rp=100e3, Rk=39e3, Ck=None,
                                 Cc=0.001e-6, RL=1e6)          # UNBYPASSED 39K (signature)
        self.v3a = TriodeStageRT(T_12AX7, fs, B=213.0, Rp=220e3, Rk=1.8e3, Ck=None,
                                 Cc=0.001e-6, RL=1e6)
        # Miller/grid-stopper HF rolloff before each grid. fc = 1/(2*pi*Rstop*Cmiller),
        # Cmiller = 1.6pF*(1+A_est).  V2A: 470K, A~40 -> ~4.6kHz;  V2B: 470K, A~14 (cold)
        # -> ~13kHz;  V3A: 220K, A~50 -> ~9kHz.  (schematic R + tube Miller = physics.)
        self.lp2a = OnePoleLP(fs, 1.0 / (2 * np.pi * 470e3 * 1.6e-12 * 41))
        self.lp2b = OnePoleLP(fs, 1.0 / (2 * np.pi * 470e3 * 1.6e-12 * 15))
        self.lp3a = OnePoleLP(fs, 1.0 / (2 * np.pi * 220e3 * 1.6e-12 * 51))
        self.stack = recti_tone_stack(fs, treble, mid, bass, master).compile(7)
        # Power amp + output transformer HF response (the head NAM includes it):
        # resonant peak (~presence, from OT leakage L) then rolloff. fc/Q identified
        # from the NAM's peak+rolloff. Full nonlinear power stage is the next build.
        self.ot = BiquadLP(fs, 3800, 1.4)
        self.gain = min(max(gain, 1e-3), 0.999) ** 2          # OR GAIN 1M, log taper
        self.master = master
        self.in_scale = in_scale

    def process(self, x):
        out = np.empty(len(x), np.float64)
        v1a, v2a, v2b, v3a, stack, g = (self.v1a, self.v2a, self.v2b, self.v3a,
                                        self.stack, self.gain)
        for n in range(len(x)):
            y = v1a.step(x[n] * self.in_scale)
            y = v2a.step(self.lp2a.step(y * g))    # OR GAIN pot + Miller LP into V2A
            y = v2b.step(self.lp2b.step(y))
            y = v3a.step(self.lp3a.step(y))
            # V3B cathode follower ~ unity low-Z buffer into the stack (refine later)
            y = stack.process(np.array([y]))[0]
            out[n] = self.ot.step(y)                    # power-amp/OT resonant response
        return out


if __name__ == "__main__":
    fs = 48000
    amp = DualRectifier(fs, gain=0.6, treble=0.5, mid=0.4, bass=0.4, master=0.7)
    t = np.arange(int(0.2 * fs)) / fs
    x = 0.03 * np.sin(2 * np.pi * 220 * t)
    y = amp.process(x)
    print(f"in pk={np.max(np.abs(x)):.3f}  out pk={np.max(np.abs(y)):.3f}  "
          f"out rms={np.sqrt(np.mean(y**2)):.4f}")
    # crude THD at 220
    c = np.exp(-2j * np.pi * 220 * t)
    fund = np.abs(np.sum(y * c)) / len(y) * 2
    thd = 100 * np.sqrt(max(np.mean(y**2) - fund**2 / 2, 0) / (fund**2 / 2))
    print(f"THD@220 ~ {thd:.0f}%  (NAM ~ 27-40% at mid drive)")
