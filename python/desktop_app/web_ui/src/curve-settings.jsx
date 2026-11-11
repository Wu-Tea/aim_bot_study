import { Select } from "./controls";
import React, { useMemo, useState } from 'react';
import { copy } from './bridge';
import { interpolate, curvePath, manualResponse, manualResponsePoints } from './curve-preview';

const decode = (text) => text.split(';').map((p) => p.split(':').map(Number));
const encode = (points) => points.map((p) => p.join(':')).join(';');
const sameCurve = (a, b) => !!a && !!b && [...a.points, ...b.points].every(([x]) =>
  Math.abs(interpolate(a.points, x) - interpolate(b.points, x)) < 1e-7);

export function curveOptions(library, snapshots) {
  const options = [...library];
  const selected = snapshots.map((snapshot, index) => {
    let match = options.find((e) => e.name === snapshot.name && sameCurve(e, snapshot)) || options.find((e) => sameCurve(e, snapshot));
    if (!match) { match = { ...snapshot, id: `snapshot-${index}` }; options.push(match); }
    return match.id;
  });
  return { options, selected };
}

const inputSnapshot = (profile, seeds) => {
  const v = profile.values;
  // Old opt-out profiles remain passthrough until the user chooses a mapping.
  if (!v['gamepad.follow_response_curve']) return { name: '线性 1:1', points: seeds.linear };
  if (v['gamepad.output_transfer.response_model'] === 'output_lut' && v['gamepad.output_transfer.custom_points'])
    return { name: v['gamepad.output_transfer.curve_name'] || '输入转译曲线', points: decode(v['gamepad.output_transfer.custom_points']) };
  if (profile.input_curve) return profile.input_curve;
  // Legacy input shaping is shown as a snapshot; no curve is recomputed when
  // users select an AI curve. Old output models are replaced only on Save.
  if (!v['gamepad.output_transfer.enabled']) return profile.curve.definition;
  return { name: '线性 1:1', points: seeds.linear };
};

