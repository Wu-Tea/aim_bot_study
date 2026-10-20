# Cue 拥有目标身份：离线职责实验

2026-10-20 更新：当前 dev 已按用户确认改为 person OR cue 与同帧身体参考，见 `docs/project/CUE_PERSON_OR_20261020.md`。本 README 下方保留最初离线实验的范围，不代表当前生产行为。新录像复跑：

```powershell
node tests/cue_identity_offline_20261019/replay_or_reference_20261020.mjs
node tests/cue_identity_offline_20261019/replay_or_reference_20261020.mjs --summarize-only
node tests/cue_identity_offline_20261019/review_live_20261020.mjs
```

需要现有本地录像、FFmpeg、TensorRT/CUDA 与原生 `vision_cue_video_review` 工具；新结果使用 `*-or-reference.jsonl`，不会覆盖旧的 `*-person.jsonl` / `*-cue.jsonl`。`replay_comparison_20261020.mjs` 生成旧对照，cue 模式明确使用 `runs/cue-review-20261020/cue-owned-before.exe`，缺少该冻结工具时失败；不要用当前融合实现冒充旧 cue 方案。原生工具的历史 CLI 参数 `cue-owned` 仍用于启用当前实验路径，具体行为取决于所用 exe。整个流程只离线读视频，不连接输出设备。

用户在2026-10-19提出：cue不会被人体遮挡；索敌用cue，先盯住一个，避免person驱动的两个目标来回跳。延续“先离线、生产不改”的范围。

同日用户进一步确认：COD死敌骷髅标记也属于有效cue，与圆点统一参与索敌；不按细节形状另设目标类别。血条和菱形仍不参与索敌。骷髅不是“目标死亡”的证据。当前通用颜色/紧凑形状提议已包含两个真实骷髅例，新增离线检查确认可进入并保持cue身份；没有按glyph增加单独门槛。

`cue_lock.mjs` 的 CueLock API 不接收person。空闲时在190px范围内选择离准星最近的cue；持有身份后，只寻找原cue的同色、相近尺度、运动连续观测，不因其他cue靠近准星或person变化重新排序。重复框、人体丢失、置信度变化均不能撤销cue身份。person只在锁定后附着几何；关联失败不会撤销cue。

这里的输入仍是上一轮宽松的颜色/形状提议，不是真正已经验证的游戏标记。自动索敌结果必须与已确认cue起点的诊断分开，防止用人工指定起点掩盖自动误锁。

实验保留50ms无新cue观测后的身份释放，未匹配帧不输出fresh坐标；不把此实验身份期限当作生产控制器的32/40ms，也没有改生产期限。匹配假设为最大42px邻帧距离、36px预测残差、0.5–2尺度比、4px竞争差距。它们只用于离线职责验证，未宣称是最优参数。显式退出当帧清空且不重获。

本实验没有控制器、胸部重建、ADS/BodyLock、开火、真实输入或闭环相机。即使cue身份稳定，person提供的胸部仍可能跳；不把两者混同。

运行：

```powershell
node tests/cue_identity_offline_20261019/run.mjs
node tests/cue_identity_offline_20261019/check.mjs
node tests/cue_identity_offline_20261019/media.mjs
```

run读取上一轮冻结JSONL，不重跑模型。media只为核对抽取本地录像帧，需要ffmpeg。输出位于 `runs/cue-identity-offline-20261019/`，完整说明见该目录REPORT.md。

参考标签最终范围：用户确认骷髅有效后，v2-f631和v3-f150均纳入。上轮最初统计保留为initial文件，临时严格圆点统计保留为circle-only文件，当前统计为46帧、12个有效cue。v2-seed631是指定已确认有效死敌标记起点的诊断；标记类别获确认并不等于整个后续序列身份都已人工复标。
