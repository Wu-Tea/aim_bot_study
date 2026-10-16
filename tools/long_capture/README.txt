长时间实战采集 v2

在新版 GUI 的「模型与设备 → 手柄与诊断 → 诊断记录」打开「性能采集」，
然后正常启动助手。开关立即生效，无需保存配置；助手运行时需先停止再切换。
界面显示等待启动、正在采集、到期或异常状态。已有日志始终保留。
arm.ps1 / stop.ps1 仍可用于命令行控制。
默认允许接下来 12 小时内的新助手会话附带诊断；系统采集同步运行 12 小时。
打完先在 GUI 停止助手，等待日志排空，再关闭「性能采集」。
停止系统采集不会停止正在运行的助手。超时后仍在运行的助手会继续写自己的日志，
但系统数据已停止；超过 12 小时应重新开启采集，并正常重启助手以建立完整新会话。

记录内容：
- gpu.csv：每秒整卡显存总量/已用/空闲、GPU/显存活动率、时钟、温度、功耗、降频原因。
- system.jsonl：约每两秒每进程专用/共享显存、各 GPU engine 活动、游戏/助手 CPU 累计
  时间与 RAM、系统内存压力、前台 PID、磁盘余量、QPC/UTC 对齐及采样耗时/错误。
- presents.csv：各图形进程逐次 PresentMon 呈现事件，保留丢弃事件，使用 QPC 时间；
  按 system.jsonl 中游戏 PID 关联，不依赖可能受权限限制而显示 error 的进程名称。
  是呈现链路证据，不是完整 GPU kernel 调度轨迹。进程尚未开始呈现时可以暂时没有文件。
- runtime/<GUI会话>/source-config.toml、effective-config.txt：源配置与实际生效参数。
- trace/<时钟>/pipeline*.csv：V 每次 Vision poll（含空帧/旧 epoch），D 每个返回检测框，
  C 每个控制尝试的实际使用输入、请求输出、实际投递右摇杆及状态。D 无固定八框截断。
  sequence/frame_id/tick_id/input_sequence 为关联键；不要将跨会话相同 ID 混用。
- 原有详细目标选择/身份/交接/控制诊断强制开启，仅覆盖本次进程，不修改个人配置。
- health.json、completion.json、telemetry-completion.json：进度、计数、丢失、写入失败。

pipeline v2 相比 v1 新增：显存 array-copy 和预处理 kernel 的 GPU 事件跨度；CPU 提交
起止、输出复制提交与等待 API 起止；viewport；检测框/置信度/类别/颜色判断；控制
输入摇杆、扳机、按钮、触摸点；输出轴/按钮、实际右摇杆、连接和投递错误信息。
tick_begin_ns / tick_end_ns 包含输出后的诊断与后续帧准备；每个 C 行附带原有
telemetry 接收/丢失/写入失败累计计数。采集模式中普通日志等待短暂队列锁竞争，
避免非满队列情况下丢弃；磁盘写入仍在锁外，队列满时仍计数而不等待磁盘。
按钮 bit0..15：RB,LB,A,B,X,Y,back,guide,start,L3,R3,up,down,left,right,touchpad。
检测 flags bit0..3：friendly,color_classified,has_cue,has_motion_anchor。
非该 lane 的字段为 0；detail_enabled=0 时细分推理时间不可用。
GPU 事件跨度可能包含调度空隙，不能解释为纯 kernel 活跃时间；CPU 等待跨度包括
GPU 尚未完成的时间及等待返回开销，不能直接视为可删除的空转。
capture_copied_ns 是 CPU 提交/ReleaseFrame 返回，不是 GPU 复制完成。
设备提交完成不是物理震动发生，也不是游戏处理输入或显示响应。

长时保护：pipeline 约 128 MiB 分卷，不覆盖早期卷；异步固定容量队列，不阻塞控制等磁盘。
每秒刷新并记录健康状态；退出排空。队列超载计数，不能把无文件/零值当作零耗时。
系统采集在余量低于 12 GiB 时停止并报错；pipeline 在低于 2 GiB 时停止写入。
原有 telemetry 以其分卷策略保存，每次开新卷亦检查诊断磁盘余量。
异常断电/强杀可能留下末尾未完整行；缺 completion 必须标为非正常结束，保留现有数据。
不要用只支持 v1 单文件的旧分析脚本直接分析本目录。

边界：不录原始视频，不保证重演游戏画面或确认误识别；C 是控制器实际使用的输入，
不包含 I/O 线程所有未消费 USB 样本。原有选择器诊断仍存在自身抽样/数组上限，以其
计数为准；D lane 提供每帧返回检测列表补充。没有 CUDA 调度级 ETW/Nsight 全量记录。
长时自然游玩用于收集分布、相关性与异常，不能单独替代同场景因果 A/B 测试。
