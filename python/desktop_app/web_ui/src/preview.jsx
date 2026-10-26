import React, { useState, useEffect, useRef, memo } from "react";
import { rpc, fmt } from "./bridge";
import { Dialog } from "radix-ui";
import { Maximize2, X } from "lucide-react";
import { RangePlot } from "./range-plot";
export const Preview = memo(function Preview({
  valuesJson,
  disabled,
  mode,
  setMode,
  showModeSwitch = false,
}) {
  const [height, setHeight] = useState(22),
    [pose, setPose] = useState("standing"),
    [offset, setOffset] = useState([70, 0]);
  const [geometry, setGeometry] = useState(null),
    [error, setError] = useState("");
  const [observing, setObserving] = useState(false);
  const request = useRef(0),
    panel = useRef(null);
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
      rpc("geometry", { values: JSON.parse(valuesJson), mode, height, pose, offset })
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
    // Initial display is not a gesture burst; debounce only subsequent edits.
    }, serial === 1 ? 0 : 60);
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [valuesJson, mode, height, pose, offset]);
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
  const positions = () => (
    <div className="preview-positions">
      {[[0, "到点"], [0.15, "近距"], [0.5, "中距"], [1, "边界"]].map(([fraction, label]) => (
        <button key={label} disabled={disabled || !g} onClick={() => setOffset([
          (mode === "acquire" ? g.radius : g.follow_radius) * fraction, 0,
        ])}>{label}</button>
      ))}
    </div>
  );
  const coordinates = () => (
    <div className="preview-coordinates">
      <span>横向 <strong>{fmt(Number(offset[0].toFixed(1)))} px</strong></span>
      <span>纵向 <strong>{fmt(Number(offset[1].toFixed(1)))} px</strong></span>
      <span>距准星 <strong>{fmt(Number(Math.hypot(...offset).toFixed(1)))} px</strong></span>
    </div>
  );
  return (
    <Dialog.Root open={observing} onOpenChange={setObserving}>
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
          <Dialog.Trigger asChild>
            <button className="icon-button quiet" aria-label="打开观察窗口" title="打开观察窗口"
              disabled={disabled || !g}><Maximize2 size={16} /></button>
          </Dialog.Trigger>
        </div>
        <RangePlot geometry={g} offset={offset} setOffset={setOffset} mode={mode}
          width={span} height={span} disabled={disabled || observing} />
        {error ? <p className="inline-error" role="alert">{error}</p> : !g ? (
          <p className="note" role="status">正在读取范围…</p>
        ) : (
          <div className="preview-readout">
            <span>实际{mode === "acquire" ? "抓取" : "跟随"}半径</span>
            <strong>{fmt(Number((mode === "acquire" ? g.radius : g.follow_radius).toFixed(1)))} <small>px</small></strong>
          </div>
        )}
        {coordinates()}
        {positions()}
        <details className="preview-settings">
          <summary>示例设置</summary>
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
        <h3>线性响应示例（非实时）</h3>
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
      <Dialog.Portal>
        <Dialog.Overlay className="dialog-overlay" />
        <Dialog.Content className="dialog-content observation-window" style={{ "--observation-width": `${Math.max(600, Math.min(1000, w + 48))}px` }}>
          <Dialog.Title>范围观察</Dialog.Title>
          <Dialog.Description>按住空白处拖动目标，准星保持固定。范围示意，非实时画面。</Dialog.Description>
          <Dialog.Close className="icon-button dialog-close" aria-label="关闭观察窗口"><X size={18} /></Dialog.Close>
          <fieldset className="observation-controls" disabled={disabled}>
            <div className="observation-toolbar">
              <span>采集范围 <strong>{w} × {h}</strong></span>
              <select aria-label="观察阶段" value={mode} onChange={(e) => setMode(e.target.value)}>
                <option value="acquire">首次瞄准</option>
                <option value="follow">持续跟随</option>
              </select>
            </div>
            <RangePlot geometry={g} offset={offset} setOffset={setOffset} mode={mode}
              width={w} height={h} disabled={disabled} observation />
            <div className="observation-legend">
              <span className={mode === "acquire" ? "active" : ""}>抓取半径 {g ? fmt(Number(g.radius.toFixed(1))) : "—"} px</span>
              <span className={mode === "follow" ? "active" : ""}>跟随半径 {g ? fmt(Number(g.follow_radius.toFixed(1))) : "—"} px</span>
            </div>
            {coordinates()}
            {positions()}
            {error && <p className="inline-error" role="alert">{error}</p>}
          </fieldset>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
});
