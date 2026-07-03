"""Does the NFB loop oscillate at 96 kHz (the standalone's amp rate)?"""
import numpy as np
from princeton import PrincetonReverb

for FS in (96000, 192000):
    n = int(0.6 * FS)
    t = np.arange(n) / FS
    # hard transient then decay — mimics an aggressive strum
    x = 0.25 * np.sin(2 * np.pi * 220 * t) * np.exp(-4.0 * t)
    x *= np.minimum(t / 0.002, 1.0)
    amp = PrincetonReverb(FS, volume=0.5, reverb=0.25)
    y = amp.process(x)
    tail = y[int(0.45 * FS):]
    sp = np.abs(np.fft.rfft(tail * np.hanning(len(tail))))
    f = np.fft.rfftfreq(len(tail), 1 / FS)
    hf = f > 8000
    pk_i = np.argmax(sp[hf])
    pk_f = f[hf][pk_i]
    pk_v = sp[hf][pk_i] / (np.max(sp[f < 2000]) + 1e-12)
    print(f"fs={FS/1000:.0f}k: tail RMS {np.sqrt(np.mean(tail**2)):.3f} V, "
          f"strongest HF component {pk_f/1000:.1f} kHz "
          f"({20*np.log10(pk_v+1e-12):+.0f} dB re LF content)")