export function CurveSettings({ draft, seeds, library, ready, fields, setValue, onStage }) {
  const [probe, setProbe] = useState(50);
  const aiSnapshot = draft.curve.definition;
  const manualSnapshot = inputSnapshot(draft, seeds);
  const catalog = useMemo(() => curveOptions(library, [manualSnapshot, aiSnapshot]), [library, draft.curve, draft.values, seeds]);
  const [inputId, aiId] = catalog.selected;
  const input = catalog.options.find((e) => e.id === inputId), ai = catalog.options.find((e) => e.id === aiId);
  const enabled = draft.values['gamepad.output_transfer.enabled'];
  const outerDeadzone = enabled ? (draft.values['gamepad.output_transfer.outer_deadzone'] ?? 0) : 0;
  const deadzone = enabled ? draft.values['gamepad.output_transfer.deadzone'] : 0;
  const validDeadzones = [deadzone, outerDeadzone].every((v) => Number.isFinite(v) && v >= 0 && v <= .5) && deadzone + outerDeadzone < 1;
  const inputPoints = validDeadzones ? manualResponsePoints(input.points, deadzone, outerDeadzone) : [];
  const inputResponse = validDeadzones ? manualResponse(input.points, probe / 100, deadzone, outerDeadzone) : null;
  const aiResponse = interpolate(ai.points, probe / 100);
  const selection = (nextInput = input, nextAi = ai) => ({
    curve: { algorithm:'custom_lut', definition: { schema_version:1, kind:'normalized_stick_response', interpolation:'piecewise_linear', name:nextAi.name, points:copy(nextAi.points) } },
    values: {
      'gamepad.follow_response_curve':true,
      'gamepad.output_transfer.enabled':true,
      'gamepad.output_transfer.response_model':'output_lut',
      'gamepad.output_transfer.custom_points':encode(nextInput.points),
      'gamepad.output_transfer.curve_name':nextInput.name,
      'gamepad.output_transfer.deadzone':deadzone,
      'gamepad.output_transfer.outer_deadzone':outerDeadzone,
      'gamepad.output_transfer.axial':enabled ? draft.values['gamepad.output_transfer.axial'] : false,
    },
  });
  const stage = (nextInput, nextAi) => { const next=selection(nextInput,nextAi); onStage(next.curve,next.values); };
  const stageDeadzone = (path, value) => {
    const next = selection();
    onStage(next.curve, { ...next.values, [path]: value });
  };
  return <>
    <section className="curve-calibration"><div className="sectionhead"><h2>响应标定</h2></div>
      <div className="curve-prior-grid">{fields.filter((f) => f.path.endsWith('_initial_scale')).map((f) => <label className="stack" key={f.path} htmlFor={'f-' + f.path}>{f.label}<span className="number"><input id={'f-' + f.path} aria-label={f.label} type="number" min="0" step="any" placeholder="默认值"
        aria-invalid={draft.values[f.path] !== 0 && (!Number.isFinite(draft.values[f.path]) || draft.values[f.path] < 80)} value={draft.values[f.path] === 0 ? '' : draft.values[f.path]} onChange={(e) => setValue(f.path,e.target.value === '' ? 0 : Number(e.target.value))}/><span>px/s</span></span></label>)}</div>
    </section>
    <section className="curve-use-selectors">
      <div className="curve-input-field"><label htmlFor="input-curve">用户输入转译曲线</label>
        <Select id="input-curve" aria-label="用户输入转译曲线" value={inputId} disabled={!ready}
          onChange={(e) => stage(catalog.options.find((item) => item.id === e.target.value), ai)}>{catalog.options.map((e) => <option key={e.id} value={e.id}>{e.name}</option>)}</Select></div>
      <label className="stack">AI 输出解算曲线<Select aria-label="AI 输出解算曲线" value={aiId} disabled={!ready}
        onChange={(e) => stage(input, catalog.options.find((item) => item.id === e.target.value))}>{catalog.options.map((e) => <option key={e.id} value={e.id}>{e.name}</option>)}</Select></label>
    </section>
    <section className="curve-deadzones" aria-label="死区设置">
      {[
        ['deadzone', '起点死区', deadzone, '游戏开始响应前的输入范围。例如 20% 表示游戏在前 20% 行程内不响应。'],
        ['outer_deadzone', '终点死区', outerDeadzone, '游戏已达到满速后的输入范围。例如 10% 表示 90% 输入就达到满速。'],
      ].map(([key, label, value, help]) => <label className="stack" key={key} title={help}>
        <span>{label}</span><span className="number"><input id={'f-gamepad.output_transfer.' + key} aria-label={label} type="number" min="0" max="50" step="0.1" disabled={!ready}
          aria-invalid={!Number.isFinite(value) || value < 0 || value > .5 || deadzone + outerDeadzone >= 1} value={Number.isFinite(value) ? Number((value * 100).toFixed(6)) : ''}
          onChange={(e) => stageDeadzone('gamepad.output_transfer.' + key, e.target.value === '' ? 0 : Number(e.target.value) / 100)}/><span>%</span></span>
      </label>)}
    </section>
    <section className="curve-settings-preview" aria-label="当前选择的曲线预览">
      <div className="chart">
        <div className="curve-legend"><span className="curve-key-output">用户输入 · {input.name}</span><span className="curve-key-target">AI 响应 · {ai.name}</span></div>
        <div className="chart-title"><span>输出 / 响应 %</span><span>输入 %</span></div>
        <svg viewBox="0 0 660 280" aria-label="用户输入转译曲线与 AI 响应曲线" className="curve-composition-plot">
          {[0,.25,.5,.75,1].map((f) => <g key={f}><path d={`M50 ${230-f*200}H610M${50+f*560} 30V230`} stroke="var(--line-muted)"/><text x="6" y={234-f*200}>{f*100}</text><text x={50+f*560} y="260" textAnchor="middle">{f*100}</text></g>)}
          <path id="aiPreviewCurve" d={curvePath(ai.points,230,200)} fill="none" stroke="var(--accent)" strokeWidth="4"/>
          <path id="inputPreviewCurve" d={curvePath(inputPoints,230,200)} fill="none" stroke="var(--warning)" strokeWidth="2" strokeDasharray="9 5"/>
          <path d={`M${50+probe*5.6} 30V230`} stroke="var(--muted)" strokeDasharray="3 5"/>
          <circle cx={50+probe*5.6} cy={230-aiResponse*200} r="5" fill="var(--paper)" stroke="var(--accent)" strokeWidth="2"/>
          {inputResponse !== null && <circle cx={50+probe*5.6} cy={230-inputResponse*200} r="3" fill="var(--warning)"/>}
        </svg>
      </div>
      <aside className="curve-preview-values">
        <h2>曲线预览</h2>
        <p className="curve-effective-range">有效区间 {validDeadzones ? `${Number((deadzone*100).toFixed(2))}%～${Number(((1-outerDeadzone)*100).toFixed(2))}%` : '—'}</p>
        <div className="curve-probe-control"><label htmlFor="curve-preview-probe">输入位置 <output>{probe}%</output></label><input id="curve-preview-probe" type="range" min="0" max="100" value={probe} onChange={(e) => setProbe(Number(e.target.value))}/></div>
        <dl className="curve-readout"><div><dt>用户转译输出</dt><dd>{inputResponse === null ? '—' : (inputResponse*100).toFixed(2)+'%'}</dd></div><div><dt>AI 曲线响应</dt><dd>{(aiResponse*100).toFixed(2)}%</dd></div></dl>
      </aside>
    </section>

  </>;
}
