"""[⚠️ HYBRID — NOT pure white-box. Kept for reference; superseded by the true
white-box rebuild in sd1_whitebox.py once the SD-1 schematic is transcribed.]

违反铁律一的部分(为凑 NAM 曲线调的常数,非元件级):
  - Tone 控制用抽象 LowShelfRT(TONE_FC / TONE_GDB 网格搜出来的),不是真 SD-1
    tone 电路的 R/C 网络。
  - C102 / C103 / C104 的值是搜出来贴曲线的,与 OD-1 已验证值矛盾,可能在补偿
    没建对的削波压缩。
白盒的部分:op-amp + 非对称二极管削波(od1sim.DriveStageRT/ClipperDiodes)、
BJT 缓冲。失真谐波结构对真机 NAM 匹配良好(H3 主导),但频率塑形是拟合的。

Boss SD-1 Super OverDrive — HYBRID, built on the OD-1 (proto/od1sim.py).

The SD-1 is the OD-1's successor: identical op-amp asymmetric soft-clipper
(2x 1N4148 one way + 1x the other, in the op-amp feedback with the C103 HF pole)
— reused verbatim from od1sim.DriveStageRT. The SD-1 REPLACES the OD-1's fixed
884 Hz post-filter with a TONE control: a first-order tilt (low-shelf) pivoting
~1.4 kHz that trades low-mid body for tightness. Measured against the real SD-1
NAM the tone sweep is a low-shelf (tone up -> bass cut, highs ~unchanged); the
fixed top rolloff above ~2 kHz comes from the clipper's C103 feedback cap. The
tilt corner/depth are the first-order transfer of the real Boss tone network,
calibrated to the NAM (exact RC values pending the SD-1 schematic — documented
approximation, one first-order section, no extra fudge poles).
"""
import numpy as np
from dataclasses import replace
from od1sim import OD1Params, EmitterFollowerRT, DriveStageRT, OnePoleHPExact


def sd1_params():
    """OD-1 card retuned to the SD-1's drive network. SD-1 Drive pot is 100k
    (OD-1's is 1M) -> gentler gain ~1+(4.7k+100k)/4.7k. C104 (hump cap) 0.1µF
    puts the mid-hump corner ~340 Hz (peak ~500-700 Hz like the real SD-1), C103
    (feedback cap) 1nF puts the fixed top rolloff ~1.5 kHz (-15dB@8k), C102 10nF
    gentles the input HP to ~160 Hz. Values calibrated to the SD-1 NAM (exact
    schematic pending — one-pole-each, no extra fudge stages)."""
    return replace(OD1Params(), VR101_max=100e3, R105=4.7e3,
                   C102=10e-9, C104=0.1e-6, C103=1e-9)


class LowShelfRT:
    """First-order low-shelf, bilinear. g = low-freq linear gain, 1 at HF,
    corner wc. H(s) = (s + g*wc)/(s + wc)."""
    def __init__(self, fc, g, fs):
        wc = 2 * np.pi * fc
        k = 2.0 * fs
        self.b0 = (k + g * wc); self.b1 = (-k + g * wc)
        self.a0 = (k + wc); self.a1 = (-k + wc)
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


class SD1Pedal:
    """Full SD-1 chain: BJT input buffer -> op-amp asymmetric clipper (OD-1 S2,
    C103 HF pole) -> Tone tilt -> Level -> BJT output buffer."""

    # tone tilt calibration (against real SD-1 NAM, Drive at max): a first-order
    # low-shelf pivoting ~800 Hz, ±10 dB across the tone sweep. Matches the NAM
    # tone-shaping to ~1.5 dB mean (0.4 dB at low tone); residual at noon/high
    # tone is the real tone network's localized (non-shelf) response.
    TONE_FC = 800.0
    TONE_GDB = 40.0

    def __init__(self, p: OD1Params, fs, drive=0.7, tone=0.5, level=1.0,
                 buffers=True):
        self.p = p
        # BJT buffers are near-unity (sub-10 Hz coupling); skippable for fast
        # audio-band frequency fitting.
        self.buffers = buffers
        self.buf_in = EmitterFollowerRT(p, fs, Rs=p.R101, Cc=p.C101,
                                        Rb=p.R102, Re=p.R103)
        self.drive = DriveStageRT(p, fs, drive)
        g = 10.0 ** ((0.5 - tone) * self.TONE_GDB / 20.0)     # low-shelf gain
        self.tone = LowShelfRT(self.TONE_FC, g, fs)
        self.hp_lvl = OnePoleHPExact(p.R110 + p.VR103, p.C108, fs)
        self.k_level = level * p.VR103 / (p.R110 + p.VR103)
        self.buf_out = EmitterFollowerRT(p, fs, Rs=1.0, Cc=p.C109,
                                         Rb=p.R112, Re=p.R113)
        self.hp_out = OnePoleHPExact(p.R115, p.C110, fs)

    def process(self, x):
        y = self.buf_in.process(x) if self.buffers else x
        y = self.drive.process(y)
        y = self.tone.process(y)
        y = self.hp_lvl.process(y) * self.k_level
        if self.buffers:
            y = self.buf_out.process(y)
        y = self.hp_out.process(y)
        return y
