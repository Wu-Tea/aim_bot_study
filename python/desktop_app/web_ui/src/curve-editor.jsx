import React, { useState, useRef } from "react";
import { Undo2, Redo2 } from "lucide-react";
import { IconButton, Menu } from "./controls";
import { rpc, fmt, copy } from "./bridge";
export function CurveEditor({
  draft,
  seeds,
  onCurve,
  history,
  run,
  setModal,
  pointEdit,
  onPointEdit,
}) {
  const points = draft.curve.definition.points,
    [selected, setSelected] = useState(pointEdit?.index ?? 1),
    [pointError, setPointError] = useState("");
  const svg = useRef(null),
    drag = useRef(null);
  const index = Math.min(selected, points.length - 1),
    point = points[index],
    endpoint = index === 0 || index === points.length - 1;
  const pointText =
    pointEdit?.index === index
      ? pointEdit.text
      : point.map((x) => String(fmt(x * 100)));
  const replace = (next) => {
    onPointEdit(null);
    onCurve({
      algorithm: "custom_lut",
      definition: { ...draft.curve.definition, points: next },
    });
    setPointError("");
  };
  const valid = (next) =>
    next.length >= 2 &&
    next.length <= 32 &&
    next.every(
      ([x, y], i) =>
        Number.isFinite(x) &&
        Number.isFinite(y) &&
        x >= 0 &&
        y >= 0 &&
        x <= 1 &&
        y <= 1 &&
        (!i ||
          (x - next[i - 1][0] >= 0.000011 - 1e-12 &&
            y - next[i - 1][1] >= 0.000011 - 1e-12)),
    );
  const exact = () => {
    const next = copy(points);
    next[index] = pointText.map((x) =>
      x.trim() === "" ? NaN : Number(x) / 100,
    );
    if (!valid(next)) {
      setPointError("点位必须在相邻点之间，输入和响应都严格递增。");
      return;
    }
    replace(next);
  };
  const move = (e) => {
    if (!drag.current) return;
    const p = svg.current.createSVGPoint();
    p.x = e.clientX;
    p.y = e.clientY;
    const pos = p.matrixTransform(svg.current.getScreenCTM().inverse()),
      i = drag.current.index;
    const next = copy(points),
      lo = points[i - 1],
      hi = points[i + 1];
    next[i] = [
      Math.max(
        lo[0] + 0.000011,
        Math.min(hi[0] - 0.000011, (pos.x - 50) / 560),
      ),
      Math.max(
        lo[1] + 0.000011,
        Math.min(hi[1] - 0.000011, (360 - pos.y) / 310),
      ),
    ];
    onCurve(
      {
        algorithm: "custom_lut",
        definition: { ...draft.curve.definition, points: next },
      },
      false,
    );
  };
  const finish = () => {
    if (drag.current) {
      history.record(drag.current.before);
      drag.current = null;
    }
  };
  const path = points
    .map(([x, y], i) => `${i ? "L" : "M"}${50 + x * 560},${360 - y * 310}`)
    .join(" ");
  return (
    <div className="curve-layout">
      <div>
        <div className="curve-toolbar">
          <div className="segmented">
            {[
              ["linear", "线性"],
              ["cod_dynamic_legacy_lut", "动态"],
              ["custom_lut", "自定义"],
            ].map(([key, label]) => (
              <button
                key={key}
                aria-pressed={draft.curve.algorithm === key}
                className={draft.curve.algorithm === key ? "active" : ""}
                onClick={() =>
                  onCurve({
                    algorithm: key,
                    definition: {
                      ...draft.curve.definition,
                      points: copy(seeds[key] ?? points),
                    },
                  })
                }
              >
                {label}
              </button>
            ))}
          </div>
          <IconButton
            label="撤销曲线修改"
            disabled={!history.canUndo}
            onClick={history.undo}
          >
            <Undo2 size={16} />
          </IconButton>
          <IconButton
            label="重做曲线修改"
            disabled={!history.canRedo}
            onClick={history.redo}
          >
            <Redo2 size={16} />
          </IconButton>
          <Menu
            items={[
              {
                label: "导入曲线…",
                action: () =>
                  run("正在读取曲线…", async () => {
                    const d = await rpc("curve_import");
                    if (d) onCurve({ algorithm: "custom_lut", definition: d });
                  }),
              },
              {
                label: "导出曲线…",
                action: () =>
                  run("正在导出曲线…", async () => {
                    const path = await rpc(
                      "curve_export",
                      draft.curve.definition,
                    );
                    return path ? "已导出：" + path : undefined;
                  }),
              },
              {
                label: "存为曲线预设…",
                action: () =>
                  setModal({
                    kind: "presetSave",
                    title: "保存曲线预设",
                    name: draft.curve.definition.name,
                  }),
              },
              {
                label: "载入曲线预设…",
                action: () =>
                  run("正在读取预设…", async () => {
                    const result = await rpc("curve_presets");
                    setModal({ kind: "presets", title: "曲线预设", ...result });
                  }),
              },
            ]}
          />
        </div>
        <div className="chart">
          <div className="chart-title">
            <span>响应 %</span>
            <span>控制点可拖动 · 端点固定</span>
          </div>
          <svg
            ref={svg}
            viewBox="0 0 660 410"
            aria-label="输入响应曲线"
            onPointerMove={move}
            onPointerUp={finish}
            onPointerCancel={finish}
          >
            {[0, 0.25, 0.5, 0.75, 1].map((f) => (
              <g key={f}>
                <path
                  d={`M50 ${360 - f * 310}H610M${50 + f * 560} 50V360`}
                  stroke="#dfe7d7"
                />
                <text x="16" y={365 - f * 310} fontSize="12" fill="#64755b">
                  {f * 100}
                </text>
                <text x={46 + f * 560} y="386" fontSize="12" fill="#64755b">
                  {f * 100}
                </text>
              </g>
            ))}
            <path d="M50 360L610 50" stroke="#acbba2" strokeDasharray="6 6" />
            <path
              id="curvePath"
              d={path}
              fill="none"
              stroke="#286750"
              strokeWidth="4"
            />
            {points.map(([x, y], i) => (
              <circle
                key={i}
                cx={50 + x * 560}
                cy={360 - y * 310}
                r={i === index ? 8 : 5}
                fill={i === index ? "#cce0ad" : "#fff"}
                stroke="#286750"
                strokeWidth="2"
                onPointerDown={(e) => {
                  if (pointEdit) {
                    setPointError("请先应用或取消点位编辑。");
                    return;
                  }
                  setSelected(i);
                  if (i === 0 || i === points.length - 1) return;
                  e.currentTarget.setPointerCapture(e.pointerId);
                  drag.current = { index: i, before: copy(draft.curve) };
                }}
              />
            ))}
            <text x="564" y="405" fontSize="12" fill="#64755b">
              输入 %
            </text>
          </svg>
        </div>
        <p className="note">
          这是手柄输入与响应之间的映射。控制点须严格递增，以支持原生反向映射。
        </p>
      </div>
      <aside className="curve-side">
        <span className="pill">{points.length} 个控制点</span>
        <h2 style={{ marginTop: 16 }}>编辑点位</h2>
        <label className="stack">
          控制点
          <select
            aria-label="选择控制点"
            disabled={!!pointEdit}
            value={index}
            onChange={(e) => setSelected(Number(e.target.value))}
          >
            {points.map((p, i) => (
              <option key={i} value={i}>
                {i + 1} · {fmt(p[0] * 100)}% → {fmt(p[1] * 100)}%
              </option>
            ))}
          </select>
        </label>
        {["输入 %", "响应 %"].map((label, i) => (
          <label className="stack" key={label}>
            {label}
            <input
              aria-label={"控制点" + label}
              type="number"
              step="any"
              min="0"
              max="100"
              disabled={endpoint}
              value={pointText[i]}
              onChange={(e) =>
                onPointEdit({
                  index,
                  text: pointText.map((x, j) => (j === i ? e.target.value : x)),
                })
              }
            />
          </label>
        ))}
        {pointError && <p className="inline-error">{pointError}</p>}
        <button disabled={endpoint || !pointEdit} onClick={exact}>
          应用点位
        </button>
        {pointEdit && (
          <>
            <button
              className="quiet"
              onClick={() => {
                onPointEdit(null);
                setPointError("");
              }}
            >
              取消点位编辑
            </button>
            <p className="note">点位尚未应用；保存配置前请先应用或取消。</p>
          </>
        )}
        <div className="curve-point-actions">
          <button
            disabled={
              !!pointEdit || points.length >= 32 || index === points.length - 1
            }
            onClick={() => {
              const next = copy(points);
              next.splice(index + 1, 0, [
                (point[0] + points[index + 1][0]) / 2,
                (point[1] + points[index + 1][1]) / 2,
              ]);
              if (valid(next)) {
                replace(next);
                setSelected(index + 1);
              } else setPointError("相邻点之间空间不足，无法插入。");
            }}
          >
            插入点
          </button>
          <button
            disabled={!!pointEdit || endpoint || points.length <= 2}
            onClick={() => {
              replace(points.filter((_, i) => i !== index));
              setSelected(Math.max(0, index - 1));
            }}
          >
            删除点
          </button>
        </div>
        {endpoint && <p className="note">起点与终点固定为 0% 和 100%。</p>}
      </aside>
    </div>
  );
}
