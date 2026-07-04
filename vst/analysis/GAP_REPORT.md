# 白盒 amp vs 真机 NAM —— 差距分析报告

日期 2026-07-05。第一次拿到真机 ground truth(用户提供 1964 Princeton + Plexi 51 的 head NAM + 真 cab IR),按 skill 测量台流程做 A/B。**这是白盒项目第一次有真机可对。**

## 用了什么

- **真机 head**:`init_from_nam` 载入 NAM(SlimmableContainer→取 channels=8 高质量 submodel),用官方 `neural-amp-modeler` 0.13 + torch 2.12 跑推理。48kHz。已验证像真放大器(sine 0.05→0.5 输入,THD 2.3%→26%,强压缩)。
- **公平配置**(见 memory `fair-amp-comparison-protocol`):tone 全 12 点、无踏板、无 delay/EQ/room/gate、reverb 0、trem 0、head-to-head 不过 cab、响度归一化、用非调音素材(扫频 + 单音 + 未调音 DI)。
- **台架**:`vst/analysis/bench_gen.py`(造信号 + 跑 NAM)、`bench_cmp.py`(对拍)、`bench_clean_fr.py`(线性频响)。可复跑,换 `sys.argv[1]` 即换 NAM 挡位。

## 三个差距(按严重程度)

### 🔴 #1【严重 bug — ✅ 已修 2026-07-05】功率级 idle 自激 ~7 kHz

> **修复**:Cw 从无阻尼梯形改成**串绕组电阻 Rw≈33kΩ 的阻尼后向欧拉电容伴随模型**(真 OT 的绕组电容本就被绕组 R/铁损阻尼)。Princeton idle 从 -15dBFS → **-136dBFS(机器零,比真机 -44 还干净)**,20-33k 是稳健平台(非脆弱调参)。NFB 仍稳(diag_nfb),selftest finite,真弹奏 centroid 1663→1382(振荡的假亮度也随之消失)。Plexi 从 -18→-35dBFS(残留次要源,待查)。下面是原始诊断记录:


**喂纯零**,我的 amp 从第 0 秒就吐 **0.18 RMS(≈ -15 dBFS)的持续单音振荡**(crest 3.4dB=接近正弦),主峰 ~6.7 kHz(Princeton)/ ~7.8 kHz(Plexi),外加 17.3 kHz 混叠镜像(6718+17282=24000=Nyquist)。真机 idle 是 **0.0065 RMS(静的)**。

- **定位(已精确到元件)**:两个 amp 都振(Plexi 没有弹簧混响却照样振)→ 不是混响、不是 NFB(与反馈截止 250/2000/4000/8000 无关)。可回退实验:**把 OT 绕组电容 `Cw` 从 500pF 设到 ~0 → idle 振荡从 0.179@6718Hz 暴跌到 0.021@12kHz(-19dB,砍掉 89%)**。所以**主振荡源 = `PowerStage` 里的 Cw(500pF)+ OT 漏感组成的 LC 谐振**:那个 LC 用纯梯形离散(无阻尼、极点在单位圆上)+ 电子管非线性耦合 → ~7kHz 极限环。频率随功率管/OT 参数变(6V6 6718Hz vs EL34 7802Hz)佐证是功率级。残留 0.021@12kHz 是次要源(待查,可能喇叭 LC)。
- **讽刺**:memory 记着 Cw 当初是为**修另一个 NFB 振荡**加的("没有它环路增益随频率上升→振荡")。修好一个、又造一个——打地鼠。真 OT 的绕组有电阻损耗会**阻尼**掉这个谐振,我的 Cw+漏感是**无损**的 → 自激。**修复方向**:给 Cw–漏感这个 LC 加真实的 OT 绕组电阻损耗(物理阻尼,铁律一),或把该子回路从纯梯形换成微阻尼积分(TR-BDF2 / 加一点 backward-Euler)。验收:喂零 → 输出 < -60 dBFS,且 NFB 环仍稳(diag_nfb burst 测试)。
- **为什么以前没抓到**:过去所有验证都**带 cab**(cab 把它压到 ~-38 dBFS)、而且**从没测过 idle/静音**。又一次"验证测错了东西给假绿灯"(和 memory 里 NFB 那次同类错误)。这个和那次 fs/6(8k/16k)的 NFB 振荡**是两回事**——这是第二个、更隐蔽的。
- **音量依赖反常**:音量越低振得越凶(vol0.2→0.26 RMS,vol0.9→0.14)——不是简单增益驱动,是工作点极限环。
- **影响**:① 就是"fizz"的一大来源;② **污染了本 session 之前所有亮度测量**(我说"我 amp 太亮 4-8k"很大程度是这个振荡的能量,不全是真频响)。
- **必须最先修**,否则 #3/谐波都测不干净。

