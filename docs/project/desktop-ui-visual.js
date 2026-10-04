/* Visual prototype. All edits stay in this browser session. No runtime or file writes. */
const $ = selector => document.querySelector(selector);
const escapeText = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const pages = [['assist','参数调校'],['curve','响应曲线'],['common','设备与运行'],['learning','运行反馈']];
const labels = {linear:'线性（Linear）',cod_dynamic_legacy_lut:'COD 动态曲线',custom_lut:'自定义曲线',both:'RT 或 RB',RT:'右扳机 RT',RB:'右肩键 RB',balanced:'均衡',performance:'性能优先',low_latency:'低延迟',pascal_balanced:'Pascal 显卡均衡'};
const field = (id,label,kind,value,limits,help,unit='') => ({id,label,kind,value,limits,help,unit});
const groups = [
 {page:'assist',column:0,title:'开镜与跟随',fields:[
  field('ads_x','开镜水平辅助','slider',.8,[0,3],'开镜时水平方向的辅助倍率。1.0 为标准倍率。','×'),
  field('ads_y','开镜垂直辅助','slider',.6,[0,3],'开镜时垂直方向的辅助倍率。','×'),
  field('body','持续跟随力度','slider',.7,[0,3],'持续跟随的辅助力度。','×'),
  field('hip','腰射 AI 倍率','slider',1,[0,3],'腰射时 AI 辅助力度倍率。','×'),
  field('learn','学习游戏响应','toggle',true,null,'运行时更新游戏响应估计。关闭后暂停学习更新。')]},
 {page:'assist',column:0,title:'目标识别',fields:[
  field('friendly','判断友方目标','toggle',true,null,'启用友方目标识别与过滤。'),
  field('target','目标点高度','number',.35,[.01,.99],'瞄准点在目标高度中的相对位置，范围 0.01～0.99。')]},
 {page:'assist',column:1,title:'开火与压枪',fields:[
  field('manual','手动开火输入','choice','both',['both','RT','RB'],'哪个实体按键被视为手动开火。'),
  field('fire','自动开火输出','choice','RB',['RT','RB'],'自动开火使用右扳机 RT 或右肩键 RB。'),
  field('recoil','固定力度压枪','toggle',true,null,'启用固定力度的压枪辅助。'),
  field('recoil_ads','开镜压枪力度','slider',.2,[0,1],'开镜时的压枪力度，范围 0～1。'),
  field('recoil_hip','腰射压枪倍率','slider',1,[0,1],'腰射时的压枪倍率，范围 0～1。','×')]},
 {page:'assist',column:0,title:'响应学习初值',advanced:true,fields:[
  field('body_free','跟随普通区初值','number',0,[0,4000],'0 使用默认初值；自定义值为 80～4000。'),
  field('body_slow','跟随减速区初值','number',0,[0,4000],'0 使用默认初值；自定义值为 80～4000。'),
  field('ads_free','开镜普通区初值','number',0,[0,4000],'0 使用默认初值；自定义值为 80～4000。'),
  field('ads_slow','开镜减速区初值','number',0,[0,4000],'0 使用默认初值；自定义值为 80～4000。')]},
 {page:'common',column:0,title:'识别与捕获',fields:[
  field('model','识别模型','file','models/apex_480x384.engine',null,'选择与此配置匹配的 TensorRT 模型。'),
  field('fps','检测帧率','integer',200,[1,1000],'检测时的捕获帧率。','fps'),
  field('idle_fps','空闲检测帧率','integer',60,[1,1000],'空闲时的捕获帧率。','fps'),
  field('capture_w','捕获宽度','integer',640,[1,8192],'捕获图像的宽度。','px'),
  field('capture_h','捕获高度','integer',512,[1,8192],'捕获图像的高度。','px'),
  field('tensor_w','模型输入宽度','integer',480,[1,8192],'必须与模型输入尺寸匹配。','px'),
  field('tensor_h','模型输入高度','integer',384,[1,8192],'必须与模型输入尺寸匹配。','px')]},
 {page:'common',column:1,title:'手柄与性能',fields:[
  field('auto_detect','自动选择手柄','toggle',true,null,'启动时自动识别连接的手柄。'),
  field('controller','XInput 编号','integer',0,[0,3],'手动指定 XInput 设备时使用，范围 0～3。'),
  field('performance','性能档位','choice','balanced',['balanced','performance','low_latency','pascal_balanced'],'选择运行时的性能策略。')]},
 {page:'common',column:1,title:'运行记录',fields:[
  field('telemetry','记录控制日志','toggle',false,null,'记录控制数据会占用磁盘并增加运行开销。'),
  field('perf_log','记录性能摘要','toggle',false,null,'记录运行性能摘要，用于诊断。')]},
];
const fields = Object.fromEntries(groups.flatMap(group=>group.fields).map(field=>[field.id,field]));
const linearPoints = () => Array.from({length:11},(_,i)=>[i/10,i/10]);
const codPoints = [[0.0, 0.0], [0.10152, 0.012697267], [0.15032, 0.025216655], [0.20134, 0.042092669], [0.24929, 0.061012139], [0.30004, 0.084497817], [0.35003, 0.111752815], [0.40038, 0.141086402], [0.45165, 0.176631675], [0.50182, 0.211591033], [0.54907, 0.253604194], [0.59924, 0.3], [0.65057, 0.344], [0.7037, 0.39958699], [0.75021, 0.459619953], [0.80227, 0.525815217], [0.85135, 0.611374408], [0.90277, 0.710091743], [0.94913, 0.844978166], [1.0, 1.0]];
const clone = value => structuredClone(value);
function makeProfile(name,game,algorithm='linear') {
 const values = Object.fromEntries(Object.values(fields).map(field=>[field.id,field.value]));
 if(game==='COD')values.model='models/best_480x384.engine';
 const points=algorithm==='linear'?linearPoints():clone(codPoints);
 return {name,game,values,initial:clone(values),saved:clone(values),algorithm,points,savedAlgorithm:algorithm,savedPoints:clone(points),calibration:null,savedCalibration:null};
}
let profiles=[makeProfile('Apex · 日常','APEX'),makeProfile('Apex · 训练','APEX','custom_lut'),makeProfile('COD · 多人','COD','cod_dynamic_legacy_lut')];
let selected=0,page='assist',popup=null,search='',pointIndex=5,running=null,notice='';
let presets=[];
const profile=()=>profiles[selected];
const dirtyFields=p=>Object.keys(p.values).filter(key=>p.values[key]!==p.saved[key]).length+
 (p.algorithm!==p.savedAlgorithm||JSON.stringify(p.points)!==JSON.stringify(p.savedPoints)?1:0)+
 (JSON.stringify(p.calibration)!==JSON.stringify(p.savedCalibration)?1:0);
