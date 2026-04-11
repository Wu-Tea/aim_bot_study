# Vision Pipeline 性能优化方案（交付给 Codex 执行）

目标：降低 `process_vision` 主循环的端到端延迟，重点是 `infer_ms` 和 `post_ms` 两段。硬件 4070 Super + 13600KF，模型 `yolo26n-pose.engine`（TRT FP16 静态 batch=1 @ 640×640）。

---

## 背景代码结构（必读）

- 入口：`main.py` → `process_vision(controller)`（在 `vision.py` 文件尾部）
- 捕获线程：`ScreenCaptureThread`（dxcam video_mode，内部已限速 `target_fps`）
- 主循环流程：
  1. 等新帧（`capture_wait_ms`）
  2. `model.predict(source=frame, **predict_kwargs)`（`infer_ms`）
  3. `_extract_detections(results)` → `list[ParsedDetections]`（属于 `post_ms`）
  4. `target_selector.find_best_target(detections, frame)`（`post_ms` 一部分）
  5. `rb_hit_detector.update(detections)`（`post_ms` 一部分）
  6. 下发给 `controller.update(dx, dy)` / `controller.set_auto_rb(...)`

- `ParsedDetections` 数据结构（`vision.py` 顶部）：
  ```python
  @dataclass(slots=True)
  class ParsedDetections:
      boxes: np.ndarray        # shape (N, 4) xyxy, float32
      confs: np.ndarray        # shape (N,)   float32
      keypoints: np.ndarray | None  # shape (N, 17, 3) (x, y, conf)，COCO keypoint
  ```

**硬性约束**：下游的 `TargetSelector.find_best_target` 和 `CrosshairPersonHitDetector._fire_box_for` 都只认这个契约。**任何优化都必须保持 `_extract_detections` 的返回形态不变**（或者同时修改下游）。本方案前 6 步都**不修改下游**，只动 infer/前处理/后处理管线。

---

## Step 0：先取 baseline（必做，不能跳）

```
set VISION_PERF_LOG=1
set VISION_BENCH=1
python main.py --controller-mode gamepad
```

记下两组数：

1. 启动阶段的 `[Vision] warmup predict bench over 30 iters: p10=... median=... p90=...`
   这是**纯 engine 推理下限**。
2. 运行阶段每 2 秒打一次的 `[Perf] loop=... FPS | wait=... | infer=... | post=... | boxes=...`
   其中 `infer` 是实际 `model.predict()` 壳的耗时。

**`infer` 和 bench `median` 的差值 ≈ Ultralytics wrapper 开销**。这是本方案最大的优化空间。

把 baseline 数据写在 PR 描述或提交 message 里，每完成一步也跑一次 perf log 并对比。**没有量化数据就不要进入下一步**。

---

## Step 1：重新导出 engine，尝试把 NMS 烧进图里

### 改动
编辑 `export_trt.py`：

```python
model.export(
    format="engine",
    half=True,
    imgsz=640,
    batch=1,
    workspace=8,
    simplify=True,
    device=0,
    nms=True,      # 新增
    opset=17,      # 新增
)
```

跑一次 `python export_trt.py`，覆盖生成 `yolo26n-pose.engine`。

### 失败回退
- `nms=True` 对 pose 模型的支持依赖 Ultralytics 版本。如果导出报错，**去掉 `nms=True`** 保留其它改动重新导出。
- `opset=17` 不兼容时降回默认。

### 预期收益
1–3 ms / frame（省掉 Python 侧 NMS 调度）。如果 `nms=True` 被禁用，本步收益可能仅 0.2–0.5 ms。

### 验证
跑一次 perf log，对比 `infer`。

---

## Step 2：dxcam 直接输出 RGB

### 为什么
Ultralytics 的 `predict()` 内部会把 BGR 转成 RGB 喂模型，这是一次 640×640 的 channel swap。dxcam 可以直接吐 RGB，省掉这一步。

### 改动

**`vision.py` 里 `ScreenCaptureThread.__init__`**：

```python
self.camera = dxcam.create(output_color="RGB", region=self.region)  # BGR -> RGB
```

**`vision.py` 里 `TargetSelector.find_best_target`**，找到颜色 ROI 部分：

```python
roi_hsv = cv2.cvtColor(roi_bgr, cv2.COLOR_BGR2HSV)
```
改成：
```python
roi_hsv = cv2.cvtColor(roi_bgr, cv2.COLOR_RGB2HSV)
```

