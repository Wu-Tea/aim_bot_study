export const copy = (x) => structuredClone(x),
  eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
export const fmt = (x) =>
  typeof x === "number" ? Number(x.toPrecision(10)) : x;
export const phases = {
  stopped: "已停止",
  failed: "运行失败",
  starting: "正在启动",
  running: "运行中",
  waiting_device: "等待手柄",
  stopping: "正在停止",
};
export async function rpc(command, payload = {}) {
  if (!window.pywebview?.api)
    throw Error("桌面连接尚未就绪，请从“启动助手”打开应用。");
  const result = await window.pywebview.api.dispatch(command, payload);
  if (!result.ok) throw Error(result.error);
  return result.data;
}
