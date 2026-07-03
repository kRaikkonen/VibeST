# BOSS OD-1 (1977) → SD-1 白盒电路仿真 VST — 项目规划

目标：以元器件级白盒仿真复刻 BOSS OD-1 Super OverDrive 前身电路（用户提供 1977 版 schematic，见 docs/schematic-od1-1977.md），先逐个元件做到 SPICE 级精度并单独验证，再按 schematic 组装全电路，最终封装为实时 VST3 插件。

> **2026-07-03 定向说明**：用户提供的 schematic 实为 1977 年 Boss OD-1（SD-1 的前身）。
> 削波核心（2+1 非对称二极管）与 SD-1 完全一致；OD-1 无 Tone 级（固定 884Hz 低通）。
> 先打穿 OD-1，之后追加 Tone 级即升级为 SD-1。

## 当前进度（2026-07-03）

- ✅ M0（部分）：schematic 转录 + BOM + Drive 级白盒方程推导 → `docs/schematic-od1-1977.md`（少数元件值标 (?) 待高清图核对）
- ✅ M2 原型：Drive 级实时求解器（梯形离散 + Newton-Raphson）vs scipy Radau 刚性 ODE 金标准，6 组工况最差相对 RMS 误差 2.9e-5（阈值 1e-3），非对称削波偶次谐波确认（H2 −29.8dB）→ `proto/od1sim.py`、`proto/validate_drive.py`
- ✅ 全链路 Python 原型 + 8× 过采样 demo 渲染 → `proto/render_demo.py`、`proto/demo_od1_*.wav`
- ✅ 运放宏模型（两级模型：输入差分对 tanh 饱和 + 补偿积分器 → A0/GBW/压摆全物理；TL072/RC3403A/JRC4558 预设可切换），Drive 级升级为 4 状态 2 元非线性 NR 求解，vs Radau 最差误差 2.8e-5 → `proto/validate_stages.py`
- ✅ S1/S5 缓冲级升级为 Ebers-Moll BJT（DC 工作点自求解 + 逐采样标量 NR），vs Radau-DAE 误差 ~5e-8；Q1 前端失真实测 H2 −42dB
- ✅ S3 Filter 级带宏模型运放，小信号 vs 解析解偏差 0.003%
- ✅ 白盒发现（芯片 A/B）：削波时二极管动态电阻塌缩使环路增益需求骤降，OD-1 音色由二极管+C103 主导，TL072 vs RC3403A 谱差 <0.5dB——重绘版用 TL072 不构成音色失真；741 类芯片真正的贡献点是输出级交越失真（未建模，见待办）
- ✅ 交越失真（class-AB 输出级死区，闭环内建模）+ rail clamp：RC3403A vs TL072 低电平高次奇次谐波 +9~+29dB（"fizz"指纹），验证复通过（最差 1.07e-4）→ `proto/revalidate_xover.py`

## Fender Princeton Reverb AA1164（2026-07-03 同日完成第一版）

转录：`docs/schematic-princeton-aa1164.md`（Rob Robinette 注释版原厂图，fender/ 目录）。
实现：`proto/tubes.py`（Koren 管模型）、`proto/mna.py`（线性网络 MNA→双线性状态空间，DK 线性内核）、`proto/princeton.py`（全机）。

- ✅ 管模型 vs 数据手册工作点：12AX7/12AT7 在容差内；6V6 KG1 按手册点校准（583，流传值 1672 偏 3 倍）；偏置自校准解出 −32V vs 原理图 −34V ✓ 交叉自洽
- ✅ 三极管增益级（2 未知数 NR）vs Radau：2.6e-7；cathodyne 倒相 vs Radau：1.7e-7，双出平衡 0.986
- ✅ 6V6 推挽 + 输出变压器（Lm+Cw）+ 扬声器阻抗负载 vs Radau：1.9e-6（至 36W 削波）；深过载用自收敛判据 1.9e-4（Radau 被 Cw 的 125ns 刚性拖垮）
- ✅ FMV 音色堆 MNA vs 解析频响：<0.7%
- ✅ NFB 环：环路探针实测 Nyquist 图 → 发现并补上两个真实物理元素：OT 绕组分布电容 Cw=500pF（没有它环路增益随频率上升→振荡）、漏感 20kHz 滚降；NFB 数值上取两采样平均（Nyquist 零点）。闭环 220Hz 衰减 5.8×，极性验证 PASS
- ✅ 电源 sag：5U4GB + 4 节 RC 链，信号下 420V→400V；120Hz 纹波内生
- ✅ 混响：500pF 送出高通 → 12AT7 驱动 → 弹簧箱色散 IR（频域构造，双簧）→ 12AX7 回收 → 3.3M∥10pF 干湿混合
- ✅ 颤音：电路推导 LFO 摇 6V6 偏置（真振荡器 ODE 待做）
- Demo：`proto/render_chain_demo.py` → demo_princeton_clean/cranked、demo_od1_into_princeton（全链串联）

