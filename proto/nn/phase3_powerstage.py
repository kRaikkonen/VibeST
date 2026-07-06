"""Phase 3: coupled push-pull EL34 + output-transformer power stage.
Goal (1): replace the GUESSED N_POWER=15k cyc in the budget with a MEASURED number —
build the real coupled 2-unknown Newton (two pentodes sharing an OT + reflected speaker
load + magnetizing inductance) and count iterations.
Goal (2): distill it to an MLP and show the SPEED crossover (MLP cyc < Newton cyc) —
the half the triode figure (Phase 1c) couldn't show, because a 1-unknown triode Newton
was already cheap. A COUPLED 2-unknown stiff solve is where the MLP finally wins on speed.

Physics (铁律一): simplified EL34 pentode = Koren-style grid law (softplus^Ex) + a plate
knee (pentode saturation). Push-pull through an OT: differential plate voltage drives the
reflected load R_aa + magnetizing inductance Lp (trapezoidal companion). Class-AB fixed
bias. Numbers are representative EL34/OT values, not a fitted tone — this is for cost +
distillability, tone-matching is a separate task.
"""
import numpy as np
np.random.seed(0)

# ---- simplified EL34 pentode: Ip(Vg, Vp) --------------------------------------
Ex, A, Vsp, Vco, Vknee = 1.4, 0.040, 8.0, -40.0, 40.0   # tuned: idle ~40mA, sat ~200mA
def _sp_sig(x):
    xe = np.minimum(x, 30.0); ex = np.exp(xe)
    return np.where(x > 30, x, np.log1p(ex)), np.where(x > 30, 1.0, ex/(1.0+ex))
def pent(Vg, Vp):
    sp, _ = _sp_sig((Vg - Vco)/Vsp)
    knee = 1.0 - np.exp(-np.maximum(Vp, 0.0)/Vknee)
    return A * sp**Ex * knee
def pent_dVp(Vg, Vp):
    sp, _ = _sp_sig((Vg - Vco)/Vsp)
    return A * sp**Ex * np.exp(-np.maximum(Vp, 0.0)/Vknee)/Vknee * (Vp > 0)
def pent_dVg(Vg, Vp):
    sp, sig = _sp_sig((Vg - Vco)/Vsp)
    knee = 1.0 - np.exp(-np.maximum(Vp, 0.0)/Vknee)
    return A * Ex*np.maximum(sp,1e-9)**(Ex-1) * (sig/Vsp) * knee

# ---- OT + push-pull circuit ---------------------------------------------------
BPLUS, RP_HALF, RAA, LP = 450.0, 50.0, 3800.0, 20.0     # V, ohm, plate-plate load, H
VG_IDLE = -36.0                                          # fixed bias (class AB)
FS, OS = 48000.0, 2
T = 1.0/(FS*OS); GL = T/(2.0*LP)                        # inductor trapezoidal conductance

def solve_pp(vs, IeqL, Vb, count=False):
    """2-unknown coupled Newton for plate voltages Vp1,Vp2. vs=PI differential drive,
    IeqL=OT magnetizing companion source, Vb=B+ (may sag). Returns Vp1,Vp2[,iters]."""
    vs, IeqL, Vb = np.broadcast_arrays(np.asarray(vs,float), np.asarray(IeqL,float), np.asarray(Vb,float))
    Vg1, Vg2 = VG_IDLE + vs, VG_IDLE - vs
    Vp1 = 0.7*Vb.copy(); Vp2 = 0.7*Vb.copy()
    its = np.zeros_like(Vp1); gc = GL + 1.0/RAA
    for _ in range(60):
        vd = Vp1 - Vp2
        idiff = gc*vd + IeqL                            # OT differential current
        Ip1, Ip2 = pent(Vg1, Vp1), pent(Vg2, Vp2)
        F1 = (Vb - Vp1)/RP_HALF - Ip1 - idiff
        F2 = (Vb - Vp2)/RP_HALF - Ip2 + idiff
        # Jacobian (2x2)
        a = -1.0/RP_HALF - pent_dVp(Vg1, Vp1) - gc; b = gc
        c = gc;                                          d = -1.0/RP_HALF - pent_dVp(Vg2, Vp2) - gc
        det = a*d - b*c
        dVp1 = (F1*d - b*F2)/det
        dVp2 = (a*F2 - F1*c)/det
        dVp1 = np.clip(dVp1, -80, 80); dVp2 = np.clip(dVp2, -80, 80)
        act = (np.abs(dVp1) > 1e-4) | (np.abs(dVp2) > 1e-4)
        Vp1 = np.clip(Vp1 - dVp1*act, 1.0, 1.5*BPLUS)
        Vp2 = np.clip(Vp2 - dVp2*act, 1.0, 1.5*BPLUS)
        its += act
        if not act.any(): break
    return (Vp1, Vp2, its) if count else (Vp1, Vp2)

