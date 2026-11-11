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
      </div>
      <p className="note">
        FPS 统计新视觉结果的消费速率，Aim
        按实体开镜计时。暂无有效采样时显示“—”。
      </p>
      {status.control_error && <p role="alert">配置状态读取失败：{status.control_error}</p>}
    </>
  );
}
