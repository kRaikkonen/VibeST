# VibeST Practice Amp

**A white-box, real-time guitar amp & pedal simulator — every knob traces back to a real circuit, not a neural snapshot.**
**纯白盒、实时的电吉他音箱 / 效果器模拟器 —— 每一个旋钮都能追回到真实电路上的一个元件,而不是一段神经网络采样。**

<p align="center">
  <img src="vst/docs/ui.png" alt="VibeST Practice Amp UI" width="720">
  <br>
  <em>The standalone app. Default rig: Ibanez TS-808 → Boss SD-1 → Fender Princeton Reverb.</em>
</p>

---

## What is this? / 这是什么?

**EN** — VibeST is a standalone Windows app that recreates the sound of classic guitar amps and
overdrive/distortion pedals by **simulating the actual analog circuits** — vacuum-tube gain
stages, diode clippers, op-amp EQs, tone stacks, output transformers — sample by sample, in real
time. It is *white-box*: the model is built from the schematic, not fitted to a recording.

**中文** — VibeST 是一个 Windows 独立软件,通过**仿真真实的模拟电路**(电子管增益级、二极管削波、运放 EQ、
tone stack、输出变压器)来还原经典吉他音箱和过载/失真踏板的声音,逐采样、实时运行。它是**白盒**的:
模型是从**原理图**搭出来的,而不是对着一段录音拟合出来的。

---

## The idea / 核心理念

**EN** — Real tone *is* the physical behaviour of a real circuit being driven by a guitar signal.
So instead of "dialling in something that sounds good", we reconstruct how a given circuit
actually reacts — only with code instead of resistors and capacitors. Two rules are absolute:

**中文** — 真实音色**就是**一段真实电路被吉他信号驱动时的物理行为。所以我们不是"调出一个好听的效果",
而是**还原一段电路真实的反应**——只不过用代码代替电阻电容。两条铁律不可逾越:

> ### 🔒 铁律一 — Physics is the anchor. No fudge constants.
> Every number comes from a real component value, a datasheet, a measurement, or a physics law
> you can point to. No magic `gain *= 3.7` that only exists because "it sounded right".
> 每个数字都来自真实元件值 / 数据手册 / 实测 / 一条能说清的物理定律。没有说不清出处的魔法常数。

> ### 🔒 铁律二 — Ear + instrument, both or it doesn't ship.
> A model is only "done" when it passes **both** a blind listening test **and** measurement
> (frequency sweep, harmonic spectrum, THD-vs-level) against a golden reference capture.
> 一个仿真必须**同时**通过盲听检和仪器检(扫频、谐波谱、THD-vs-电平)对照 golden reference 才算做完。

The golden references are **NAM (Neural Amp Modeler)** captures of the real hardware.
Golden reference 用的是真机的 **NAM(Neural Amp Modeler)采样**。

---

## How it works / 原理

The signal flows through the chain below. Every block is a physical model, except the cab (an
impulse-response convolution — the industry-standard way to capture a speaker + cabinet).

信号按下图流过整条链路。除了箱体(用脉冲响应卷积——业界标准的喇叭+箱体还原法),每一级都是物理模型。

```mermaid
flowchart LR
  IN([Guitar In]) --> SH[Shift Pose<br/>+ Digital Capo]
  IN -.tap.-> TUN[[Tuner]]
  SH --> GATE[Noise Gate] --> COMP[Compressor]
  COMP --> A[Drive A] --> B[Drive B]
  B --> AMP[Amp<br/>preamp → tone stack → power/OT]
  AMP --> CAB[Cab IR]
  CAB --> CH[Chorus] --> DLY[Delay] --> RV[Reverb] --> RM[Room Mic] --> EQ[Graphic EQ]
  EQ --> MST[Master] --> OUT([Out])
```

**The physics under the hood / 底层的物理:**

| Circuit block | How it's modelled / 建模方式 |
|---|---|
| Vacuum tubes (12AX7 / 6L6 …) | Koren triode/pentode equations 电子管 Koren 模型 |
| Diode / germanium clippers | Shockley / Ge diode I-V, solved per sample by **warm-started Newton-Raphson** (cheap: the state barely moves between samples at 48 kHz) 二极管方程逐采样牛顿求解,热启动 |
| Op-amp tone/EQ stages | **Modified Nodal Analysis (MNA)** with ideal op-amps, bilinear-discretised 运放级 MNA 网络,双线性离散 |
| Tone stacks (Fender/Marshall/FMV) | State-space passive networks, real pot/cap values 无源 tone stack,真电位器/电容值 |
| Output transformer | Leakage-inductance resonance biquad 漏感谐振 biquad |
| Power-supply sag (Rectifier) | Envelope-driven B+ droop 包络驱动的 B+ 下垂 |
| Speaker cabinet | Impulse-response convolution 脉冲响应卷积 |

Nonlinear stages are oversampled to keep aliasing out. Everything runs on a real-time audio
thread (no allocation, no locks) via ASIO or WASAPI.
非线性级过采样防混叠;整条链跑在实时音频线程上(不分配内存、不加锁),走 ASIO 或 WASAPI。

---

## Features / 功能

