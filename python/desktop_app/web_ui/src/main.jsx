import React, { useState, useEffect, useRef } from "react";
import { createRoot } from "react-dom/client";
import { Dialog } from "radix-ui";
import {
  SlidersHorizontal,
  ChartSpline,
  Library,
  Gamepad2,
  Activity,
  X,
  Plus,
  FolderOpen,
  Play,
  Square,
  Save,
  LoaderCircle,
} from "lucide-react";
import { IconButton, Menu } from "./controls";
import { Field, Group } from "./fields";
import { Preview } from "./preview";
import { CurveEditor } from "./curve-editor";
import { Feedback } from "./feedback";
import { copy, eq, rpc, phases } from "./bridge";
import "./surfaces.css";
import "./app.css";
const titles = {
  tuning: "参数调校",
  curves: "响应曲线",
  profiles: "配置管理",
  devices: "模型与设备",
  feedback: "运行反馈",
};
function App() {
  const [boot, setBoot] = useState(null),
    [drafts, setDrafts] = useState({}),
    [saved, setSaved] = useState({}),
    [selected, setSelected] = useState(null),
    [page, setPage] = useState("tuning"),
    [tab, setTab] = useState("acquire");
  const [previewMode, setPreviewMode] = useState("acquire");
  const [deviceTab, setDeviceTab] = useState("capture");
  const [pointEdits, setPointEdits] = useState({});
  const [restartFor, setRestartFor] = useState(null);
  const [busy, setBusy] = useState(""),
    [notice, setNotice] = useState(null),
    [modal, setModal] = useState(null),
    [status, setStatus] = useState({ phase: "stopped" }),
    [devices, setDevices] = useState([]),
    [reload, setReload] = useState(null),
    [connection, setConnection] = useState("正在连接桌面…");
  const historyRef = useRef({}),
    [historyVersion, setHistoryVersion] = useState(0),
    pendingRef = useRef(false),
    statusEpoch = useRef(0),
    viewport = useRef(null);
  const draft = drafts[selected],
    dirty = (p) =>
      p &&
      (!!pointEdits[p.id] ||
        !eq(
          { values: p.values, curve: p.curve, config: p.config },
          {
            values: saved[p.id]?.values,
            curve: saved[p.id]?.curve,
            config: saved[p.id]?.config,
          },
        ));
  const anyDirty = Object.values(drafts).some(dirty),
    active = status.record,
    owned = !!(
      draft &&
      active &&
      (active.profile_id === draft.id ||
        String(active.config_path)
          .replaceAll("\\", "/")
          .endsWith("/" + draft.id + ".toml"))
    );
  const transitioning =
    owned && ["starting", "stopping"].includes(status.phase);
  const setDraft = (d) => setDrafts((prev) => ({ ...prev, [d.id]: d }));
  const remember = (d) => {
    setPointEdits((prev) => ({ ...prev, [d.id]: null }));
    setSaved((prev) => ({ ...prev, [d.id]: copy(d) }));
    setDraft(d);
  };
  const run = async (label, work) => {
    if (pendingRef.current) return;
    pendingRef.current = true;
    setBusy(label);
    try {
      const result = await work();
      if (typeof result === "string") setNotice({ text: result });
      return result;
    } catch (e) {
      setNotice({ error: true, text: e.message });
      throw e;
    } finally {
      pendingRef.current = false;
      setBusy("");
    }
  };
  const safely = (label, work) => run(label, work).catch(() => {});
  useEffect(() => {
    let alive = true;
    const load = () =>
      rpc("bootstrap")
        .then((data) => {
          if (!alive) return;
          setBoot(data);
          const items = Object.fromEntries(data.profiles.map((p) => [p.id, p]));
          setDrafts(copy(items));
          setSaved(copy(items));
          setSelected(
            items[data.selected]
              ? data.selected
              : (data.profiles[0]?.id ?? null),
          );
          setConnection("");
          if (!data.profiles.length) setPage("profiles");
          if (data.errors.length)
            setNotice({
              error: true,
              text:
                "部分配置未能读取，原文件已保留：\n" + data.errors.join("\n"),
            });
        })
        .catch((e) => {
          if (alive) setConnection(e.message);
        });
    if (window.pywebview?.api) load();
    else window.addEventListener("pywebviewready", load, { once: true });
    return () => {
      alive = false;
      window.removeEventListener("pywebviewready", load);
    };
  }, []);
  useEffect(() => {
    if (!boot) return;
    let alive = true,
      timer;
    const poll = async () => {
      const epoch = statusEpoch.current;
      try {
        const s = await rpc("status");
        if (alive && epoch === statusEpoch.current) setStatus(s);
      } catch (e) {
        if (alive && epoch === statusEpoch.current)
          setNotice({ error: true, text: "运行状态读取失败：" + e.message });
      }
      if (alive) timer = setTimeout(poll, 1000);
    };
    poll();
    return () => {
      alive = false;
      clearTimeout(timer);
    };
  }, [boot]);
  useEffect(() => {
    if (!reload || !status.learning) return;
    const state = status.learning;
    if (state.request_id === reload.request_id && state.status !== 1) {
      setReload(null);
      if (state.status === 3) setRestartFor(status.record?.process_id);
      if (state.status === 2) setRestartFor(null);
      setNotice({
        error: state.status !== 2,
        text:
          state.status === 2
            ? "配置已热加载。"
            : "配置已保存，需要重启才能全部生效。",
      });
    }
  }, [status.learning, reload]);
  useEffect(() => {
    window.requestDesktopClose = () => {
      if (pendingRef.current) {
        setNotice({ text: "请等待当前操作完成后关闭。" });
        return;
      }
      if (anyDirty)
        setModal({
          kind: "close",
          title: "关闭工作空间",
          description:
            "还有未保存的草稿。关闭会丢弃这些修改，正在运行的原生程序会继续运行。",
        });
      else
        rpc("window_close").catch((e) =>
          setNotice({ error: true, text: e.message }),
        );
    };
    return () => delete window.requestDesktopClose;
  }, [anyDirty]);
  useEffect(() => {
    viewport.current?.scrollTo(0, 0);
  }, [page]);
  const valuesUpdate = (changes) =>
    setDraft({ ...draft, values: { ...draft.values, ...changes } });
  const setValue = (path, value) => valuesUpdate({ [path]: value });
  const recordCurve = (before) => {
    const h = (historyRef.current[selected] ??= { undo: [], redo: [] });
    h.undo.push(copy(before));
    h.undo = h.undo.slice(-100);
    h.redo = [];
    setHistoryVersion((x) => x + 1);
  };
  const onCurve = (curve, record = true) => {
    setPointEdits((prev) => ({ ...prev, [selected]: null }));
    if (eq(curve, draft.curve)) return;
    if (record) recordCurve(draft.curve);
    setDraft({ ...draft, curve });
  };
  const h = historyRef.current[selected] ?? { undo: [], redo: [] };
  const history = {
    record: recordCurve,
    canUndo: !!h.undo.length,
    canRedo: !!h.redo.length,
    undo: () => {
      h.redo.push(copy(draft.curve));
      setDraft({ ...draft, curve: h.undo.pop() });
      setHistoryVersion((x) => x + 1);
    },
    redo: () => {
      h.undo.push(copy(draft.curve));
      setDraft({ ...draft, curve: h.redo.pop() });
      setHistoryVersion((x) => x + 1);
    },
  };
  const applyResult = (result) => {
    if (result.status) {
      ++statusEpoch.current;
      setStatus(result.status);
    }
    if (result.profile) remember(result.profile);
    if (result.error) setNotice({ error: true, text: result.error });
    else if (result.reload) {
      const r = result.reload;
      setReload(r.status === 1 ? r : null);
      if (r.status === 3) setRestartFor(active?.process_id);
      if (r.status === 2) setRestartFor(null);
      setNotice({
        error: ![1, 2].includes(r.status),
        text:
          r.status === 2
            ? "已保存并热加载。"
            : r.status === 1
              ? "已保存；等待新视觉帧，尚未全部生效。"
              : "已保存，需要重启：" + r.message,
      });
    } else if (result.message) setNotice({ text: result.message });
  };
  const proposal = (candidate = draft) => {
    if (pointEdits[candidate.id])
      throw Error("有尚未应用的曲线点位，请在响应曲线中应用或取消后再保存。");
    return candidate;
  };
  const save = (apply = true) =>
    safely("正在校验并保存…", async () =>
      applyResult(await rpc("save", { ...proposal(), apply })),
    );
  const start = (candidate = draft, confirmed_runtime = null) =>
    safely("正在校验模型与配置…", async () => {
      const result = await rpc("start", {
        ...proposal(candidate),
        confirmed_runtime,
      });
      if (result.needs_model_sync) {
        setModal({
          kind: "modelSync",
          title: "同步模型规格",
          description:
            "模型实际输入与当前设置不同。确认后更新草稿，再继续启动。",
          changes: result.needs_model_sync,
          candidate,
          confirmed_runtime,
        });
        return;
      }
      if (result.needs_restart) {
        setModal({
          kind: "restart",
          title: owned ? "重启当前配置" : "切换运行配置",
          description:
            "将先保存并校验当前草稿，正常停止运行中的程序，再启动此配置。",
          identity: result.needs_restart,
          candidate,
        });
        return;
      }
      applyResult(result);
      if (!result.error) setRestartFor(null);
    });
  const primary = () => {
    if (owned && restartFor === active.process_id) start();
    else if (owned) save();
    else if (!draft?.values["runtime.vision.model_path"]) {
      setPage("devices");
      setDeviceTab("capture");
      chooseModel();
    } else start();
  };
  useEffect(() => {
    const key = (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "s") {
        e.preventDefault();
        if (draft && !modal && !busy) save();
      }
    };
    document.addEventListener("keydown", key);
    return () => document.removeEventListener("keydown", key);
  }, [draft, modal, busy, pointEdits]);
  useEffect(() => {
    if (selected && boot)
      rpc("select", { id: selected }).catch((e) =>
        setNotice({ error: true, text: e.message }),
      );
  }, [selected, boot]);
  const chooseModel = () =>
    safely("正在选择并读取模型…", async () => {
      const config = copy(draft.config);
      config.runtime = {
        ...config.runtime,
        vision: {
          ...config.runtime?.vision,
          ...Object.fromEntries(
            Object.entries(draft.values)
              .filter(([key]) => key.startsWith("runtime.vision."))
              .map(([key, value]) => [key.slice(15), value]),
          ),
        },
      };
      const result = await rpc("model", { config });
      if (result)
        setModal({
          kind: "modelSelect",
          title: "使用识别模型",
          description: result.path,
          changes: {
            "runtime.vision.model_path": result.path,
            ...result.changes,
          },
          candidate: draft,
        });
    });
  const chooseImport = () =>
    safely("正在读取配置文件…", async () => {
      const result = await rpc("import_choose");
      if (result)
        setModal({
          kind: "import",
          title: "导入独立配置",
          ...result,
          game: result.choices[0],
        });
    });
  const submitModal = async (e) => {
    e?.preventDefault();
    if (!modal || busy) return;
    const m = modal;
    await safely("正在处理…", async () => {
      if (m.kind === "create" || m.kind === "copy") {
        const p = await rpc(
          m.kind,
          m.kind === "create"
            ? { name: m.name }
            : { ...proposal(), name: m.name },
        );
        remember(p);
        setSelected(p.id);
        setPage("devices");
        setDeviceTab("capture");
      } else if (m.kind === "rename") {
        const p = await rpc("rename", { ...draft, name: m.name });
        setSaved((prev) => ({ ...prev, [p.id]: p }));
        setDraft({ ...draft, name: p.name, revision: p.revision });
      } else if (m.kind === "delete") {
        const archive = await rpc("delete", draft);
        setDrafts((prev) => {
          const next = { ...prev };
          delete next[draft.id];
          return next;
        });
        setSaved((prev) => {
          const next = { ...prev };
          delete next[draft.id];
          return next;
        });
        delete historyRef.current[draft.id];
        setSelected(Object.keys(drafts).find((id) => id !== draft.id) ?? null);
        setNotice({ text: "已移至可恢复备份：" + archive });
      } else if (m.kind === "import") {
        const p = await rpc("import", {
          token: m.token,
          name: m.name,
          game: m.game,
        });
        remember(p);
        setSelected(p.id);
        setPage("devices");
        setDeviceTab("capture");
      } else if (m.kind === "raw") {
        const p = await rpc("raw_apply", { ...draft, text: m.text });
        setDraft(p);
        recordCurve(draft.curve);
        setNotice({ text: "完整配置已校验并载入草稿，尚未保存。" });
      } else if (m.kind === "presetSave") {
        await rpc("curve_preset_save", {
          name: m.name,
          points: draft.curve.definition.points,
        });
        setNotice({ text: "曲线预设已保存。" });
      } else if (m.kind === "close") await rpc("window_close");
      else if (m.kind === "reload") {
        remember(await rpc("reload", { id: draft.id }));
        delete historyRef.current[draft.id];
      } else if (m.kind === "reset") {
        setPointEdits((prev) => ({ ...prev, [selected]: null }));
        setDraft(await rpc("reset", draft));
        recordCurve(draft.curve);
      } else if (m.kind === "modelSelect") {
        setDraft({
          ...m.candidate,
          values: { ...m.candidate.values, ...m.changes },
        });
        setNotice({ text: "模型与输入规格已载入草稿，保存后生效。" });
      }
      setModal(null);
    });
  };
  const modalAction = () => {
    if (modal.kind === "modelSync") {
      const candidate = {
        ...modal.candidate,
        values: { ...modal.candidate.values, ...modal.changes },
      };
      setDraft(candidate);
      setModal(null);
      start(candidate, modal.confirmed_runtime);
    } else if (modal.kind === "restart") {
      const m = modal;
      setModal(null);
      start(m.candidate, m.identity);
    } else submitModal();
  };
  const groups = boot ? Object.groupBy(boot.fields, (f) => f.group) : {};
  const grouped = (names, fold = false) =>
    names
      .filter((name) => groups[name]?.length)
      .map((name) => (
        <Group
          key={name}
          name={name}
          fields={groups[name]}
          draft={draft}
          setValue={setValue}
          fold={fold}
        />
      ));
  const namedModal = (kind, title) =>
    setModal({
      kind,
      title,
      name:
        kind === "rename"
          ? draft.name
          : kind === "copy"
            ? draft.name + " 副本"
            : "",
    });
  const logs = () =>
    safely("正在读取运行日志…", async () =>
      setModal({ kind: "logs", title: "运行日志", ...(await rpc("logs")) }),
    );
  const menuItems = draft
    ? [
        { label: "仅保存配置", action: () => save(false) },
        { label: "重启当前配置…", action: () => start() },
        {
          label: "编辑完整配置…",
          action: () =>
            safely("正在生成配置草稿…", async () =>
              setModal({
                kind: "raw",
                title: "完整配置 · 当前草稿",
                text: await rpc("raw", proposal()),
              }),
            ),
        },
        null,
        {
          label: "重新载入已保存内容…",
          action: () =>
            setModal({
              kind: "reload",
              title: "重新载入配置",
              description: "会丢弃此配置的未保存草稿，并读取磁盘上的最新版本。",
            }),
        },
        {
          label: "恢复初始参数…",
          action: () =>
            setModal({
              kind: "reset",
              title: "恢复初始参数",
              description:
                "将创建或导入时的参数和曲线载入草稿。保存后才影响配置文件。",
            }),
        },
      ]
    : [];
  return (
    <>
      <div className="app">
        <aside className="sidebar">
          <div className="brand">
            <div className="brandmark">
              <Gamepad2 size={23} />
            </div>
            <span>手柄助手</span>
          </div>
          <div className="navlabel">工作空间</div>
          <nav className="nav" aria-label="主导航">
            {[
              [SlidersHorizontal, "tuning"],
              [ChartSpline, "curves"],
              [Library, "profiles"],
              [Gamepad2, "devices"],
              [Activity, "feedback"],
            ].map(([Icon, id]) => (
              <button
                key={id}
                className={page === id ? "active" : ""}
                disabled={!!busy}
                aria-current={page === id ? "page" : undefined}
                title={titles[id]}
                onClick={() => setPage(id)}
              >
                <Icon />
                <span>{titles[id]}</span>
              </button>
            ))}
          </nav>
          <div className="sidebottom">
            <span className={"dot " + (active ? "" : "off")} />
            <span>{phases[status.phase]}</span>
            <p>
              关闭窗口后
              <br />
              运行中的程序继续工作
            </p>
          </div>
        </aside>
        <div className="shell">
          <header className="topbar">
            <div className="profile-top">
              <div className="profile-icon">
                <Gamepad2 size={21} />
              </div>
              <div>
                <select
                  className="profile-select"
                  aria-label="当前配置"
                  value={selected ?? ""}
                  disabled={!!busy || !boot}
                  onChange={(e) => {
                    setSelected(e.target.value);
                  }}
                >
                  {!draft && <option value="">尚无配置</option>}
                  {Object.values(drafts).map((p) => (
                    <option key={p.id} value={p.id}>
                      {p.name}
                      {dirty(p) ? " · 未保存" : ""}
                    </option>
                  ))}
                </select>
                <div className="small muted">
                  {busy || (dirty(draft) ? "有未保存修改" : "修改已保存")}
                </div>
              </div>
            </div>
            <div className="top-actions">
              <span className="run-state">
                <i className={"dot " + (active ? "" : "off")} />
                {phases[status.phase]}
              </span>
              <button
                className="quiet"
                disabled={!active || !!busy}
                onClick={() => safely("正在请求退出…", () => rpc("stop"))}
              >
                <Square size={13} />
                停止
              </button>
              <button
                className="primary"
                disabled={!draft || !!busy || transitioning}
                aria-busy={!!busy || transitioning}
                onClick={primary}
              >
                {busy || transitioning ? (
                  <LoaderCircle className="busy-spinner" size={15} />
                ) : owned ? (
                  <Save size={15} />
                ) : (
                  <Play size={15} />
                )}{" "}
                {busy
                  ? busy === "正在校验模型与配置…"
                    ? "正在检查…"
                    : "正在处理…"
                  : transitioning
                    ? phases[status.phase]
                    : owned
                      ? restartFor === active.process_id
                        ? "保存并重启"
                        : "保存并应用"
                      : active
                        ? "切换并启动"
                        : !draft?.values["runtime.vision.model_path"]
                          ? "选择模型"
                          : "启动配置"}
              </button>
              <Menu disabled={!draft || !!busy} items={menuItems} />
            </div>
          </header>
          {notice && (
            <div
              className={"notice " + (notice.error ? "error" : "")}
              role={notice.error ? "alert" : "status"}
            >
              <span>{notice.text}</span>
              <IconButton label="收起提示" onClick={() => setNotice(null)}>
                <X size={16} />
              </IconButton>
            </div>
          )}
          {status.phase === "failed" && (
            <div className="runtime-failure" role="alert">
              <span>
                运行失败：
                {status.error?.split("\n").filter(Boolean).at(-1) ||
                  "请查看日志"}
              </span>
              <button onClick={logs}>查看详情</button>
            </div>
          )}
          <main className="viewport">
            <div className="workspace">
              {!boot ? (
                <div className="empty">
                  <Gamepad2 size={54} />
                  <h1>连接工作空间</h1>
                  <p>{connection}</p>
                  <button onClick={() => location.reload()}>重新连接</button>
                </div>
              ) : (
                <>
                  <div className="heading">
                    <div>
                      <h1>{titles[page]}</h1>
                      <p>
                        {page === "tuning"
                          ? "从首次瞄准到持续跟随，分别调整手感。"
                          : page === "curves"
                            ? "编辑输入与响应的映射。"
                            : page === "profiles"
                              ? "每份配置独立保存参数与曲线。"
                              : page === "devices"
                                ? "选择识别模型，设置采集与输入设备。"
                                : "查看真实运行状态、帧率与响应学习。"}
                      </p>
                    </div>
                    {page === "profiles" && (
                      <button
                        className="primary"
                        disabled={!!busy}
                        onClick={() => namedModal("create", "新建配置")}
                      >
                        <Plus size={16} />
                        新建配置
                      </button>
                    )}
                  </div>
                  <fieldset
                    ref={viewport}
                    className="workspace-fields"
                    disabled={!!busy}
                  >
                    {page === "profiles" ? (
                      <>
                        <div className="tablewrap">
                          <table className="profiletable profile-list">
                            <thead>
                              <tr>
                                <th>配置名称</th>
                                <th>曲线</th>
                                <th>状态</th>
                                <th>操作</th>
                              </tr>
                            </thead>
                            <tbody>
                              {Object.values(drafts).map((p) => (
                                <tr
                                  key={p.id}
                                  className={
                                    selected === p.id ? "selected-row" : ""
                                  }
                                >
                                  <td>
                                    <div className="profilecell">
                                      <span className="profile-icon">
                                        <Gamepad2 size={19} />
                                      </span>
                                      <div>
                                        <strong>{p.name}</strong>
                                        <small>
                                          {p.values["runtime.vision.model_path"]
                                            ? "已选择模型"
                                            : "待选择模型"}
                                        </small>
                                      </div>
                                    </div>
                                  </td>
                                  <td>
                                    {
                                      {
                                        linear: "线性",
                                        cod_dynamic_legacy_lut: "动态",
                                        custom_lut: "自定义",
                                      }[p.curve.algorithm]
                                    }
                                  </td>
                                  <td>
                                    {dirty(p)
                                      ? "草稿未保存"
                                      : active?.profile_id === p.id
                                        ? "运行中"
                                        : "已保存"}
                                  </td>
                                  <td>
                                    <button
                                      disabled={selected === p.id}
                                      onClick={() => {
                                        setSelected(p.id);
                                      }}
                                    >
                                      {selected === p.id ? "当前配置" : "选用"}
                                    </button>
                                  </td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                          {!Object.keys(drafts).length && (
                            <div className="empty">
                              <Library size={42} />
                              <h2>创建第一份配置</h2>
                              <p>填写名称即可创建，随后选择模型。</p>
                            </div>
                          )}
                        </div>
                        <div className="library-actions">
                          <button onClick={chooseImport}>
                            <FolderOpen size={16} />
                            导入配置
                          </button>
                          {draft && (
                            <Menu
                              items={[
                                {
                                  label: "重命名…",
                                  action: () =>
                                    namedModal("rename", "重命名配置"),
                                },
                                {
                                  label: "复制当前草稿…",
                                  action: () => namedModal("copy", "复制配置"),
                                },
                                {
                                  label: "导出当前草稿…",
                                  action: () =>
                                    safely("正在导出配置…", async () => {
                                      const path = await rpc(
                                        "export_profile",
                                        proposal(),
                                      );
                                      return path
                                        ? "已导出：" + path
                                        : undefined;
                                    }),
                                },
                                null,
                                {
                                  label: "删除当前配置…",
                                  danger: true,
                                  action: () =>
                                    setModal({
                                      kind: "delete",
                                      title: "删除配置",
                                      description: `删除“${draft.name}”？未保存草稿将丢弃，已保存文件会移入可恢复备份。`,
                                    }),
                                },
                              ]}
                            />
                          )}
                        </div>
                      </>
                    ) : page === "feedback" ? (
                      <Feedback status={status} logs={logs} run={safely} />
                    ) : !draft ? (
                      <div className="empty">
                        <Library size={45} />
                        <h2>还没有可编辑的配置</h2>
                        <button onClick={() => setPage("profiles")}>
                          前往配置管理
                        </button>
                      </div>
                    ) : page === "tuning" ? (
                      <>
                        <div
                          className="sectionnav"
                          role="group"
                          aria-label="调校分类"
                        >
                          {[
                            ["acquire", "首次瞄准"],
                            ["follow", "持续跟随"],
                            ["assist", "输入与手动"],
                            ["target", "目标与识别"],
                            ["fire", "开火与压枪"],
                          ].map(([id, label]) => (
                            <button
                              key={id}
                              className={tab === id ? "active" : ""}
                              aria-pressed={tab === id}
                              onClick={() => {
                                setTab(id);
                                if (id === "acquire" || id === "follow")
                                  setPreviewMode(id);
                              }}
                            >
                              {label}
                            </button>
                          ))}
                        </div>
                        <div
                          className={
                            "split tuning-layout " +
                            (["assist", "fire"].includes(tab)
                              ? "without-preview"
                              : "")
                          }
                        >
                          <div key={tab}>
                            {(() => {
                              const names = {
                                acquire: ["首次瞄准"],
                                follow: ["持续跟随"],
                                assist: [
                                  "辅助输入",
                                  "响应学习与手动介入",
                                  "手动输出补偿",
                                ],
                                target: ["目标与识别"],
                                fire: ["开火与压枪"],
                              }[tab];
                              return names.map((name) =>
                                ["响应学习与手动介入", "手动输出补偿"].includes(
                                  name,
                                ) ? (
                                  <Group
                                    key={name}
                                    name={name}
                                    fields={groups[name] ?? []}
                                    draft={draft}
                                    setValue={setValue}
                                    fold
                                  />
                                ) : (
                                  <React.Fragment key={name}>
                                    {(groups[name] ?? []).some(
                                      (f) => !f.advanced,
                                    ) && (
                                      <Group
                                        name={name}
                                        fields={groups[name].filter(
                                          (f) => !f.advanced,
                                        )}
                                        draft={draft}
                                        setValue={setValue}
                                        fold={
                                          name === "响应学习与手动介入" ||
                                          name === "手动输出补偿"
                                        }
                                      />
                                    )}
                                    {groups[name]?.some((f) => f.advanced) && (
                                      <Group
                                        name={name + " · 高级参数"}
                                        fields={groups[name].filter(
                                          (f) => f.advanced,
                                        )}
                                        draft={draft}
                                        setValue={setValue}
                                        fold
                                      />
                                    )}
                                  </React.Fragment>
                                ),
                              );
                            })()}
                          </div>
                          <div hidden={["assist", "fire"].includes(tab)}>
                            <Preview
                              key={selected}
                              draft={draft}
                              disabled={!!busy}
                              mode={previewMode}
                              showModeSwitch={tab === "target"}
                              setMode={(mode) => {
                                setPreviewMode(mode);
                                if (tab === "acquire" || tab === "follow")
                                  setTab(mode);
                              }}
                            />
                          </div>
                        </div>
                      </>
                    ) : page === "curves" ? (
                      <CurveEditor
                        key={selected}
                        draft={draft}
                        seeds={boot.seeds}
                        onCurve={onCurve}
                        history={history}
                        run={safely}
                        setModal={setModal}
                        pointEdit={pointEdits[selected]}
                        onPointEdit={(edit) =>
                          setPointEdits((prev) => ({
                            ...prev,
                            [selected]: edit,
                          }))
                        }
                      />
                    ) : page === "devices" ? (
                      <>
                        <div
                          className="sectionnav"
                          role="group"
                          aria-label="设备分类"
                        >
                          {[
                            ["capture", "模型与采集"],
                            ["input", "手柄与诊断"],
                          ].map(([key, label]) => (
                            <button
                              key={key}
                              className={deviceTab === key ? "active" : ""}
                              aria-pressed={deviceTab === key}
                              onClick={() => setDeviceTab(key)}
                            >
                              {label}
                            </button>
                          ))}
                        </div>
                        <section
                          className="section model-section"
                          hidden={deviceTab !== "capture"}
                        >
                          <h2>识别模型</h2>
                          <div className="model-file">
                            <span className="fileicon">ENGINE</span>
                            <div className="model-name">
                              <strong>
                                {draft.values["runtime.vision.model_path"]
                                  ?.split(/[\\/]/)
                                  .at(-1) || "尚未选择模型"}
                              </strong>
                              <p className="small muted">
                                {draft.values["runtime.vision.model_path"]
                                  ? `${draft.values["runtime.vision.tensor_width"]} × ${draft.values["runtime.vision.tensor_height"]} · 模型输入`
                                  : "选择模型后读取输入尺寸"}
                              </p>
                              <p className="small muted path">
                                {draft.values["runtime.vision.model_path"]}
                              </p>
                            </div>
                            <button onClick={chooseModel}>选择模型…</button>
                          </div>
                        </section>
                        <div className={"device-grid device-tab-" + deviceTab}>
                          <section>
                            <h2>捕获与推理</h2>
                            {(groups["捕获与模型"] ?? [])
                              .filter(
                                (f) =>
                                  ![
                                    "runtime.vision.model_path",
                                    "runtime.vision.tensor_width",
                                    "runtime.vision.tensor_height",
                                  ].includes(f.path),
                              )
                              .map((f) => (
                                <Field
                                  key={f.path}
                                  field={f}
                                  value={draft.values[f.path]}
                                  onChange={(v) => setValue(f.path, v)}
                                />
                              ))}
                          </section>
                          <section>
                            <div className="sectionhead">
                              <h2>输入设备</h2>
                              <button
                                onClick={() =>
                                  safely("正在枚举手柄…", async () => {
                                    setDevices(await rpc("devices"));
                                    return "手柄列表已刷新。";
                                  })
                                }
                              >
                                刷新
                              </button>
                            </div>
                            <label className="stack">
                              选择手柄
                              <select
                                value={
                                  draft.values["runtime.input.auto_detect"]
                                    ? "auto"
                                    : draft.values["runtime.input.device_id"]
                                }
                                onChange={(e) => {
                                  const d = devices.find(
                                    (x) => x.id === e.target.value,
                                  );
                                  valuesUpdate({
                                    "runtime.input.auto_detect":
                                      e.target.value === "auto",
                                    "runtime.input.device_id": d?.id ?? "",
                                    "runtime.input.device_name": d?.name ?? "",
                                  });
                                }}
                              >
                                <option value="auto">自动选择</option>
                                {!draft.values["runtime.input.auto_detect"] &&
                                  !devices.some(
                                    (d) =>
                                      d.id ===
                                      draft.values["runtime.input.device_id"],
                                  ) && (
                                    <option
                                      value={
                                        draft.values["runtime.input.device_id"]
                                      }
                                    >
                                      {draft.values[
                                        "runtime.input.device_name"
                                      ] || "保存的设备（未枚举）"}
                                    </option>
                                  )}
                                {devices.map((d) => (
                                  <option key={d.id} value={d.id}>
                                    {d.name}
                                  </option>
                                ))}
                              </select>
                            </label>
                            {(groups["输入设备"] ?? [])
                              .filter(
                                (f) =>
                                  f.path === "runtime.input.controller_index",
                              )
                              .map((f) => (
                                <Field
                                  key={f.path}
                                  field={f}
                                  value={draft.values[f.path]}
                                  onChange={(v) => setValue(f.path, v)}
                                />
                              ))}
                            <p className="note">
                              切换输入设备后需要重启。刷新列表只枚举设备，不启动输出。
                            </p>
                            <h2 style={{ marginTop: 28 }}>诊断记录</h2>
                            {(groups["诊断记录"] ?? []).map((f) => (
                              <Field
                                key={f.path}
                                field={f}
                                value={draft.values[f.path]}
                                onChange={(v) => setValue(f.path, v)}
                              />
                            ))}
                          </section>
                        </div>
                      </>
                    ) : null}
                  </fieldset>
                  <footer className="footer">
                    <span>
                      {anyDirty ? "草稿尚未保存" : "配置与曲线独立保存"} · Ctrl
                      + S 保存并应用
                    </span>
                    <span>运行状态来自原生程序</span>
                  </footer>
                </>
              )}
            </div>
          </main>
        </div>
      </div>
      <Dialog.Root
        open={!!modal}
        onOpenChange={(open) => {
          if (!open && !busy) setModal(null);
        }}
      >
        <Dialog.Portal>
          <Dialog.Overlay className="dialog-overlay" />
          <Dialog.Content
            className={
              "dialog-content " +
              (["raw", "logs"].includes(modal?.kind) ? "wide" : "")
            }
            onEscapeKeyDown={(e) => {
              if (busy) e.preventDefault();
            }}
            onPointerDownOutside={(e) => e.preventDefault()}
          >
            <Dialog.Title>{modal?.title}</Dialog.Title>
            <Dialog.Description>
              {modal?.description ||
                (modal?.kind === "raw"
                  ? "校验后载入当前草稿，保存后才写入配置。"
                  : modal?.kind === "logs"
                    ? "实际运行日志。错误会保留在这里。"
                    : "")}
            </Dialog.Description>
            <IconButton
              label="关闭对话框"
              className="dialog-close"
              disabled={!!busy}
              onClick={() => setModal(null)}
            >
              <X size={18} />
            </IconButton>
            <form onSubmit={submitModal}>
              <fieldset className="modal-fields" disabled={!!busy}>
                {["create", "copy", "rename", "import", "presetSave"].includes(
                  modal?.kind,
                ) && (
                  <label className="stack">
                    名称
                    <input
                      autoFocus
                      required
                      maxLength="80"
                      value={modal.name ?? ""}
                      onChange={(e) =>
                        setModal({ ...modal, name: e.target.value })
                      }
                    />
                  </label>
                )}
                {modal?.kind === "import" && (
                  <label className="stack">
                    文件中的配置分支
                    <select
                      value={modal.game}
                      onChange={(e) =>
                        setModal({ ...modal, game: e.target.value })
                      }
                    >
                      {modal.choices.map((c) => (
                        <option key={c}>{c}</option>
                      ))}
                    </select>
                  </label>
                )}
                {modal?.changes && (
                  <dl className="changes">
                    {Object.entries(modal.changes).map(([key, value]) => (
                      <div key={key}>
                        <dt>
                          {boot?.fields.find((f) => f.path === key)?.label ??
                            key}
                        </dt>
                        <dd>{String(value)}</dd>
                      </div>
                    ))}
                  </dl>
                )}
                {modal?.kind === "raw" && (
                  <textarea
                    className="code-editor"
                    spellCheck="false"
                    value={modal.text}
                    onChange={(e) =>
                      setModal({ ...modal, text: e.target.value })
                    }
                  />
                )}
                {modal?.kind === "logs" && (
                  <>
                    <button type="button" disabled={!!busy} onClick={logs}>
                      刷新日志
                    </button>
                    <h3>标准错误</h3>
                    <pre>{modal.stderr || "暂无错误输出"}</pre>
                    <h3>运行输出</h3>
                    <pre>{modal.stdout || "暂无运行输出"}</pre>
                  </>
                )}
                {modal?.kind === "presets" && (
                  <div className="preset-list">
                    {modal.entries.map((p) => (
                      <button
                        type="button"
                        key={p.id}
                        onClick={() => {
                          onCurve({
                            algorithm: "custom_lut",
                            definition: {
                              schema_version: p.schema_version,
                              kind: p.kind,
                              name: p.name,
                              interpolation: p.interpolation,
                              points: p.points,
                            },
                          });
                          setModal(null);
                        }}
                      >
                        {p.name}
                        <span>{p.points.length} 个点</span>
                      </button>
                    ))}
                    {!modal.entries.length && (
                      <p>
                        暂无曲线预设。在曲线菜单中选择“存为曲线预设”即可添加。
                      </p>
                    )}
                    {modal.errors.map((e, i) => (
                      <p className="inline-error" key={i}>
                        {e}
                      </p>
                    ))}
                  </div>
                )}
                {notice?.error && modal && (
                  <p className="inline-error" role="alert">
                    {notice.text}
                  </p>
                )}
                <div className="actions">
                  <button
                    type="button"
                    disabled={!!busy}
                    onClick={() => setModal(null)}
                  >
                    {["logs", "presets"].includes(modal?.kind)
                      ? "关闭"
                      : "取消"}
                  </button>
                  {!["logs", "presets"].includes(modal?.kind) && (
                    <button
                      type="button"
                      className={
                        "primary " +
                        (modal?.kind === "delete" ? "danger-button" : "")
                      }
                      disabled={!!busy}
                      onClick={modalAction}
                    >
                      {busy ||
                        ({
                          delete: "删除并保留备份",
                          close: "丢弃草稿并关闭",
                          raw: "校验并载入草稿",
                          modelSync: "同步并继续",
                          restart: "保存并重启",
                          reset: "载入初始参数",
                        }[modal?.kind] ??
                          "确定")}
                    </button>
                  )}
                </div>
              </fieldset>
            </form>
          </Dialog.Content>
        </Dialog.Portal>
      </Dialog.Root>
    </>
  );
}
createRoot(document.getElementById("root")).render(<App />);
