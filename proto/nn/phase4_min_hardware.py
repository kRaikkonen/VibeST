"""Phase 4: what is the MINIMUM hardware that runs the 8-effect chain in real time?
Chain: gate -> SD-1 -> TS9 -> Plexi -> chorus -> delay -> reverb -> EQ @ 48 kHz.

Uses the HONEST config that Phase 1-3 converged on: LUT the low-dim nonlinear solves
(clippers, preamp triodes) + keep Newton for the few high-dim stages (PI, power stage) --
they turned out CHEAP (measured, Phase 3). NO MLP: it was slower AND less accurate than
both LUT (low-dim) and Newton (high-dim). The neural net is not needed for this chain.

Two binding constraints: (1) cycles/sample = f_cpu/48k, and (2) RAM for the FX delay
lines. FPU is mandatory (no-FPU cores are dead). MCU specs web-verified (2026-07).
"""
# ---- best-config per-stage cost (Cortex-M7 cycles/call), from Phase 1-3 -------
STAGES = [  # (name, rate, cyc)  rate 'os' runs OS x per base sample
  ("gate",'base',30), ("SD1 buf/out",'os',22), ("SD1 clip (LUT)",'os',36),
  ("SD1 tone MNA",'os',95), ("TS9 buf/out",'os',22), ("TS9 clip (LUT)",'os',36),
  ("TS9 tone MNA",'os',95), ("Plexi V1a (LUT)",'os',56), ("Plexi tonestk",'os',72),
  ("Plexi V1b/V2 (LUT)",'os',112), ("Plexi cath-fol (LUT)",'os',56),
  ("Plexi PI (Newton)",'os',1156), ("Plexi power+OT (Newton)",'os',549),
  ("Plexi NFB/pres",'os',40), ("resample up+dn",'base',200), ("cab IR",'base',300),
  ("chorus",'base',80), ("delay",'base',60), ("reverb FDN",'base',500), ("EQ",'base',48),
]
def chain_m7(OS): return sum(c*(OS if r=='os' else 1) for _,r,c in STAGES)

# ---- verified MCU table (a8051166: core, FPU, MHz, RAM KB, ~price, ipc vs M7) --
INF = float('inf')
MCU = [  # name, MHz, fpu, ram_kb, price$, core, ipc(cyc mult vs M7 for our code)
 ("RP2040 (M0+)",       133,'none', 264, 1,  'M0+',  INF),   # no FPU -> dead
 ("RP2350 (M33)",       150,'sp',   520, 1,  'M33',  1.4),
 ("STM32F411",          100,'sp',   128, 3,  'M4F',  1.4),
 ("STM32G431",          170,'sp',    32, 3,  'M4F',  1.4),
 ("STM32F405",          168,'sp',   192, 6,  'M4F',  1.4),
 ("STM32F446",          180,'sp',   128, 6,  'M4F',  1.4),
 ("ESP32-S3 (LX7)",     240,'sp',   512, 3,  'Xtensa',1.8),  # non-pipelined FPU
 ("STM32F722/746 (M7)", 216,'sp',   320, 10, 'M7',   1.0),
 ("NXP RT1010 (M7)",    500,'dp',   128, 3,  'M7',   1.0),   # cheap M7, tiny RAM
 ("STM32H750/Daisy",    480,'dp',  1024, 8,  'M7',   1.0),   # ~$30 as Daisy board
 ("STM32H743 (M7)",     480,'dp',  1024, 12, 'M7',   1.0),
 ("Teensy4 (RT1062)",   600,'dp',  1024, 25, 'M7',   1.0),
]
# FX RAM footprint (KB): full = 1 s delay + big reverb; trim = 0.35 s + smaller reverb
RAM_FULL, RAM_TRIM = 358, 160
LUT_FLASH = 736                              # clipper+triode LUTs+cab+code (QSPI ok if small internal)