### 🟠 #2【大差距】增益结构过热 —— 过载灵敏度高约 10-20 倍

同样 Volume 7、喂同一串升电平单音(0.02→0.7):

| 输入 | 0.02 | 0.05 | 0.1 | 0.2 | 0.35 | 0.5 | 0.7 |
|---|---|---|---|---|---|---|---|
| **真机** out/in | 1.00 | 0.98 | 0.97 | 0.60 | 0.36 | 0.26 | 0.19 |
| **我的** out/in | 1.00 | **0.62** | 0.38 | 0.18 | 0.11 | 0.08 | 0.04 |

真机干净到输入 ~0.1-0.2 才平滑压缩;**我的从 0.05 就在硬压、几乎没有 clean headroom**。这就是长期"像线性公式凑的 / 一开就破 / 没有干净音"那些抱怨的根。要么我 preamp 增益偏高,要么 Volume 挡位映射太猛,要么输入电平标定(inTrim)偏热。

### 🟡 #3【结构性认知纠正】真 head 几乎是平的,音色全在 cab

真 Princeton **head**(不过喇叭)线性频响:80Hz-8kHz 内 **±5 dB,基本平、略亮**。也就是说吉他音箱那种"暗、中频集中"的音色**全部来自喇叭/箱体**,不在 head。

**我一直以为"亮 head 要配暗 cab"—— 反了。** 正解:head 做到接近平,cab(真 IR)负责塑形。这解释了为什么我那个"太暗"的合成 cab 是在给我 head 被振荡抬高的假亮度做补偿——两个错互相掩盖。**方向就是用真 cab IR**(用户已给 24 个 Princeton Jensen P10R 话筒位)。

