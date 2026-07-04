# Reference · 电路 → 代码 食谱

把常见的电路 block 直接对应到"代码里怎么算"。Yeh 和工程师干活时查这本。
**每个条目都标了物理来源(喂给铁律一)+ 给 Leo 的动画比方。**

## 目录
1. RC 低通(砍高频 / 亮度控制)
2. RC 高通 / 耦合电容(砍低频 / 去直流)
3. 二极管钳位(overdrive / distortion 的破音)
4. 三极管增益级(胆机 preamp 一级)
5. Tone Stack(Fender/Marshall 三段 EQ)
6. Cabinet / 喇叭(用 IR 卷积,不硬建模)
7. 延时线(delay / chorus / flanger 的核心)

---

## 1. RC 低通(砍高频)

**电路**:一个电阻 R 串联,后面一个电容 C 对地。
**声音职责**:滤波(线性)。把高于截止频率的部分逐渐衰减 → 变暗。
**物理来源**:截止频率 `f = 1 / (2π·R·C)`。例:R=10kΩ, C=1nF → f ≈ 15.9kHz。
**代码**(一阶,便宜):
```cpp
// coeff 由 f 和采样率算出(双线性变换),不是凑的 → 铁律一 OK
float lp = 0.f;                       // 状态(记忆上一帧)
float process(float x) {
    lp += coeff * (x - lp);           // 朝新值滑一点,coeff 越小越暗
    return lp;
}
```
**给 Leo**:就是**对一串数做平滑**。coeff 小 = 平滑力度大 = 把毛刺(高频)磨掉。和你给动画曲线做 smooth 一模一样。

---

## 2. RC 高通 / 耦合电容(砍低频、去直流)

**电路**:电容 C 串联,后面电阻 R 对地。
**声音职责**:滤波(线性)。砍掉低于截止频率的部分 + 挡掉直流偏置。
**为什么重要**:几乎每一级增益之间都有耦合电容。**Tube Screamer 的性格("紧、中频突出")就是靠钳位前一个高通先砍低频**——Merlin 会反复提到它。
**代码**:
```cpp
float prevIn = 0.f, prevOut = 0.f;
float process(float x) {
    float y = coeff * (prevOut + x - prevIn); // 只让"变化"通过,挡住慢速漂移
    prevIn = x; prevOut = y;
    return y;
}
```
**给 Leo**:只保留"抖动 / 快速变化",挡掉"整体缓慢位移(直流)"。像动画里只留 jitter、去掉整体平移。

---

## 3. 二极管钳位(破音来源)⚠ 非线性

**电路**:两个二极管反并联(或对地),把信号顶削平。
**声音职责**:削波(非线性)→ 产生失真谐波。**必过采样。**
**两种情况(Yeh 会判)**:
- **无记忆**(纯二极管对地,不与电容耦合)→ **查表(waveshaper)**:
  ```cpp
  // 离线把"输入电压→输出电压"曲线烘成表,运行时查+插值
  float shape(float x) { return lut.lookup(x); } // 曲线形状来自二极管方程,非凑
  // ⚠ 外面必须包过采样(见 dsp-methods.md),否则刺耳混叠
  ```
- **有记忆**(钳位与反馈电容耦合,如真实 TS)→ **state-space/WDF + 牛顿迭代**(见 dsp-methods.md)。
**物理来源**:二极管方程用数据手册参数(1N4148: Is=2.52nA, n=1.75)。软/硬取决于二极管类型(硅硬、锗软、LED 更硬更亮)。
**给 Leo**:硬钳位 = 把值 `clamp(x, -1, 1)`,超了直接压平,棱角硬 → 刺;软钳位 = 越接近上限越使不上劲(ease-out),棱角圆 → 暖。**这就是二极管失真和胆机失真手感差别的根。**

---

## 4. 三极管增益级(胆机 preamp)⚠ 非线性

