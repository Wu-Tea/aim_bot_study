# BodyLock 权重/预算职责探针

分析用途，链接 DEV 未修改的生产库。没有修改生产算法、个人配置或 clean。对应生产实现为 `f9f42d3`，当前 DEV HEAD `a11f221`。

探针执行180个点上组合（两种生产曲线、五个authority、六种目标速度、三种公式）和24个2/20秒的隔离闭环，另直接调用生产BodyLock、无手动输入的最终仲裁和coordinator。生产反例断言确认现有行为确实存在，**PASS 不表示控制缺陷已修复**。

- policy=0：当前生产公式。
- policy=1：反事实公式，保留当前位置增益及最终预算，仅取消已确认总目标速度的身份权重缩放。为复用旧API，夹具用速度除以authority再调用生产solver；这只是等式探针，不是建议加入产品的除法补偿。
- policy=2：反事实公式，authority只保留为同样的最终物理预算，位置/运动需求不预先乘该系数。

两种反事实均断言输出不超过原预算，authority=0仍为0。反事实结果不是可发布方案：没有完整运动估计、位置降噪、ADS交接、手动轨迹、开火和缺帧链路。

闭环为理想一维线性相机：响应1000 px/(stick·s)、已知恒定目标速度10/30/60/100 px/s、真实输出延迟10 ms、1000 Hz，100 ms位置响应参数，最低追赶0.5与到点半径2保留。误差从0开始，后段统计500 ms之后。恒定速度与相机模型是已知真值，不能外推到有噪声和复杂机动。24例共264,000 tick，未做独立随机种子发布验证。

`point.csv`保留最终杆量、相机速度和是否限幅；`loop.csv`保留误差、平均输出与峰值；`coordinator.csv`确认同一身份/证据的0、69.9、70.1 px对应0.0736、0.0736、0.2208。

```powershell
./tests/clean_incident_20261016/build.ps1 -ProductRoot D:/work/AI/yolo-study-001 -BuildName bodylock-authority-probe -SourceRoot D:/work/AI/yolo-study-001/tests/bodylock_authority_20261018
Copy-Item native/build/Release/SDL2.dll runs/clean-incident-fix-20261016/bodylock-authority-probe/Release/SDL2.dll
./runs/clean-incident-fix-20261016/bodylock-authority-probe/Release/authority_probe.exe runs/bodylock-authority-20261018
python tests/bodylock_authority_20261018/analyze.py
```

分析脚本还读取上轮实际生产全链case8轨迹。该线性、无手动场景的400–1399 ms共1000个BodyLock tick，21个接近最终预算，平均使用预算49.25%。此统计只对这个明确反例成立，不代表全部场景。