### 不需要改
HSV 的 H 通道数值与输入是 RGB 还是 BGR 无关（OpenCV 的 `COLOR_*2HSV` 都输出同一个 HSV 空间，H∈[0,180]）。因此 `LOWER_GREEN / UPPER_GREEN / LOWER_BLUE / UPPER_BLUE / LOWER_YELLOW / UPPER_YELLOW / LOWER_RED1 / UPPER_RED1 / LOWER_RED2 / UPPER_RED2` **全部保持原值**。

### 预期收益
0.5–1.5 ms / frame。

### 验证
运行 vision，确认：
- 正常检测到人（box 数 > 0）
- 颜色 ROI 逻辑仍然区分友军/敌方（可以在 find_best_target 里临时加一行 `print` 确认 friendly_count 和 enemy_count 的分布没有反转）

---

## Step 3：`predict_kwargs` 瘦身

### 改动
`vision.py` 的 `process_vision()` 中：

```python
predict_kwargs = {
    "classes": list(config.classes),
    "conf": config.conf,
    "verbose": False,
    "half": config.half,          # ← 删掉（engine FP16 下是 no-op）
    "device": config.device,
    "imgsz": config.crop_size,
    "max_det": 10,
    "agnostic_nms": False,        # ← 删掉（默认值）
}
```

### 预期收益
< 0.1 ms，主要是让代码干净。

---

## Step 4：绕过 Ultralytics `predict()` 外壳（★最大收益）

本步是整个方案的核心。`model.predict()` 每次调用的开销主要来自：autograd guard、letterbox 逻辑、tensor 分配和 `.to('cuda').half()`、Results 对象构造、verbose 分支、多个 callback hook。engine 本身只占其中很小一部分。

### 思路
- 保留 Ultralytics 的模型加载（`YOLO(...)` 方便解决 engine 路径、FP16、device 等细节）
- **手写 forward 调用 + 最小 NMS**，直接在主循环里用

### 子步骤

#### 4.1 `_warmup_model` 末尾新增 GPU 输入缓冲

```python
import torch
# 模块级，或作为 process_vision 的 local 变量，供热循环复用
GPU_INPUT = torch.empty((1, 3, 640, 640), dtype=torch.float16, device="cuda")
```

#### 4.2 新建 `_fast_predict(model, frame_rgb_uint8, gpu_input)`

替代 `model.predict(source=frame, **predict_kwargs)` 的调用：

```python
def _fast_predict(model, frame_rgb, gpu_input):
    """
    frame_rgb: np.ndarray HWC uint8, shape (640, 640, 3), RGB (Step 2 已保证)
    gpu_input: 预分配的 (1, 3, 640, 640) fp16 cuda tensor
    returns: list[ParsedDetections]  — 保持既有契约
    """
    # 1) HWC uint8 -> CHW fp16 /255，一步到 GPU buffer
    #    使用 torch.from_numpy 后 non_blocking 拷贝
    t = torch.from_numpy(frame_rgb).to(device="cuda", non_blocking=True)
    t = t.permute(2, 0, 1).unsqueeze(0).to(dtype=torch.float16) / 255.0
    gpu_input.copy_(t)

    # 2) 直接走 AutoBackend forward
    with torch.inference_mode():
        raw = model.model(gpu_input)  # pose 模型返回的 tuple / tensor

    # 3) 自己做 NMS + keypoint 解码
    return _tiny_nms_pose(raw, conf_thr=0.50, max_det=10)
```

#### 4.3 实现 `_tiny_nms_pose`

**这一步 Codex 必须先 dump 一次 `raw` 的结构再动手**：

```python
# 先插一段临时代码：
with torch.inference_mode():
    raw = model.model(gpu_input)
print("RAW TYPE:", type(raw))
if isinstance(raw, (tuple, list)):
    for i, x in enumerate(raw):
        print(f"  raw[{i}] type={type(x)} shape={getattr(x, 'shape', None)}")
else:
    print("  shape:", raw.shape)
```

pose 模型通常返回 `(pred, [aux])`，其中 `pred` shape 约为 `(1, 56, 8400)`：
- 前 4 行 = cx, cy, w, h
- 第 5 行 = person class score（单类）
- 后 51 行 = 17 个关键点 × (x, y, conf)

实现细节（参考 Ultralytics 源码 `ultralytics/models/yolo/pose/predict.py` → `PosePredictor.postprocess`）：

