# 低延迟 FPS 2D Target Memory / Tracker 方案

## 1. 目标与边界

输入来自 YOLO/TensorRT detector：

- target dx/dy
- body box
- confidence
- target tier / class
- capture timestamp 与 detector ready timestamp

输出给 controller：

- ADS snap / bodylock 使用的 target snapshot
- recoil-aware 的 2D screen-space projection
- observed-only fire authority

系统只做 **2D screen-space tracking**，不做 3D 重建。tracker 的作用是短期记忆、detector latency compensation、missing frame coasting、候选关联和稳定 controller 输入。

最重要的安全/正确性约束：

```text
predicted-only target 可以辅助低增益 bodylock，但不能触发 fire authority。
```

---

## 2. 坐标系统

建议使用一个 screen-angle-like 的 2D 坐标，而不是直接在 raw pixel 里保存 Kalman state：

```text
track_x = (screen_x - center_x) / focal_x(mode)
track_y = (screen_y - center_y) / focal_y(mode)
```

它仍然是 2D screen-space，不是 3D。好处是 ADS/FOV/zoom 变化时不会把画面缩放误认为目标突然加速。没有真实 FOV 时，可以用不同 ADS/zoom mode 的经验 scale 替代。

对于 detector observation：

```text
p_det = screenToTrack(det.bodyCenterPx, mode_at_capture)
z     = p_det + E(capture_time)
```

其中 `E(t)` 是 cumulative ego displacement。静止目标满足：

```text
p_screen_like(t) = compensated_coord - E(t)
```

controller 查询时：

```text
p_pred = c_pred - E(query_time)
screen_error_px = trackToScreen(p_pred, mode_at_query)
```

---

## 3. Track state 字段

推荐跟踪 **body center / body box**，不要直接把 head/target aim point 当作 identity anchor。aim point/tier 可以作为 controller anchor，但不要污染 body center velocity。

建议字段：

```cpp
TrackId id;
TrackLife life;             // Tentative / Confirmed / Coasting / Lost
int ageFrames;
int hitStreak;
int missStreak;
int totalHits;

TimeSec filterTime;
TimeSec lastObsCaptureTime;
TimeSec lastObsReadyTime;
uint64_t lastObsFrameSeq;
uint64_t lastMatchedDetectionId;

// Kalman state: [cx, cy, vx, vy] in ego-compensated track coordinate
KalmanCv2d filter;

Vec2 boxSizePx;             // EWMA width/height
Vec2 aimOffsetTrack;        // aim point relative to body center
TargetTier tier;
TargetClass cls;

float detConfEwma;
float assocQualityEwma;
float innovationEwma;
float ambiguity;
float switchRisk;
float confidence;

bool observedInLatestVisionFrame;
bool hasDirectObservationToken;
AssistAuthority assistAuthority;
FireAuthority fireAuthority;
```

生命周期建议：

```text
Tentative:  1 次命中，低 gain，不给 fire authority
Confirmed:  2/3 或 3/5 vision frames 命中
Coasting:   短时间 missing，可低 gain bodylock，不给 fire authority
Lost:       超过 max coast time 或离开 ROI 太久
```

---

## 4. Filter 选择

推荐 **constant-velocity Kalman filter**：

```text
x = [cx, cy, vx, vy]^T
```

预测：

```text
cx' = cx + vx * dt
cy' = cy + vy * dt
vx' = vx
vy' = vy
```

优点：

- 足够快，适合 100–160Hz 控制循环。
- 能处理 variable dt 和 detector latency。
- 有 covariance `P`，association gating、confidence、authority 可以使用同一套 uncertainty。
- missing frames 时 covariance 自然变大。

alpha-beta filter 可作为简化 baseline；纯 CV extrapolation 只适合作 baseline，不建议作为主 tracker。

---

## 5. Detector latency 和 missing frames

canonical track state 不要每个 controller tick 永久推进到 now。建议：

```text
track.filterTime = last processed vision capture_time
```

vision frame 到来：

```text
1. predict track 到 frame.captureTime
2. 用该 capture_time 的 detection 做 association/update
3. track.filterTime = frame.captureTime
```

control tick 查询：

```text
1. copy filter
2. predict copy 到 query_time 或 control_horizon
3. subtract E(query_time)
```

missing frame：

```text
missStreak += 1
observedInLatestVisionFrame = false
hasDirectObservationToken = false
fireAuthority = None
confidence *= exp(-dt / tau_miss)
```

coasting 策略：

```text
0–1 vision gap:    可继续低/中 gain aim assist，无 fire
1–3 vision gaps:   low gain bodylock only，无 fire
> 50–100 ms:       no assist 或 very low reacquire prior
> 150–250 ms:      kill track
```

---

## 6. Detector box 到 track association

association 使用 body center + box，不使用 aim point/tier 作为主要 identity。

每个 vision frame：

```text
tracks_i predicted at frame.captureTime
detections_j transformed into compensated track coordinate
```

pair gating：

```text
innovation y = z - Hx
S = HPH^T + R
d2 = y^T S^-1 y
```

hard gate：

```text
d2 < chi_square_gate_2d
pixel_distance < max_gate_cap
class compatible
size jump not impossible
```

cost：

```text
cost =
    w_pos    * clamp(d2 / gate_d2, 0, 1)
  + w_iou    * (1 - IoU(pred_box, det_box))
  + w_size   * abs(log(det_area / pred_area))
  + w_aspect * abs(log(det_aspect / pred_aspect))
  + w_tier   * tierMismatchPenalty
  - w_conf   * det.confidence
  + switchPenalty
  - currentTargetHysteresisBonus
```

