# Project Goal — Embedded White-Box Amp/Pedal Modeling via Distilled Solvers

## Thesis
Distill each nonlinear circuit **stage's** Newton-Raphson physics solver into the
**cheapest surrogate that fits the stage's input dimension** — a LUT for the simple
stages, a tiny **MLP** for the higher-dim ones (not a temporal CNN — the state is
passed explicitly, so the solve is a memoryless map). Keep the full circuit
**topology, component values, knobs, and explicit physical state**. Run a complete
high-gain amp + FX chain in real time on an **STM32H743VIT6** (Cortex-M7 @480 MHz,
2 MB Flash, 1 MB RAM, DP-FPU, no NEON).

The contribution is NOT "use a neural net" — it is the **per-stage method-selection
map** below, backed by measured cost/accuracy. MLP is not universal; it earns its
place only in the middle dimensions where LUTs explode and Newton is too slow.

## Method-selection map (measured on the TS clipper, `proto/nn/phase1b_method_map.py`)
Order-of-magnitude Cortex-M7 cycle model (FMA 1/cyc, exp≈25, tanh≈18):

| stage input dim | best tool | why (measured) |
|---|---|---|
| **1–2D** (clippers: `b`, drive) | **LUT** | 36 cyc, −67 dB ESR, 8 KB. A 2D LUT is *both* cheaper and more accurate than any MLP here — MLP is pointless. |
| **3D** (≈8 MB LUT) | LUT borderline / MLP | LUT needs 8 MB > 2 MB Flash → compress LUT or switch to MLP. |
| **4–6D** (tube stages: vin + cathode-C + coupling-C + B+ sag [+ temp]) | **MLP** | LUT = 1 GB–17 TB (impossible); MLP stays flat ~1–40 KB. **This is the only zone where MLP is the right answer.** |
| **long-memory** (hysteresis, dielectric absorption) | **+explicit state / light CNN** | needs bigger state space; MLP's edge shrinks. Out of scope for v1, noted. |

