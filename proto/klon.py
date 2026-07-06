"""Klon Centaur ("Silver") — white-box, SCHEMATIC-FIRST (the NAM capture is tone-colored,
so we reconstruct from the real circuit, not by chasing the NAM).
Values from the reverse-engineered schematic (Aion/ElectroSmash/Coda, 铁律一):

  IC1A buffer (unity) -> IC1B non-inverting gain, gain A = 1 + R12/lower,
  R12 = 422k, lower = R11(15k)+R10(2k)+GAIN(100k). TWO 1N34A germanium diodes
  ANTI-PARALLEL across R12 soft-limit the FEEDBACK EXCESS at ~±0.35V. Because the
  diodes clip only the *excess* (vo - vin), the clean input passes straight through
  -> that is exactly why the Centaur is "transparent". Then an active tilt Treble.

The germanium clip shape: the feedback excess current drives the anti-parallel Ge pair;
solving I=2*Is*sinh(vd/nVT)=g_drive gives vd = nVT*asinh(drive/(2*Is)) — a soft, early,
gradual knee (germanium's smooth character), asymptoting near the 0.35V anchor.
"""
import numpy as np

VT = 0.02585
IS_GE, N_GE = 2e-6, 1.3                   # 1N34A germanium (knee ~0.35V)
R12, R11, R10, GAIN_POT = 422e3, 15e3, 2e3, 100e3

def klon_gain(gain):
    lower = R11 + R10 + (1.0 - min(max(gain, 0.0), 0.999)) * GAIN_POT
    return 1.0 + R12 / lower               # 4.6x (gain=0) .. 25.8x (gain=1)

def ge_softclip(drive):
    """Anti-parallel Ge pair across R12: the feedback voltage vd that carries the
    'drive' current. vd = nVT*asinh(drive/(2*Is)) — soft germanium knee."""
    return N_GE * VT * np.arcsinh(drive / (2.0 * IS_GE))

class Klon:
    def __init__(self, fs, gain=0.5, treble=0.5, level=0.7, in_scale=0.6):
        self.fs = fs; self.level = level; self.in_scale = in_scale
        self.set_gain(gain); self.set_treble(treble)
        self.t_x1 = self.t_y1 = 0.0

    def set_gain(self, g):
        self.A = klon_gain(g)
        # the feedback 'excess' the diodes must absorb, referred to a drive current:
        # excess voltage (A-1)*vin would flow as (A-1)*vin/R12 through the diodes.
        self.gexc = (self.A - 1.0) / R12

    def set_treble(self, t):
        fc = 1600.0                        # tilt pivot (C14 3.9nF w/ 100k/4.7k network)
        wc = 2 * np.pi * fc; k = 2 * self.fs
        self.b0 = wc / (k + wc); self.a1 = (wc - k) / (k + wc)
        self.tilt = (t - 0.5) * 2.0        # -1 (dark) .. +1 (bright)

    def process(self, x):
        vin = x * self.in_scale
        # IC1B output = clean input + soft-clipped feedback excess. The diodes limit the
        # excess (A-1)*vin toward the ~0.35V germanium knee; vin itself passes through
        # untouched -> transparent, with grit growing as gain (A) pushes the excess in.
        excess = (self.A - 1.0) * vin
        knee = 0.35
        vo = vin + knee * np.tanh(excess / knee)
        y = vo * self.level
        # active tilt Treble
        out = np.empty_like(y); x1 = self.t_x1; y1 = self.t_y1
        for n in range(len(y)):
            lp = self.b0 * (y[n] + x1) - self.a1 * y1
            x1 = y[n]; y1 = lp
            out[n] = lp + (1.0 + self.tilt) * (y[n] - lp)
        self.t_x1 = x1; self.t_y1 = y1
        return out


if __name__ == "__main__":
    fs = 48000
    t = np.arange(int(0.2 * fs)) / fs
    for gn in (0.2, 0.6, 1.0):
        k = Klon(fs, gain=gn, treble=0.5, level=0.7)
        x = 0.3 * np.sin(2 * np.pi * 220 * t)
        y = k.process(x)
        c = np.exp(-2j * np.pi * 220 * t); f = abs(np.sum(y * c)) / len(y) * 2
        thd = 100 * np.sqrt(max(np.mean(y ** 2) - f * f / 2, 0) / (f * f / 2))
        print(f"gain={gn}: A={klon_gain(gn):.1f}x  out pk={np.max(np.abs(y)):.2f}  THD={thd:.0f}%")
