import numpy as np
from princeton import PrincetonReverb

FS = 48000 * 4
n = int(0.25 * FS)
t = np.arange(n) / FS
x = 0.02 * np.sin(2 * np.pi * 220 * t) * np.minimum(t / 0.02, 1.0)  # small sig


def comp(y, f):
    seg = y[n // 2:]
    tt = np.arange(len(seg)) / FS
    w = np.hanning(len(seg))
    return abs(np.sum((seg - seg.mean()) * w * np.exp(-2j * np.pi * f * tt))
               * 2 / np.sum(w))


for label, rn in (("on", 2700.0), ("off", 1e12)):
    amp = PrincetonReverb(FS, volume=0.5, reverb=0.0)
    amp.R_nfb = rn
    y = amp.process(x)
    seg = y[n // 2:]
    sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    fpk = np.fft.rfftfreq(len(seg), 1 / FS)[np.argmax(sp)]
    print(f"NFB {label:>3}: peak {np.max(np.abs(seg)):7.2f}V | "
          f"H(220Hz) {comp(y,220):7.3f}V | spectral max @ {fpk:8.0f} Hz")