function errorFor(field,value) {
 if(!['number','integer','slider'].includes(field.kind))return field.kind==='file'&&!String(value).trim()?'请选择识别模型。':'';
 const number=Number(value);
 if(String(value).trim()===''||!Number.isFinite(number))return '请输入有效数值。';
 if(field.kind==='integer'&&!Number.isInteger(number))return '请输入整数。';
 if(number<field.limits[0]||number>field.limits[1])return `范围：${field.limits[0]}～${field.limits[1]}`;
 if(field.id.endsWith('_free')||field.id.endsWith('_slow'))return number!==0&&number<80?'使用 0 或 80～4000。':'';
 return '';
}
function feedback(message) {
 if(message!==undefined)notice=message;
 const count=dirtyFields(profile()),invalid=Object.values(fields).some(f=>errorFor(f,profile().values[f.id]));
 $('#changes').textContent=count?`${count} 项待保存`:'所有修改已保存';
 $('#notice').textContent=notice||`${profile().game} · ${labels[profile().algorithm]}`;
 $('#save').disabled=!count||invalid;$('#start').disabled=invalid;
 $('#start').textContent=running===profile()?'重新启动配置':count?'保存并启动':'启动配置';
 $('#stop').disabled=!running;
 $('.status').textContent=running?`● 演示运行中 · ${running.name}`:'● 已停止';
 document.querySelectorAll('.profile').forEach((b,i)=>b.classList.toggle('dirty',Boolean(dirtyFields(profiles[i]))));
}
function selectButton(value,options,key='') {
 return `<button class="select" data-options="${escapeText(JSON.stringify(options))}" data-key="${key}" aria-label="${escapeText(fields[key]?.label||(key==='new-game'?'游戏':'曲线类型'))}"><span>${escapeText(labels[value]||value)}</span><span>⌄</span></button>`;
}
function fieldRow(field) {
 const value=profile().values[field.id],error=errorFor(field,value),attr=`data-key="${field.id}" aria-label="${field.label}"`;
 let control;
 if(field.kind==='toggle')control=`<input type="checkbox" ${attr} ${value?'checked':''}>`;
 else if(field.kind==='choice')control=selectButton(value,field.limits,field.id);
 else if(field.kind==='file')control=`<button class="model-file" ${attr} title="${escapeText(value)}">${escapeText(String(value).split('/').pop())}</button>`;
 else {const step=field.kind==='integer'?1:.01;
  control=(field.kind==='slider'?`<input type="range" ${attr} aria-label="${field.label}滑杆" min="${field.limits[0]}" max="${field.limits[1]}" step="${step}" value="${escapeText(value)}">`:'')+
  `<input type="number" ${attr} min="${field.limits[0]}" max="${field.limits[1]}" step="${step}" value="${escapeText(value)}" class="${error?'invalid':''}">`+
  (field.unit?`<span class="unit">${field.unit}</span>`:'');}
 return `<div class="row" data-field="${field.id}"><label title="${field.help}">${field.label}</label><div class="control">${control}</div><button class="more" aria-label="${field.label}的更多操作" data-more="${field.id}">⋯</button><span class="field-error">${error}</span></div>`;
}
function renderGroups(column) {
 return groups.filter(group=>group.page===page&&group.column===column).map(group=>{
  const visible=group.fields.filter(f=>!search||`${f.label} ${f.help}`.includes(search));
  if(!visible.length)return '';
  const content=visible.map(fieldRow).join('');
  return group.advanced?`<details class="advanced" ${search?'open':''}><summary>${group.title}</summary>${content}</details>`:
   `<section class="group"><h2>${group.title}</h2>${content}</section>`;
 }).join('');
}
function renderWorkspace() {
 const main=$('#workspace');
 if(['assist','common'].includes(page))main.innerHTML=`<div class="columns"><div>${renderGroups(0)}</div><div>${renderGroups(1)}</div></div>`+
  (page==='common'?'<div class="toolbar"><button class="quiet" data-demo="源文件">配置源文件…</button><button class="quiet" data-demo="重新载入">重新载入</button></div>':'');
 if(page==='curve')main.innerHTML=`<div class="toolbar">${selectButton(profile().algorithm,['linear','cod_dynamic_legacy_lut','custom_lut'],'curve')}<span class="spacer"></span><button id="presets">已保存预设 ⌄</button><button id="curve-more" aria-label="曲线操作">⋯</button></div><svg class="curve" viewBox="0 0 1000 300" role="img" aria-label="响应曲线，可拖动控制点" tabindex="0"></svg><div class="point-toolbar"><span class="note" id="point-name"></span><label>输入 <input id="point-x" type="number" step=".1" min="0" max="100"> %</label><label>响应 <input id="point-y" type="number" step=".1" min="0" max="100"> %</label><button class="quiet" id="remove-point">删除点</button><span class="spacer"></span><button class="quiet" id="baseline">显示保存基准</button></div><div class="inline-error" id="point-error"></div><p class="note">拖动调整 · 双击添加 · Delete 删除 · 左右键选点 · 上下键微调。修改后保存并重启。</p>`;
 if(page==='learning')main.innerHTML=`<div class="toolbar"><span class="note">视觉稿未连接原生运行时，暂无学习数据。</span><span class="spacer"></span><button data-demo="Fusion">开启 Fusion</button></div><table><thead><tr><th>响应区域</th><th>生效系数</th><th>学习系数</th><th>置信度</th><th>样本</th></tr></thead><tbody>${['跟随 · 普通区','跟随 · 减速区','开镜 · 普通区','开镜 · 减速区'].map(label=>`<tr><td>${label}</td><td>—</td><td>—</td><td>—</td><td>—</td></tr>`).join('')}</tbody></table><p class="note">响应系数是控制器的估计，单位为像素 /（有效摇杆 × 秒）。</p><button class="quiet" data-demo="运行日志">运行日志…</button><button class="quiet" data-demo="导出学习数据">导出学习数据</button>`;
 if(!main.textContent.trim())main.innerHTML='<p class="note">没有匹配的参数，试试“开镜”“压枪”或“帧率”。</p>';
 main.querySelectorAll('input[data-key]').forEach(input=>{
  input.addEventListener('input',()=>{
   const key=input.dataset.key,field=fields[key],raw=input.type==='checkbox'?input.checked:input.value;
   profile().values[key]=input.type==='checkbox'||errorFor(field,raw)?raw:Number(raw);
   const row=input.closest('.row'),error=errorFor(field,raw);row.querySelector('.field-error').textContent=error;
   row.querySelectorAll('input[type=number]').forEach(other=>{if(other!==input)other.value=raw;other.classList.toggle('invalid',Boolean(error))});
   if(!error)row.querySelectorAll('input[type=range]').forEach(other=>{if(other!==input)other.value=raw});
   feedback('修改已保留在此配置的草稿中。');
  });
 });
 main.querySelectorAll('[data-options]').forEach(button=>button.onclick=()=>{
  const key=button.dataset.key,current=key==='curve'?profile().algorithm:profile().values[key];
  openMenu(button,JSON.parse(button.dataset.options).map(value=>({label:labels[value]||value,value})),current,value=>{
   if(key==='curve'){profile().algorithm=value;if(value!=='custom_lut')profile().points=value==='linear'?linearPoints():clone(codPoints);drawCurve()}
   else profile().values[key]=value;
   button.firstElementChild.textContent=labels[value]||value;feedback('修改已保留在此配置的草稿中。');
  });
 });
 main.querySelectorAll('[data-more]').forEach(button=>button.onclick=()=>{
  const field=fields[button.dataset.more];
  openMenu(button,[{label:'参数说明',value:'help'},{label:'恢复创建时的值',value:'reset'}],null,value=>{
   if(value==='reset'){profile().values[field.id]=profile().initial[field.id];renderWorkspace();feedback('已恢复创建时的值，保存后生效。')}
   else showDialog(field.label,`<p class="note">${field.help}</p><button class="primary" data-close>知道了</button>`);
  });
 });
 main.querySelectorAll('[data-demo]').forEach(button=>button.onclick=()=>feedback('视觉稿演示：'+button.dataset.demo));
 main.querySelectorAll('.model-file').forEach(button=>button.onclick=()=>{
  showDialog('识别模型',`<p class="note">模型属于当前 Profile。此视觉稿仅记录路径。</p><input class="path" id="model-path" value="${escapeText(profile().values.model)}" aria-label="模型路径"><p class="dialog-error"></p><div class="dialog-actions"><button data-close>取消</button><button class="primary" id="model-confirm">确定</button></div>`);
  $('#model-confirm').onclick=()=>{const value=$('#model-path').value.trim();if(!value){$('.dialog-error').textContent='请输入模型路径。';return}profile().values.model=value;closeDialog();renderWorkspace();feedback('识别模型已修改。')};
 });
 main.querySelectorAll('input').forEach(input=>input.addEventListener('wheel',event=>{event.preventDefault();main.scrollBy(0,event.deltaY)},{passive:false}));
 if(page==='curve')setupCurve();
}
function render() {
 closeMenu(false);
 $('#profiles').innerHTML=profiles.map((p,index)=>`<button class="profile ${selected===index?'active':''}" data-id="${index}" aria-pressed="${selected===index}">${escapeText(p.name)}<span>${p.game}</span></button>`).join('');
 $('#nav').innerHTML=pages.map(([id,label])=>`<button class="${page===id?'active':''}" data-page="${id}" aria-current="${page===id?'page':'false'}">${label}</button>`).join('')+
  `<input class="search" placeholder="搜索当前页参数…" value="${escapeText(search)}" aria-label="搜索当前页参数" ${!['assist','common'].includes(page)?'hidden':''}>`;
 $('#profiles').querySelectorAll('button').forEach(button=>button.onclick=()=>{selected=Number(button.dataset.id);pointIndex=5;notice='';render()});
 $('#nav').querySelectorAll('button').forEach(button=>button.onclick=()=>{page=button.dataset.page;search='';notice='';render()});
 $('.search').oninput=event=>{search=event.target.value.trim();renderWorkspace()};
 renderWorkspace();feedback();
}
function closeMenu(returnFocus=true) {if(!popup)return;const opener=popup.opener;popup.remove();popup=null;opener.setAttribute('aria-expanded','false');if(returnFocus&&opener.isConnected)opener.focus()}
function openMenu(opener,options,current,onSelect) {
 closeMenu(false);const menu=document.createElement('div');menu.className='popup';menu.setAttribute('role','menu');menu.opener=opener;
 menu.innerHTML=options.map(option=>`<button role="${current===null?'menuitem':'menuitemradio'}" ${current===null?'':`aria-checked="${current===option.value}"`}><span class="check">${option.value===current?'✓':''}</span>${escapeText(option.label)}</button>`).join('');
 ($('dialog')||document.body).append(menu);popup=menu;const rect=opener.getBoundingClientRect();
 menu.style.left=Math.max(8,Math.min(rect.left,innerWidth-menu.offsetWidth-8))+'px';
 menu.style.top=Math.max(8,rect.bottom+4+menu.offsetHeight>innerHeight?rect.top-menu.offsetHeight-4:rect.bottom+4)+'px';
 const buttons=[...menu.querySelectorAll('button')];
 buttons.forEach((button,index)=>button.onclick=()=>{closeMenu();onSelect(options[index].value)});
 if(buttons.length)buttons[Math.max(0,options.findIndex(option=>option.value===current))].focus();
 menu.onkeydown=event=>{const index=buttons.indexOf(document.activeElement);
  if(['Escape','Tab'].includes(event.key)){event.preventDefault();closeMenu()}
  if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();let next=event.key==='Home'?0:event.key==='End'?buttons.length-1:(index+(event.key==='ArrowDown'?1:-1)+buttons.length)%buttons.length;buttons[next].focus()}
 };
 menu.onwheel=event=>event.preventDefault();
 opener.setAttribute('aria-expanded','true');
}
document.addEventListener('pointerdown',event=>{if(popup&&!popup.contains(event.target)&&!popup.opener.contains(event.target))closeMenu(false)});
function closeDialog() {closeMenu(false);const dialog=$('dialog');if(dialog){dialog.close();dialog.remove()}}
function showDialog(title,content) {
 closeDialog();closeMenu(false);const dialog=document.createElement('dialog');
 dialog.innerHTML=`<h2>${title}</h2>${content}`;document.body.append(dialog);dialog.showModal();
 dialog.querySelectorAll('[data-close]').forEach(button=>button.onclick=closeDialog);
 dialog.addEventListener('cancel',event=>{event.preventDefault();closeDialog()});
}
function newProfile() {
 showDialog('新建配置','<label class="note" for="profile-name">名称</label><input class="path" id="profile-name" placeholder="例如 Apex · 训练"><label class="note">游戏</label>'+selectButton('APEX',['APEX','COD','BO3'],'new-game')+'<p class="dialog-error"></p><div class="dialog-actions"><button data-close>取消</button><button class="primary" id="create-profile">创建配置</button></div>');
 let game='APEX';$('dialog .select').onclick=event=>openMenu(event.currentTarget,['APEX','COD','BO3'].map(value=>({value,label:value})),game,value=>{game=value;$('dialog .select span').textContent=value});
 $('#create-profile').onclick=()=>{const name=$('#profile-name').value.trim();if(!name){$('.dialog-error').textContent='请输入配置名称。';return}profiles.push(makeProfile(name,game,game==='COD'?'cod_dynamic_legacy_lut':'linear'));selected=profiles.length-1;closeDialog();render();feedback('已创建独立配置。')};
 $('#profile-name').focus();
}
$('#profile-more').onclick=event=>openMenu(event.currentTarget,[{label:'复制配置',value:'copy'},{label:'重命名',value:'rename'}],null,value=>{
 if(value==='copy'){const copy=clone(profile());copy.name+=' 副本';copy.initial=clone(copy.values);profiles.push(copy);selected=profiles.length-1;render();feedback('已复制为独立配置。')}
 else{showDialog('重命名配置',`<input class="path" id="rename" value="${escapeText(profile().name)}" aria-label="配置名称"><p class="dialog-error"></p><div class="dialog-actions"><button data-close>取消</button><button class="primary" id="rename-confirm">确定</button></div>`);$('#rename-confirm').onclick=()=>{const name=$('#rename').value.trim();if(!name){$('.dialog-error').textContent='请输入配置名称。';return}profile().name=name;closeDialog();render()}}
});
function save() {profile().saved=clone(profile().values);profile().savedAlgorithm=profile().algorithm;profile().savedPoints=clone(profile().points);profile().savedCalibration=clone(profile().calibration);feedback('已保存至演示会话；未写入个人配置。')}
$('#new').onclick=newProfile;$('#save').onclick=save;
$('#start').onclick=()=>{save();running=profile();feedback('已模拟启动此配置；未启动原生程序。')};
$('#stop').onclick=()=>{running=null;feedback('演示运行已停止。')};
$('#references').onclick=()=>location.href='desktop-ui-reference-atlas.html';
let showBaseline=false;
const graphPoint=(x,y)=>[52+x*915,262-y*236];
function drawCurve() {
 const svg=$('.curve');if(!svg)return;pointIndex=Math.max(0,Math.min(profile().points.length-1,pointIndex));
 const polyline=(points,color,width,dash='')=>`<polyline points="${points.map(p=>graphPoint(...p).join(',')).join(' ')}" fill="none" stroke="${color}" stroke-width="${width}" stroke-dasharray="${dash}"/>`;
 svg.innerHTML=Array.from({length:5},(_,index)=>{const t=index/4,[x,y]=graphPoint(t,t);return `<path d="M${x} 26V262M52 ${y}H967" stroke="#343a45" fill="none"/><text x="${x}" y="284" fill="#9ca5b4" text-anchor="middle" font-size="11">${t*100}%</text><text x="40" y="${y+4}" fill="#9ca5b4" text-anchor="end" font-size="11">${t*100}%</text>`}).join('')+
 '<path d="M52 262L967 26" stroke="#566373" stroke-dasharray="4 4"/>'+
 (showBaseline?polyline(profile().savedPoints,'#949eab',2,'6 4'):'')+polyline(profile().points,'#89baff',3)+
 profile().points.map((point,index)=>{const [x,y]=graphPoint(...point);return `<circle cx="${x}" cy="${y}" r="${index===pointIndex?6:4}" fill="${index===pointIndex?'#eef0f4':'#89baff'}" data-index="${index}"/>`}).join('');
 const fixed=pointIndex===0||pointIndex===profile().points.length-1;
 $('#point-name').textContent=`控制点 ${pointIndex+1} / ${profile().points.length}`;
 $('#point-x').value=(profile().points[pointIndex][0]*100).toFixed(3);$('#point-y').value=(profile().points[pointIndex][1]*100).toFixed(3);
 $('#point-x').disabled=fixed;$('#point-y').disabled=fixed;$('#remove-point').disabled=fixed;
}
function changedCurve() {profile().algorithm='custom_lut';$('.toolbar .select span').textContent=labels.custom_lut;drawCurve();feedback('曲线已修改；保存并重启后生效。')}
function movePoint(x,y,constrain=true) {
 const points=profile().points;if(pointIndex===0||pointIndex===points.length-1)return false;
 const before=points[pointIndex-1],after=points[pointIndex+1],margin=.00002;
 if(!Number.isFinite(x)||!Number.isFinite(y)||!constrain&&(x<=before[0]+margin||x>=after[0]-margin||y<=before[1]+margin||y>=after[1]-margin))return false;
 points[pointIndex]=[Math.max(before[0]+margin,Math.min(after[0]-margin,x)),Math.max(before[1]+margin,Math.min(after[1]-margin,y))];changedCurve();return true;
}
function setupCurve() {
 drawCurve();const svg=$('.curve');
 svg.onpointerdown=event=>{
  const index=Number(event.target.dataset.index);if(!Number.isInteger(index))return;pointIndex=index;drawCurve();svg.focus();
  svg.setPointerCapture(event.pointerId);svg.onpointermove=event=>{const point=new DOMPoint(event.clientX,event.clientY).matrixTransform(svg.getScreenCTM().inverse());movePoint((point.x-52)/915,(262-point.y)/236)};
 };
 svg.onpointerup=()=>svg.onpointermove=null;
 svg.onpointercancel=()=>svg.onpointermove=null;
 svg.ondblclick=event=>{
  if(profile().points.length>=32)return;
  const point=new DOMPoint(event.clientX,event.clientY).matrixTransform(svg.getScreenCTM().inverse()),x=(point.x-52)/915,y=(262-point.y)/236;
  const index=profile().points.findIndex((p,i)=>i&&x>profile().points[i-1][0]+.002&&x<p[0]-.002&&p[1]-profile().points[i-1][1]>.004);
  if(index<0)return;const a=profile().points[index-1],b=profile().points[index];profile().points.splice(index,0,[x,Math.max(a[1]+.001,Math.min(b[1]-.001,y))]);pointIndex=index;changedCurve();
 };
 const remove=()=>{if(pointIndex===0||pointIndex===profile().points.length-1)return;profile().points.splice(pointIndex,1);pointIndex=Math.min(pointIndex,profile().points.length-2);changedCurve()};
 svg.onkeydown=event=>{
  if(['ArrowLeft','ArrowRight'].includes(event.key)){event.preventDefault();pointIndex=Math.max(0,Math.min(profile().points.length-1,pointIndex+(event.key==='ArrowRight'?1:-1)));drawCurve()}
 if(['ArrowUp','ArrowDown'].includes(event.key)){event.preventDefault();const [x,y]=profile().points[pointIndex];const step=event.shiftKey?.001:.0001;movePoint(x,y+(event.key==='ArrowUp'?step:-step))}
  if(event.key==='Delete'){event.preventDefault();remove()}
 };
 $('#remove-point').onclick=remove;$('#baseline').onclick=()=>{showBaseline=!showBaseline;$('#baseline').textContent=showBaseline?'隐藏保存基准':'显示保存基准';drawCurve()};
 ['x','y'].forEach(axis=>$('#point-'+axis).onchange=()=>{const x=Number($('#point-x').value)/100,y=Number($('#point-y').value)/100;if(!movePoint(x,y,false))$('#point-error').textContent='控制点必须处于相邻点之间，输入与响应均保持递增。';else $('#point-error').textContent=''});
 $('#presets').onclick=event=>openMenu(event.currentTarget,presets.length?presets.map((preset,index)=>({label:preset.name,value:index})):[{label:'尚无预设 · 保存当前曲线',value:'new'}],null,value=>{if(value==='new')savePreset();else{profile().points=clone(presets[value].points);changedCurve()}});
 $('#curve-more').onclick=event=>openMenu(event.currentTarget,[{label:'保存为预设…',value:'preset'},{label:'导入 JSON…',value:'import'},{label:'导出 JSON…',value:'export'},{label:'恢复保存的曲线',value:'restore'}],null,value=>{
  if(value==='preset')savePreset();
  if(value==='restore'){profile().points=clone(profile().savedPoints);profile().algorithm=profile().savedAlgorithm;renderWorkspace();feedback('已恢复保存基准。')}
  if(value==='import')importCurve();
  if(value==='export'){const data=curveDocument(profile().name+' 曲线'),blob=new Blob([JSON.stringify(data,null,2)],{type:'application/json'}),url=URL.createObjectURL(blob),link=document.createElement('a');link.href=url;link.download='response-curve.json';link.click();URL.revokeObjectURL(url);feedback('演示曲线已导出。')}
 });
}
function curveDocument(name) {return {schema_version:1,kind:'normalized_stick_response',name,interpolation:'piecewise_linear',points:clone(profile().points)}}
function savePreset() {
 showDialog('保存曲线预设','<input class="path" id="preset-name" placeholder="曲线名称"><p class="dialog-error"></p><div class="dialog-actions"><button data-close>取消</button><button class="primary" id="preset-save">保存</button></div>');
 $('#preset-save').onclick=()=>{const name=$('#preset-name').value.trim();if(!name){$('.dialog-error').textContent='请输入曲线名称。';return}presets.push(curveDocument(name));closeDialog();feedback('预设已保存至演示会话。')};
}
function importCurve() {
 const input=document.createElement('input');input.type='file';input.accept='.json';input.onchange=async()=>{
  try {const data=JSON.parse(await input.files[0].text()),points=data.points;
   if(data.schema_version!==1||data.kind!=='normalized_stick_response'||data.interpolation!=='piecewise_linear'||!Array.isArray(points)||points.length<2||points.length>32)throw Error('曲线格式或版本无效。');
   if(points.some((p,i)=>!Array.isArray(p)||p.length!==2||p.some(v=>typeof v!=='number'||!Number.isFinite(v)||v<0||v>1)||i&&(p[0]-points[i-1][0]<.000011||p[1]-points[i-1][1]<.000011))||JSON.stringify(points[0])!=='[0,0]'||JSON.stringify(points.at(-1))!=='[1,1]')throw Error('控制点必须归一化、严格递增，并具有固定端点。');
   profile().points=clone(points);presets.push(data);changedCurve();
  }catch(error){feedback('导入未完成：'+error.message)}
 };input.click();
}
render();
