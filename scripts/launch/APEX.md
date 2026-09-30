# Apex 设置入口

双击仓库根目录的 `启动助手.vbs`，选择 Apex Legends，调整设置后启动。
停止、重启与游戏切换都在同一个窗口完成。说明见 `docs/project/DESKTOP_ASSISTANT.md`。

Apex 专属设置位于 `config.toml` 的 `[games.apex.*]`：

- 专属模型：`artifacts/game-specialists-20260914/exports/apex/apex_480x384.engine`
- 工具响应曲线：`linear`
- 固定压枪腰射倍率：`0.5`，ADS 使用共用的 `feedback_amount`

其他参数继承共用配置。GUI 的当前游戏页只修改 Apex 分块；共用设置页修改所有游戏继承的值。
工具曲线不会改变游戏内设置，游戏内曲线仍由玩家设置。

所有游戏使用原程序 `native/vision_native/build/Release/cod_native_runtime.exe`。
原生 `--game apex` 直接选择配置分块，不再生成 `config.apex.toml`。
旧的 Apex 启停脚本作为兼容入口保留，也使用统一进程记录；停止会停止当前共用运行实例。

模型缺失时启动失败，不回退到其他模型。本次 GUI 接入不代表 Apex 实战效果验收。
