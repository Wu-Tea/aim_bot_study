/* Design extensions. Calibration and performance samples are synthetic; no game input. */
let curveWorkspace='shape';
const calibrationDrafts=new WeakMap(),performanceSessions=new WeakMap();
const calibrationLabels={hip:'腰射',ads:'开镜',right:'右',left:'左',up:'上',down:'下'};
const originalWorkspace=renderWorkspace;
const gameResponse=(wire,d=.16,e=2.2)=>wire<=d?0:Math.pow((wire-d)/(1-d),e);
const inverseGameResponse=(intent,d=.16,e=2.2)=>intent===0?0:d+(1-d)*Math.pow(intent,1/e);
function calibrationState(){if(!calibrationDrafts.has(profile()))calibrationDrafts.set(profile(),{stage:0,mode:'hip',direction:'right',results:clone(profile().calibration?.cohorts||{}),failed:false});return calibrationDrafts.get(profile())}
function saveBrowserJson(document,name){const url=URL.createObjectURL(new Blob([JSON.stringify(document,null,2)],{type:'application/json'})),link=document.createElement('a');link.href=url;link.download=name;link.click();URL.revokeObjectURL(url)}
renderWorkspace=function(){
 originalWorkspace();
 if(page==='curve'){
  const tabs=document.createElement('div');tabs.className='local-tabs';tabs.innerHTML=`<button data-curve-workspace="shape" aria-pressed="${curveWorkspace==='shape'}">手感曲线</button><button data-curve-workspace="calibration" aria-pressed="${curveWorkspace==='calibration'}">游戏校准</button>`;
  $('#workspace').prepend(tabs);tabs.querySelectorAll('button').forEach(button=>button.onclick=()=>{curveWorkspace=button.dataset.curveWorkspace;renderWorkspace()});
  if(curveWorkspace==='calibration')renderCalibration(tabs);else setupPrecision();
 }
 if(page==='learning')renderPerformance();
};
function setupPrecision(){
 const bar=$('.point-toolbar');
 const selector=document.createElement('button');selector.id='choose-point';selector.className='quiet';selector.setAttribute('aria-label','选择控制点');selector.textContent='选点 ⌄';bar.prepend(selector);
 selector.onclick=()=>openMenu(selector,profile().points.map((p,index)=>({label:`点 ${index+1} · ${Number((p[0]*100).toFixed(3))}% → ${Number((p[1]*100).toFixed(3))}%`,value:index})),pointIndex,index=>{pointIndex=index;drawCurve()});
 ['x','y'].forEach(axis=>{const input=$('#point-'+axis);input.step='.001';input.setAttribute('aria-label',axis==='x'?'控制点输入百分比':'控制点响应百分比')});
 const details=document.createElement('details');details.className='precision-table';details.innerHTML='<summary>全部控制点 · 精确编辑</summary><div class="point-list"></div>';
 $('#workspace').append(details);
 const updateTable=()=>{
  $('.point-list').innerHTML=profile().points.map((p,i)=>`<div class="point-row"><button class="quiet" data-point="${i}" aria-label="选择点 ${i+1}">点 ${i+1}</button><label>输入 <input type="number" step=".001" data-coordinate="x" data-point="${i}" aria-label="点 ${i+1} 输入百分比" value="${(p[0]*100).toFixed(3)}" ${i===0||i===profile().points.length-1?'disabled':''}> %</label><label>响应 <input type="number" step=".001" data-coordinate="y" data-point="${i}" aria-label="点 ${i+1} 响应百分比" value="${(p[1]*100).toFixed(3)}" ${i===0||i===profile().points.length-1?'disabled':''}> %</label><span class="inline-error" data-point-error="${i}"></span></div>`).join('');
  $('.point-list').querySelectorAll('button').forEach(button=>button.onclick=()=>{pointIndex=Number(button.dataset.point);drawCurve()});
  $('.point-list').querySelectorAll('input').forEach(input=>{
   input.onchange=()=>{const index=Number(input.dataset.point),row=input.closest('.point-row');pointIndex=index;const x=row.querySelector('[data-coordinate=x]').value,y=row.querySelector('[data-coordinate=y]').value;
    if(!x.trim()||!y.trim()||!movePoint(Number(x)/100,Number(y)/100,false))row.querySelector('.inline-error').textContent='输入与响应必须位于相邻点之间。';else{row.querySelector('.inline-error').textContent='';updateTable()}
   };
   input.onwheel=event=>{event.preventDefault();$('#workspace').scrollBy(0,event.deltaY)};
  });
 };
 details.ontoggle=()=>{if(details.open)updateTable()};
 // Refresh expanded coordinates when the plot changes; retain invalid edits for correction.
 const observer=new MutationObserver(()=>{if(!details.isConnected){observer.disconnect();return}if(details.open&&!details.contains(document.activeElement))updateTable()});
 observer.observe($('.curve'),{childList:true});
 $('#workspace > .note').textContent='拖点调整形状；选点后输入精确坐标。↑↓ 微调 0.01%，Shift 加速；端点固定。';
}
function calibrationGraph(stage){
 const coords=(x,y)=>`${45+x*910},${144-y*124}`;
 const points=Array.from({length:65},(_,i)=>i/64);
 const path=(fn,color,dash='')=>`<polyline points="${points.map(x=>coords(x,fn(x))).join(' ')}" fill="none" stroke="${color}" stroke-width="3" stroke-dasharray="${dash}"/>`;
 return `<svg class="calibration-graph" viewBox="0 0 1000 170" aria-label="原始游戏响应与补偿后响应对比">${[0,.25,.5,.75,1].map(x=>`<path d="M${45+x*910} 20V144M45 ${144-x*124}H955" stroke="#343a45"/><text x="${45+x*910}" y="164" text-anchor="middle" fill="#9ca5b4" font-size="12">${x*100}%</text>`).join('')}${path(x=>x,'#929caa','6 5')}${stage>=2?path(x=>gameResponse(x),'#e6bd83'):''}${stage>=3?path(x=>gameResponse(inverseGameResponse(x)),'#89baff'):''}${stage===1?'<path d="M45 140H955" stroke="#e6bd83" stroke-dasharray="3 4"/>':''}</svg>`;
}
function renderCalibration(tabs){
 const main=$('#workspace'),s=calibrationState(),key=s.mode+':'+s.direction,result=s.results[key];
 [...main.children].filter(child=>child!==tabs).forEach(child=>child.remove());
 const stages=['准备环境','静止基线','采样响应','生成补偿','复测与保存'];
 const instructions=['进入训练场，停止移动和射击，画面对准有纹理的静态背景。保持游戏灵敏度、曲线、FOV 与当前模式一致。','先测不推摇杆时的背景运动，作为区分噪声与有效响应的基线。','固定不同水平力度，识别镜头转满 360° 的周期建立基准；局部视觉补充小力度与曲线转折处。','根据实测角速度点集生成逆映射，不预设幂曲线。零输入保持零输出，辅助曲线独立保存。','用未参与拟合的力度做完整旋转复测，检查补偿后的角速度比例。当前结果仅覆盖所选模式与水平转向。'];
 const section=document.createElement('section');section.className='calibration';section.innerHTML=`<div class="toolbar"><button class="select" id="cal-mode" aria-label="校准模式">${calibrationLabels[s.mode]} ⌄</button><button class="select" id="cal-direction" aria-label="校准方向">方向：${calibrationLabels[s.direction]} ⌄</button><span class="spacer"></span><span class="note">${Object.keys(s.results).length} / 4 项水平演示结果</span></div><ol class="stages">${stages.map((name,i)=>`<li class="${i===s.stage?'current':i<s.stage?'complete':''}">${i+1} ${name}</li>`).join('')}</ol><h2>${stages[s.stage]}</h2><p class="note">${instructions[s.stage]}</p><p class="demo-source">校准流程演示 · 曲线为示意，未测量游戏或发送摇杆输入</p>${calibrationGraph(s.stage)}<div class="graph-legend"><span class="legend-target">目标线性</span><span class="legend-raw">游戏原始响应</span><span class="legend-fit">补偿后响应</span></div><div class="cal-result">${s.stage>=3?'<span>响应点 <strong>26（示意）</strong></span><span>整圈周期 <strong>待实测</strong></span><span>复测偏差 <strong>待实测</strong></span>':s.stage===1?'<span>静止噪声基线：模拟采样中</span>':'<span>等待完成当前步骤</span>'}</div><div class="inline-error" role="status">${s.failed?'画面纹理不足，未生成结果。重新采样后再复测。':''}</div><div class="toolbar cal-actions"><button class="quiet" id="cal-reset">重新校准</button><button class="quiet" id="cal-fail" ${s.stage!==2?'hidden':''}>演示采样失败</button><span class="spacer"></span><button class="primary" id="cal-next">${s.failed?'重新采样':s.stage===4?'写入此配置草稿':s.stage===0?'开始演示校准':s.stage===3?'演示复测':'下一步'}</button></div>${result?'<p class="note">当前模式与方向已有复测结果，可重新测量后替换草稿。</p>':''}${profile().calibration?`<p class="note">本配置已保存 ${Object.keys(profile().calibration.cohorts).length} 项补偿草稿；保存并重启后生效。<button class="quiet" id="cal-export">导出校准 JSON</button><button class="quiet" id="cal-disable">关闭补偿</button></p>`:''}`;
 main.append(section);
 const choose=(id,values,current,apply)=>$('#'+id).onclick=event=>openMenu(event.currentTarget,values.map(value=>({value,label:calibrationLabels[value]})),current,value=>{apply(value);s.stage=0;s.failed=false;renderWorkspace()});
 choose('cal-mode',['hip','ads'],s.mode,value=>s.mode=value);choose('cal-direction',['right','left'],s.direction,value=>s.direction=value);
 $('#cal-reset').onclick=()=>{s.stage=0;s.failed=false;renderWorkspace()};
 $('#cal-fail').onclick=()=>{s.failed=true;renderWorkspace()};
 $('#cal-next').onclick=()=>{
  if(s.failed){s.failed=false;renderWorkspace();return}
  if(s.stage<4){s.stage++;renderWorkspace();return}
  const sample={deadzone:.16,model:'deadzone_power',exponent:2.2,neutral_output:0,source:'synthetic_demo',validation:{normalized_rmse:0,kind:'ideal_model'},response_points:Array.from({length:26},(_,i)=>[i/25,gameResponse(i/25)])};
  s.results[key]=sample;
  profile().calibration={schema_version:1,kind:'game_stick_calibration',enabled:true,game:profile().game,cohorts:clone(s.results)};
  renderWorkspace();feedback('模拟校准已写入此配置草稿；未写入原生配置。');
 };
 if($('#cal-export'))$('#cal-export').onclick=()=>saveBrowserJson(profile().calibration,'game-calibration-demo.json');
 if($('#cal-disable')){ $('#cal-disable').textContent=profile().calibration.enabled?'关闭补偿':'启用补偿';$('#cal-disable').onclick=()=>{profile().calibration.enabled=!profile().calibration.enabled;$('#cal-disable').textContent=profile().calibration.enabled?'关闭补偿':'启用补偿';feedback(profile().calibration.enabled?'补偿已启用为草稿。':'补偿已关闭为草稿。')}; }
}
function performanceSummary(windows){
 const active=windows.filter(w=>w.aiming_ns>0),ns=active.reduce((n,w)=>n+w.aiming_ns,0),frames=active.reduce((n,w)=>n+w.active_vision_frames,0);
 return {aiming_ns:ns,frames,mean_fps:ns?frames/(ns/1e9):null,current_fps:windows.length&&windows.at(-1).aiming_ns?windows.at(-1).active_vision_frames/(windows.at(-1).aiming_ns/1e9):null};
}
function demoPerformanceWindows(){return [{window_ms:5000,aiming_ns:1e9,active_vision_frames:180,aim_latency_p95_ms:8.5},{window_ms:5000,aiming_ns:0,active_vision_frames:0,aim_latency_p95_ms:null},{window_ms:5000,aiming_ns:4e9,active_vision_frames:640,aim_latency_p95_ms:10.25}]}
function renderPerformance(){
 const main=$('#workspace'),session=performanceSessions.get(profile()),windows=session?.windows||[],summary=performanceSummary(windows),format=value=>value===null?'—':value.toFixed(1);
 const old=main.innerHTML;
 main.innerHTML=`<section class="performance"><div class="toolbar"><h2>Aim 性能</h2><span class="spacer"></span><button id="perf-record">${session?.recording?'停止演示记录':'开始演示记录'}</button><button class="quiet" id="perf-export" ${!windows.length?'disabled':''}>导出 JSON</button></div><div class="performance-heading"><div><span class="note">本次 Aim 平均 · 应用视觉帧率</span><div class="perf-value">${format(summary.mean_fps)} <small>fps</small></div></div><dl><div><dt>最近窗口</dt><dd>${format(summary.current_fps)} fps</dd></div><div><dt>Aim 时长</dt><dd>${(summary.aiming_ns/1e9).toFixed(1)} s</dd></div><div><dt>有效帧数</dt><dd>${summary.frames}</dd></div><div><dt>状态</dt><dd>${session?.recording?'模拟记录中':windows.length?'记录已停止':'尚无记录'}</dd></div></dl></div><p class="demo-source">${windows.length?'模拟数据 · Aim 指开镜；不包含空闲时间，不是游戏渲染 FPS':'未连接原生运行时。开始演示记录可查看平均值、趋势与历史窗口。'}</p><svg class="performance-graph" viewBox="0 0 1000 200" aria-label="Aim 视觉帧率趋势"></svg><div class="toolbar"><span class="note">${session?`记录对象：${escapeText(session.profile_name)} · ${session.game}`:'记录与实际运行配置绑定，切换编辑对象不会串用记录。'}</span><span class="spacer"></span><button class="quiet" id="perf-append" ${!session?.recording?'disabled':''}>追加演示窗口</button></div><details class="perf-history"><summary>记录窗口 · ${windows.length} 条</summary><table><thead><tr><th>窗口</th><th>Aim 时长</th><th>有效帧</th><th>Aim 平均</th><th>Aim 延迟 P95</th></tr></thead><tbody>${windows.map((w,i)=>`<tr><td>${i+1}</td><td>${(w.aiming_ns/1e9).toFixed(1)} s</td><td>${w.active_vision_frames}</td><td>${w.aiming_ns?(w.active_vision_frames/(w.aiming_ns/1e9)).toFixed(1)+' fps':'空闲'}</td><td>${w.aim_latency_p95_ms===null?'—':w.aim_latency_p95_ms.toFixed(2)+' ms'}</td></tr>`).join('')}</tbody></table></details></section><details class="learning-details"><summary>响应学习与运行日志</summary>${old}</details>`;
 const graph=$('.performance-graph');graph.innerHTML=[0,100,200].map(fps=>`<path d="M48 ${170-fps*.65}H968" stroke="#343a45"/><text x="36" y="${174-fps*.65}" text-anchor="end" fill="#9ca5b4" font-size="12">${fps}</text>`).join('')+(!windows.length?'<text x="500" y="100" text-anchor="middle" fill="#9ca5b4" font-size="14">等待 Aim 性能记录</text>':windows.map((w,i)=>{const x=80+i*840/Math.max(1,windows.length-1),fps=w.aiming_ns?w.active_vision_frames/(w.aiming_ns/1e9):null;return `<text x="${x}" y="193" text-anchor="middle" fill="#9ca5b4" font-size="12">${(i+1)*5}s</text>`+(fps===null?`<text x="${x}" y="100" text-anchor="middle" fill="#9ca5b4" font-size="12">空闲</text>`:`<circle cx="${x}" cy="${170-fps*.65}" r="5" fill="#89baff"/><text x="${x}" y="${154-fps*.65}" text-anchor="middle" fill="#eef0f4" font-size="13">${fps.toFixed(0)} fps</text>`)}).join(''));
 $('#perf-record').onclick=()=>{if(session?.recording)session.recording=false;else performanceSessions.set(profile(),{schema_version:1,kind:'aim_performance_session',source:'synthetic_demo',metric:'application_vision_frames_per_aim_second',aim_definition:'physical_ads_active',profile_name:profile().name,game:profile().game,started_at:new Date().toISOString(),recording:true,windows:demoPerformanceWindows()});renderWorkspace()};
 $('#perf-append').onclick=()=>{session.windows.push({window_ms:5000,aiming_ns:5e9,active_vision_frames:850,aim_latency_p95_ms:9.75});renderWorkspace()};
 $('#perf-export').onclick=()=>saveBrowserJson({...session,summary:performanceSummary(session.windows)},'aim-performance-demo.json');
 main.querySelectorAll('[data-demo]').forEach(button=>button.onclick=()=>feedback('视觉稿演示：'+button.dataset.demo));
}
renderWorkspace();
