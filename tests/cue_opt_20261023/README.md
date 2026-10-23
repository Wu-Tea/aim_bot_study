# cue 等价优化验证

基线固定为优化前dev `4c19029`。从Git导出旧cue与selector，重命名成独立测试类；同一个程序对同一张原始图像及同一份TensorRT检测结果分别运行两版，避免GPU推理差异被误算为cue优化差异。所有工具只读录像，没有设备输出。

先构建dev原生Release库，再在仓库根目录运行：

```powershell
python tests/cue_opt_20261023/prepare.py
cmake -S tests/cue_opt_20261023 -B runs/cue-opt-20261023/build
cmake --build runs/cue-opt-20261023/build --config Release -- /m:3 /nr:false
runs/cue-opt-20261023/build/Release/equivalence.exe
node tests/cue_opt_20261023/videos.mjs runs/cue-opt-20261023/videos
python tests/cue_opt_20261023/summarize.py runs/cue-opt-20261023/videos
```

需要MSVC、CUDA13.1、TensorRT10.15.1.29及ffmpeg/ffprobe，路径与当前项目环境一致。视频脚本拒绝覆盖已有CSV；复跑时传入新的产物目录。视频路径为用户提供的10月20日两段录像和10月22日两个拼接片段。

- `equivalence`遍历全部16,777,216种RGB颜色、BGRA/RGB两种格式，对照旧浮点分类器；另用两个独立种子生成4800张不同尺寸、pitch、原点的图，包含随机颜色、小圆环、边缘、大区域及对角连接，对照每个cue的数量、顺序、坐标、尺寸、评分和颜色。
- `replay`以生产0.40置信度、640×512截取框、480×384模型运行每一个视频帧，不跳帧。按视频名义帧率生成两版相同时间戳，以6个E核运行，前后计时顺序逐帧交替。
- 每帧比较cue全部字段，再比较最终selector记录：选中person、target身份generation及变化、胸部/身体/瞄准区域、权限、当前cue、motion cue及各检测框/纹理锚点。float使用9位有效数字序列化，可无损区分float32值。遇到任何差异立即失败并报告帧号。
- 耗时排除解码、文件读取、TensorRT和结果比较；汇总丢弃前30帧。单独cue耗时已包含在selector总耗时内，不能相加。计时只说明阶段成本，不代表游戏完整FPS。
- 旧下拉诊断脚本使用0.20阈值，本工具按生产0.40；目标帧数不宜跨工具直接比较，本轮前后使用同一输入和阈值。

生产回归另由`ctest --test-dir native/build -C Release --output-on-failure`执行；专项用例`BaseVisionSelection.sparse_cue_seed_rows_preserve_small_markers`验证小标记的种子行相位及相邻大型同色背景。