**电路**:一颗三极管(如 12AX7 半边)+ 阳极电阻 + 阴极电阻 + 阴极旁路电容。
**声音职责**:放大 + 软削波 + 塑形(三合一,所以胆机一级顶三件事)。
**关键声音元件**:
- 阴极旁路电容 → 决定这级放大多少低频(bypass 越多,低频增益越高)。
- 三极管的转移曲线**天生不对称** → 偶次谐波 → "暖"。这是胆机音色的物理根,别用对称曲线糊弄。
**方法**:非线性带记忆 → state-space + 牛顿迭代(见 dsp-methods.md)。曲线来自三极管模型(Koren 模型是业界常用近似,参数可查)。
**给 Leo**:一个 modifier,把曲线拉高 + 顶部 ease-out 压软 + 顺便改了频率平衡。一级干三件事,所以胆机"一级就有味"。

---

## 5. Tone Stack(三段 EQ)

**电路**:Fender/Marshall/Vox 各有经典拓扑,一堆 R、C + 三个电位器(bass/mid/treble)。
**声音职责**:滤波(线性),但**三个旋钮互相耦合**——动 treble 会影响 mid,这是真实 tone stack 的灵魂,必须还原。
**方法**:整个 tone stack 是个线性网络 → 可以整体解成一个**随旋钮变化的 biquad**(系数是三个旋钮位置的函数)。经典 tone stack 的系数公式是公开的(Duncan Amps 的 Tone Stack Calculator 是标准参考)。
**⚠ 常见错误**:用三个独立的 EQ band 去凑 → **错**,丢了耦合,走向不对(金耳朵会立刻听出来)。必须解真实网络。
**给 Leo**:三个 EQ 滑块被一根橡皮筋连着,拉一个另外俩会动。不能当三个独立滑块做。

---

## 6. Cabinet / 喇叭 → 用 IR 卷积(不硬建模)

**为什么不硬建模**:喇叭 + 箱体 + 拾音位置的频响极其复杂,但**基本是线性的** → 用一段脉冲响应(impulse response, IR)卷积就能高保真还原,这是**全行业标准做法**(比硬建模又准又省)。
**方法**:拿到目标 cab 的 IR(.wav),做**分块卷积(partitioned convolution)**保证低延迟。
**代码**:用现成的 FFT 卷积(工程师会写分块 FFT 卷积,或直接用库)。
**给 Leo**:IR = 给这个箱体"拍一张频率指纹",然后把你的信号盖上这张指纹。像给画面套一个 LUT / 一张环境反射贴图 —— 一次采样,处处套用。

---

## 7. 延时线(delay / chorus / flanger / reverb 基石)

**电路/原理**:把信号存进一个环形缓冲区(circular buffer),过一段时间再读出来。
**声音职责**:
- 固定长延时 + 反馈 → **delay / echo**。
- 很短(1–20ms)且延时量被 LFO 调制 → **chorus / flanger**。
- 一堆延时线 + 反馈网络 → **reverb**(如 Schroeder / FDN 结构)。
**关键**:读取位置是**小数**时要插值(线性 / 全通插值),否则调制会有杂音。
**代码**:
```cpp
std::vector<float> buf;  // prepare() 里分配好,audio thread 不再分配!
int writePos = 0;
float process(float x, float delaySamples) {
    buf[writePos] = x;
    float readPos = writePos - delaySamples;    // 可能是小数 → 要插值
    float y = interpolate(buf, readPos);        // 线性/全通插值
    writePos = (writePos + 1) % buf.size();
    return y;
}
```
**给 Leo**:一条"录音磁带回环"。写头一直写,读头在后面某个距离读;读头离写头远近 = 延时长短;读头位置被 LFO 晃 = chorus。BBD 老 chorus 那种"模拟味",就是磁带 + 一点点噪声和高频损失。

---

## 用法提示

- **线性 block(1、2、5、6、7 的骨架)**:直接查表算系数,便宜稳当,不用过采样。
- **非线性 block(3、4)**:必走 Yeh 的流程定方法 + 过采样,再交测量台用谐波谱验证够不够。
- 拿不准某个 block 是线性还是非线性 → 回去问 Merlin 贴标签。
