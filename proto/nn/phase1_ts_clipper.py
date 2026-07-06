"""Phase 1 go/no-go: distill the TS-808 clipper's Newton root-find into a tiny MLP.
The clipper solve is  i(vd) + glin(drive)*vd = b  ->  vd,  where i(vd)=2*Is*sinh(vd/nVT)
(symmetric 1N914) and glin depends on the Drive knob. So the map is g(b, drive)->vd.
Teacher = Newton (the physics). We test KNOB-GENERALIZATION: train on some drive
values, evaluate on HELD-OUT drives. Compare MLP vs a 2D LUT vs Newton (ESR/speed).
"""
import numpy as np, torch, torch.nn as nn
torch.manual_seed(0); np.random.seed(0)

Is, nVT = 2.52e-9, 1.75 * 0.02585            # 1N914
def taper(d): return np.expm1(np.log(51.0) * d) / 50.0
def glin_of(drive, os=4):                     # feedback cap cC + 1/Rf(drive)
    cC = 2 * 51e-12 * (48000 * os)
    Rf = 51e3 + 500e3 * taper(np.asarray(drive))
    return cC + 1.0 / Rf
def diode_i(vd):
    a = np.clip(vd / nVT, -60, 60); return Is * (np.exp(a) - np.exp(-a))
def newton_vd(b, glin, count_iters=False):
    vd = np.zeros_like(b); its = np.zeros_like(b)
    for k in range(60):
        a = np.clip(vd / nVT, -60, 60); e, em = np.exp(a), np.exp(-a)
        F = Is * (e - em) + glin * vd - b
        J = Is * (e + em) / nVT + glin
        step = np.clip(F / J, -0.2, 0.2)
        active = np.abs(step) > 1e-12
        vd = vd - step * active; its += active
    return (vd, its) if count_iters else vd

def bfeat(b): return np.arcsinh(b / 1e-6)      # compress the exp range of b

# ---- data: sample vd uniformly (good coverage), compute b (forward map) --------
TRAIN_D = np.array([0.1, 0.3, 0.5, 0.7, 0.9])   # knob values seen in training
TEST_D  = np.array([0.2, 0.4, 0.6, 0.8])        # HELD-OUT knob values
def make(drives, n):
    d = drives[np.random.randint(len(drives), size=n)]
    vd = np.random.uniform(-0.7, 0.7, n)
    b = diode_i(vd) + glin_of(d) * vd
    X = np.stack([bfeat(b), d], 1).astype(np.float32)
    return torch.tensor(X), torch.tensor(vd.astype(np.float32))
Xtr, ytr = make(TRAIN_D, 300000)

# ---- tiny MLP: (bfeat, drive) -> vd -------------------------------------------
mlp = nn.Sequential(nn.Linear(2, 16), nn.Tanh(), nn.Linear(16, 16), nn.Tanh(), nn.Linear(16, 1))
opt = torch.optim.Adam(mlp.parameters(), 1e-3)
for ep in range(400):
    opt.zero_grad(); p = mlp(Xtr).squeeze(1)
    loss = ((p - ytr) ** 2).mean(); loss.backward(); opt.step()
n_params = sum(p.numel() for p in mlp.parameters())
n_macs = 2 * 16 + 16 * 16 + 16 * 1              # forward MACs

# ---- 2D LUT baseline: grid over (bfeat, drive), bilinear -----------------------
GB, GD = 128, 16
bf_ax = np.linspace(bfeat(-0.02), bfeat(0.02), GB)
d_ax = np.linspace(0, 1, GD)
lut = np.zeros((GB, GD), np.float32)
for j, dd in enumerate(d_ax):
    b = np.sinh(bf_ax) * 1e-6
    lut[:, j] = newton_vd(b, glin_of(dd))
def lut_lookup(bf, d):
    fi = np.interp(bf, bf_ax, np.arange(GB)); fj = np.interp(d, d_ax, np.arange(GD))
    i0 = np.clip(fi.astype(int), 0, GB - 2); j0 = np.clip(fj.astype(int), 0, GD - 2)
    ti = fi - i0; tj = fj - j0
    return ((lut[i0, j0] * (1 - ti) + lut[i0 + 1, j0] * ti) * (1 - tj)
            + (lut[i0, j0 + 1] * (1 - ti) + lut[i0 + 1, j0 + 1] * ti) * tj)

def esr(pred, true): return 10 * np.log10(np.sum((pred - true) ** 2) / np.sum(true ** 2))

# ---- evaluate on HELD-OUT drives ----------------------------------------------
print("=== Phase 1: TS clipper solver distillation ===")
print(f"MLP: {n_params} params, {n_macs} MAC/sample | LUT: {GB*GD} cells ({GB*GD*4} B)")
_, its = newton_vd(np.random.uniform(-0.005, 0.005, 5000), glin_of(0.5), count_iters=True)
print(f"Newton it replaces: ~{its.mean():.1f} iters/sample (each = 2 exp)")
print(f"{'drive':>6} {'MLP ESR':>9} {'LUT ESR':>9}  ({'held-out' if True else ''})")
with torch.no_grad():
    for dd in TEST_D:
        vd = np.random.uniform(-0.7, 0.7, 40000)
        b = diode_i(vd) + glin_of(dd) * vd
        Xf = torch.tensor(np.stack([bfeat(b), np.full_like(b, dd)], 1).astype(np.float32))
        mp = mlp(Xf).squeeze(1).numpy(); lp = lut_lookup(bfeat(b), np.full_like(b, dd))
        print(f"{dd:>6.2f} {esr(mp, vd):>7.1f}dB {esr(lp, vd):>7.1f}dB")

# ---- figure: transfer curve at a held-out drive -------------------------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
dd = 0.6; vd = np.linspace(-0.65, 0.65, 400); b = diode_i(vd) + glin_of(dd) * vd
with torch.no_grad():
    mp = mlp(torch.tensor(np.stack([bfeat(b), np.full_like(b, dd)], 1).astype(np.float32))).squeeze(1).numpy()
plt.figure(figsize=(10, 4))
plt.subplot(1, 2, 1); plt.plot(bfeat(b), vd, 'k', lw=2, label='Newton (truth)')
plt.plot(bfeat(b), mp, 'r--', lw=1.5, label='MLP (distilled)')
plt.title(f'clipper solve, HELD-OUT drive={dd}'); plt.xlabel('asinh(b)'); plt.ylabel('vd'); plt.legend()
plt.subplot(1, 2, 2); plt.plot(bfeat(b), (mp - vd) * 1000, 'r'); plt.title('MLP error (mV)'); plt.xlabel('asinh(b)')
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\phase1_ts_clipper.png", dpi=100)
print("wrote phase1_ts_clipper.png")
