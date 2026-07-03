import numpy as np
from scipy.io import wavfile

for name in ("demo_princeton_clean_v2.wav", "demo_princeton_cranked_v2.wav",
              "demo_od1_into_princeton_v2.wav"):
    fs, y = wavfile.read(name)
    y = y.astype(float) / 32767.0
    n = len(y)
    ok = np.all(np.isfinite(y))
    # envelope: attack vs 1.5s tail (reverb should keep tail alive)
    att = np.sqrt(np.mean(y[int(0.05*fs):int(0.25*fs)] ** 2))
    tail = np.sqrt(np.mean(y[int(1.7*fs):] ** 2))
    # spectral: harmonic content above 1 kHz vs below (drive indicator)
    sp = np.abs(np.fft.rfft(y * np.hanning(n))) ** 2
    f = np.fft.rfftfreq(n, 1/fs)
    hf = np.sqrt(sp[(f > 1000) & (f < 5000)].sum() / sp[f < 1000].sum())
    # tremolo: 2-4 Hz envelope modulation depth
    env = np.abs(y)
    k = int(fs * 0.01)
    env = np.convolve(env, np.ones(k)/k, mode="same")[int(0.3*fs):int(1.8*fs)]
    es = np.abs(np.fft.rfft((env - env.mean()) * np.hanning(len(env))))
    ef = np.fft.rfftfreq(len(env), 1/fs)
    band = (ef > 2.0) & (ef < 8.0)
    trem = es[band].max() / (env.mean() * len(env) / 4)
    print(f"{name}:")
    print(f"  finite={ok}, attack RMS {att:.3f}, tail@1.7s RMS {tail:.4f} "
          f"(tail/attack {tail/att*100:.1f}%)")
    print(f"  HF(1-5k)/LF(<1k) energy ratio {hf:.3f}, "
          f"tremolo-band env mod {trem:.3f}")
