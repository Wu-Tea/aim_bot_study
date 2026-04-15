# Person Detector Training

这份文档对应当前的短期路线：

- 不做 detector 架构重构
- 继续使用当前 `YOLO26 detect` 推理链路
- 先用 `CrowdHuman visible-body + 你的游戏截图` 做 one-class `person` 微调

## 本阶段范围

这周只解决两件事：

1. 提升半身 / 遮挡人物的检出率
2. 降低静态物体被误检成 `person` 的概率

`WiderPerson` 暂时不混入第一版。原因很简单：现在优先统一标注语义，先把 `visible-body` 这一条线跑通。

## 推荐数据语义

统一使用 `visible-body` 语义：

- 只框当前画面里实际可见的人体区域
- 不要为了“补全全身”去硬框被掩体挡住的部分
- 对游戏截图，优先覆盖上胸到腿部的可见区域

这样和 `CrowdHuman` 的 `vbox` 更一致，也更贴近你当前“半身也要稳”的目标。

## 目录约定

### 1. CrowdHuman 原始数据

默认目录：

```text
training_data/
  raw/
    crowdhuman/
      Images/
      annotation_train.odgt
      annotation_val.odgt
```

脚本也兼容常见变体：

- `images/`
- `annotations/annotation_train.odgt`
- `Annotations/annotation_train.odgt`

### 2. 你的游戏截图数据

如果你已经自己标成 YOLO 格式，默认目录：

```text
training_data/
  raw/
    game_yolo/
      images/
        train/
        val/
      labels/
        train/
        val/
```

要求：

- `images/train/*.jpg` 对应 `labels/train/*.txt`
- `images/val/*.jpg` 对应 `labels/val/*.txt`
- 标签只有一个类别：`0`

## 脚本

### 1. 组装训练数据

默认会：

- 读取 CrowdHuman
- 使用 `visible` 框
- 如果存在 `training_data/raw/game_yolo`，自动一并合入
- 输出到 `training_data/assembled/person_detect_visible`

命令：

```powershell
python tools\prepare_person_dataset.py --force
```

如果你的目录不在默认位置：

```powershell
python tools\prepare_person_dataset.py `
  --crowdhuman-root D:\datasets\CrowdHuman `
  --game-yolo-root D:\datasets\game_yolo `
  --output-root D:\datasets\person_detect_visible `
  --box-kind visible `
  --force
```

输出目录结构：

```text
training_data/
  assembled/
    person_detect_visible/
      dataset.yaml
      images/
        train/
          crowdhuman/
          game/
        val/
          crowdhuman/
          game/
      labels/
        train/
          crowdhuman/
          game/
        val/
          crowdhuman/
          game/
```

### 2. 训练

默认底座：

- `models/yolo26n.pt`

默认训练命令：

```powershell
python tools\train_person_detector.py `
  --data training_data\assembled\person_detect_visible\dataset.yaml `
  --model models\yolo26n.pt `
  --epochs 80 `
  --imgsz 896 `
  --batch 24 `
  --name crowdhuman_visible_v1
```

说明：

- `rect=True` 已在脚本里开启，更适合当前宽屏裁切方向
- 第一版先别追太多增强参数，先看误检和半身检出是否明显改善

训练输出默认在：

```text
runs/person_train/crowdhuman_visible_v1/
```

权重一般在：

```text
runs/person_train/crowdhuman_visible_v1/weights/best.pt
```

### 3. 导出 TensorRT

命令：

```powershell
python tools\export_person_detector.py `
  --weights runs\person_train\crowdhuman_visible_v1\weights\best.pt `
  --width 896 `
  --height 512
```

这一步会导出适合当前 `896x512` 视觉裁切的 engine。

## 在当前程序里测试新模型

现在 `VisionConfig` 支持环境变量覆盖模型路径，所以你不需要先覆盖仓库里的默认模型。

示例：

```powershell
$env:VISION_MODEL_PATH="D:\work\AI\yolo-study-001\runs\person_train\crowdhuman_visible_v1\weights\best.engine"
$env:VISION_FALLBACK_MODEL_PATH="D:\work\AI\yolo-study-001\runs\person_train\crowdhuman_visible_v1\weights\best.pt"
python main.py --controller-mode gamepad --vision-debug --vision-debug-save
```

如果导出的 engine 不在 `weights/` 目录，按实际路径改即可。

## 第一版建议

建议你第一版只做这件事：

- `CrowdHuman visible-body`
- 加你自己的少量高质量游戏截图
- 跑出一个 `best.pt`
- 先用 debug 窗口观察“静物误检”和“半身漏检”是否改善

只要这一步有效，下周再决定：

- 是否混入 `WiderPerson`
- 是否换到 `RT-DETR`
- 是否对 detector 做独立后端重构