1. `pred = raw[0] if isinstance(raw, (tuple, list)) else raw`（取主输出）
2. `pred = pred[0].transpose(0, 1)`  # → (8400, 56)
3. 按 `conf >= 0.50` 过滤（注意这里没有 objectness，直接是 class score）
4. 剩余候选按 conf 排序，截到前 K（比如 K=50）减少 NMS 负载
5. `xywh -> xyxy`
6. `torchvision.ops.nms(boxes, scores, iou_threshold=0.45)` → 取前 `max_det=10`
7. `keypoints` 需要从剩余 56 通道里抽 51 个，reshape 成 `(n, 17, 3)`
8. 最后 `.cpu().numpy()` 一次，组装成 `ParsedDetections`

**坐标空间**：因为我们输入的 GPU buffer 就是 640×640 原图，**不需要 letterbox 反变换**（Ultralytics 的 predict 会做这一步，我们绕过了它，但也不需要它，因为 dxcam 捕获的 region 已经是 640×640）。

#### 4.4 主循环替换

`process_vision` 的推理那段：
```python
results = model.predict(source=frame, **predict_kwargs)
```
改为：
```python
detections = _fast_predict(model, frame, GPU_INPUT)
```

同时删掉 `_extract_detections(results)` 调用（`_fast_predict` 直接返回 `list[ParsedDetections]`），以及整个 `_extract_detections` 函数。

### 预期收益
3–6 ms / frame（这是最大的一块）。

### 失败回退
如果 `_tiny_nms_pose` 的坐标与 `find_best_target` 对不上（比如 box 位置飘了），**第一反应是检查 3 件事**：
1. `pred[0].transpose(0, 1)` 的维度顺序是否正确
2. xywh→xyxy 的换算是否把中心点当成角点了
3. 关键点是不是按 `[x, y, conf, x, y, conf, ...]` 排列的（不同版本可能不一样）

如果解决不了，保留 Step 1–3，**只回滚 Step 4**，继续 Step 5。

### 验证
- 检测到的 box 数量与旧路径一致（跑同一段游戏，boxes_seen 均值误差 < 5%）
- AI 能像 Step 3 之前一样拉到目标
- perf log 的 `infer` 显著下降

---

## Step 5：INT8 量化（可选，第二大收益）

仅在 Step 4 完成后仍觉得 infer 不够快时执行。

### 5.1 采集校准图
在 `vision.py` 的 `ScreenCaptureThread.run()` 临时加一段：

```python
import os as _os
_dump_dir = _os.getenv("VISION_DUMP_FRAMES")
_dump_every = 10  # 每 10 帧存 1 张
_dump_counter = 0
```

循环里：
```python
if _dump_dir:
    _dump_counter += 1
    if _dump_counter % _dump_every == 0:
        _idx = _dump_counter // _dump_every
        cv2.imwrite(f"{_dump_dir}/frame_{_idx:04d}.png", frame)
```

然后：
```
mkdir calib
set VISION_DUMP_FRAMES=calib
python main.py --controller-mode gamepad
```
玩 3–5 分钟，采到 300–500 张，停掉。把这段代码**再删掉**（不要留在正式代码里）。

### 5.2 `calib.yaml`

```yaml
path: D:/work/AI/yolo-study-001/calib
train: .
val: .
nc: 1
names: ['person']
```

### 5.3 新建 `export_trt_int8.py`

```python
from ultralytics import YOLO

def export_int8():
    print("[Export] INT8 量化导出开始...")
    model = YOLO("yolo26n-pose.pt")
    model.export(
        format="engine",
        int8=True,
        data="calib.yaml",
        imgsz=640,
        batch=1,
        workspace=8,
        simplify=True,
        device=0,
    )
    print("[Export] 完成")

if __name__ == "__main__":
    export_int8()
```

跑一次，会产出 `yolo26n-pose.engine`（覆盖 FP16 的）。**先备份 FP16 engine**：

```
copy yolo26n-pose.engine yolo26n-pose_fp16.engine
```

### 5.4 验证

- `infer_ms` 应再降 1–3 ms
- 关键点置信度可能整体下降 0.05–0.10：观察 `VISION_KPT_CONF` 默认 0.40 下是否还能稳定用肩膀中点
- 如果肩膀 keypoint 大量低于 0.40，**把 `VISION_KPT_CONF=0.30`** 加到启动 env

### 失败回退
精度掉太多（打起来准星都找不到目标），恢复：
```
copy yolo26n-pose_fp16.engine yolo26n-pose.engine
```

---

## Step 6：流水线化（可选，延迟优化）

仅在前 5 步都完成后、且 `infer_ms > post_ms` 时做。

### 思路

