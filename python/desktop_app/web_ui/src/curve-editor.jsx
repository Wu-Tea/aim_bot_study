import { Select } from "./controls";
import React, { useState, useRef } from "react";
import { displayPoints, curvePath, curveWindow } from "./curve-preview";
import { fmt, copy } from "./bridge";
export function CurveEditor({
  draft,
  onCurve,
  pointEdit,
  onPointEdit,
}) {
  const points = draft.curve.definition.points,
    [selected, setSelected] = useState(pointEdit?.index ?? 1),
    [pointError, setPointError] = useState("");
  const [start,end] = curveWindow(draft.curve.definition, 'deadzone');
  const [lower,upper] = curveWindow(draft.curve.definition, 'compensation');
  const toDisplay = ([x,y]) => [start+(1-start-end)*x, lower+(1-lower-upper)*y];
  const fromDisplay = ([x,y]) => [(x-start)/(1-start-end), (y-lower)/(1-lower-upper)];
  const svg = useRef(null),
    drag = useRef(null);
  const index = Math.min(selected, points.length - 1),
    point = points[index],
    endpoint = index === 0 || index === points.length - 1;
  const pointText =
    pointEdit?.index === index
      ? pointEdit.text
      : toDisplay(point).map((x) => String(fmt(x * 100)));
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
    next.length <= 128 &&
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
    next[index] = fromDisplay(pointText.map((x) =>
      x.trim() === "" ? NaN : Number(x) / 100,
    ));
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
        Math.min(hi[0] - 0.000011, ((pos.x - 50) / 560-start)/(1-start-end)),
      ),
      Math.max(
        lo[1] + 0.000011,
        Math.min(hi[1] - 0.000011, ((360 - pos.y) / 310-lower)/(1-lower-upper)),
      ),
    ];
    onCurve(
      {
        algorithm: "custom_lut",
        definition: { ...draft.curve.definition, points: next },
      },
    );
  };
  const finish = () => {
    drag.current = null;
  };
  const path = curvePath(displayPoints(draft.curve.definition));
  return (
    <div className="curve-layout">
      <div>
        <div className="chart">
          <div className="chart-title">
            <span>归一化幅度 %</span>
            <span>拖动控制点，端点固定</span>
          </div>
          <div className="curve-plot">
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
                  stroke="var(--line-muted)"
                />
              </g>
            ))}
            <path
              id="curvePath"
              d={path}
              fill="none"
              stroke="var(--accent)"
              strokeWidth="4"
            />
            {points.map(toDisplay).map(([x, y], i) => (
              <circle
                key={i}
                cx={50 + x * 560}
                cy={360 - y * 310}
                r={i === index ? 8 : 5}
                fill={i === index ? "var(--tint)" : "var(--paper)"}
                stroke="var(--accent)"
                strokeWidth="2"
                onPointerDown={(e) => {
                  if (pointEdit) {
                    setPointError("请先应用或取消点位编辑。");
                    return;
                  }
                  setSelected(i);
                  if (i === 0 || i === points.length - 1) return;
                  e.currentTarget.setPointerCapture(e.pointerId);
                  drag.current = { index: i };
                }}
              />
            ))}
          </svg>
          {[0, 0.25, 0.5, 0.75, 1].map((f) => (
            <React.Fragment key={f}>
              <span className="curve-axis curve-axis-y" style={{ top: `${(360 - f * 310) / 410 * 100}%` }}>{f * 100}</span>
              <span className="curve-axis curve-axis-x" style={{ left: `${(50 + f * 560) / 660 * 100}%` }}>{f * 100}</span>
            </React.Fragment>
          ))}
          </div>
          <div className="chart-axis-title">输入 %</div>

        </div>

      </div>
      <aside className="curve-side">

        <div className="sectionhead">
          <h2>编辑点位</h2>
          <span className="pill">{points.length} 个点</span>
        </div>
        <label className="stack">
          控制点
          <Select
            aria-label="选择控制点"
            disabled={!!pointEdit}
            value={index}
            onChange={(e) => setSelected(Number(e.target.value))}
          >
            {points.map(toDisplay).map((p, i) => (
              <option key={i} value={i}>
                {i + 1}：{fmt(p[0] * 100)}% → {fmt(p[1] * 100)}%
              </option>
            ))}
          </Select>
        </label>
        {["输入 %", "响应 %"].map((label, i) => (
          <label className="stack" key={label}>
            {label}
            <input
              id={i === 0 ? "curve-point-input" : "curve-point-response"}
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
            <p className="note">点位尚未应用；保存曲线前请先应用或取消。</p>
          </>
        )}
        <div className="curve-point-actions">
          <button
            disabled={
              !!pointEdit || points.length >= 128 || index === points.length - 1
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
      </aside>
    </div>
  );
}