### Amplifiers / 音箱 (4)
| Amp | Notes | Match vs NAM |
|---|---|---|
| **Fender Princeton Reverb** | clean/edge-of-breakup Fender | **0.27 dB (gold)** |
| **Marshall Super Lead Plexi** | presence via deep negative feedback | voicing being refined |
| **Mesa Dual Rectifier** | Rhythm ch. + **Raw/Vintage/Modern** voicing + **Diode/Spongy** rectifier sag | **1.7 dB** |
| **Dumble Steel String Singer** | high-headroom boutique clean | **1.37 dB** |

The Rectifier's **power-supply sag envelope** (Diode = tight, Spongy = 12 % compression with slow
recovery) is a real dynamic behaviour that pure frequency-response models miss.
Rectifier 的**电源 sag 包络**(Diode 硬、Spongy 12% 压缩+慢恢复)是纯频响模型抄不到的动态手感。

### Pedals / 踏板 (8, two stackable slots A → B)
`Boss OD-1` · `Boss SD-1` *(white-box + hybrid)* · `Ibanez TS-808` *(white-box + hybrid)* ·
`Mad Professor Red` · `Klon Centaur` · `Marshall Bluesbreaker`

- **white-box** = tone circuit modelled *and* NAM-validated. **(white-box)** = tone 电路建模 + NAM 验证过。
- **(schematic)** = built from the real schematic (real component values, real clipping topology,
  white-box tone) but **not yet** instrument-validated (no capture on hand). Marked honestly.
  **(schematic)** = 真图真值真拓扑搭的,但**还没**仪器验证(手上没 capture),诚实标注。

### Dynamics (before the drives) / 动态(在失真前)
Noise Gate · Keeley-style feed-forward Compressor 噪声门 · Keeley 式前馈压缩

### Post-amp effects / 后级效果
CE-2 Chorus · Boss Digital Delay · Digital Reverb (Freeverb) · Studio Room Mic (stereo) ·
9-band Graphic EQ + HPF/LPF

### Tools / 工具
- **Chromatic Tuner** — decimating-autocorrelation pitch detection, **±2.5 cents** accurate,
  adjustable reference **A = 425–455 Hz**. 抽取自相关测音,±2.5 音分,标准音可调。
- **Shift Pose** — fine pitch shift (input target Hz) to sit with old recordings tuned off-440.
  微调移调,适配非 440 标准音的老录音。
- **Digital Capo** — clean **±12 fret** transpose. 数字变调夹,±12 品移调。
- **Cab IR loader**, output **clip meter** (green→red), factory presets, per-amp auto-preset on
  amp switch, prominent red-framed Master. 支持加载 cab IR、输出电平表、工厂预设、切 amp 自动加载预设。

---

## UI guide / 界面导览

Signal flows **top-left → down → top-right → down**, matching the chain above.
界面按信号链排:**左列从上到下(输入→放大)**,**右列从上到下(后级效果)**。

- **Top bar** — audio driver/device, buffer, Start, Cab IR, and (top-right) Preset, Rectifier
  voicing, output meter, Input Trim, **Capo**. 顶栏:音频设备、Preset、Recto 档、电平表、Capo。
- **Left column** — `TUNER & SHIFT POSE` → `DYNAMICS (gate + comp)` → `PEDALBOARD (A→B)` →
  `AMPLIFIER` → **`MASTER VOLUME`** (big value, red highlight). 左列:调音/移调→动态→踏板→功放→主音量。
- **Right column** — `CHORUS` → `DELAY` → `REVERB` → `ROOM MIC` → `GRAPHIC EQ`. 右列:后级效果链。

Knob labels re-map per amp (e.g. the Rectifier's row reads Gain/Treble/Bass/Mid/Master/Drive),
and switching amps auto-loads that amp's sensible preset. 换 amp 时旋钮标签和预设会自动切换。

---

## Build / 构建

Windows, C++20, native Win32 GUI, MinGW-w64. Full instructions (dependencies, exact `g++` command,
Python golden-reference tests) are in **[`vst/README.md`](vst/README.md)**.

Windows + C++20 + 原生 Win32 GUI + MinGW-w64。完整构建说明(依赖、`g++` 命令、Python 对拍测试)见
**[`vst/README.md`](vst/README.md)**。

Layout / 代码结构:
- `vst/src/engine/` — the white-box circuit engine (tube/diode/op-amp models, tone stacks, FX DSP)
- `vst/src/standalone/` — real-time audio wiring + the Win32 GUI front-end
- `proto/` — the Python prototypes each C++ block is checked against (sample-for-sample)

---

## Honesty / 诚实说明

This is a personal research project. White-box modelling of amps is an ongoing craft: Princeton,
Rectifier and Dumble are validated against NAM captures; Plexi's presence voicing is still being
tightened; the three `(schematic)` pedals are circuit-accurate but not yet instrument-verified.
The time-domain pitch shifter (Shift Pose / Capo) is cleanest for small shifts.

这是个人研究项目。白盒建模是持续打磨的手艺:Princeton / Rectifier / Dumble 已对 NAM 验证;Plexi 的
presence voicing 还在收;三个 `(schematic)` 踏板是电路准确但未经仪器验证;时域移调器在小幅移调下最干净。

Amp/pedal names refer to the circuits being studied and are trademarks of their respective owners;
this project is not affiliated with or endorsed by them.
音箱/踏板名称指代所研究的电路,是各自厂商的商标;本项目与其无隶属或背书关系。
