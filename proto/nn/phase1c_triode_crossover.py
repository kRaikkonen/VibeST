"""Phase 1c: the LUT->MLP CROSSOVER on a real 12AX7 gain stage.
Teacher = the actual Koren-triode plate-load Newton solve (physics, no fudge).
The solve is a map  (grid voltage, + physical state/knobs) -> plate voltage.
We add ONE real physical dimension at a time and watch the cost of representing it:
  d=1  vg only            (fixed bias/supply/load)   -> raw tube transfer
  d=2  + cathode bias Vk  (cathode-cap bias memory)
  d=3  + B+ supply        (power-supply sag)
  d=4  + plate load Ra    (a knob / component value)
For each d: fit a LUT (G^d grid) AND a small MLP, measure held-out ESR, memory, cost.
Punchline: LUT memory = G^d explodes past the 2 MB Flash wall at d>=4 (and past the
per-stage share even earlier), while the MLP stays flat ~KB. That is the ONLY regime
where the MLP is the right tool — exactly the dimension argument, now on a real tube.
"""
import numpy as np, torch, torch.nn as nn
torch.manual_seed(0); np.random.seed(0)

# ---- 12AX7 Koren triode model (standard published params) ---------------------
mu, Ex, Kg1, Kp, Kvb = 100.0, 1.4, 1060.0, 600.0, 300.0
def _sp_sig(x):                                   # stable softplus + its sigmoid
    xe = np.minimum(x, 30.0); ex = np.exp(xe)
    return np.where(x > 30, x, np.log1p(ex)), np.where(x > 30, 1.0, ex/(1.0+ex))
def koren_ip(Vpk, Vgk):
    Vpk = np.maximum(Vpk, 1e-6); s = np.sqrt(Kvb + Vpk*Vpk)
    sp, _ = _sp_sig(Kp*(1.0/mu + Vgk/s)); E1 = np.maximum((Vpk/Kp)*sp, 0.0)
    return (E1**Ex)/Kg1                            # plate current, amps
def koren_didvp(Vpk, Vgk):
    Vpk = np.maximum(Vpk, 1e-6); s = np.sqrt(Kvb + Vpk*Vpk)
    sp, sig = _sp_sig(Kp*(1.0/mu + Vgk/s)); E1 = np.maximum((Vpk/Kp)*sp, 1e-12)
    dx = Kp*Vgk*(-Vpk)*(Kvb+Vpk*Vpk)**-1.5
    dE1 = (1.0/Kp)*sp + (Vpk/Kp)*sig*dx
    return Ex*E1**(Ex-1.0)/Kg1 * dE1               # dIp/dVpk (=dIp/dVp, Vk fixed)

def solve_vp(vg, Vk, Bplus, Ra, count=False):
    """Plate-load line (Bplus-Vp)/Ra = Ip(Vp-Vk, vg-Vk). Newton for Vp. Physics teacher."""
    vg, Vk, Bplus, Ra = np.broadcast_arrays(vg, Vk, Bplus, Ra)
    Vgk = np.minimum(vg - Vk, 0.3)                 # clamp at grid-conduction knee
    Vp = 0.5*Bplus.copy(); its = np.zeros_like(Vp)
    for _ in range(40):
        Vpk = Vp - Vk
        f = (Bplus - Vp)/Ra - koren_ip(Vpk, Vgk)
        fp = -1.0/Ra - koren_didvp(Vpk, Vgk)
        step = np.clip(f/fp, -60.0, 60.0)
        act = np.abs(step) > 1e-5
        Vp = np.clip(Vp - step*act, 1.0, Bplus); its += act
    return (Vp, its) if count else Vp

# ---- dimension ladder: each new axis is a real physical effect -----------------
ORDER  = ['vg', 'Vk', 'Bplus', 'Ra']
LABEL  = {'vg':'vg', 'Vk':'+cathode-cap bias', 'Bplus':'+B+ sag', 'Ra':'+plate-load knob'}
NOM    = {'vg': 0.0,          'Vk': 1.5,        'Bplus': 310.0,     'Ra': 100e3}
RANGE  = {'vg': (-3.2, 1.6),  'Vk': (1.0, 2.2), 'Bplus': (270,340), 'Ra': (82e3, 220e3)}
Vp0    = float(solve_vp(np.array([NOM['vg']]), NOM['Vk'], NOM['Bplus'], NOM['Ra'])[0])

def sample_args(names, n):                          # random points in the d-box
    cols = {k: NOM[k]*np.ones(n) for k in ORDER}
    for k in names: cols[k] = np.random.uniform(*RANGE[k], n)
    return cols
def teacher(cols): return solve_vp(cols['vg'], cols['Vk'], cols['Bplus'], cols['Ra']) - Vp0
def norm(cols, names):                              # -> inputs in [-1,1] per axis
    return np.stack([2*(cols[k]-RANGE[k][0])/(RANGE[k][1]-RANGE[k][0])-1 for k in names], 1)

def lut_interp(axes, grid, pts):                    # d-linear interp, any d, no scipy
    d = len(axes); N = pts.shape[0]
    lo = np.empty((N, d), int); fr = np.empty((N, d))
    for k in range(d):
        i = np.clip(np.searchsorted(axes[k], pts[:, k])-1, 0, len(axes[k])-2)
        lo[:, k] = i; x0, x1 = axes[k][i], axes[k][i+1]
        fr[:, k] = np.clip((pts[:, k]-x0)/(x1-x0), 0, 1)
    out = np.zeros(N)
    for c in range(2**d):
        w = np.ones(N); off = [(c >> k) & 1 for k in range(d)]
        for k in range(d): w *= fr[:, k] if off[k] else (1-fr[:, k])
        out += w * grid[tuple(lo[:, k]+off[k] for k in range(d))]
    return out

