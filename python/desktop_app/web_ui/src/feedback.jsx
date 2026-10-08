import React from "react";
import { Gamepad2 } from "lucide-react";
import { rpc, phases } from "./bridge";

export function Feedback({ status, logs, run }) {
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
            run("正在导出响应学习…", async () => {
              const path = await rpc("learning_export");
              return path ? "已导出：" + path : undefined;
            })
          }
        >
          导出响应学习
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
              <th>响应学习区域</th>
              <th>当前响应</th>
              <th>学习响应</th>
              <th>置信度</th>
              <th>样本</th>
            </tr>
          </thead>
          <tbody>
            {[
              "跟随 · 普通区",
              "跟随 · 减速区",
              "ADS · 普通区",
              "ADS · 减速区",
            ].map((label, index) => {
              const value = status.learning?.regions[index];
              return (
                <tr key={label}>
                  <td>{label}</td>
                  <td>{value?.effective?.toFixed(2) ?? "—"}</td>
                  <td>{value?.samples ? value.learned.toFixed(2) : "—"}</td>
                  <td>
                    {value ? (value.confidence * 100).toFixed(1) + "%" : "—"}
                  </td>
                  <td>{value?.samples ?? "—"}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </>
  );
}
