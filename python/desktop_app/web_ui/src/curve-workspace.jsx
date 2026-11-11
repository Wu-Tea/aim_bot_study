import { Select } from "./controls";
import React, { useEffect, useState } from "react";
import { ArrowLeft } from "lucide-react";
import { rpc, copy } from "./bridge";
import { CurveSettings } from "./curve-settings";
import { CurveEditor } from "./curve-editor";
import { interpolate, curvePath } from "./curve-preview";

const identity = { schema_version: 1, kind: 'normalized_stick_response', name: '新曲线',
  interpolation: 'piecewise_linear', points: [[0, 0], [1, 1]], source: { kind: 'manual' } };

export function CurveWorkspace({ draft, seeds, fields, setValue, onApply, run, cache, onStateChange, saveRef }) {
  const [tab, setTab] = useState(cache.tab || 'transfer');
  const [library, setLibrary] = useState({ entries: [], errors: [] });
  const [baseId, setBaseId] = useState(cache.baseId || ''), [targetId, setTargetId] = useState(cache.targetId || '');
  const [preparing, setPreparing] = useState(cache.preparing || false);
  const [result, setResult] = useState(cache.result || null), [probe, setProbe] = useState(cache.probe ?? 50);
  const [edit, setEdit] = useState(cache.edit || null), [original, setOriginal] = useState(cache.original || null);
  const [editorOpen, setEditorOpen] = useState(cache.editorOpen ?? !!cache.edit);
  const [pointEdit, setPointEdit] = useState(cache.pointEdit || null), [archiveConfirm, setArchiveConfirm] = useState(false);
  const [imported, setImported] = useState(cache.imported || []);
  const [message, setMessage] = useState('');
  const [discard, setDiscard] = useState(null);
  const [ready, setReady] = useState(false);
  const [loadError, setLoadError] = useState(''), [loadAttempt, setLoadAttempt] = useState(0);
  const entries = [...imported, ...library.entries];
  const base = entries.find((e) => e.id === baseId), target = entries.find((e) => e.id === targetId);
  const dirty = edit && JSON.stringify(edit) !== JSON.stringify(original);
  const editPending = !!edit && (!!dirty || !!pointEdit || !edit.id || !!edit.temporary);
  const compositionPending = !!(baseId || targetId || result);
  const changeSelection = (setter, value) => { setter(value); setResult(null); setMessage(''); };
  useEffect(() => { Object.assign(cache, { tab, edit, original, pointEdit, editorOpen }); }, [tab, edit, original, pointEdit, editorOpen]);
  useEffect(() => { Object.assign(cache, { baseId, targetId, preparing, result, probe, imported }); }, [baseId, targetId, preparing, result, probe, imported]);
  useEffect(() => { onStateChange(draft.id, { pending: editPending || compositionPending, inLibrary: tab === 'library' }); }, [draft.id, editPending, compositionPending, tab, onStateChange]);
  useEffect(() => {
    let active = true;
    setReady(false); setLoadError('');
    (async () => {
      const data = await rpc('curve_library');
      if (active) { setLibrary(data); setReady(true); }

    })().catch((error) => { if (active) setLoadError(error.message); });
    return () => { active = false; };
  }, [loadAttempt]);
  const calculate = () => run('正在计算…', async () => {
    const computed = await rpc('curve_compose', { base, target, name: `${base.name} → ${target.name}`.slice(0, 80) });
    setResult(computed); setMessage('');
  });
  const openEditor = (entry) => { setEdit(copy(entry)); setOriginal(copy(entry)); setPointEdit(null); setArchiveConfirm(false); setMessage(''); setEditorOpen(true); };
  const backToLibrary = () => {
    setEditorOpen(false); setArchiveConfirm(false);
    // Navigation preserves both applied edits and unfinished point text.
    if (!editPending) { setEdit(null); setOriginal(null); }
  };
  const importCurve = () => run('正在导入…', async () => {
    const data = await rpc('curve_import');
    if (!data) { setMessage('已取消导入'); return; }
    const entry = { ...data, id: `import-${Date.now()}`, temporary: true };
    setImported((prev) => [...prev, entry]);
    if (!preparing) openEditor(entry);
    else changeSelection(setBaseId, entry.id);
  });
  const saveLibrary = (asNew = false) => run('正在保存曲线…', async () => {
    const data = await rpc('curve_library_save', { ...edit, id: !asNew && !edit.readonly && !edit.temporary ? edit.id : undefined });
    setLibrary(data); setEdit(null); setOriginal(null); setMessage('已保存到曲线库');
  });
  const saveResult = () => run('正在保存曲线…', async () => {
    setLibrary(await rpc('curve_library_save', result)); setMessage('转译曲线已保存到曲线库'); setPreparing(false); setResult(null); setBaseId(''); setTargetId('');
  });
  const exportCurve = (curve) => run('正在导出曲线…', async () => {
    const path = await rpc('curve_export', curve);
    return path ? `已导出：${path}` : '已取消导出';
  });
  const discardChanges = () => {
    if (discard === 'edit') { setEdit(null); setOriginal(null); setPointEdit(null); setEditorOpen(false); setArchiveConfirm(false); }
    else { setBaseId(''); setTargetId(''); setResult(null); setProbe(50); setPreparing(false); }
    setDiscard(null); setMessage('已放弃未保存的修改');
  };
  // Ctrl+S belongs to the current workspace; library edits never save a profile.
  useEffect(() => {
    const saveCurrent = tab === 'library' ? () => {
      if (edit && !preparing) {
        setEditorOpen(true);
        if (pointEdit) { setMessage('请先应用或取消点位编辑，再保存曲线。'); return; }
        if (!edit.name.trim()) { setMessage('请填写曲线名称。'); return; }
        return saveLibrary();
      }
      if (result) {
        setPreparing(true);
        if (!result.name.trim()) { setMessage('请填写曲线名称。'); return; }
        return saveResult();
      }
      setMessage(compositionPending ? '请先计算转译曲线，再存入曲线库。' : '请先选择或新建一条曲线。');
    } : null;
    saveRef.current = saveCurrent;
    return () => { if (saveRef.current === saveCurrent) saveRef.current = null; };
  });
  const sample = result && base && target && {
    target: interpolate(target.points, probe / 100),
    output: interpolate(result.points, probe / 100),
    actual: interpolate(base.points, interpolate(result.points, probe / 100)),
  };
  return <div className="curve-workspace">
    <div className="sectionnav" aria-label="曲线工作区">
      <button className={tab === 'transfer' ? 'active' : ''} onClick={() => setTab('transfer')}>转译设置</button>
      <button className={tab === 'library' ? 'active' : ''} onClick={() => { setTab('library'); setPreparing(false); if (edit && editorOpen) backToLibrary(); }}>曲线库</button>
    </div>
    {message && <p role="status" className="curve-status">{message}</p>}
    {loadError && <div className="inline-error">{loadError}<button onClick={() => setLoadAttempt((n) => n + 1)}>重新读取曲线库</button></div>}
    {discard && <div className="curve-library-toolbar" role="alert"><span>放弃这次{discard === 'edit' ? '曲线编辑' : '融合制作'}？未保存的修改将丢失。</span><button onClick={discardChanges}>确认放弃</button><button onClick={() => setDiscard(null)}>保留修改</button></div>}
    {tab === 'transfer' ? <CurveSettings draft={draft} seeds={seeds}
      library={library.entries} ready={ready} fields={fields} setValue={setValue}
      onStage={onApply}/> : preparing ? <>
      <div className="curve-library-toolbar"><button onClick={() => setPreparing(false)}><ArrowLeft size={16} />返回曲线库</button><h2>制作融合曲线</h2>{compositionPending && <button onClick={() => setDiscard('composition')}>放弃制作</button>}</div>
      <section className="curve-selection">
        <label className="stack">基准曲线 A<Select aria-label="基准曲线 A" value={baseId} onChange={(e) => changeSelection(setBaseId, e.target.value)}>
          <option value="">选择当前游戏响应曲线</option>{entries.map((e) => <option key={e.id} value={e.id}>{e.name}{e.temporary ? '（临时导入）' : ''}</option>)}
        </Select></label>
        <label className="stack">目标曲线 B<Select aria-label="目标曲线 B" value={targetId} onChange={(e) => changeSelection(setTargetId, e.target.value)}>
          <option value="">选择目标响应曲线</option>
          {entries.map((e) => <option key={e.id} value={e.id}>{e.name}</option>)}
        </Select></label>
        <div className="curve-selection-actions"><button disabled={!base || !target || !ready} onClick={calculate}>计算转译曲线</button><button onClick={importCurve}>临时导入…</button></div>
      </section>
      {sample ? <div className="curve-layout curve-result">
        <div className="chart">
          <div className="curve-legend"><span className="curve-key-game">A · 基准响应</span><span className="curve-key-target">B · 目标响应</span><span className="curve-key-output">C · 转译输出</span></div>
          <div className="chart-title"><span>输出 / 响应 %</span><span>输入 %</span></div>
          <svg viewBox="0 0 660 410" aria-label="基准、目标与转译三条曲线" className="curve-composition-plot">
            {[0,.25,.5,.75,1].map((f) => <g key={f}><path d={`M50 ${360-f*310}H610M${50+f*560} 50V360`} stroke="var(--line-muted)"/><text x="6" y={364-f*310}>{f*100}</text><text x={50+f*560} y="390" textAnchor="middle">{f*100}</text></g>)}
            <path id="baseCurve" d={curvePath(base.points)} fill="none" stroke="var(--muted)" strokeWidth="2" strokeDasharray="6 5"/>
            <path id="targetCurve" d={curvePath(target.points)} fill="none" stroke="var(--accent)" strokeWidth="3"/>
            <path id="translationCurve" d={curvePath(result.points)} fill="none" stroke="var(--warning)" strokeWidth="3" strokeDasharray="10 3 2 3"/>
          </svg>
        </div>
        <aside className="curve-side"><h2>换算结果</h2>
          <label className="stack">曲线名称<input aria-label="计算结果名称" maxLength="80" value={result.name} onChange={(e) => setResult({ ...result, name: e.target.value })}/></label>
          <div className="curve-probe-control"><label htmlFor="composition-probe">验证输入 <output>{probe}%</output></label><input id="composition-probe" type="range" min="0" max="100" value={probe} onChange={(e) => setProbe(Number(e.target.value))}/></div>
          <dl className="curve-readout"><div><dt>转译输出</dt><dd>{(sample.output*100).toFixed(2)}%</dd></div><div><dt>目标响应</dt><dd>{(sample.target*100).toFixed(2)}%</dd></div><div><dt>计算响应</dt><dd>{(sample.actual*100).toFixed(2)}%</dd></div><div><dt>偏差 / 百分点</dt><dd>{(Math.abs(sample.actual-sample.target)*100).toFixed(3)}</dd></div></dl>
          <div className="curve-result-actions"><button disabled={!result.name.trim()} onClick={saveResult}>存入曲线库</button><button disabled={!result.name.trim()} onClick={() => exportCurve(result)}>导出…</button></div>
        </aside>
      </div> : <div className="curve-empty">选择基准和目标曲线，计算转译结果。</div>}
    </> : <>
      {(!edit || !editorOpen) && <>
      {edit && <div className="curve-library-toolbar"><button onClick={() => setEditorOpen(true)}>继续编辑</button><span>{edit.name} · 未保存</span><button onClick={() => setDiscard('edit')}>放弃修改</button></div>}
      <div className="curve-library-toolbar"><button disabled={editPending} onClick={() => openEditor(copy(identity))}>新建曲线</button><button disabled={editPending} onClick={() => { setPreparing(true); setMessage(''); }}>{baseId || targetId ? '继续制作融合曲线' : '制作融合曲线'}</button><button disabled={editPending} onClick={importCurve}>导入曲线…</button></div>
      {library.errors.map((e) => <p className="inline-error" key={e}>{e}</p>)}
      <div className="tablewrap"><table className="profiletable"><thead><tr><th>名称</th><th>来源</th><th>点数</th><th>操作</th></tr></thead><tbody>
        {library.entries.map((e) => <tr key={e.id}><td>{e.name}</td><td>{e.readonly ? '内置 · ' : ''}{{measurement:'实测',analytic:'公式',calculated:'曲线换算',manual:'手动编辑'}[e.source?.kind] || '导入'}</td><td>{e.points.length}</td><td><button disabled={editPending} onClick={() => openEditor(e)}>查看 / 编辑</button></td></tr>)}
      </tbody></table></div>
      </>}
      {edit && editorOpen && <section className="curve-library-editor">
        <div className="curve-library-toolbar"><button onClick={backToLibrary}><ArrowLeft size={16} />返回曲线库</button><label>名称<input className="text-field" aria-label="库中曲线名称" maxLength="80" value={edit.name} onChange={(e) => setEdit({ ...edit, name: e.target.value })}/></label>
          {editPending && <span className="save-state dirty">未保存</span>}
          <button disabled={!!pointEdit || !edit.name.trim()} onClick={() => saveLibrary(false)}>{edit.readonly ? '保存为副本' : '保存曲线'}</button>
          {!edit.readonly && edit.id && !edit.temporary && <button disabled={!!pointEdit} onClick={() => saveLibrary(true)}>另存副本</button>}
          <button disabled={!!pointEdit || !edit.name.trim()} onClick={() => exportCurve(edit)}>导出…</button>
          {editPending && <button onClick={() => setDiscard('edit')}>放弃修改</button>}
          {!edit.readonly && edit.id && !edit.temporary && <button disabled={editPending} onClick={() => setArchiveConfirm(true)}>移入归档</button>}
        </div>
        {archiveConfirm && <div className="curve-library-toolbar"><span>将“{edit.name}”移入归档？</span><button onClick={() => run('正在归档曲线…', async () => { setLibrary(await rpc('curve_library_archive', { id: edit.id })); setMessage(`已归档“${edit.name}”`); setEdit(null); setOriginal(null); setArchiveConfirm(false); })}>确认归档</button><button onClick={() => setArchiveConfirm(false)}>取消</button></div>}
        {edit.source?.url && /^https?:\/\//.test(edit.source.url) && <a href={edit.source.url} target="_blank" rel="noreferrer">数据来源</a>}
        {edit.source?.kind === 'calculated' && <p className="curve-status">{edit.source.base_name} → {edit.source.target_name}</p>}
        <CurveEditor key={edit.id || 'new'} draft={{ ...draft, curve: { algorithm:'custom_lut', definition: edit } }}
          onCurve={(curve) => setEdit({ ...edit, points: curve.definition.points, source: { ...edit.source, kind:'manual' } })}
          pointEdit={pointEdit} onPointEdit={setPointEdit}/>
      </section>}
    </>}
  </div>;
}
