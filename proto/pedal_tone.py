"""PURE WHITE-BOX pedal tone stages — real op-amp active circuits built as MNA
networks from the actual schematics (NOT fitted shelves). Shared linear-circuit
primitive: mna.LinearNetwork (now with ideal op-amps). Values traceable to the
schematics; validated against the circuit's own physics / ElectroSmash plots.
"""
import numpy as np
from mna import LinearNetwork


def ts808_tone(fs, tone):
    """Ibanez TS808/TS9 Tone/Volume stage (JRC4558) — ElectroSmash / Jack Orman
    "Son of Screamer" topology and values.
      +in : R7(1k) from clipper out, C5(0.22µF) to gnd (723Hz LP), R9(10k) bias.
      -in : R11(1k) feedback from out, + Tone pot wiper.
      Tone pot P2(20k): wiper=-in, left end -> C6(0.22µF)+R8(220) series to gnd
                        (3.3kHz leg), right end -> output.
    tone in [0,1]: 1 = treble (bright, wiper toward output), 0 = bass (dark).
    nodes: 1=src(clip out) 2=+in 3=-in 4=out 5=pot-left 6=C6/R8 junction."""
    a = 1.0 - min(max(tone, 1e-3), 0.999)     # pot-left fraction (wiper->C6/R8 leg)
    net = LinearNetwork(6, fs)
    net.Vsrc(1)
    net.R(1, 2, 1e3)          # R7
    net.Cap(2, 0, 0.22e-6)    # C5  (with R7 -> 723 Hz LP on +in)
    net.R(2, 0, 10e3)         # R9  bias (4V5 = AC ground)
    net.R(3, 4, 1e3)          # R11 feedback
    net.R(3, 5, a * 20e3)     # tone pot: wiper(-in) -> left end (C6/R8 leg)
    net.R(3, 4, (1 - a) * 20e3)   # tone pot: wiper -> right end (output) || R11
    net.Cap(5, 6, 0.22e-6)    # C6
    net.R(6, 0, 220.0)        # R8   (C6+R8 -> 3.3 kHz)
    net.Opamp(2, 3, 4)        # JRC4558 (ideal)
    return net.compile(4)


def sd1_tone(fs, tone):
    """Boss SD-1 Tone control — ACTIVE op-amp EQ. Per electric-safari and the
    Boss schematic (fig1), the SD-1 tone is "the SAME topology as the TS9 but
    with different component values", using the 4558's second half as an EQ that
    can BOOST or CUT (not the passive tilt I first modelled). REAL SD-1 values:
      +in : R7(10k)+C4(0.018µF) -> 884 Hz input LP, 100k bias.
      -in : R9(10k) feedback with C6(0.01µF) across it (1.59 kHz) + tone-pot wiper.
      Tone pot 20k: wiper=-in, left end -> C5(0.027µF)+R8(470) to gnd (12.5 kHz),
                    right end -> output. Sweeps a variable boost in ~1.6-12.5 kHz.
    tone in [0,1]: 1 = bright. nodes: 1=src 2=+in 3=-in 4=out 5=pot-left 6=C5/R8.
    ⚠️ C6's exact placement in the feedback (across R9 vs series) is one reading of
    "C6 in the feedback loop removes bass" — verify against a clean SD-1 schematic."""
    a = 1.0 - min(max(tone, 1e-3), 0.999)
    net = LinearNetwork(6, fs)
    net.Vsrc(1)
    net.R(1, 2, 10e3); net.Cap(2, 0, 0.018e-6); net.R(2, 0, 100e3)   # +in 884Hz LP + bias
    net.R(3, 4, 10e3); net.Cap(3, 4, 0.01e-6)                        # R9 || C6 feedback (1.59kHz)
    net.R(3, 5, a * 20e3); net.R(3, 4, (1 - a) * 20e3)               # tone pot 20k
    net.Cap(5, 6, 0.027e-6); net.R(6, 0, 470.0)                      # C5+R8 EQ leg (12.5kHz)
    net.Opamp(2, 3, 4)                                               # µPC4558 (ideal)
    return net.compile(4)


if __name__ == "__main__":
    fs = 48000.0
    FR = [82, 165, 330, 659, 1031, 2131, 4127, 8237]
    print("TS808 tone (pure white-box MNA) — dB rel 1kHz:")
    print("freq   " + " ".join(f"{f:>6}" for f in FR))
    for tone, lab in [(1.0, "treble"), (0.5, "mid"), (0.0, "bass")]:
        net = ts808_tone(fs, tone)
        H = np.abs(net.freq_response(FR, 4)); H = 20 * np.log10(H / H[4])
        print(f"{lab:6s} " + " ".join(f"{v:+6.1f}" for v in H))
    # backward-compat: Princeton tone stack (no op-amps) must still work
    from mna import fender_tone_stack
    ts = fender_tone_stack(fs, 0.4, 0.2, 0.4)
    H = np.abs(ts.freq_response([100, 1000, 5000], 7))
    print("fender_tone_stack still compiles+runs:", np.round(20*np.log10(H/H[1]), 1))
