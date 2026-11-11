import React from "react";
import { Gamepad2 } from "lucide-react";
import { rpc, phases } from "./bridge";

export function Feedback({ status, logs, run, draft, saved, owned, onStage, onRevert }) {
  const rates = status.rates;
  const rate = (count, duration) =>
    rates?.[duration]
      ? ((rates[count] * 1e9) / rates[duration]).toFixed(1)
      : "—";
  const metrics = [
    ["应用平均 FPS", "vision_frames", "elapsed_ns"],
    ["Aim 平均 FPS", "aim_frames", "aim_ns"],
    ["近 5 秒 Aim FPS", "recent_aim_frames", "recent_aim_ns"],
  ];
  return (
    <>
      <div className="feedback-top">
        <div className="runtime-tile">
          <Gamepad2 size={48} />
          <h2>{phases[status.phase]}</h2>
          <p>{status.device ?? "暂无运行设备"}</p>
          <p className="small muted">
            {status.virtual_connected ? "虚拟输出已连接" : "虚拟输出未连接"}
          </p>
        </div>
        <div className="fps-grid">
          {metrics.map(([label, count, duration]) => (
            <div key={count}>
              <span>{label}</span>
              <strong>{rate(count, duration)}</strong>
            </div>
          ))}
        </div>
      </div>
      <div className="library-actions">
        <button onClick={logs}>查看运行日志</button>
        <button
          disabled={!status.initialized && !status.fusion}
          onClick={() =>
            run("正在切换 Fusion…", () =>
              rpc("fusion", { enabled: !status.fusion }),
            )
          }
        >
          {status.fusion ? "关闭" : "开启"} Fusion
        </button>
        <button
          disabled={!status.learning}
          onClick={() =>
            run("正在导出响应测量…", async () => {
              const path = await rpc("learning_export");
              return path ? "已导出：" + path : '已取消导出';
            })
          }
        >
          导出响应测量
        </button>
      </div>
      <p className="note">
        FPS 统计新视觉结果的消费速率，Aim
        按实体开镜计时。暂无有效采样时显示“—”。
      </p>
      <div className="tablewrap">
        <table className="profiletable">
          <thead>
            <tr>
              <th>响应测量</th>
              <th>运行值（px/s）</th>
              <th>测得值（px/s）</th>
              <th>拟合度</th>
              <th>有效拟合</th>
              <th>配置草稿</th>
            </tr>
          </thead>
          <tbody>
            {[
              "腰射（Hipfire）",
              "开镜（ADS）",
            ].map((label, index) => {
              const value = status.learning?.regions[index];
              const path = 'gamepad.ai_aim.' + (index === 0 ? 'body_free_initial_scale' : 'ads_free_initial_scale');
              const available = value?.samples > 0 && value.confidence > 0;
              const staged = available && draft?.values[path] === value.learned;
              return (
                <tr key={label}>
                  <td>{label}</td>
                  <td>{value?.effective?.toFixed(2) ?? "—"}</td>
                  <td>{available ? value.learned.toFixed(2) : "尚未测得"}</td>
                  <td>
                    {value ? (value.confidence * 100).toFixed(1) + "%" : "—"}
                  </td>
                  <td>{value?.samples ?? "—"}</td>
                  <td><div className="measurement-actions"><button disabled={!owned || !available || staged} onClick={() => onStage(index)}>
                    {staged ? draft?.values[path] === saved?.values[path] ? '已保存' : '已填入' : '填入配置'}
                  </button>{owned && draft?.values[path] !== saved?.values[path] &&
                    <button onClick={() => onRevert(path)}>恢复已保存值</button>}</div></td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      <p className="small muted">测量结果不会自动应用。填入配置后，保存配置才会生效。</p>
    </>
  );
}