> ⚠️ 我 head 的**真实**线性频响和谐波性格**现在测不准**(被 #1 振荡淹了)。修完 #1 才能干净对比 voicing 和谐波(暖/硬)。

## 真机对拍协议结果(2026-07-05 深夜,按用户 head→cab→combined + 频谱/波形图)

工具:`head_match.py`(我的 head vs NAM head)、`cab_match.py`(我的 cab vs 真 IR)。图在 `renders/head_match.png`、`cab_match.png`。

**HEAD(我的 vs 真 Princeton Clean NAM,DI 实弹 Welch 谱 + 波形):**
- 🔴 **高频严重过量**:真 head 在 1kHz 以上狠狠滚降(5k=-40dB),我的却在 4-5kHz 鼓 +20dB 的包 → **差 ~50dB@5k**。缺了真放大器的高频滚降(Miller 电容、耦合、OT 带宽)。
- 🔴 **削波太硬**:波形上真机是**圆润软削波**,我的是**尖锐带毛刺硬削波**(fizz 来源)。
- 🟠 **偏薄**:低中频(160-640Hz)比真机低 ~8dB。
- 🔴 **喇叭阻抗烤进了 head**:我的 head 输出 vspk_ 带着功率级喇叭 LC 的浴缸形阻抗曲线,而 NAM head 是无喇叭的(load box)。等于把喇叭算了两遍(功率级阻抗 + cab IR)。
- 🟡 **hum**:信号极低时 PSU 纹波冒头(idle 静=推挽共模抵消;有信号时不对称→出 hum)。

**CAB(我的合成 C10R vs 真 Princeton Jensen P10R IR):**
- 🔴 **太暗**:真箱有 3-4.5kHz 的 presence 峰再滚降;我的合成 cab 1kHz 以上就狂砍(5k 差 33dB)、没 presence、脉冲响应细节也糊。**修复=直接用真 IR**(用户已给)。

**结论(完全自洽)**:head 太亮 + cab 太暗,两个错**部分互相抵消**所以"能出声",但都不对,不抵消的地方就是 fizz/发硬/发薄。**这就是"一坨屎"的量化答案。**

## ⚠️ 方法论大修正(2026-07-05 更深夜):DI-Welch 法在骗人,改用干净纯音探针

之前用"DI 实弹的 Welch 平均谱"对比 head,得出"我的 head 4-5kHz 太亮、鼓包"——**全错**。DI 是宽带+有瞬态,我的 head 对它的失真 + 数值杂散尖刺被 Welch **糊成了一个假的"亮包"**,我追了几十轮鬼影、还往"压暗"方向调(otbw 下压),方向完全反了。

**正解:干净多音探针**(`gen_probe.py` 各频率低电平纯音 → 在每个音自己的频率测增益,`measure_probe.py`/`head_probe_cmp.py`;NAM 侧用 `nam_probe.py`)。这才是 NAM 该有的测法。真相(`renders/head_probe.png`):
- **真 Princeton head**:平滑,高频 **+5dB presence 上扬**。
- **我的 head**:2-5k **滚降(偏暗,-4~-18dB)**、坑坑洼洼、**6000Hz 一根 +25dB 数值尖刺**、250Hz 低频 junk。

**即:我的 head 偏暗 + 缺 presence + 有杂散尖刺/低频 junk,不是太亮。** 而且证实 v1a **不失真(THD 0% 到 0.7V)、不谐振**(线性响应温和滚降)——之前"v1a +47dB@4800 谐振"是 DI-Welch 噪声地板伪影。**教训(第 N 次):验证音频必须用干净可控激励在被测频率上直接测,别用宽带实弹的平均谱去糊。**

新方向:①灭掉杂散尖刺(6k 等,疑似轻阻尼数值谐振)②补 3-8k presence(我的前级 HF 滚降比真机多)③修低频 junk。head_match.py(DI-Welch)弃用,改 head_probe_cmp.py。

## 头拟合迭代(2026-07-05 深夜,细分辨率谱 + 逐级探针)

工具:`head_match.py`(叠加 输入DI/NAM/我的,1/24 倍频程)、`tap_scan.py` + amp `setTapSel` 逐级探针、render_di 的 `flatload/otbw/nfbscale/otdamp/tapsel` 旗标(全默认关,不动出厂)。

**关键进展:**
- ✅ **喇叭剥出 head(flatLoad,新架构)**:head 输出改为驱动电阻性 load-box(gRef≈8Ω),喇叭全交给 cab IR。之前 vspk_ 把喇叭阻抗浴缸烤进 head、和 cab IR **重复计喇叭两遍**。修后 **低中频(100-700Hz)已经和 NAM 贴合**,之前低电平那个 ~55Hz 波形垃圾也消失。⚠ 之前 flatLoad 有 bug(geq_ 用了喇叭高频极限值 ≈160Ω 轻负载),已修成真正的 8Ω。
- ✅ **决定性对照**:把输入 DI 自己的谱画进去 → **NAM 几乎完全贴着 DI(真 Princeton Clean head ≈ 透明)**,1kHz 以上的滚降是 DI 自己的,不是 head。

**🔴 头拟合的硬卡点(精确定位,但未解)**:我的 head 在 700Hz 以上不跟 DI 滚、还在 **4-5kHz 鼓 +45dB 的包**。逐级探针一击定位:**第一个前级管 v1a 的输出就已经 4800/1k = +47dB**(输入 DI 才 -44、NAM 才 -37)。特征:level-independent(0.02 和 0.5 都 +47)、idle 静(非自激)、48k/96k 同频、**不是**阴极电容(改后向欧拉无效)、不是失真/NFB/OT/混响/重采样。单管 1 阶不该谐振——是个**没查明的 v1a 数值伪谐振**,需要冷静重查(嫌疑:Koren 管模型 HF 行为、Newton 求解器条件数、或输入路径)。**这是头拟合到 ballpark 的唯一拦路虎,低中频已经对上。**

**CAB**:合成 C10R 太暗(2k 以上狂砍),真 P10R 有 3-4.5kHz presence 峰 → 直接换真 IR。

## 修复计划(顺序不能乱)

1. **先杀 #1 功率级自激**。查 PowerStage 的 vaa bracketed-Newton + OT(Cw/漏感)+ 喇叭 LC 离散化,大概率是某个 LC 回路在离散域极点跑到单位圆外。手段:idle 零输入下逐元件看谁在振(先冻结喇叭模型、再冻结 Cw,二分定位),用梯形→TR-BDF2 或给该回路加物理阻尼。**验收:喂零 → 输出 < -60 dBFS。**
2. 修完后**重测线性频响 + 谐波谱** vs NAM,校准 #3 的 voicing。
3. **重标增益结构**(#2):调 preamp 增益/inTrim,让我的压缩曲线贴合真机(clean 到 ~0.15,平滑压缩)。
4. **接真 cab IR**:Princeton 用 P10R(SM57/R121 的 Cone/Cap 位),Plexi 用给的 Plexi cab,替掉合成 cab。
5. 全部走**双证**(耳测在你,测量在台架)后才 ship。

## 参考素材清单(pp 文件夹)

- **1964 Princeton head NAM**:Clean/Sweet Spot/Cranked × Tone 4/7 共 6 挡。
- **1964 Princeton Jensen P10R cab**:C414/R121/SM57/M160/SM94 × Cone/Cap/Edge/Distant/Rear 共 24 位。
- **Plexi 51 head NAM**:3 个(All access DI / DI#03 / 基础)。
- **Plexi cab**:2 个 IR。

## 一句话

真机第一次上桌就照出两处硬伤:**功率级在 idle 自激 ~7kHz(一直被 cab 掩盖)**,和**增益过热约 10-20 倍**;外加一个认知纠正:**head 该是平的,音色归 cab**。先杀振荡,其余才测得准。
