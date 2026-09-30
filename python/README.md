# Python 代码与工具

这里集中保存 GUI、Python fallback、配置读取、控制器、Vision 桥接、压枪、
训练支持、分析工具和测试。Python 模块名称仍是 `desktop_app`、`config`、
`controllers`、`vision`、`runtime`、`training` 和 `tools`。

项目资源位于上一层：`config.toml`、`models/`、`training_data/`、`runs/` 和
`artifacts/`。`project_paths.py` 区分源码根与资源根。移动 Python 代码不会
改变本机配置和模型的相对位置。

## 日常启动

从项目根目录双击 `启动助手.vbs`。Windows 启动器会设置当前进程的 Python
导入路径，并使用项目根作为工作目录。

直接运行 fallback 或分析工具：

```powershell
python python/main.py --help
python python/tools/train_person_detector.py --help
python python/tools/manage_native_logs.py list
```

需要直接调用模块或 unittest 时，从项目根目录设置导入路径：

```powershell
$env:PYTHONPATH = (Join-Path $PWD 'python')
python -m desktop_app.gui
python -B -m unittest tests.test_main_cli tests.test_startup_scripts -v
```

也可在所选 Python 环境中运行 `python -m pip install --no-deps -e ./python`，
注册本地源码的可编辑导入；依赖清单位于 `python/requirements.txt`。

## 验证

在项目根目录运行 `python -B -m pytest -q`。根目录 `pytest.ini` 会配置
`python/` 导入路径，同时收集 `python/tests/` 和 `python/tools/tests/`。
原生桥接和启动器集成测试需要先构建 `native/build/Release/`。

`tools/benchmarks/`、`tools/training/`、`tools/verify/` 保存原来位于
根目录 `scripts/` 的 Python 工具。PowerShell、BAT 和 VBS 入口继续位于
根目录的 `scripts/` 与 `tools/`。