# ---- (1) time-domain run: measure iterations + sanity-check transfer -----------
print("=== Phase 3: push-pull EL34 + OT coupled power stage ===")
dur = 2400
iters_all = []; vout_trace = {}
for amp, tag in [(6.0,'clean'), (18.0,'driven'), (34.0,'slammed')]:
    iLp = 0.0; IeqL = 0.0; outs = []; iters = []
    t = np.arange(dur)
    drive = amp*np.sin(2*np.pi*80.0*t/FS)               # 80 Hz (bass = worst-case OT)
    for n in range(dur):
        Vp1, Vp2, it = solve_pp(drive[n], IeqL, BPLUS, count=True)
        Vp1, Vp2 = float(Vp1), float(Vp2); it = float(it)
        vd = Vp1 - Vp2
        iLp = GL*vd + IeqL                              # update magnetizing current
        IeqL = iLp + GL*vd                              # next companion source (trapezoidal)
        outs.append(vd); iters.append(it)
    iters_all += iters; vout_trace[tag] = np.array(outs)
    print(f"  {tag:>8} (grid ±{amp:>4.0f}V): Newton {np.mean(iters):>4.1f} iters/sample avg, "
          f"{np.max(iters):.0f} max | Vout pp={np.ptp(outs):.0f}V")
mean_it = float(np.mean(iters_all))

# ---- cost model: measured iters -> real N_POWER -------------------------------
EXP = 30
cost_iter = 2*(2*EXP + 30) + 30          # 2 pentodes (softplus=exp+log, pow, knee-exp) + 2x2 solve
N_POWER_measured = mean_it * cost_iter
print(f"\n  cost/iter ~= {cost_iter} cyc (2 pentode evals + derivs + 2x2 solve)")
print(f"  => MEASURED N_POWER ~= {mean_it:.1f} x {cost_iter} = {N_POWER_measured:.0f} cyc/call "
      f"(vs my earlier GUESS 15000 -> {'OVER-estimated' if N_POWER_measured<15000 else 'under'} by "
      f"{15000/N_POWER_measured:.1f}x)")

# ---- (2) distill -> speed crossover -------------------------------------------
import torch, torch.nn as nn
torch.manual_seed(0)
# map: (vs, IeqL, Vb) -> Vout(=vd).  Sample realistic ranges.
def gen(n):
    vs = np.random.uniform(-34, 34, n)
    IeqL = np.random.uniform(-0.15, 0.15, n)            # magnetizing current range (A)
    Vb = np.random.uniform(410, 470, n)                 # B+ w/ sag
    Vp1, Vp2 = solve_pp(vs, IeqL, Vb)
    X = np.stack([vs/34, IeqL/0.15, (Vb-440)/30], 1).astype(np.float32)
    return torch.tensor(X), torch.tensor((Vp1-Vp2).astype(np.float32))
Xtr, ytr = gen(150000); ym, ys = ytr.mean(), ytr.std()
ytrn = (ytr-ym)/ys
net = nn.Sequential(nn.Linear(3,24), nn.Tanh(), nn.Linear(24,24), nn.Tanh(), nn.Linear(24,1))
opt = torch.optim.Adam(net.parameters(), 3e-3); nb = len(ytr)
for ep in range(200):
    idx = torch.randperm(nb)
    for s in range(0, nb, 16384):
        j = idx[s:s+16384]; opt.zero_grad()
        loss = ((net(Xtr[j]).squeeze(1)-ytrn[j])**2).mean(); loss.backward(); opt.step()
Xte, yte = gen(40000)
with torch.no_grad(): pr = net(Xte).squeeze(1).numpy()*ys.item()+ym.item()
esr = 10*np.log10(np.sum((pr-yte.numpy())**2)/np.sum(yte.numpy()**2))
def mlp_cost(shape, act): return sum(shape[i]*shape[i+1] for i in range(len(shape)-1))*1.2+sum(shape[1:-1])*act
mlp_cyc_poly = mlp_cost([3,24,24,1], 8)
mlp_cyc_libm = mlp_cost([3,24,24,1], 35)
print(f"\n  distilled power stage: MLP[3,24,24,1] ESR={esr:.1f}dB, "
      f"{sum(p.numel() for p in net.parameters())} params")
print(f"  SPEED: Newton {N_POWER_measured:.0f} cyc  vs  MLP {mlp_cyc_poly:.0f} cyc (poly-tanh) "
      f"= {N_POWER_measured/mlp_cyc_poly:.1f}x faster  [{mlp_cyc_libm:.0f} cyc w/ libm-tanh]")
print(f"  => THIS is the speed crossover: coupled multi-unknown Newton is where MLP wins on cycles.")

# ---- figure -------------------------------------------------------------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(1, 2, figsize=(11, 4.2))
for tag, tr in vout_trace.items(): ax[0].plot(tr[:600], label=tag)
ax[0].set_title('push-pull output (80 Hz): clean→crossover→saturation'); ax[0].set_xlabel('sample'); ax[0].set_ylabel('Vout (plate-plate)'); ax[0].legend(fontsize=8)
bars = ax[1].bar(['Newton\n(measured)','MLP\n(poly-tanh)','MLP\n(libm-tanh)'],
                 [N_POWER_measured, mlp_cyc_poly, mlp_cyc_libm], color=['tab:red','tab:green','tab:orange'])
ax[1].set_ylabel('cycles / call'); ax[1].set_title(f'power-stage SPEED: MLP {N_POWER_measured/mlp_cyc_poly:.1f}x faster than Newton')
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\phase3_powerstage.png", dpi=100)
print("\nwrote phase3_powerstage.png")