**Guard rails (both measured):**
- *Zone-1 (don't reach for MLP at low dim):* even a 16×2 MLP costs 940 cyc > Newton's
  457 — for a 2D map the LUT wins outright.
- *Zone-2 (don't over-size):* pushing the net to 64×4 costs ~19.6k cyc (**40× over
  Newton**) for <1 dB extra accuracy — you lose the entire point of distilling.
  Sweet spot = smallest net that fits budget AND beats the LUT-at-this-dim.

## Concrete target (minimum viable)
Real-time chain on the H743VIT6:
`noise gate → SD-1 → TS9 → Marshall Plexi → chorus → delay → reverb → EQ`

**Success criteria**
1. 48 kHz real-time, CPU < ~80 %, no xruns.
2. End-to-end latency < ~10 ms.
3. Each distilled stage: ESR vs Newton < threshold **and** blind-A/B pass (铁律二).
4. **Knob-generalization**: change drive/tone/gain/presence with NO retraining.

## Why this needs the neural acceleration (the motivation, honest)
Rough budget @480 MHz, 2× oversample (~5000 cycle / 96 k sample):

| block | Newton (now) | fits H743? |
|---|---|---|
| gate / EQ / chorus / delay / reverb | ~few k cycle total | ✅ (Daisy-class proven) |
| SD-1 + TS9 | ~3k cycle each | ⚠️ borderline |
| **Plexi (V1+V2+CF+LTP+EL34+PSU)** | **~15k cycle / sample** | ❌ **~3× over on its own** |

Whole chain with per-sample Newton ≈ **5–8× too slow**. A triode Newton solve
(~2500 cycle: Koren evals × exp/pow × iterations) distilled to a small MLP
(4→16→16→2 ≈ 350 MAC ≈ 500 cycle) is **3–5× cheaper per stage** → the chain fits
(tight, with fixed-point/CMSIS-NN + careful oversampling). **The hardware target is
reachable ONLY via distilled solvers — that IS the paper's motivation.**
(Numbers are order-of-magnitude; profile on-board. The conclusion is robust.)

## Architecture — hybrid, pluggable (three tiers, by the map above)
- **Linear + cheap** (gate/EQ/chorus/delay/reverb, tone-stack MNA, buffers): stay
  **pure physics** — they already run in budget.
- **Low-dim nonlinear** (diode clippers, ≤2D): **LUT** — cheaper AND more accurate
  than any net (measured), tiny memory.
- **Mid/high-dim nonlinear** (tube stages, 4–6D): **MLP-distilled solver** — the LUT
  can't fit; swappable back to Newton (the training ground-truth).
- Double-precision physics → **float** on-board (MLP/LUT are float-native).

## Related work (VERIFIED 2026-07-06) & honest novelty
Prior-art check (web-verified; see per-item confidence). **We are NOT the first to
replace a circuit's nonlinear solve with a learned/tabulated surrogate while keeping
explicit state — cite these, don't claim them:**

- **⭐ CLOSEST PRIOR ART — cite up front, cede the concept to them:** Giampiccolo,
  Gafencu & Bernardini, *Explicit Modeling of Audio Circuits with Multiple
  Nonlinearities for Virtual Analog Applications*, **IEEE OJSP vol. 6, 156–164, 2025**
  (Politecnico di Milano ISPL). Vector waves lump multiple (multi-port) nonlinearities
  into one block; a **port-resistance decoupling condition** makes it explicit (no global
  Newton), and an **NN learns that explicit wave-scattering map → replaces the iterative
  solve.** This is our exact idea, generalized to multiple nonlinearities, in the wave
  domain. Active lineage: 2020 (problem), 2021 (WD Newton-Raphson baseline), DAFx23
  (explicit vector-WDF, single BJT), ISCAS25 (pentode-NN; time-varying BJT fuzz w/ 2
  pots = knobs w/o retrain), DAFx25 (multi-port NN training). **Desktop plugins (VIOLA);
  NO MCU anywhere.** `ieeexplore.ieee.org/document/10839128`
- **Closest (single-nonlinearity, MLP-for-solver):** Darabundit, Roosenburg & Smith,
  *Neural Net Tube Models for Wave Digital Filters*, DAFx 2022 (Stanford CCRMA — NOT
  Välimäki, a common miscite). WDF keeps the stateful/linear part; an MLP replaces the
  triode nonlinearity as "an explicit alternative to Newton-Raphson."
  `dafx.de/paper-archive/2022/papers/DAFx20in22_paper_13.pdf`
- **Closest (LUT-for-solver):** Yeh, *DK-method* (PhD 2009; TASLP "Automated Physical
  Modeling…" I & II, 2010/12). Precomputes the Newton solve into a **multidimensional
  interpolated LUT**, state-space circuit kept explicit. This is exactly our low-dim
  recommendation — our Phase-1 LUT is a rediscovery of it. `ccrma.stanford.edu/~dtyeh`
- Chowdhury & Clarke, *Emulating Diode Circuits with Differentiable WDFs*, SMC 2022 —
  learns element/params in a WDF (autodiff), accuracy-focused, not solver-elimination.
- Pentode-in-WDF via NN (Bernardini lineage, 2025); hysteresis-in-WDF via DL (EURASIP
  2023) — same "NN element inside WDF" family.
- **Learned-solver analogues (non-audio):** *Neural-Newton Solvers* (arXiv 2106.02543,
  NN mimics the Newton step, physics residual kept in loop); NeuroSPICE PINN circuit
  modeling (2025). No instance found applying these to per-sample audio-circuit solves.
- **Embedded VA:** Chowdhury, *Comparison of VA Techniques for Desktop & Embedded*
  (arXiv 2009.02833) — closest white-box-on-embedded ref; RTNeural runs NN VA real-time
  (all black-box). **No prior instance of component-level white-box + distilled solver
  on a Cortex-M7-class MCU** — that combination appears open.
- *Cite-from-library, not re-verified:* Holters & Zölzer, Werner (WDF), Parker,
  Zavalishin (K-method / state-space VA lineage).

**DROP these claims (falsified):**
- ✗ "Black-box needs retraining per knob." False — **knob-conditioned** black boxes
  exist and generalize: Schmitz & Embrechts LSTM (2018), Steinmetz & Reiss micro-TCN
  w/ FiLM (2022), GuitarML NeuralPi. Reframe: *our* knobs are native circuit parameters
  needing no extra training and extrapolating to unseen components/topology.
- ✗ "Novel to replace the solver inside a physical model." Darabundit + Yeh own it.

**GOAL = DAFx systems/engineering paper** (decided 2026-07-06). The solver-distillation
*concept* is Polimi's — we cite it and pitch **embedding + tooling**, not method invention.

**Load-bearing novelty (both are engineering/systems — DAFx welcomes this):**
1. **(C, LEAD) Full high-gain chain on a low-cost Cortex-M7 MCU** — white-box,
   solver-distilled, real-time, live knobs, explicit topology. **No prior instance.**
   Paper lives or dies here → must quantify HARD: cycles/sample, RAM, latency, headroom
   at 48 kHz w/ oversampling. If it can't close the budget, this pillar collapses.
2. **(A) Per-stage surrogate selection by dimension** — LUT ≤2D, small MLP for higher-dim
   where tables explode — one unified policy, vs LUT-only (Yeh) or NN-only (Polimi).
   Backed by our measured crossover (Phase 1b/1c). Frame with cost/accuracy numbers or a
   reviewer calls it incremental.

**Ceded / must-cite (do NOT claim as novel):**
- ✗ Solver distilled to a learned surrogate while keeping physical state — **Giampiccolo/
  Bernardini own it** (wave domain, multi-nonlinearity, since 2023). Nodal-vs-WDF is a
  THIN wedge; position (B) as "we port the known WDF idea to the nodal/MNA domain to make
  it embeddable," a convenience/engineering argument, not conceptual novelty.
- ✗ "Black-box needs retraining per knob." False — knob-conditioned black boxes exist
  (Schmitz LSTM 2018, Steinmetz-Reiss micro-TCN FiLM 2022, GuitarML); Polimi even
  conditions their in-WDF NN on pot settings. Do not use this contrast.
Do NOT put any solver-surrogate novelty in the abstract; lead with the MCU system.

## Phases
0. Data harness — teacher = existing Newton solvers (done). ✅
1. **One stage (TS clipper)** ✅ — knob-generalization CONFIRMED (LUT −67 dB / MLP
   −24 dB on **held-out** drives; drive is an input, no retrain). Finding: at 2D the
   **LUT wins** → clippers ship as LUT, not MLP. Scripts: `proto/nn/phase1_ts_clipper.py`,
   `proto/nn/phase1b_method_map.py`.
1b. **Triode stage (12AX7)** ✅ — LUT→MLP crossover shown (`proto/nn/phase1c_triode_crossover.py`,
    fig `vst/renders/phase1c_triode_crossover.png`). Teacher = real Koren plate-load
    Newton. Added one physical axis at a time (vg → +cathode bias → +B+ sag → +plate
    knob): LUT −55 dB but memory 128 B→4 MB (dead past 2 MB Flash at d=4, earlier in a
    6-stage chain); MLP holds −38 dB in **2.9 KB (1400× smaller)**. Zone-1 (LUT wins
    ≤2D) reconfirmed on a real tube. **Honest scope:** this is the MEMORY/feasibility
    crossover — the 1-unknown triode Newton is only 4.3 iters (cheap), so MLP's *speed*
    win vs Newton is NOT shown here; it belongs to the coupled power stage (Phase 3).
**REPRIORITIZED for the DAFx systems goal — de-risk (C) EARLY, before more MLPs:**
2. **⭐ EMBEDDED DE-RISK** — ON-PAPER budget ✅ (`proto/nn/phase2_h743_budget.py`, fig
   `vst/renders/phase2_h743_budget.png`). Full-chain cycle estimate @48k/480MHz/OS2:
   **Newton 410% (4.1× over) → naive distill (MLP everywhere+libm tanh) 243% (STILL
   over!) → dimension-aware distill (3D triode→LUT, 4-6D→MLP+poly-tanh) 75% → CLOSES,
   25% headroom.** KEY: distillation ALONE doesn't close it; the **dimension-aware policy
   (A) is what makes (C) possible** — the two contributions are one story. Memory fits
   (Flash 740 KB/2 MB, the 512 KB triode LUTs are the "spend Flash to save cycles" trade;
   RAM 357 KB/1 MB, constrained by FX delay lines NOT the models). Caveats to validate on
   metal: N_POWER=15k is the most uncertain input (drives the Newton baseline); OS=4
   whole-chain = 138% (needs selective OS); all ESTIMATES — real-time claim pending H743.
   REMAINING: buy an H743/Daisy, flash ONE stage, confirm the cycle model.
3. Full TS-808 (clipper LUT + tone MNA), PC real-time + blind A/B (铁律二).
4. Plexi all-MLP — also the SPEED-crossover figure (coupled power stage, ~15k cyc Newton
   → MLP faster; the half Phase 1c couldn't show).
5. Full chain on H743, profile + optimize to real-time → a playable pedal (the artifact).
6. DAFx paper: tables (per-stage cycles/RAM/ESR, LUT-vs-MLP-vs-Newton), the dimension
   map, blind A/B, the hardware build. Cite Giampiccolo/Bernardini OJSP 2025 up front.

## Go/No-Go gates (honest)
- **NEW LEAD GATE:** budget doesn't close on real H743 for even a representative stage →
  pillar (C) collapses → no DAFx systems paper. **De-risk this FIRST (Phase 2).**
- Phase-1 knob-generalization ✅ passed (drive as input, held-out generalizes).
- A Plexi stage won't distill accurately (EL34 power stage most at risk) → leave that
  stage on Newton, MLP the rest, re-budget.

## Paper framing — DAFx systems/engineering (chosen)
Lead with the **hardware system**: a full white-box high-gain chain, solver-distilled,
real-time on a low-cost Cortex-M7, with live knobs. Contributions = **(C) embedded
deployment + measurements** and **(A) dimension-aware LUT/MLP policy**. Cite Giampiccolo/
Bernardini (OJSP 2025) as closest prior art; position our solver-distillation as the
nodal, embeddable port of their idea — NOT as new method. Evidence: cycles/RAM/latency
tables + blind A/B (铁律二) + the crossover figures. (ICASSP method-novelty angleは
retired — Polimi owns the concept.)
