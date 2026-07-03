import numpy as np
from princeton import PrincetonReverb
from render_chain_demo import guitar

FS = 48000 * 4
x = guitar(FS)
n = len(x)
amp = PrincetonReverb(FS, volume=0.35, treble=0.55, bass=0.5, reverb=0.25)

# tap intermediate signals by replicating process() passes
v1a_out = np.empty(n)
for i in range(n):
    v1a_out[i] = amp.v1a.step(x[i])
vtone = amp.stack.process(v1a_out)
v1b_out = np.empty(n)
for i in range(n):
    v1b_out[i] = amp.v1b.step(vtone[i])
send = amp._rev_hp(v1b_out)
drv = np.empty(n)
for i in range(n):
    drv[i] = amp.v2.step(send[i]) * amp.tank_send_k
from scipy.signal import fftconvolve
wet_raw = fftconvolve(drv, amp.tank_ir)[:n] * amp.tank_ret_k
wet = np.empty(n)
for i in range(n):
    wet[i] = amp.v3a.step(wet_raw[i])
wet *= amp.k_reverb

y = np.empty(n)
vspk = 0.0
vspk2 = 0.0
for i in range(n):
    vg = amp.mixer.step(v1b_out[i], wet[i])
    Rt, Rn = amp.R_tail, amp.R_nfb
    v_fb = 0.5 * (vspk + vspk2)
    vJ = (Rt * amp.v3b.ik_prev + Rt * v_fb / Rn) / (1.0 + Rt / Rn)
    amp.v3b.nfb = vJ
    v3b_out = amp.v3b.step(vg)
    out_t, out_b = amp.pi.step(v3b_out)
    vA, vB, vC, vD = amp.psu.step(amp.power.i_plates, amp.power.i_screens)
    vspk2 = vspk
    vspk = amp.power.step(out_t, out_b, vA, vB)
    y[i] = vspk


def env(sig, t0, t1):
    return np.sqrt(np.mean(sig[int(t0 * FS):int(t1 * FS)] ** 2))


print(f"{'tap':>10} {'RMS@0.2s':>10} {'RMS@1.8s':>10} {'ratio%':>7}")
for name, sig in (("input", x), ("v1a", v1a_out), ("tone", vtone),
                  ("v1b", v1b_out), ("wet", wet), ("v3b", None),
                  ("spk", y)):
    if sig is None:
        continue
    a, b = env(sig, 0.15, 0.35), env(sig, 1.7, 1.95)
    print(f"{name:>10} {a:>10.4f} {b:>10.4f} {b/(a+1e-12)*100:>6.1f}%")
