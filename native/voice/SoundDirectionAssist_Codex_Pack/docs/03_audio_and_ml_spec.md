# 音频、DSP 与机器学习规范

## 1. 内部音频规范

- 48,000 Hz；
- 双声道；
- float32，范围约 `[-1, 1]`；
- interleaved `[L0, R0, L1, R1, ...]`；
- 单调 frame index；
- 输入为 mono 时复制声道仅供检测，方向必须标记 unavailable，不能伪造 stereo。

## 2. 预处理顺序

1. 解码/样本类型转换；
2. 声道布局映射；
3. 重采样；
4. DC removal（可选）；
5. **共享**增益或响度归一化；
6. 分帧和窗函数；
7. 计算时频特征；
8. 检测；
9. 在检测窗内估计方向；
10. 融合与平滑。

禁止分别对 L/R 做 peak/RMS normalization，因为会抹掉 ILD。

## 3. 基线方向特征

### 3.1 宽带 ILD

```text
rms_L = sqrt(mean(L^2) + eps)
rms_R = sqrt(mean(R^2) + eps)
ILD_dB = 20 * log10(rms_L / rms_R)
```

首版阈值不得写死在算法中，应配置并由 synthetic test 校准：

- `ILD_dB < -T` -> left 或 right 的符号需通过固定声道约定验证；
- `|ILD_dB| <= T` -> center；
- 能量低或冲突 -> unknown。

注：代码必须用测试明确“左声道更强代表 Left”，避免符号反转。

### 3.2 频带 ILD

对 4-8 个宽频带分别计算 ILD，输出 median、方差和一致性。若不同频带方向冲突，降低置信度。

### 3.3 GCC-PHAT / ITD

```text
G(f) = X_L(f) * conj(X_R(f))
R(f) = G(f) / (abs(G(f)) + eps)
r_xy(tau) = IFFT(R(f))
tau_hat = argmax r_xy(tau), tau in allowed_range
```

默认搜索范围可从约 `+/-1.2 ms` 开始，但必须通过具体游戏输出校准。GCC-PHAT 只能作为统计特征，不能单独声称解决前后方向。

### 3.4 置信度

置信度至少综合：

- 检测概率；
- 窗口能量/SNR；
- ILD 符号稳定性；
- 频带一致性；
- GCC 峰值与次峰比；
- 时间平滑后的稳定帧数。

低于阈值时输出 `Unknown` 或 suppress。

## 4. 基线事件检测

M3 允许三种 baseline：

1. 用户提供时间标记，仅验证方向；
2. 模板 log-mel cosine similarity；
3. 简单 onset/能量门控 + 类别模板。

模板系统必须：

- 支持多个正模板和负模板；
- 记录采样率/音频模式；
- 共享归一化；
- 有 cooldown 和 hysteresis；
- 输出概率或经校准的相似度，而不是布尔黑盒。

## 5. ONNX 模型规范

### 5.1 输入

推荐首个模型：

- 窗口 0.25-0.50 s；
- 64 或 80 mel bins；
- 特征通道：`logmel_L`、`logmel_R`、`logmel_L-logmel_R`；
- 可选频带 ILD/相位统计作为额外向量；
- batch = 1；
- 明确输入 tensor 名称、shape、归一化均值/方差。

### 5.2 输出

```text
class_logits: [1, num_event_classes + background]
direction_logits: [1, num_bins + unknown]
optional_quality: [1, 1]
```

模型 metadata 必须包含：

- schema version；
- sample rate、window、hop；
- class/bin labels；
- feature config hash；
- training dataset version；
- license/usage rights；
- calibration temperature 或阈值。

### 5.3 推理约束

- CPU provider 是默认；
- `SDA_ENABLE_ONNX=OFF` 时可构建；
- session 创建在启动阶段，不在热路径；
- 输入/输出 buffer 复用；
- 捕获所有 ORT 异常并转为 degraded；
- 不在运行时下载模型。

## 6. 数据集规范

### 6.1 清单字段

参见 `schemas/dataset_manifest.schema.json`。每个 clip 至少有：

- `clip_id`、`audio_path`；
- `event_class`；
- `direction_bin` 或 `unknown`；
- `session_id`、`split_group`；
- `audio_profile`（HRTF/动态范围/系统增强）；
- `rights`；
- `sha256`。

### 6.2 采集

- 方向均衡；
- 正/负样本比例可控；
- 包含音乐、语音、环境音和重叠干扰负样本；
- 按 session/map/profile 分组划分 train/val/test；
- 不把同一次录制相邻切片分到不同 split；
- 不提交未经授权游戏素材到公共仓库。

### 6.3 增强

可用：共享 gain、共享 EQ、共享噪声、轻度时间偏移。
谨慎：独立声道 gain、独立相位、stereo swap、强混响，这些会改变标签。
使用 stereo swap 时必须同步反转方向标签。

## 7. 评估

### 检测

- precision、recall、F1；
- false positives/min；
- event-based onset tolerance；
- 按 profile 和背景类型分层。

### 方向

- 3/4/8-way confusion matrix；
- left/right sign accuracy；
- macro F1；
- unknown coverage 与 accepted accuracy；
- 可选 circular MAE（只有连续角度标签时）。

### 延迟

记录：capture timestamp、feature-ready、inference-complete、snapshot-published、rendered。至少输出 p50/p95/max。

## 8. 多声源规则

MVP 假设一个分析窗内只有一个主目标事件。若模型或特征显示：

- 双峰；
- 频带方向强冲突；
- 两个类别同时高概率；

则输出 unknown/suppress，不尝试虚假的多目标定位。多声源分离是独立研究项目。