def esr(p, t): return 10*np.log10(np.sum((p-t)**2)/np.sum(t**2))
FLASH = 2*1024*1024                                 # H743 2 MB Flash
G = 32                                              # grid points / axis

print("=== 12AX7 gain stage: LUT -> MLP crossover (teacher = Koren Newton) ===")
_, it = solve_vp(np.random.uniform(-3, 1, 4000), np.random.uniform(1,2.2,4000),
                 np.random.uniform(270,340,4000), np.random.uniform(82e3,220e3,4000), count=True)
print(f"Newton teacher: {it.mean():.1f} iters/sample (each = Koren eval: exp+log+sqrt+pow)\n")
print(f"{'d':>2} {'physical axis':>18} {'LUT cells':>11} {'LUT mem':>9} {'LUT ESR':>8} "
      f"{'MLP par':>8} {'MLP mem':>8} {'MLP ESR':>8}  {'fits 2MB?':>9}")
rows = []
for d in range(1, 5):
    names = ORDER[:d]
    axes = [np.linspace(*RANGE[k], G) for k in names]
    mesh = np.meshgrid(*axes, indexing='ij')
    gcols = {k: NOM[k]*np.ones(mesh[0].size) for k in ORDER}
    for j, k in enumerate(names): gcols[k] = mesh[j].ravel()
    grid = teacher(gcols).reshape([G]*d)            # teacher on the LUT grid
    # held-out test points (random -> tests knob/state generalization for d>=2)
    tc = sample_args(names, 40000); yt = teacher(tc)
    lp = lut_interp(axes, grid, np.stack([tc[k] for k in names], 1))
    # MLP (small, cost-competitive with Newton): d->24->24->1, NORMALIZED target
    tr = sample_args(names, 120000)
    Xtr = torch.tensor(norm(tr, names).astype(np.float32))
    ytr_raw = teacher(tr); ym, ys = ytr_raw.mean(), ytr_raw.std()
    ytr = torch.tensor(((ytr_raw-ym)/ys).astype(np.float32))
    net = nn.Sequential(nn.Linear(d,24), nn.Tanh(), nn.Linear(24,24), nn.Tanh(), nn.Linear(24,1))
    opt = torch.optim.Adam(net.parameters(), 3e-3); n = len(ytr)
    for ep in range(220):
        idx = torch.randperm(n)
        for s in range(0, n, 16384):
            j = idx[s:s+16384]; opt.zero_grad()
            loss = ((net(Xtr[j]).squeeze(1)-ytr[j])**2).mean(); loss.backward(); opt.step()
    with torch.no_grad():
        mp = net(torch.tensor(norm(tc, names).astype(np.float32))).squeeze(1).numpy()*ys + ym
    cells = G**d; lmem = cells*4; par = sum(p.numel() for p in net.parameters()); mmem = par*4
    fits = "LUT+MLP" if lmem <= FLASH else "MLP only"
    print(f"{d:>2} {LABEL[names[-1]]:>18} {cells:>11,} {lmem/1024:>7.0f}K {esr(lp,yt):>6.1f}dB "
          f"{par:>8} {mmem/1024:>6.1f}K {esr(mp,yt):>6.1f}dB  {fits:>9}")
    rows.append((d, cells, lmem, esr(lp,yt), par, mmem, esr(mp,yt)))

print(f"\nGrid G={G}. Flash=2MB. Note: a full chain has ~6 nonlinear stages -> per-stage")
print(f"share ~340KB, so the LUT is already infeasible at d=3 ({G**3*4//1024}KB) in practice.")
print("Analytic G^d wall (bytes): d=3 -> 32^3=131K/64^3=1M/128^3=8M ; d=4 -> 32^4=4M/64^4=67M/128^4=1G")

# ---- figure -------------------------------------------------------------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
ds = [r[0] for r in rows]
fig, ax = plt.subplots(1, 2, figsize=(11, 4.2))
for Gg, c in [(32,'tab:blue'),(64,'tab:cyan'),(128,'tab:purple')]:
    ax[0].semilogy(ds, [(Gg**d)*4 for d in ds], 'o-', color=c, label=f'LUT G={Gg}')
ax[0].semilogy(ds, [r[5] for r in rows], 's-', color='tab:red', lw=2, label='MLP 24x24')
ax[0].axhline(FLASH, color='k', ls='--'); ax[0].text(1, FLASH*1.3, '2 MB Flash')
ax[0].axhline(FLASH/6, color='gray', ls=':'); ax[0].text(1, FLASH/6*1.3, 'per-stage share (/6)')
ax[0].set_xlabel('state/knob dimensions kept'); ax[0].set_ylabel('memory (bytes)')
ax[0].set_xticks(ds); ax[0].set_title('LUT memory explodes; MLP flat'); ax[0].legend(fontsize=8)
ax[1].plot(ds, [r[3] for r in rows], 'o-', color='tab:blue', label='LUT ESR')
ax[1].plot(ds, [r[6] for r in rows], 's-', color='tab:red', label='MLP ESR')
ax[1].set_xlabel('state/knob dimensions kept'); ax[1].set_ylabel('held-out ESR (dB)')
ax[1].set_xticks(ds); ax[1].set_title('both accurate where they fit'); ax[1].legend()
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\phase1c_triode_crossover.png", dpi=100)
print("wrote phase1c_triode_crossover.png")
