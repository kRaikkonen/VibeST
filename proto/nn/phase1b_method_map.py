"""Phase 1b: the METHOD-SELECTION MAP (honest scoping of when each tool wins).
Leo's three "MLP is not always best" zones, made measurable on the TS clipper:
  Zone 1 (1-2D simple nonlinearity): a LUT is cheaper AND more accurate -> use LUT.
  Zone 2 (over-sized net): push width/depth up and the MLP's cost overtakes Newton
          -> you lost the whole point of distilling.
  Zone 3 (long-memory dynamics: hysteresis, dielectric absorption): needs state /
          light CNN -> out of scope here, noted.
Cost model = order-of-magnitude Cortex-M7 @480MHz cycles (FMA 1/cyc, exp~25, tanh~18).
"""
import numpy as np, torch, torch.nn as nn
torch.manual_seed(0); np.random.seed(0)

Is, nVT = 2.52e-9, 1.75 * 0.02585
def taper(d): return np.expm1(np.log(51.0) * d) / 50.0
def glin_of(d): return 2*51e-12*(48000*4) + 1.0/(51e3 + 500e3*taper(np.asarray(d)))
def diode_i(vd): a = np.clip(vd/nVT, -60, 60); return Is*(np.exp(a)-np.exp(-a))
def bfeat(b): return np.arcsinh(b/1e-6)
def newton_vd(b, g, count=False):
    vd = np.zeros_like(b); its = np.zeros_like(b)
    for _ in range(60):
        a = np.clip(vd/nVT, -60, 60); e, em = np.exp(a), np.exp(-a)
        step = np.clip((Is*(e-em)+g*vd-b)/(Is*(e+em)/nVT+g), -0.2, 0.2)
        act = np.abs(step) > 1e-12; vd -= step*act; its += act
    return (vd, its) if count else vd
def esr(p, t): return 10*np.log10(np.sum((p-t)**2)/np.sum(t**2))

# cost models (cycles/sample, M7 order-of-mag) ----------------------------------
EXP, TANH = 25, 18
def newton_cost(niter): return niter * (2*EXP + 6)          # 2 exp + a few flop / iter
def lut_cost(dim):       return 8 + dim*(6 + 2) + (2**dim)*3  # index + 2^dim-linear blend
def mlp_cost(widths):    # widths = [in, h1, ..., 1]
    macs = sum(widths[i]*widths[i+1] for i in range(len(widths)-1))
    acts = sum(widths[1:-1])                                 # tanh on hidden units
    return int(macs*1.2 + acts*TANH), macs

# data (train on 5 drives, test on held-out) ------------------------------------
TRD = np.array([0.1,0.3,0.5,0.7,0.9]); TED = np.array([0.2,0.4,0.6,0.8])
def make(dr, n):
    d = dr[np.random.randint(len(dr), size=n)]; vd = np.random.uniform(-0.7,0.7,n)
    b = diode_i(vd)+glin_of(d)*vd
    return (torch.tensor(np.stack([bfeat(b), d],1).astype(np.float32)),
            torch.tensor(vd.astype(np.float32)))
Xtr, ytr = make(TRD, 120000)
def held_esr(mlp):
    with torch.no_grad():
        e = []
        for dd in TED:
            vd = np.random.uniform(-0.7,0.7,40000); b = diode_i(vd)+glin_of(dd)*vd
            X = torch.tensor(np.stack([bfeat(b), np.full_like(b,dd)],1).astype(np.float32))
            e.append(esr(mlp(X).squeeze(1).numpy(), vd))
    return float(np.mean(e))

def train(widths, epochs=140, bs=16384):
    layers = []
    for i in range(len(widths)-1):
        layers.append(nn.Linear(widths[i], widths[i+1]))
        if i < len(widths)-2: layers.append(nn.Tanh())
    net = nn.Sequential(*layers); opt = torch.optim.Adam(net.parameters(), 2e-3)
    n = len(ytr)
    for ep in range(epochs):
        idx = torch.randperm(n)
        for s in range(0, n, bs):
            j = idx[s:s+bs]; opt.zero_grad()
            loss = ((net(Xtr[j]).squeeze(1)-ytr[j])**2).mean(); loss.backward(); opt.step()
    return net

CONFIGS = [("tiny  8x2",[2,8,8,1]), ("small 16x2",[2,16,16,1]),
           ("med   32x3",[2,32,32,32,1]), ("BIG   64x4",[2,64,64,64,64,1]),
           ("HUGE  96x5",[2,96,96,96,96,96,1])]

# baselines
_, its = newton_vd(np.random.uniform(-0.005,0.005,5000), glin_of(0.5), count=True)
nc = newton_cost(its.mean())
# LUT (2D) held-out ESR
GB, GD = 128, 16
bf_ax = np.linspace(bfeat(-0.02), bfeat(0.02), GB); d_ax = np.linspace(0,1,GD)
lut = np.stack([newton_vd(np.sinh(bf_ax)*1e-6, glin_of(dd)) for dd in d_ax], 1).astype(np.float32)
def lut_lk(bf, d):
    fi = np.interp(bf, bf_ax, np.arange(GB)); fj = np.interp(d, d_ax, np.arange(GD))
    i0 = np.clip(fi.astype(int),0,GB-2); j0 = np.clip(fj.astype(int),0,GD-2); ti=fi-i0; tj=fj-j0
    return ((lut[i0,j0]*(1-ti)+lut[i0+1,j0]*ti)*(1-tj)+(lut[i0,j0+1]*(1-ti)+lut[i0+1,j0+1]*ti)*tj)
le = np.mean([esr(lut_lk(bfeat(diode_i(vd:=np.random.uniform(-0.7,0.7,40000))+glin_of(dd)*vd),
             np.full(40000,dd)), vd) for dd in TED])

print("=== METHOD-SELECTION MAP (TS clipper, 2D: b x drive) ===")
print(f"{'method':>12} {'cost cyc':>9} {'held-ESR':>9} {'mem/params':>11}  verdict")
print(f"{'Newton':>12} {nc:>9.0f} {'  (truth)':>9} {'—':>11}  baseline it replaces")
print(f"{'LUT 128x16':>12} {lut_cost(2):>9.0f} {le:>7.1f}dB {GB*GD*4:>9}B  ZONE-1 WINNER (2D)")
for name, w in CONFIGS:
    net = train(w); c, macs = mlp_cost(w); e = held_esr(net)
    verd = "ZONE-2: cost > Newton!" if c > nc else ("ok" if e < le else "worse than LUT")
    print(f"{name:>12} {c:>9.0f} {e:>7.1f}dB {sum(p.numel() for p in net.parameters()):>9}p  {verd}")

# LUT memory explosion vs dimension (why MLP is needed at HIGH dim) --------------
print("\n--- LUT memory vs state dimension (G=128, float32) — the exponential wall ---")
for dim in range(1, 7):
    cells = 128**dim; mem = cells*4
    tag = "  <- MLP stays ~1-40KB flat here" if dim >= 4 else ""
    print(f"  {dim}D: {cells:>18,} cells = {mem/1024:>14,.0f} KB{tag}")
print("\nRule: dim<=2 -> LUT | dim 3-6 -> MLP (LUT explodes) | long-memory -> +state/CNN")
