import sys
import numpy as np

sys.path.insert(0, r"D:\sd1\proto")
from od1sim import OD1Params, OD1Pedal, RC3403A   # noqa: E402

FS = 48000 * 8
x = np.fromfile("od1_input.f64")
pedal = OD1Pedal(OD1Params(), FS, drive=0.6, level=0.8, opamp=RC3403A)

taps = {}
y = pedal.buf_in.process(x);            taps["s1_bufin"] = y.copy()
y = pedal.drive.process(y);             taps["s2_drive"] = y.copy()
y = pedal.filt.process(y);              taps["s3_filt"] = y.copy()
y = pedal.hp_lvl.process(y) * pedal.k_level; taps["s4_level"] = y.copy()
y = pedal.buf_out.process(y);           taps["s5_bufout"] = y.copy()
y = pedal.hp_out.process(y);            taps["s6_out"] = y.copy()

for k, v in taps.items():
    v.tofile(f"py_{k}.f64")
print("dumped", list(taps))