当前时序（单线程）：
```
[capture_wait][infer][post][capture_wait][infer][post]...
```

目标时序（两个后台线程）：
```
capture  : [frame0][frame1][frame2]...
infer    :        [forward0][forward1]...
main     :                [post0][post1]...
```

### 改动概览

在 `vision.py` 新建 `InferenceThread`，结构类似 `ScreenCaptureThread`：

- 持有 `capture_thread` 和 `model` 引用
- `run()` 里循环：
  1. `frame, fid = capture_thread.get_latest_frame(last_seen_id=self._last_seen, timeout=0.1)`
  2. 若 `frame is None` 就 `continue`
  3. `detections = _fast_predict(model, frame, gpu_input)`
  4. 写入 `(fid, detections)` 到一个容量 1 的 slot（加锁 + condition）
  5. `self._last_seen = fid`
- 对外暴露 `get_latest_detections(last_seen_id, timeout)`

`process_vision` 主循环不再做 predict，只消费 `InferenceThread.get_latest_detections(...)`，然后跑 `find_best_target` / `rb_hit_detector.update`。

### 注意事项
- `gpu_input` 要属于 `InferenceThread`（不能跨线程共享同一个 cuda tensor 做写操作，否则得加锁）
- `InferenceThread` 的停止在 `process_vision` 的 `finally` 里
- `controller.is_aiming()` 退出时，`InferenceThread` 仍然可以 idle（`time.sleep(0.01)`），不必停掉，省去重新 warmup

### 预期收益
端到端延迟下降 `min(infer, post)`，即把原来串行的两段部分重叠。典型情况下收益 1–3 ms 延迟（不是 throughput）。

### 风险
- GIL：TRT forward 时释放 GIL，numpy/OpenCV 也释放，所以 Python 线程可以并行
- 同步抖动：用 condition variable 而不是 polling

---

## 执行顺序总表

| 步 | 动作 | 预期收益 | 风险 |
|---|---|---|---|
| 0 | 取 baseline perf log | — | — |
| 1 | `export_trt.py` 加 `nms=True, opset=17` 重导出 | 1–3 ms | 低 |
| 2 | dxcam `output_color="RGB"` + HSV 转换同步改 | 0.5–1.5 ms | 极低 |
| 3 | `predict_kwargs` 瘦身 | ≈ 0 | 无 |
| 4 | **bypass predict() + 预分配 GPU buffer + `_tiny_nms_pose`** | **3–6 ms** | 中 |
| 5 | INT8 量化（含采集校准图） | 1–3 ms | 中 |
| 6 | 流水线化 InferenceThread | 1–3 ms 延迟 | 中 |

**每一步都要跑 `VISION_PERF_LOG=1` 对比前后数据**，否则不准进入下一步。

---

## 不做的事情（避免浪费时间）

- ❌ 降低 imgsz 到 480：远距离目标识别严重劣化，收益 1 ms 不值得
- ❌ 换 yolo26s：反方向，慢 2–3 倍
- ❌ 把颜色 HSV 检测移走或向量化：ROI 总面积 < 5000 像素，耗时亚毫秒
- ❌ `find_best_target` 向量化：`max_det=10` 下 Python 循环 < 0.1 ms
- ❌ 盲目调高 `target_fps`：受显示器刷新率约束
- ❌ 动 `gamepad_controller.py`：摇杆层已经调好，不要碰

---

## 交付物检查清单

完成时应当有：
- [ ] `PERF_PLAN.md` 上每一步都有「前后对比数据」标注（写在 commit message 或 PR 里）
- [ ] `export_trt.py` 更新（Step 1）
- [ ] `vision.py` dxcam RGB + HSV 改动（Step 2）
- [ ] `vision.py` predict_kwargs 瘦身（Step 3）
- [ ] `vision.py` `_fast_predict` + `_tiny_nms_pose` + 主循环接入（Step 4，**核心**）
- [ ] `_extract_detections` 被删除或被 `_fast_predict` 内联替代
- [ ] `ParsedDetections` 契约保持不变，`TargetSelector` 和 `CrosshairPersonHitDetector` **一行没动**
- [ ] （可选）`export_trt_int8.py` + `calib.yaml` + `yolo26n-pose_fp16.engine` 备份
- [ ] （可选）`InferenceThread` 实现 + 主循环消费端改写

每完成一个 Step，跑一次带 `VISION_PERF_LOG=1` 的游戏实测（≥ 30 秒），把 `loop FPS / infer / post` 三项记到提交信息里。
