"""[⚠️ HYBRID — NOT pure white-box. Kept for reference; superseded by the true
white-box rebuild in ts9_whitebox.py once the TS808 schematic is transcribed.]

违反铁律一的部分(为凑 NAM 曲线调的常数,非元件级):
  - Tone = LowShelfRT+HighShelfRT tilt,TONE_FC/GLO/GHI/TONE_MID=0.68 全是搜出来
    贴曲线的,不是真 TS tone 电路。
  - OUT_LP=2000Hz 是硬加的一个极点,没对应到具体元件。
  - C102/C104 等值是搜出来的。
白盒的部分:op-amp + 对称二极管削波(1+1)、BJT 缓冲、mid-hump 网络。失真谐波对
真机 NAM 匹配很好(H3/H5,对称确认)。

Ibanez TS9 Tube Screamer (≈TS808) — HYBRID, built on od1sim.py.

Differs from the SD-1: SYMMETRIC clipper (1x 1N4148 each way) in the op-amp
feedback; the classic TS gain/hump network (51k fixed + 500k Drive pot, 4.7k /
0.051µF -> ~660 Hz mid-hump) and a brighter fixed top (C103 51pF with the large
Rf -> high-cut ~5.7 kHz). The Tone control is a first-order TILT pivoting
~1.3 kHz (tone up -> treble boost + bass cut), calibrated to the real TS9 NAM.
"""
import numpy as np
from dataclasses import replace
from od1sim import (OD1Params, EmitterFollowerRT, DriveStageRT, ClipperDiodes,
                    OnePoleHPExact)
from sd1sim import LowShelfRT


class HighShelfRT:
    """First-order high-shelf, bilinear. HF gain g, LF gain 1, corner wc.
    H(s) = (g*s + wc)/(s + wc)."""
    def __init__(self, fc, g, fs):
        wc = 2 * np.pi * fc; k = 2.0 * fs
        self.b0 = g * k + wc; self.b1 = -g * k + wc
        self.a0 = k + wc; self.a1 = -k + wc
        self.x1 = 0.0; self.y1 = 0.0

    def process(self, x):
        y = np.empty_like(x)
        b0, b1, a0, a1 = self.b0, self.b1, self.a0, self.a1
        x1, y1 = self.x1, self.y1
        for n in range(len(x)):
            yn = (b0 * x[n] + b1 * x1 - a1 * y1) / a0
            y[n] = yn; x1 = x[n]; y1 = yn
        self.x1, self.y1 = x1, y1
        return y


class OnePoleLPRT:
    """First-order low-pass, bilinear. H(s) = 1/(1 + s/wc)."""
    def __init__(self, fc, fs):
        wc = 2 * np.pi * fc; k = 2.0 * fs
        self.b0 = wc / (k + wc); self.b1 = self.b0
        self.a1 = (wc - k) / (k + wc)
        self.x1 = 0.0; self.y1 = 0.0

    def process(self, x):
        y = np.empty_like(x)
        b0, b1, a1 = self.b0, self.b1, self.a1
        x1, y1 = self.x1, self.y1
        for n in range(len(x)):
            yn = b0 * x[n] + b1 * x1 - a1 * y1
            y[n] = yn; x1 = x[n]; y1 = yn
        self.x1, self.y1 = x1, y1
        return y


def ts9_params():
    """TS808/TS9 drive network. Symmetric 1N4148 clipper. C104 0.1µF puts the
    hump peak low (like the real TS bandpass), C102 22nF gentles the input HP;
    the fixed ~4.5 kHz top rolloff is a separate output pole (op-amp/output
    bandwidth) since the 51pF feedback cap alone can't span the drive range."""
    return replace(OD1Params(), C102=33e-9, R104=51e3, R105=51e3,
                   VR101_max=500e3, R106=4.7e3, C104=0.1e-6, C103=51e-12)


class TS9Pedal:
    TONE_FC = 1300.0        # tilt pivot (Hz)
    TONE_MID = 0.68         # tone-knob position where the tilt is flat (the TS
                            # tone stays dark below noon, brightens near max)
    TONE_GLO = 8.0          # low-shelf cut range across tone (dB)
    TONE_GHI = 30.0         # high-shelf boost range across tone (dB)
    OUT_LP = 2000.0         # fixed output low-pass (op-amp/output bandwidth)

    def __init__(self, p, fs, drive=0.7, tone=0.5, level=0.7, buffers=True):
        self.p = p
        self.buffers = buffers
        self.buf_in = EmitterFollowerRT(p, fs, Rs=p.R101, Cc=p.C101,
                                        Rb=p.R102, Re=p.R103)
        self.drive = DriveStageRT(p, fs, drive,
                                  clipper=ClipperDiodes(p, nDown=1, nUp=1))
        gLo = 10.0 ** ((self.TONE_MID - tone) * self.TONE_GLO / 20.0)
        gHi = 10.0 ** ((tone - self.TONE_MID) * self.TONE_GHI / 20.0)
        self.tlo = LowShelfRT(self.TONE_FC, gLo, fs)
        self.thi = HighShelfRT(self.TONE_FC, gHi, fs)
        self.out_lp = OnePoleLPRT(self.OUT_LP, fs)
        self.hp_lvl = OnePoleHPExact(p.R110 + p.VR103, p.C108, fs)
        self.k_level = level * p.VR103 / (p.R110 + p.VR103)
        self.buf_out = EmitterFollowerRT(p, fs, Rs=1.0, Cc=p.C109,
                                         Rb=p.R112, Re=p.R113)
        self.hp_out = OnePoleHPExact(p.R115, p.C110, fs)

    def process(self, x):
        y = self.buf_in.process(x) if self.buffers else x
        y = self.drive.process(y)
        y = self.tlo.process(y)
        y = self.thi.process(y)
        y = self.out_lp.process(y)
        y = self.hp_lvl.process(y) * self.k_level
        if self.buffers:
            y = self.buf_out.process(y)
        y = self.hp_out.process(y)
        return y
