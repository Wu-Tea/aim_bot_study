# cue控制观测与未选中残框回归

生产修复只改`NativeGamepadController::observation_batch_from`：选择器明确授权cue时构造唯一cue控制观测，原始snapshot/detections仍保留诊断框。身份/时效验证、权限、求解器和手动仲裁不变。

原生测试`BaseEndToEnd.cue_ignores_unselected_clutter`先红后绿；覆盖1/3/40个残框、同一30%输入的输出/胸部/权限不变、cue无开火权、真实缺帧32/40ms及手动透传。`cue_clutter_preserves_boundaries`覆盖无原身份、错代际、换代、50ms旧观测、坏几何和无瞄准权，不回退到未选中框。

## 构建与复跑

在修改生产前已保存静态库到`runs/cue-clutter-20261019/controller-before.lib`（dev HEAD89449f8的未修改生产库）。不提交二进制。其他机器应单独编译该基线并提供该库，不能用修复后的库冒充before。

```powershell
$cmake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
python tests/clean_incident_20261016/run_env.py $cmake -S tests/cue_clutter_20261019 -B runs/cue-clutter-20261019/build -G 'Visual Studio 17 2022' -A x64
python tests/clean_incident_20261016/run_env.py $cmake --build runs/cue-clutter-20261019/build --config Release -- /m:1 /nr:false
python tests/cue_clutter_20261019/replay_video.py 'E:/videoCut/Call of Duty  Black Ops 7 2026.10.18 - 23.30.26.04-cut-merged-1792337635861.mp4'
foreach ($seed in 2026101901,32452937,49979807,104395489) {
    foreach ($version in 'before','after') {
        & "runs/cue-clutter-20261019/build/Release/closed_loop_$version.exe" $seed "runs/cue-clutter-20261019/$version/$seed" .03
    }
}
foreach ($seed in 49979807,104395489) {
    foreach ($version in 'before','after') {
        & "runs/cue-clutter-20261019/build/Release/closed_loop_$version.exe" $seed "runs/cue-clutter-20261019/noise01/$version/$seed" .01
    }
}
python tests/cue_clutter_20261019/analyze.py
```

## 覆盖与解释

- 开发种子2026101901、32452937；独立49979807、104395489。生产修复固定后跑独立种子，未按结果调整参数。
- 每种子/配置32场景×2秒及20秒，每场景有两个独立相机世界：同一来源且无诊断残框的参考、cue附带1～4个残框的版本。生产VisionTargetSelector→adapter→NativeGamepadController→实际浮点交付账本→二维延迟相机闭环；补充cue用外部标记模拟，非真实颜色/检测器重放。
- 1000Hz控制，5～12ms capture、2～10ms投递延迟、6～30ms相机作用延迟；真实响应900～2500，与启动先验可失配；线性/动态曲线、±.25px位置噪声、最低追赶.5、到点2px。前四场景持续/反转30%手动，其余含二维、强手动退出、放开、居中及二维移动、开火/压枪。
- 固定1575ms目标槽+50ms间隔；1500ms松LT。250～350ms弱关联，400～490和900～1020ms cue，650～675ms短缺帧、1150～1210ms长缺帧。保留真实ADS/BodyLock阶段，不强行交接。
- 开发及独立均跑3%降噪，独立另跑实际profile的1%。总计每版本384个配对场景（768个世界），8,448,000控制tick；before/after合计16,896,000tick。额外的跟踪诊断复跑不另计种子或覆盖。
- 功能不变量：修复后带残框与无残框参考逐tick输出一致，参考行为与修复前一致；真实缺帧、胸部源点、阶段互斥、手动退出、cue无开火权都保留。`analyze.py`验证这些关系并输出逐例数值，不提供实机发布结论。
- `late_mean`是每目标槽400～1400ms误差均值，包含持续手动与目标对抗的场景，不能当作单纯AI跟随能力评分。原缺陷改变阶段生命周期，所以旧程序误差较小也可能源于异常保留ADS；这不等于允许隐去退步。
- 第一案例197帧通过生产GPU模型/颜色ROI/selector；对9个cue结果各自建立同代际已拥有目标，再以固定+.30手动对照原始结果与去除诊断残框的参考。该探针验证传递不变量，不伪造缺失的实机LT、阶段或输出。
- 仿真为浮点FIFO，不包含实机量化、完整枪械/ADS动画、真实200Hz检测与latest-only调度；视频为约60fps有损编码。仿真和回放通过不等于实机停稳验收。

同步核对时另设构建目录并传入 `-DPRODUCT_ROOT=D:/work/AI/yolo-study-001-clean`，`closed_loop_after`将使用clean生产头文件、adapter和静态库；DEV仅提供测试夹具。用同一独立种子复跑并逐字段比较CSV，结果留在DEV。
