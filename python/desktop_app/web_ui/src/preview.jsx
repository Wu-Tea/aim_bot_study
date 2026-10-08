import React, { useState, useEffect, useRef } from "react";
import { rpc, fmt } from "./bridge";
export function Preview({
  draft,
  disabled,
  mode,
  setMode,
  showModeSwitch = false,
}) {
  const [height, setHeight] = useState(22),
    [pose, setPose] = useState("standing"),
    [offset, setOffset] = useState([70, 0]);
  const [geometry, setGeometry] = useState(null),
    [error, setError] = useState("正在读取原生范围规则…");
  const request = useRef(0),
    panel = useRef(null),
    svg = useRef(null),
    drag = useRef(false);
  useEffect(() => {
    const root = panel.current;
    const body = root.closest(".workspace-fields");
    const surface = root.querySelector(".preview-surface");
    let frame;
    const fit = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(() => {
        if (!root.getClientRects().length) return;
        const top =
          root.getBoundingClientRect().top -
          body.getBoundingClientRect().top +
          body.scrollTop;
        const chrome =
          root.getBoundingClientRect().height -
          surface.getBoundingClientRect().height;
        const available =
          body.clientHeight -
          parseFloat(getComputedStyle(body).paddingBottom) -
          top;
        const size = Math.floor(
          Math.max(120, Math.min(260, available - chrome - 2)),
        );
        const value = size + "px";
        if (surface.style.getPropertyValue("--preview-size") !== value)
          surface.style.setProperty("--preview-size", value);
      });
    };
    const observer = new ResizeObserver(fit);
    observer.observe(body);
    observer.observe(root);
    const tabs = body.querySelector(".sectionnav");
    if (tabs) observer.observe(tabs);
    fit();
    return () => {
      observer.disconnect();
      cancelAnimationFrame(frame);
    };
  }, []);
  useEffect(() => {
    let alive = true;
    const serial = ++request.current;
    const timer = setTimeout(() => {
      rpc("geometry", { values: draft.values, mode, height, pose, offset })
        .then((g) => {
          if (alive && serial === request.current) {
            setGeometry(g);
            setError("");
          }
        })
        .catch((e) => {
          if (alive && serial === request.current) {
            setGeometry(null);
            setError(e.message);
          }
        });
    }, 60);
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [draft.values, mode, height, pose, offset]);
  const g = geometry,
    w = g?.width ?? 640,
    h = g?.height ?? 512,
    span =
      Math.max(
        2 * (g?.radius ?? 150),
        2 * (g?.follow_radius ?? 150),
        (g?.body_height ?? 100) * 2,
        (g?.body_width ?? 40) * 2,
      ) * 1.2;
  const place = (e) => {
    const point = svg.current.createSVGPoint();
    point.x = e.clientX;
    point.y = e.clientY;
    const pos = point.matrixTransform(svg.current.getScreenCTM().inverse());
    setOffset([
      Math.max(-w / 2, Math.min(w / 2, pos.x)),
      Math.max(-h / 2, Math.min(h / 2, pos.y)),
    ]);
  };
  return (
    <aside ref={panel} className="aside">
      <div>
        <div className="preview-heading">
          <h2>{mode === "acquire" ? "抓取范围" : "跟随范围"}</h2>
          {showModeSwitch && (
            <select
              aria-label="预览阶段"
              value={mode}
              onChange={(e) => setMode(e.target.value)}
            >
              <option value="acquire">首次瞄准</option>
              <option value="follow">持续跟随</option>
            </select>
          )}
          {!showModeSwitch && <span>可拖动目标</span>}
        </div>
        <div className="preview-surface live-preview">
          <svg
            ref={svg}
            viewBox={`${-span / 2} ${-span / 2} ${span} ${span}`}
            role="img"
            aria-label="准星与目标范围"
            onPointerDown={(e) => {
              if (disabled || !g) return;
              drag.current = true;
              e.currentTarget.setPointerCapture(e.pointerId);
              place(e);
            }}
            onPointerMove={(e) => {
              if (drag.current) place(e);
            }}
            onPointerUp={() => (drag.current = false)}
            onPointerCancel={() => (drag.current = false)}
          >
            <defs>
              <pattern
                id="dots"
                width="28"
                height="28"
                patternUnits="userSpaceOnUse"
              >
                <circle cx="1" cy="1" r="1" fill="#b7c79f" />
              </pattern>
            </defs>
            <rect
              x={-span / 2}
              y={-span / 2}
              width={span}
              height={span}
              fill="url(#dots)"
            />
            {g && (
              <>
                <rect
                  x={-w / 2}
                  y={-h / 2}
                  width={w}
                  height={h}
                  fill="none"
                  stroke="#b9cba9"
                  strokeDasharray="5 7"
                />
                {[
                  ["radius", "acquire"],
                  ["follow_radius", "follow"],
                ].map(([key, m]) => (
                  <circle
                    key={key}
                    r={g[key]}
                    fill="none"
                    stroke={m === mode ? "#476d39" : "#8a9f77"}
                    strokeWidth={m === mode ? 2.5 : 1}
                    strokeDasharray={m === mode ? "7 7" : "2 8"}
                  />
                ))}
                <path d="M-12 0H12M0-12V12" stroke="#5d7150" strokeWidth="2" />
                <g
                  transform={`translate(${offset[0] - g.aim[0]},${offset[1] - g.aim[1]})`}
                >
                  <rect
                    width={g.body_width}
                    height={g.body_height}
                    fill="#9cb38c55"
                    stroke="#749061"
                    rx="3"
                  />
                  <rect
                    x={g.region[0]}
                    y={g.region[1]}
                    width={g.region[2] - g.region[0]}
                    height={g.region[3] - g.region[1]}
                    fill="#dde9c9"
                    stroke="#799469"
                  />
                </g>
                <path
                  d={`M0 0L${offset.join(" ")}`}
                  stroke="#829974"
                  strokeDasharray="4 6"
                />
                <circle cx={offset[0]} cy={offset[1]} r="6" fill="#286750" />
                <text
                  x={offset[0] + 12}
                  y={offset[1] - 12}
                  fontSize="17"
                  fill="#3f6036"
                >
                  目标点
                </text>
              </>
            )}
          </svg>
        </div>
        {error ? (
          <p className="inline-error">{error}</p>
        ) : (
          <div className="preview-readout">
            <strong>
              {fmt(
                Number(
                  (mode === "acquire" ? g?.radius : g?.follow_radius)?.toFixed(
                    1,
                  ),
                ),
              )}{" "}
              <small>px</small>
            </strong>
            <span>实际{mode === "acquire" ? "抓取" : "跟随"}半径</span>
          </div>
        )}
        <div className="preview-positions">
          {[
            [0, "到点"],
            [0.15, "近距"],
            [0.5, "中距"],
            [1, "边界"],
          ].map(([f, label]) => (
            <button
              key={f}
              disabled={!g}
              onClick={() =>
                setOffset([
                  (mode === "acquire" ? g.radius : g.follow_radius) * f,
                  0,
                ])
              }
            >
              {label}
            </button>
          ))}
        </div>
        <details className="preview-settings">
          <summary>示例设置 · 高度、姿态与位置</summary>
          <div className="preview-options">
            <label>
              目标高度{" "}
              <input
                type="range"
                min="1"
                max="90"
                value={height}
                onChange={(e) => setHeight(Number(e.target.value))}
              />
              <span>{height}%</span>
            </label>
            <label>
              姿态{" "}
              <select value={pose} onChange={(e) => setPose(e.target.value)}>
                <option value="standing">站立</option>
                <option value="crouching">蹲姿</option>
                <option value="wide">宽矮</option>
              </select>
            </label>

            <label>
              水平偏差{" "}
              <input
                type="number"
                value={fmt(offset[0])}
                min={-w / 2}
                max={w / 2}
                onChange={(e) => {
                  if (e.target.value !== "")
                    setOffset([Number(e.target.value), offset[1]]);
                }}
              />{" "}
              px
            </label>
            <label>
              垂直偏差{" "}
              <input
                type="number"
                value={fmt(offset[1])}
                min={-h / 2}
                max={h / 2}
                onChange={(e) => {
                  if (e.target.value !== "")
                    setOffset([offset[0], Number(e.target.value)]);
                }}
              />{" "}
              px
            </label>
          </div>
        </details>
      </div>
      <div className="preview-info">
        <h3>线性响应示例 · 非实时输出</h3>
        <dl>
          {[
            ["横向", g?.stick?.[0]],
            ["纵向", g?.stick?.[1]],
          ].map(([label, v]) => (
            <div key={label}>
              <dt>{label}</dt>
              <dd>{v === undefined ? "—" : (v * 100).toFixed(1) + "%"}</dd>
            </div>
          ))}
        </dl>
      </div>
    </aside>
  );
}
