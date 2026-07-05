"""PURE WHITE-BOX Ibanez TS808/TS9 Tube Screamer.

Every element is a real circuit component with its real value (fig2 Jack Orman
"Son of Screamer" / ElectroSmash) — NO fitted constants (contrast the HYBRID
ts9sim.py). Shared physical primitives:
  - clipping stage  : od1sim.DriveStageRT (ideal op-amp + Shockley 1N914 diodes,
                      per-sample Newton) — SYMMETRIC 1+1 clipper.
  - tone/volume     : pedal_tone.ts808_tone (real op-amp active RC network, MNA).
  - input buffer    : od1sim.EmitterFollowerRT (Ebers-Moll BJT).
Validated against the circuit's own physics / ElectroSmash plots — NOT the
clipping-contaminated NAM tone sweep.
"""
import numpy as np
from dataclasses import replace
from od1sim import OD1Params, EmitterFollowerRT, DriveStageRT, ClipperDiodes
from pedal_tone import ts808_tone


def ts808_params():
    """TS808 clipping-stage real values (fig2 / ElectroSmash):
      Drive pot 500k + 51k fixed feedback; 4.7k gain-leg with 0.047µF -> 720 Hz
      mid-hump; 51pF feedback cap (HF roll); 0.047µF input coupling; symmetric
      1N914 silicon clipping diodes (Is=2.52nA, N=1.752)."""
    return replace(OD1Params(),
                   C102=0.047e-6, R104=510e3, R105=51e3, VR101_max=500e3,
                   R106=4.7e3, C104=0.047e-6, C103=51e-12,
                   diode_Is=2.52e-9, diode_N=1.752)


class TS808WhiteBox:
    def __init__(self, fs, drive=0.7, tone=0.5, level=0.7, buffers=True):
        p = ts808_params()
        self.p = p
        self.buffers = buffers
        self.buf_in = EmitterFollowerRT(p, fs, Rs=p.R101, Cc=p.C101,
                                        Rb=p.R102, Re=p.R103)
        self.drive = DriveStageRT(p, fs, drive,
                                  clipper=ClipperDiodes(p, nDown=1, nUp=1))
        self.tone = ts808_tone(fs, tone)
        self.level = level

    def process(self, x):
        y = self.buf_in.process(x) if self.buffers else x
        y = self.drive.process(y)
        y = self.tone.process(y)
        return y * self.level


if __name__ == "__main__":
    fs = 48000.0
    seg = int(0.15 * fs)
    FR = [82, 330, 1031, 2131, 4127, 8237]
    def measure(ped, amp):
        out = []
        for f in FR:
            p2 = TS808WhiteBox(fs, ped[0], ped[1], 1.0, buffers=False)
            t = np.arange(seg) / fs; x = amp * np.sin(2*np.pi*f*t)
            y = p2.process(x); s = y[seg//2:]; xi = x[seg//2:]
            c = np.exp(-2j*np.pi*f*np.arange(len(s))/fs)
            g = np.abs(np.sum(s*c))/np.abs(np.sum(xi*c))
            out.append(20*np.log10(g))
        out = np.array(out); return out - out[2]
    print("TS808 pure-white-box tone-shaping (low level, dB rel 1kHz):")
    print("freq   " + " ".join(f"{f:>6}" for f in FR))
    for tn in (1.0, 0.0):
        print(f"tone{tn} " + " ".join(f"{v:+6.1f}" for v in measure((0.5, tn), 0.02)))
    # distortion: harmonic parity of the symmetric clipper on a 220Hz tone
    ped = TS808WhiteBox(fs, 0.9, 0.5, 1.0, buffers=False)
    t = np.arange(int(0.3*fs))/fs; x = 0.2*np.sin(2*np.pi*220*t)
    y = ped.process(x); s = y[3000:-3000]; sp = np.abs(np.fft.rfft(s*np.hanning(len(s))))
    fr = np.fft.rfftfreq(len(s), 1/fs); H = [sp[np.argmin(np.abs(fr-k*220))] for k in range(1,6)]
    H = 20*np.log10(np.array(H)/H[0])
    print("220Hz harmonics H2..H5 (dB rel H1):", np.round(H[1:], 0),
          "-> symmetric = odd-dominant (H3>H2)")
