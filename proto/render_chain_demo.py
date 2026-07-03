"""Demos: Princeton Reverb alone (clean / cranked+reverb) and the full
OD-1 -> Princeton Reverb chain.

Output = speaker terminal voltage + a clearly-labelled MONITOR filter
(2nd-order 75 Hz HP + 5.5 kHz LP standing in for cab/mic until a proper
cabinet IR stage exists — it is NOT part of the white-box model).
"""

import numpy as np
from scipy.signal import resample_poly, butter, sosfilt
from scipy.io import wavfile

from od1sim import OD1Params, OD1Pedal, RC3403A
from princeton import PrincetonReverb

FS_OUT = 48000
FS_AMP = 48000 * 4          # 192 kHz
FS_PEDAL = 48000 * 8        # 384 kHz
DUR = 2.0


def pluck(f0, t, decay=3.0, nharm=12, fs_base=FS_OUT):
    y = np.zeros_like(t)
    rng = np.random.default_rng(1964)
    for k in range(1, nharm + 1):
        fk = f0 * k * np.sqrt(1 + 3e-4 * k * k)
        if fk > 0.45 * fs_base:
            break
        a = 1.0 / k ** 1.2
        y += a * np.exp(-decay * k ** 0.5 * t) * np.sin(
            2 * np.pi * fk * t + rng.uniform(0, np.pi))
    return y


def guitar(fs):
    t = np.arange(int(DUR * fs)) / fs
    x = pluck(110.0, t) + pluck(164.8, t) + pluck(220.0, t)
    x *= 0.15 / np.max(np.abs(x))
    return x


def monitor(y, fs):
    sos = np.vstack([butter(2, 75.0, "hp", fs=fs, output="sos"),
                     butter(2, 5500.0, "lp", fs=fs, output="sos")])
    return sosfilt(sos, y)


def save(name, y, fs):
    y48 = resample_poly(y, FS_OUT, fs)
    y48 = 0.9 * y48 / np.max(np.abs(y48))
    wavfile.write(name, FS_OUT, (y48 * 32767).astype(np.int16))
    print(f"{name} written")


def main():
    # 1) amp clean, touch of reverb
    x = guitar(FS_AMP)
    amp = PrincetonReverb(FS_AMP, volume=0.35, treble=0.55, bass=0.5,
                          reverb=0.25)
    save("demo_princeton_clean_v2.wav", monitor(amp.process(x), FS_AMP),
         FS_AMP)

    # 2) amp cranked (power-stage breakup + sag), tremolo on
    amp = PrincetonReverb(FS_AMP, volume=0.9, treble=0.6, bass=0.45,
                          reverb=0.2, trem_intensity=0.5, trem_speed=0.45)
    save("demo_princeton_cranked_v2.wav", monitor(amp.process(x), FS_AMP),
         FS_AMP)

    # 3) full chain: guitar -> OD-1 (RC3403A) -> Princeton
    xp = guitar(FS_PEDAL)
    pedal = OD1Pedal(OD1Params(), FS_PEDAL, drive=0.6, level=0.8,
                     opamp=RC3403A)
    ped_out = pedal.process(xp)
    ped_192 = resample_poly(ped_out, 1, 2)          # 384k -> 192k
    amp = PrincetonReverb(FS_AMP, volume=0.4, treble=0.55, bass=0.5,
                          reverb=0.25)
    save("demo_od1_into_princeton_v2.wav",
         monitor(amp.process(ped_192), FS_AMP), FS_AMP)


if __name__ == "__main__":
    main()
