import React, { useEffect, useState } from "react";
import { Minus, Square, Copy, X } from "lucide-react";
import { rpc } from "./bridge";

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
    <button title="最小化" aria-label="最小化" onClick={() => action("window_minimize")}><Minus /></button>
    <button title={maximized ? "还原窗口" : "最大化"} aria-label={maximized ? "还原窗口" : "最大化"}
      onClick={() => action("window_toggle_maximize")}>{maximized ? <Copy /> : <Square />}</button>
    <button className="window-close" title="关闭窗口" aria-label="关闭窗口"
      onClick={() => window.requestDesktopClose?.()}><X /></button>
  </div>;
}
