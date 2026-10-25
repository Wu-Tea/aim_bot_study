import React, { useId, useRef } from "react";

// Coordinates stay in capture pixels, independently of the rendered size or DPI.
export function RangePlot({ geometry: g, offset, setOffset, mode, width, height, disabled, observation = false }) {
  const pattern = useId();
  const drag = useRef(null);
  const localPoint = (event) => {
    const svg = event.currentTarget;
    const point = svg.createSVGPoint();
    point.x = event.clientX;
    point.y = event.clientY;
    return point.matrixTransform(svg.getScreenCTM().inverse());
  };
  const move = (x, y) => setOffset([
    Math.max(-g.width / 2, Math.min(g.width / 2, x)),
    Math.max(-g.height / 2, Math.min(g.height / 2, y)),
  ]);
  const end = (event) => {
    drag.current = null;
    delete event.currentTarget.dataset.dragging;
  };
  const inView = Math.abs(offset[0]) <= width / 2 && Math.abs(offset[1]) <= height / 2;
  return (
    <div
      className={observation ? "observation-surface" : "preview-surface live-preview"}
      style={observation ? { "--capture-width": `${width}px`, "--capture-ratio": width / height, aspectRatio: `${width} / ${height}` } : undefined}
    >
      <svg
        viewBox={`${-width / 2} ${-height / 2} ${width} ${height}`}
        role="img"
        aria-label="准星与目标范围，可拖动空白处移动目标，方向键微调"
        tabIndex={disabled || !g ? -1 : 0}
        onPointerDown={(event) => {
          if (disabled || !g || event.button !== 0 || drag.current) return;
          drag.current = { pointerId: event.pointerId, start: localPoint(event), offset: [...offset] };
          event.currentTarget.dataset.dragging = "true";
          event.currentTarget.setPointerCapture(event.pointerId);
        }}
        onPointerMove={(event) => {
          const current = drag.current;
          if (disabled || !g || !current || event.pointerId !== current.pointerId) return;
          const point = localPoint(event);
          move(current.offset[0] + point.x - current.start.x, current.offset[1] + point.y - current.start.y);
        }}
        onPointerUp={end}
        onPointerCancel={end}
        onLostPointerCapture={end}
        onKeyDown={(event) => {
          if (disabled || !g) return;
          const delta = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[event.key];
          if (!delta) return;
          event.preventDefault();
          const step = event.shiftKey ? 10 : 1;
          move(offset[0] + delta[0] * step, offset[1] + delta[1] * step);
        }}
      >
        <defs>
          <pattern id={pattern} width="28" height="28" patternUnits="userSpaceOnUse">
            <circle cx="1" cy="1" r="1" fill="var(--line)" />
          </pattern>
        </defs>
        <rect x={-width / 2} y={-height / 2} width={width} height={height} fill={`url(#${pattern})`} />
        {g && <>
          <rect x={-g.width / 2} y={-g.height / 2} width={g.width} height={g.height}
            fill="none" stroke="var(--line)" strokeDasharray="5 7" />
          {[["radius", "acquire"], ["follow_radius", "follow"]].map(([key, stage]) => (
            <circle key={key} data-range={stage} r={g[key]} fill="none"
              stroke={stage === mode ? "var(--accent)" : "var(--muted)"}
              strokeWidth={stage === mode ? 1.5 : 0.5} vectorEffect="non-scaling-stroke"
              strokeDasharray={stage === mode ? "7 7" : "2 8"} />
          ))}
          <g transform={`translate(${offset[0] - g.aim[0]},${offset[1] - g.aim[1]})`}>
            <rect width={g.body_width} height={g.body_height} fill="var(--subtle)" stroke="var(--muted)" rx="3" />
            <rect x={g.region[0]} y={g.region[1]} width={g.region[2] - g.region[0]} height={g.region[3] - g.region[1]}
              fill="var(--tint)" stroke="var(--accent)" />
          </g>
          <path d={`M0 0L${offset.join(" ")}`} stroke="var(--muted)" strokeDasharray="4 6" />
          <circle data-target="true" cx={offset[0]} cy={offset[1]} r="6" fill="var(--accent)" />
          <path data-crosshair="true" d="M-12 0H12M0-12V12" stroke="var(--ink)" strokeWidth="1.5" vectorEffect="non-scaling-stroke" />
        </>}
      </svg>
      {g && inView && <span className="preview-target-label" style={{
        left: `clamp(0px, calc(${50 + offset[0] / width * 100}% + 10px), calc(100% - 36px))`,
        top: `clamp(0px, calc(${50 + offset[1] / height * 100}% - 20px), calc(100% - 18px))`,
      }}>目标点</span>}
      {observation && <span className="observation-center-label">准星</span>}
      {observation && !inView && <span className="observation-outside">目标点在采集范围外</span>}
    </div>
  );
}