print("=== Min hardware for the 8-effect chain @ 48 kHz (best config: LUT + Newton, no MLP) ===")
print(f"chain cost: OS1 = {chain_m7(1)} , OS2 = {chain_m7(2)} Cortex-M7 cycles/sample\n")
print(f"{'MCU':>22} {'MHz':>4} {'FPU':>4} {'budget':>7} {'OS1 use':>8} {'OS2 use':>8} "
      f"{'RAM':>6} {'$':>3}  verdict")
best = None
for name, mhz, fpu, ram, price, core, ipc in MCU:
    budget = mhz*1e6/48000
    if ipc == INF:
        print(f"{name:>22} {mhz:>4} {fpu:>4} {budget:>7.0f} {'--':>8} {'--':>8} "
              f"{ram:>5}K {price:>3} DEAD (no hardware FPU)"); continue
    u1 = chain_m7(1)*ipc/budget*100; u2 = chain_m7(2)*ipc/budget*100
    ram_ok = "full" if ram >= RAM_FULL else ("trim" if ram >= RAM_TRIM else "ext-RAM")
    if u2 <= 85 and ram >= RAM_FULL:   v = "COMFORTABLE (OS2, full FX)"
    elif u1 <= 85 and ram >= RAM_TRIM: v = f"FITS (OS1, {ram_ok} FX)"
    elif u1 <= 100:                    v = f"TIGHT (OS1, {ram_ok} FX)"
    else:                              v = "OVER on cycles"
    if v.startswith(("FITS","COMF")) and (best is None or price < best[1]): best = (name, price, v)
    print(f"{name:>22} {mhz:>4} {fpu:>4} {budget:>7.0f} {u1:>7.0f}% {u2:>7.0f}% "
          f"{ram:>5}K {price:>3}  {v}")

print(f"\nLUT/code Flash ~= {LUT_FLASH} KB (coarser LUT grids or QSPI for <512KB-flash parts).")
print(f"RAM: FX delay lines need ~{RAM_FULL}KB full / ~{RAM_TRIM}KB trimmed -> the REAL floor")
print("     for cheap parts (they have 32-128KB), NOT the DSP. 480MHz M7 has 1MB, no issue.")
if best: print(f"\n=> CHEAPEST that fits full chain: {best[0]} (~${best[1]}) -- {best[2]}")
print("=> Sweet spot: any 480 MHz Cortex-M7 (H750/Daisy ~$30 board, RT1010 ~$3+extRAM).")
print("=> Floor: ~200 MHz M7 (F722/746) at OS1 with trimmed FX. M4-class (<=180MHz) is")
print("   borderline/over once the ipc penalty hits the Newton PI/power stages.")
print("\nHONEST HEADLINE: LUTs + cheap Newton (NOT a neural net) are what let this run on")
print("cheap silicon. The MLP is dominated everywhere in this chain.")
print("*** cycle counts are ESTIMATES; validate on real hardware before any RT claim. ***")

# ---- figure -------------------------------------------------------------------
import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
import numpy as np
alive = [(n,mhz,ipc) for n,mhz,f,r,p,c,ipc in MCU if ipc!=INF]
names = [a[0] for a in alive]
use1 = [chain_m7(1)*a[2]/(a[1]*1e6/48000)*100 for a in alive]
use2 = [chain_m7(2)*a[2]/(a[1]*1e6/48000)*100 for a in alive]
y = np.arange(len(names))
plt.figure(figsize=(10,5))
plt.barh(y-0.2, use1, 0.4, label='OS1 (base rate nonlinear)', color='tab:green')
plt.barh(y+0.2, use2, 0.4, label='OS2 (2x oversampled)', color='tab:orange')
plt.axvline(100, color='k', ls='--'); plt.text(101, 0, 'real-time limit', rotation=90, va='bottom')
plt.axvline(85, color='gray', ls=':')
plt.yticks(y, names, fontsize=8); plt.gca().invert_yaxis()
plt.xlabel('CPU load (% of one core)'); plt.title('8-effect chain: CPU load per MCU (best LUT+Newton config)')
plt.legend(); plt.xlim(0, 300); plt.tight_layout()
plt.savefig(r"D:\sd1\vst\renders\phase4_min_hardware.png", dpi=100)
print("\nwrote phase4_min_hardware.png")
