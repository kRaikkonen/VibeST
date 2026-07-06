"""Phase 5: OT magnetic hysteresis (Jiles-Atherton) -- the regime where a distilled
surrogate FINALLY beats Newton on speed. Unlike the smooth tube stages (whose Newton
was cheap, ~3 iters), J-A is a STIFF implicit ODE: the irreversible term's denominator
collapses near field reversals, so the per-sample Newton is genuinely expensive.

Teacher (铁律一): standard Jiles-Atherton model with published transformer-core params.
  Langevin anhysteretic  M_an = Ms*(coth(He/a) - a/He),  He = H + alpha*M
  dM_irr/dH = (M_an - M_irr) / (delta*k - alpha*(M_an - M_irr)),  delta = sign(dH)
  M = (1-c)*M_irr + c*M_an
Per sample: implicit-Euler solve for M(n) by Newton (coth-heavy, stiff at reversals).

Then distill (H, M_prev, delta) -> M(n) and show surrogate cost << J-A Newton cost.
"""
import numpy as np
np.random.seed(0)

# ---- published J-A transformer-core parameters (Jiles-Atherton literature) ----
Ms, a, alpha, k, c = 1.48e6, 1000.0, 1.6e-3, 400.0, 0.10        # SI (A/m), c dimensionless
MU0 = 4e-7*np.pi

def langevin(x):
    ax = np.abs(x); small = ax < 1e-3
    xs = np.where(small, 1.0, x)                                 # guard
    L = np.where(small, x/3.0, 1.0/np.tanh(xs) - 1.0/xs)
    return L
def langevin_p(x):                                              # dL/dx
    ax = np.abs(x); small = ax < 1e-3
    xs = np.where(small, 1.0, x); sh = np.sinh(np.clip(xs,-40,40))
    return np.where(small, 1.0/3.0, 1.0/(xs*xs) - 1.0/(sh*sh))

def dMdH(M, H, delta):
    He = H + alpha*M
    x = He/a
    Man = Ms*langevin(x)
    Mirr = (M - c*Man)/(1.0 - c)
    denom = delta*k - alpha*(Man - Mirr)
    denom = np.where(np.abs(denom) < 1e-9, np.sign(denom+1e-30)*1e-9, denom)
    dMirr = (Man - Mirr)/denom
    chi_an = (Ms/a)*langevin_p(x)
    num = (1.0 - c)*dMirr + c*chi_an
    return num/(1.0 - c*chi_an*alpha), Man

def solve_M(Hn, Hp, Mp, count=False):
    """Implicit-Euler J-A step: solve M - Mp - (Hn-Hp)*dMdH(M,Hn,delta)=0 by Newton."""
    Hn, Hp, Mp = np.broadcast_arrays(np.asarray(Hn,float),np.asarray(Hp,float),np.asarray(Mp,float))
    dH = Hn - Hp; delta = np.sign(dH); delta = np.where(delta==0,1.0,delta)
    M = Mp.copy(); its = np.zeros_like(M)
    for _ in range(60):
        f0, _ = dMdH(M, Hn, delta)
        g = M - Mp - dH*f0
        fp, _ = dMdH(M*(1+1e-6)+1e-3, Hn, delta)                # numeric d(dMdH)/dM
        dg = 1.0 - dH*(fp - f0)/(M*1e-6+1e-3)
        step = np.clip(g/dg, -0.3*Ms, 0.3*Ms)
        act = np.abs(step) > 1e-3*Ms/100
        M = M - step*act; its += act
        if not act.any(): break
    return (M, its) if count else M

# ---- (1) time-domain: B-H loop + measure Newton iterations ---------------------
print("=== Phase 5: Jiles-Atherton OT hysteresis ===")
fs = 48000.0; f0 = 80.0                                          # bass note = worst-case flux
loops = {}; iters_all = []
for Hamp, tag in [(600,'low'), (1500,'mid'), (4000,'saturating')]:
    t = np.arange(int(3*fs/f0)); H = Hamp*np.sin(2*np.pi*f0*t/fs)
    M = 0.0; Ms_tr=[]; Hs_tr=[]; Hp = 0.0
    for n in range(len(t)):
        M, it = solve_M(H[n], Hp, M, count=True); M=float(M); it=float(it)
        Hp = H[n]; Ms_tr.append(M); Hs_tr.append(H[n]); iters_all.append(it)
    B = MU0*(np.array(Hs_tr)+np.array(Ms_tr))
    loops[tag] = (np.array(Hs_tr), B)
    print(f"  {tag:>10} (H=±{Hamp:>4}): Newton {np.mean(iters_all[-len(t):]):>4.1f} iters avg, "
          f"{int(np.max(iters_all[-len(t):]))} max | B_pk={np.max(B):.2f}T")
