import React, { useEffect, useState } from "react";
import { rpc } from "./bridge";

// Caption symbols share a 20px canvas and a ~12px visible footprint.
// Filled, integer-aligned edges avoid scaling a thin 24px library stroke down.
function CaptionIcon({ name }) {
  return <svg viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" focusable="false">
    {name === "minimize" && <path d="M4 9h12v2H4z" />}
    {name === "maximize" && <path fillRule="evenodd" d="M4 4h12v12H4z M6 6v8h8V6z" />}
    {name === "restore" && <path fillRule="evenodd" d="M7 3h10v10h-2V5H7z M3 7h10v10H3z M5 9v6h6V9z" />}
    {name === "close" && <path d="m5 5 10 10M15 5 5 15" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" />}
  </svg>;
}

export function WindowControls({ onError }) {
  const [maximized, setMaximized] = useState(false);
  useEffect(() => {
    const update = (event) => setMaximized(!!event.detail.maximized);
    window.addEventListener("desktop-window-state", update);
    return () => window.removeEventListener("desktop-window-state", update);
  }, []);
  const action = (command) => rpc(command)
    .then((state) => setMaximized(!!state.maximized))
    .catch((error) => onError(error.message));
  return <div className="window-controls" role="group" aria-label="窗口控制">
    <button title="最小化" aria-label="最小化" onClick={() => action("window_minimize")}><CaptionIcon name="minimize" /></button>
    <button title={maximized ? "还原窗口" : "最大化"} aria-label={maximized ? "还原窗口" : "最大化"}
      onClick={() => action("window_toggle_maximize")}><CaptionIcon name={maximized ? "restore" : "maximize"} /></button>
    <button className="window-close" title="关闭窗口" aria-label="关闭窗口"
      onClick={() => window.requestDesktopClose?.()}><CaptionIcon name="close" /></button>
  </div>;
}