少量目标可用 gated greedy；拥挤场景可替换为 Hungarian。代码包里使用 deterministic gated greedy，接口上可以替换。

防止 prediction 锁死：

```text
1. prediction 只影响 association cost，不隐藏 detector candidate
2. unmatched high-confidence detection 永远可 spawn tentative track
3. current target hysteresis 有上限
4. high innovation 会降低 confidence / 增加 switchRisk
5. ambiguous association 降低 authority
6. no detection matched => no fire authority
```

---

## 7. final stick output 的 ego-motion compensation

定义 cumulative ego displacement：

```text
E(t) = E_stick(t) + E_recoil_visual(t)
```

right stick 正方向约定为 camera/reticle 正方向。静止目标：

```text
p(t) = c - E(t)
```

因此 detector measurement：

```text
z = screenToTrack(body_center_px) + E(capture_time)
```

controller query：

```text
screen_position = predicted_compensated_position - E(query_time)
```

必须使用 **final right stick output**：

```text
raw aim command
+ recoil compensation
+ clamp / mix / anti-deadzone / saturation
= finalRightStick
```

只有 finalRightStick 才是真正影响下一帧 screen projection 的控制输入。

需要校准：

```text
deadzone
anti-deadzone
axis gain
ADS gain
zoom/FOV scale
stick response exponent
diagonal saturation
game acceleration/ramp
device-to-image delay
```

---

## 8. Recoil visual kick 与 recoil compensation

必须分开：

### recoil compensation

controller 加到 stick 的控制量。它已经进入 finalRightStick，因此已经通过 `E_stick(t)` 影响 tracker。不要重复加。

### recoil visual kick

游戏开火造成的视觉/camera impulse，可能不完全由 stick 解释。它作为额外项：

```text
E_recoil_visual(t) = sum RecoilProfile(shot_time, shot_index, weapon, ads)
```

如果某游戏的 recoil 只是 weapon model 动画，不移动 target world projection，则该项应为 0。用 shot-aligned residual 验证：

```text
residual(t) = measured_compensated_pos - predicted_compensated_pos
```

若 recoil model 正确，开火后 residual 不应有系统性偏移。

开火后短窗口内：

```text
1. 增大 Q / R
2. 降低 velocity update gain
3. large innovation 降低 confidence
4. 不要让 recoil residual 被 filter 学成 target velocity
```

---

## 9. Authority / confidence

confidence 是连续值；authority 必须离散。

```cpp
enum class AssistAuthority {
    None,
    AimObserved,
    AimCoast
};

enum class FireAuthority {
    None,
    ObservedOnly
};
```

fire authority 必须满足：

```text
track.life == Confirmed
last matched detection exists
last matched detection belongs to latest usable vision frame
no newer vision frame explicitly missed this track
det_conf >= fire_min_conf
obs_capture_age <= fire_max_capture_age
position_sigma <= fire_max_sigma
association ambiguity <= max_fire_ambiguity
inside fire aperture
not predictedOnly
not coasting
```

强 invariant：

```text
predictedOnly == true  => fireAuthority == None
missStreak > 0         => fireAuthority == None
no backing detection   => fireAuthority == None
```

---

## 10. Benchmark 方案

### 真实日志

vision frame 日志：

```text
capture_time
ready_time
frame_seq
ROI transform
ADS/zoom/mode
all detections: box, conf, tier, class, target dx/dy
selected detection id
```

control tick 日志：

```text
tick_time
query_time
tracker snapshot
selected track id
assist authority
fire authority
raw controller command
recoil compensation command
final right stick after clamp
ADS state
fire state
```

track debug：

```text
state x/P
predicted screen error
matched detection id
innovation
mahalanobis d2
association cost
confidence
missStreak
ambiguity
```

### synthetic benchmark

合成序列包含：

```text
constant velocity / abrupt strafe / stop-start
recorded or synthetic ego stick sweeps
ADS zoom scale changes
recoil visual impulse
confidence flicker
false positives
random dropout: 5%, 10%, 20%
burst dropout: 2, 3, 5 frames
latency jitter: +8/+16/+24 ms
two-target crossing
exit/re-enter ROI
```

指标：

```text
target_error_px p50/p95/p99
ID switches
wrong lock duration
reacquire time
false association rate
predicted-only fire violations, must be 0
stale fire violations
latency effective error
ego residual
recoil residual
CPU p50/p95/p99
allocations per tick
```

Ablation：

```text
A0: detector-only
A1: CV extrapolation only
A2: alpha-beta
A3: CV Kalman without ego compensation
A4: CV Kalman + final stick ego compensation
A5: A4 + recoil visual model
A6: A5 + capture-time update
```

---

## 11. 推荐落地顺序

```text
1. 统一 timestamp：capture_time / ready_time / control_time
2. detector-only selector baseline
3. CV Kalman without ego compensation
4. finalStick -> E(t)，验证静态目标 p + E 稳定
5. ADS/zoom scale，验证 ADS transition 不产生假 velocity
6. missing-frame coasting，但 fire authority observed-only
7. recoil visual model，验证 shot-aligned residual
8. crossing-target association benchmark
9. 最后再调 controller gain
```

整体数据流：

```text
detector observation
  -> projection to 2D track coordinate
  -> add cumulative ego E(capture_time)
  -> CV Kalman update body center
  -> association by gated covariance + IoU + size + confidence
  -> controller query predicts to now/horizon
  -> subtract E(query_time)
  -> controller uses snapshot
  -> final stick written back into EgoMotionBuffer
```