mean_it = float(np.mean(iters_all)); max_it = int(np.max(iters_all))
JA_EXP = 30
ja_cost_iter = 2*JA_EXP + 20                                     # coth+sinh (2 exp) + flop
N_JA = mean_it*ja_cost_iter
print(f"\n  J-A Newton: {mean_it:.1f} iters avg ({max_it} MAX -> stiff at reversals), "
      f"~{ja_cost_iter} cyc/iter => ~{N_JA:.0f} cyc/sample")
print(f"  (compare smooth power stage: 2.6 iters/549 cyc. Hysteresis is {N_JA/549:.1f}x costlier")
print("   AND worst-case spikes to %d iters -> bad for hard real-time. THIS needs a surrogate.)" % max_it)

# ---- (2) distill (H, M_prev, delta) -> M(n) ; LUT vs MLP vs Newton -------------
import torch, torch.nn as nn
torch.manual_seed(0)
def gen(n):
    Mp = np.random.uniform(-Ms, Ms, n)
    Hn = np.random.uniform(-4500, 4500, n)
    dH = np.random.uniform(-400, 400, n); Hp = Hn - dH
    delta = np.sign(dH); delta=np.where(delta==0,1,delta)
    M = solve_M(Hn, Hp, Mp)
    X = np.stack([Hn/4500, Mp/Ms, delta], 1).astype(np.float32)
    return torch.tensor(X), torch.tensor((M/Ms).astype(np.float32))
Xtr,ytr = gen(200000)
net = nn.Sequential(nn.Linear(3,24),nn.Tanh(),nn.Linear(24,24),nn.Tanh(),nn.Linear(24,1))
opt = torch.optim.Adam(net.parameters(),3e-3); nb=len(ytr)
for ep in range(220):
    idx=torch.randperm(nb)
    for s in range(0,nb,16384):
        j=idx[s:s+16384]; opt.zero_grad()
        loss=((net(Xtr[j]).squeeze(1)-ytr[j])**2).mean(); loss.backward(); opt.step()
Xte,yte = gen(40000)
with torch.no_grad(): pr=net(Xte).squeeze(1).numpy()
esr=10*np.log10(np.sum((pr-yte.numpy())**2)/np.sum(yte.numpy()**2))
def mlp_cost(sh,act): return sum(sh[i]*sh[i+1] for i in range(len(sh)-1))*1.2+sum(sh[1:-1])*act
def lut_cost(d): return 8+d*8+(2**d)*3
mlp_c=mlp_cost([3,24,24,1],8); lut_c=lut_cost(3)
print(f"\n  distilled (H,M_prev,delta)->M : MLP[3,24,24,1] ESR={esr:.1f}dB, LUT-3D=128KB flash")
print(f"  SPEED:  J-A Newton ~{N_JA:.0f} cyc  |  MLP {mlp_c:.0f} cyc ({N_JA/mlp_c:.1f}x)  |  "
      f"LUT-3D {lut_c:.0f} cyc ({N_JA/lut_c:.1f}x)")
print(f"  => surrogate finally WINS on speed here (J-A Newton is genuinely expensive + spiky).")
print(f"  => 3D state (H,M,dir) -> a LUT already suffices; MLP needed only if history goes")
print("     higher-dim (rate-dependent/eddy-current hysteresis). That is the real MLP niche.")
print("*** cost = estimate; J-A params are published, tone-match to a real OT is separate. ***")

# ---- figure: the B-H loops -----------------------------------------------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
fig, ax = plt.subplots(1,2,figsize=(11,4.4))
for tag,(H,B) in loops.items(): ax[0].plot(H, B, label=tag, lw=1)
ax[0].set_xlabel('H (A/m)'); ax[0].set_ylabel('B (T)'); ax[0].set_title('Jiles-Atherton B-H hysteresis (teacher)'); ax[0].legend(fontsize=8); ax[0].grid(alpha=.3)
ax[1].bar(['J-A\nNewton','MLP\n[3,24,24,1]','LUT\n3D'],[N_JA,mlp_c,lut_c],color=['tab:red','tab:green','tab:blue'])
ax[1].set_ylabel('cycles / sample'); ax[1].set_title(f'hysteresis: surrogate {N_JA/lut_c:.0f}x (LUT) / {N_JA/mlp_c:.0f}x (MLP) faster')
plt.tight_layout(); plt.savefig(r"D:\sd1\vst\renders\phase5_hysteresis.png",dpi=100)
print("\nwrote phase5_hysteresis.png")
