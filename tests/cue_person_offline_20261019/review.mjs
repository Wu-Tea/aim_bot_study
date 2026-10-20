import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(here,'../..'),out=path.join(root,'runs/cue-person-offline-20261019');
const read=n=>JSON.parse(fs.readFileSync(path.join(out,n)));
const m=read('metrics.json'),scored=read('scored-samples.json'),sources=read('sources.json');
const labels=JSON.parse(fs.readFileSync(path.join(here,'labels.json')));
const suppLabels=JSON.parse(fs.readFileSync(path.join(here,'supplementary-labels.json')));
const rows=Object.fromEntries(sources.map(c=>[c.id,fs.readFileSync(path.join(out,c.id+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse)]));
const items=scored.map(s=>({...s,group:'固定验证',image:s.id+'.png',r:rows[s.clip][s.frame]}));
for(const s of read('supplementary.json'))items.push({...s,group:'定向反例',image:s.id+'.png',r:rows[s.clip][s.frame],note:suppLabels.cases[s.id].note,correct:suppLabels.cases[s.id].correct});
for(const d of m.development)items.push({id:d.id,group:'旧问题开发样本',image:'../../output/cue-review-20261014/'+d.id.slice(4)+'.png',truth:labels.development[d.id],r:JSON.parse(fs.readFileSync(path.join(out,d.id+'.jsonl'),'utf8').trim()),note:'开发样本；旧误差取历史连续回放。本次单帧 baseline 冷启动不能替代历史状态。'});
const html=`<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Cue / person 离线实验</title>
<style>body{margin:28px auto;padding:0 20px;max-width:1100px;font:16px/1.65 system-ui;background:#10151d;color:#e3eaf2}h1{font-size:27px}p{max-width:950px}a{color:#8dd6ff}button,select{font:inherit;padding:5px 12px;background:#253344;color:white;border:1px solid #526177;border-radius:4px}label{display:inline-block;margin:6px 14px 6px 0}.viewer{position:relative;max-width:800px}.viewer img{width:100%;display:block}.viewer svg{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}table{border-collapse:collapse;margin:18px 0}td,th{padding:8px 20px;border-bottom:1px solid #425268;text-align:left}pre{white-space:pre-wrap;color:#b3c0d2;font-size:13px}#note{min-height:52px}input[type=range]{width:min(790px,95%)}.small{color:#adbccc}</style>
<h1>Cue 与 person 独立检测，再关联</h1>
<p>冻结的第一版候选 · 9 段 / 5,435 帧 · ${m.evaluated_frames} 张固定抽帧。用户已确认圆点与 COD 死敌骷髅标记都属于有效 cue，两张原待定帧均已纳入。此前统计保留为 initial / circle-only 文件。生产未改。</p>
<table><tr><th>目视参考样本指标</th><th>当前方法</th><th>实验候选</th></tr><tr><td>小圆点定位正确 / 可见圆点</td><td>${m.old_cue.tp} / ${m.positive_cues}</td><td>${m.independent_cue.tp} / ${m.positive_cues}（关联前）</td></tr><tr><td>正确配对 / 有可用人体框的圆点</td><td>${m.old_pair.tp} / ${m.pairable_cues}</td><td>${m.new_pair.tp} / ${m.pairable_cues}</td></tr><tr><td>固定样本错误配对</td><td>${m.old_pair.fp}</td><td>${m.new_pair.fp}；定向反例另查到 6 个</td></tr></table>
<p class="small">绿色圆：目视参考；红色十字：旧 cue；蓝色小框：独立 cue 候选；青色圆与连线：新配对；紫色框：模型 person。图层可以独立开关。蓝色候选含大量背景纹理，并不代表已确认敌人。</p>
<div><select id="group"><option>固定验证</option><option>旧问题开发样本</option><option>定向反例</option></select> <button id="prev">上一帧</button> <button id="next">下一帧</button> <select id="pick"></select></div>
<div id="controls"></div><input id="slider" type="range" min="0"><p id="note"></p>
<div class="viewer"><img id="frame"><svg id="overlay" viewBox="0 0 640 512"></svg></div><pre id="detail"></pre>
<p><a href="REPORT.md">完整报告</a> · <a href="metrics.json">统计</a> · <a href="scored-samples.json">逐样本判定</a> · <a href="sources.json">视频索引</a></p>
<script>const items=${JSON.stringify(items).replaceAll('<','\\u003c')};
const $=id=>document.getElementById(id);let subset=[],index=0;
const layers=[['truth','目视参考',true],['old','旧 cue',true],['new','独立候选',false],['pairs','新配对',true],['person','person 框',true]];
$('controls').innerHTML=layers.map(([id,n,on])=>'<label><input type="checkbox" id="'+id+'" '+(on?'checked':'')+'>'+n+'</label>').join('');
function circle(x,y,rad,col){return '<circle cx="'+x+'" cy="'+y+'" r="'+rad+'" fill="none" stroke="'+col+'" stroke-width="1.5"/>'}
function rect(b,col){return '<rect x="'+b[0]+'" y="'+b[1]+'" width="'+(b[2]-b[0])+'" height="'+(b[3]-b[1])+'" fill="none" stroke="'+col+'" stroke-width="1"/>'}
function show(){const s=subset[index],r=s.r;let svg='';$('frame').src=s.image;$('slider').value=index;$('pick').value=index;
 if($('person').checked)r.detections.forEach((d,i)=>{svg+=rect(d.box,'#d398ff')+'<text x="'+d.box[0]+'" y="'+Math.max(12,d.box[1]-3)+'" fill="#d398ff" font-size="12">d'+i+'</text>'});
 if($('new').checked)r.cues.forEach(c=>svg+=rect(c.box,'#669cff'));
 if($('old').checked)r.detections.filter(d=>d.has_cue).forEach(d=>{let [x,y]=d.cue;svg+='<path d="M'+(x-7)+','+y+'h14 M'+x+','+(y-7)+'v14" stroke="#ff6868" stroke-width="2"/>'});
 if($('pairs').checked)r.pairs.forEach(p=>{let [x,y]=r.cues[p.c].xy,b=r.detections[p.d].box;svg+=circle(x,y,8,'#44e7ed')+'<path d="M'+x+','+y+'L'+((b[0]+b[2])/2)+','+((b[1]+b[3])/2)+'" stroke="#44e7ed" stroke-width="1"/>'});
 if($('truth').checked)(s.truth||[]).forEach(t=>svg+=circle(...t.xy,12,'#81ff86'));
 $('overlay').innerHTML=svg;$('note').textContent=s.id+' · '+(index+1)+' / '+subset.length+' · '+(s.excluded?'排除：'+s.excluded:s.note||('参考圆点 '+(s.truth||[]).length+'；旧正确配对 '+(s.old_pair?.tp??'—')+'，新正确配对 '+(s.new_pair?.tp??'—')));
 $('detail').textContent=JSON.stringify({old_pairs:r.detections.flatMap((d,i)=>d.has_cue?[{d:i,xy:d.cue}]:[]),new_pairs:r.pairs.map(p=>({d:p.d,xy:r.cues[p.c].xy,cost:p.cost})),truth:s.truth||[],note:s.note},null,2);
}
function group(){subset=items.filter(s=>s.group===$('group').value);index=0;$('pick').innerHTML=subset.map((s,i)=>'<option value="'+i+'">'+s.id+'</option>').join('');$('slider').max=subset.length-1;show()}
$('group').onchange=group;$('prev').onclick=()=>{index=(index+subset.length-1)%subset.length;show()};$('next').onclick=()=>{index=(index+1)%subset.length;show()};$('pick').onchange=e=>{index=+e.target.value;show()};$('slider').oninput=e=>{index=+e.target.value;show()};layers.forEach(([id])=>$(id).onchange=show);group();</script></html>`;
fs.writeFileSync(path.join(out,'review.html'),html);
console.log(`review: ${items.length} cases`);