## C++ 移植 + Standalone 练习 amp（2026-07-03 同日完成第一版）

工具链：便携版 MinGW-w64 GCC 16.1（D:\sd1\toolchain，无需管理员）；JUCE 8.0.4 已克隆备用（D:\sd1\vst\JUCE）。

- ✅ OD-1 引擎 C++ 移植（`vst/src/engine/od1.hpp`）：vs Python 逐采样比对 **1.6e-9**（S1–S4 达 1e-15 bit 级）→ `vst/test/test_od1.cpp`
- ✅ 求解器加固（Python/C++ 同步）：残差范数回溯线搜索 + 不收敛时 4× 子步长——修复交越死区+大瞬态下 2D 牛顿的解支分岔
- ✅ Princeton 引擎 C++ 移植（`vst/src/engine/princeton.hpp`，含 MNA 音色堆 Gauss-Jordan 版）：vs Python **1.39e-10**
- ✅ 白盒教训（记录）：解析雅可比/chord Newton 会改变硬削波下的牛顿路径→与参考实现在近多解区选不同分支→已回退 FD 逐位对齐版；解析雅可比优化须两语言同步 + 全局化求解器（待办）
- ✅ 性能（实测）：OD-1@384k 3.8× 实时；Princeton@192k 1.32×；**standalone 全链（OD-1@192k + Princeton@96k + 弹簧箱分段卷积 + 重采样）1.47× 实时，67.8% 单核**
- ✅ Standalone 练习 amp（`vst/PrincetonPractice.exe`，miniaudio 后端）：`--list` 列设备、`--in/--out` 选设备、`--selftest` 自检；键盘实时调 OD-1 开关/Drive/Level + Volume/Treble/Bass/Reverb/Tremolo/输入 trim/master；自研 FFT 分段卷积（零延迟弹簧箱）+ 63 阶半带重采样级联
- ⏳ JUCE VST3 版（GUI + DAW 插件）：引擎已就绪，JUCE 8 + MinGW 兼容性未验证，可能需要用户装 VS Build Tools

## 下一步
1. 真机试奏 standalone（吉他 + 声卡），校准输入 trim/电平映射
2. LTspice（需手动装）建两台全电路参考工程交叉验证
3. Princeton 待核对清单（docs）：TR2/TR3 实测参数、弹簧箱参数、V4A 真振荡器、6V6 栅流 blocking、NFB 量对标真机（当前 15dB 略偏强）、真箱体 IR（当前监听滤波占位）
4. 求解器优化（两语言同步全局化 NR + 解析雅可比）以及 WASAPI 独占/ASIO 低延迟模式
5. JUCE VST3 + GUI；SD-1 升级（OD-1 + Tone 级，4558 参数已备）

---

## 1. 电路结构（SD-1 信号链分解）

SD-1 电路按缓冲级天然分成可级联仿真的 stage（运放/射随器输出阻抗低，级间可视为单向）：

```
Input ─→ [S1 输入缓冲] ─→ [S2 FET 旁路开关] ─→ [S3 运放增益+非对称削波] ─→
        [S4 Tone 音色网络] ─→ [S5 Level 音量] ─→ [S6 输出缓冲] ─→ Output
```

| Stage | 电路内容 | 非线性元件 |
|---|---|---|
| S1 输入缓冲 | NPN 射极跟随器（2SC2240 类），偏置到 Vref=4.5V | BJT（弱非线性） |
| S2 电子开关 | BOSS 标志性 JFET（2SK118）+ 触发器切换电路 | JFET（开关态可简化） |
| S3 增益/削波级 | JRC4558 半颗，非反相放大；反馈回路内非对称削波：**一个方向 2 只二极管串联，另一方向 1 只**（SD-1 音色的核心，区别于 TS 的对称削波）；Drive 电位器 1MΩ 在反馈支路 | 二极管 ×3、运放 |
| S4 Tone 级 | 有源/无源混合音色网络，Tone 电位器 20kΩ | 运放（另半颗 4558，视版本） |
| S5 Level | 10kΩ 音量电位器分压 | 无 |
| S6 输出缓冲 | NPN 射极跟随器 | BJT（弱非线性） |
| 电源 | 9V 供电，分压产生 Vref=4.5V 偏置，退耦电容 | 可扩展"电池电压下垂"参数 |

> 注：以上件值以流传最广的 SD-1 原版 schematic 为基准，动工前第一步是核对一份高清 schematic（含 SD-1 vs SD-1W、日产/台产版本差异），锁定 BOM。

