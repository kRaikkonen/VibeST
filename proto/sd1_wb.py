"""PURE WHITE-BOX Boss SD-1 Super OverDrive.

Real components / real values from the official Boss schematic (fig1,
ETS212-510A) — NO fitted constants (contrast the HYBRID sd1sim.py, which fudged
the Drive pot to 100k and used shelf filters). The real SD-1 is the OD-1
clipper (op-amp + ASYMMETRIC 2x+1x 1N4148, 1M Drive pot — od1sim values ARE the
SD-1 values, since SD-1 = OD-1 + Tone) plus a passive Tone control.
  - clipping stage : od1sim.DriveStageRT (default ASYMMETRIC 2+1 clipper).
  - tone           : pedal_tone.sd1_tone (real passive RC pot network, MNA).
  - input buffer   : od1sim.EmitterFollowerRT (Ebers-Moll BJT).
"""
import numpy as np
from od1sim import OD1Params, EmitterFollowerRT, DriveStageRT
from pedal_tone import sd1_tone


class SD1WhiteBox:
    def __init__(self, fs, drive=0.5, tone=0.5, level=1.0, buffers=True):
        p = OD1Params()                       # OD-1/SD-1 shared clipper values
        self.p = p
        self.buffers = buffers
        self.buf_in = EmitterFollowerRT(p, fs, Rs=p.R101, Cc=p.C101,
                                        Rb=p.R102, Re=p.R103)
        self.drive = DriveStageRT(p, fs, drive)   # default ASYMMETRIC 2+1 clipper
        self.tone = sd1_tone(fs, tone)
        self.level = level

    def process(self, x):
        y = self.buf_in.process(x) if self.buffers else x
        y = self.drive.process(y)
        y = self.tone.process(y)
        return y * self.level


if __name__ == "__main__":
    fs = 48000.0
    FR = [82, 330, 1031, 2131, 4127, 8237]
    seg = int(0.15 * fs)
    def measure(tone, amp):
        out = []
        for f in FR:
            p2 = SD1WhiteBox(fs, 0.5, tone, 1.0, buffers=False)
            t = np.arange(seg)/fs; x = amp*np.sin(2*np.pi*f*t)
            y = p2.process(x); s = y[seg//2:]; xi = x[seg//2:]
            c = np.exp(-2j*np.pi*f*np.arange(len(s))/fs)
            out.append(20*np.log10(np.abs(np.sum(s*c))/np.abs(np.sum(xi*c))))
        out = np.array(out); return out - out[2]
    print("SD-1 pure-white-box tone-shaping (dB rel 1kHz):")
    print("freq   " + " ".join(f"{f:>6}" for f in FR))
    for tn in (1.0, 0.0):
        print(f"tone{tn} " + " ".join(f"{v:+6.1f}" for v in measure(tn, 0.02)))
    # distortion: ASYMMETRIC clipper -> even harmonics present (H2)
    ped = SD1WhiteBox(fs, 0.9, 0.5, 1.0, buffers=False)
    t = np.arange(int(0.3*fs))/fs; x = 0.15*np.sin(2*np.pi*220*t)
    y = ped.process(x); s = y[3000:-3000]; sp = np.abs(np.fft.rfft(s*np.hanning(len(s))))
    fr = np.fft.rfftfreq(len(s), 1/fs); H = [sp[np.argmin(np.abs(fr-k*220))] for k in range(1,6)]
    H = 20*np.log10(np.array(H)/H[0])
    print("220Hz harmonics H2..H5 (dB rel H1):", np.round(H[1:], 0),
          "-> asymmetric = H2 present (even)")