## 2. 元器件模型库（Phase 1 — 逐元件白盒建模）

每个元件独立实现 + 独立测试，验证标准：与 LTspice 同参数仿真的误差曲线。

| 元件 | 物理模型 | 关键参数来源 | 验证方法 |
|---|---|---|---|
| 电阻 | 线性 R（可选 Johnson 噪声） | 色环标称 + 容差分布 | 平凡 |
| 薄膜电容 | 理想 C，梯形/TR 离散化 | 标称值 ±5% | 频响 vs 解析解 |
| 电解电容 | C + ESR（可选老化漂移） | 数据手册 | 频响 vs LTspice |
| 二极管 1S1588 / 1N4148 | Shockley 方程 i=Is(e^(v/nVt)−1) + 结电容 Cj | SPICE model card + 实测 I-V | DC 扫描 + 瞬态 vs LTspice |
| BJT 2SC2240 / 2SC732 | Ebers-Moll（必要时 Gummel-Poon） | 厂商 SPICE model | DC 工作点 + 增益/失真 vs LTspice |
| JFET 2SK118 | Shichman-Hodges；开关用途可退化为变阻 | 数据手册 | 开关瞬态 vs LTspice |
| 运放 JRC4558 | 宏模型：开环增益、GBW≈3MHz、slew rate≈1.7V/µs、输出摆幅饱和、输入阻抗 | 数据手册 + 晶体管级 LTspice 金标准校准 | 阶跃响应/大信号失真对比 |
| 电位器 | 电路矩阵中的时变双电阻（考虑负载效应，非事后增益） | 阻值 + 锥度（log/linear） | 各挡位频响 vs LTspice |

物理常量层面统一处理：热电压 Vt=kT/q（默认 300K，温度可作全局参数）。

## 3. 电路解算方法（Phase 2 — 按 schematic 组装）

- **每个 stage 用 Nodal DK-method**（Yeh 2009；Holters & Zölzer 对可变电位器的扩展）：电路网表 → 状态空间 + 非线性方程，Newton–Raphson 逐采样求解。备选/对照实现：WDF（`chowdsp_wdf` 库，R 型适配器处理多非线性）。
- **级间单向级联**：利用缓冲级隔离，避免全电路联立求解（全电路 SPICE 仅作离线金标准）。
- **过采样 4–8×**（多相 FIR 或 halfband IIR）抑制削波混叠；发布前用正弦扫频测混叠残留。
- **电位器参数变化**：DK 矩阵随 pot 值重算/插值 + 参数平滑，避免 zipper noise。

## 4. 验证体系（贯穿全程）

1. **金标准**：LTspice 全电路瞬态仿真（含晶体管级 4558），同一激励 wav 输入，逐样本对比波形 + 频谱误差。
2. **元件级单测**：每个模型类附带 vs-LTspice 的自动化误差断言（CI 可跑）。
3. **硬件对照**：真机 SD-1 录制 DI 干湿对（正弦扫频、吉他 DI），做客观指标（谐波结构、THD 曲线）+ 盲听。
4. **容差实验**：Monte Carlo 撒容差，确认音色敏感元件（预期：削波二极管、削波级 RC、Tone 网络）。

## 5. 技术栈

- C++20 + **JUCE**（VST3/AU），矩阵运算 Eigen，可复用 `chowdsp_utils` / `chowdsp_wdf`
- LTspice 作离线参考；Python (numpy/scipy) 做误差分析与绘图
- 原型阶段可先用 Python 验证 DK-method 数学，再移植 C++

## 6. 里程碑

- **M0 资料锁定**：高清 schematic + 完整 BOM + 各元件 SPICE model card；搭好 LTspice 全电路参考工程 ✅ 定义"对"的标准
- **M1 元件库**：§2 全部元件模型 + 单测通过（误差阈值 vs LTspice）
- **M2 削波级原型**：S3（运放+非对称削波+Drive pot）实时 DK 实现，离线对比 LTspice —— *这是音色核心，最先打穿*
- **M3 全电路**：六个 stage 级联 + 三个 pot 全参数化，全链路 vs LTspice
- **M4 插件化**：JUCE VST3 封装、过采样、参数平滑、预设、简单 UI
- **M5 打磨**：真机对照校准、容差/温度/电池电压等"玄学"参数、性能优化（SIMD、NR 迭代上限）

## 7. 已知风险

- 4558 晶体管级 vs 宏模型的音色差异 → M2 阶段用 LTspice A/B 定量确认宏模型够不够
- schematic 版本差异（SD-1 历代改版）→ M0 锁定一个具体版本
- 实时性能：单 stage NR 求解在现代 CPU 上无压力，但 8× 过采样 × 6 stage 需要 M4 做预算测试
