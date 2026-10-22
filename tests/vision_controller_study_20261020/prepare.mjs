import fs from 'node:fs';
import readline from 'node:readline';
const out='runs/vision-controller-study-20261020';
const sources=JSON.parse(fs.readFileSync(out+'/sources.json','utf8'));
const windows=[],events=[],summary=[];
for(const clip of sources){
 const file=`${out}/${clip.id}.records`;
 const rows=fs.readFileSync(file,'utf8').trim().split(/\r?\n/).map(l=>l.split(' ').map(Number));
 if(rows.length!==clip.frames)throw Error('frame mismatch');
 const time=i=>(rows[i][1]-rows[0][1])/1e9;
 let paired=0,returns=0,maxJump=0,run=0,longest=0;
 for(let i=1;i<rows.length;i++){
  const a=rows[i-1],b=rows[i];run=b[5]===2?run+1:0;longest=Math.max(longest,run);
  if(a[4]&&b[4]&&a[2]===b[2]){
   const jump=Math.hypot(a[8]-b[8],a[9]-b[9]);maxJump=Math.max(maxJump,jump);++paired;
   if(a[5]===2&&b[5]===1)++returns;
   if(jump>=12||a[5]!==b[5])events.push({clip:clip.id,t:time(i),frame:b[0],generation:b[2],from:a[5],to:b[5],jump,dx:b[8]-a[8],dy:b[9]-a[9],dt:time(i)-time(i-1)});
  }
 }
 summary.push({...clip,paired,returns,maxJump,longestCueSeconds:longest/clip.fps});
 const candidates=[];
 for(let t=0;t<time(rows.length-1)-1.575;t+=.25){
  const a=rows.filter(r=>r[1]>=rows[0][1]+t*1e9&&r[1]<rows[0][1]+(t+1.575)*1e9);
  const target=a.filter(r=>r[4]).length/a.length,cue=a.filter(r=>r[5]===2).length/a.length;
  candidates.push({start:t,target,cue});
 }
 const coverage=[...candidates].sort((a,b)=>b.target-a.target)[0];
 const cue=[...candidates].filter(a=>Math.abs(a.start-coverage.start)>=1.575).sort((a,b)=>b.cue-a.cue||b.target-a.target)[0];
 for(const [kind,a] of [['coverage',coverage],['cue',cue]])if(a)windows.push({id:clip.id,path:file,start:a.start,kind,coverage:a.target,cue:a.cue});
}
// Retain the cue-only cold start as an explicit no-acquisition control. The
// incident window below includes person evidence before prolonged cue tracking.
windows.push({id:'clean',path:out+'/clean.records',start:121.5,kind:'reported-incident-with-person'},
 {id:'dev',path:out+'/dev.records',start:87.5,kind:'source-radius-transition'},
 {id:'v1',path:out+'/v1.records',start:10,kind:'source-radius-transition'},
 {id:'dev',path:out+'/dev.records',start:189,kind:'hitmarker-confusion'});
fs.writeFileSync(out+'/open.txt',sources.map(c=>`${c.id} ${out}/${c.id}.records 0`).join('\n')+'\n');
fs.writeFileSync(out+'/windows.txt',windows.map(c=>`${c.id} ${c.path} ${c.start}`).join('\n')+'\n');
fs.writeFileSync(out+'/windows.json',JSON.stringify(windows,null,2));
fs.writeFileSync(out+'/vision-summary.json',JSON.stringify(summary,null,2));
fs.writeFileSync(out+'/source-events.json',JSON.stringify(events.sort((a,b)=>b.jump-a.jump),null,2));
const directory='D:/work/AI/yolo-study-001-clean/runs/native_perf/sessions/20261019T162914Z_43900_1';
const telemetry=directory+'/'+fs.readdirSync(directory).find(x=>x.endsWith('.jsonl'));
let metadata;const inputs=[];
for await(const line of readline.createInterface({input:fs.createReadStream(telemetry)})){
 const r=JSON.parse(line);if(r.type==='session_metadata')metadata=r;
 if(r.type!=='controller_sample'||!r.sample_ns)continue;
 const t=(r.sample_ns-metadata.clock_steady_ns)/1e9+metadata.clock_utc_ns/1e9-Date.parse('2026-10-19T16:29:16Z')/1000-.89;
 if(t<0||t>130)continue;
 // RB cannot be reconstructed from final_fire_button: automatic fire also uses it.
 // Only the explicit manual-fire reason is used; this remains incomplete evidence.
 inputs.push([t,r.physical_x,r.physical_y,r.left_trigger,r.auto_fire_block_reason==='manual_fire'?1:0].join(' '));
}
fs.writeFileSync(out+'/clean-inputs.txt',inputs.join('\n')+'\n');
console.log(JSON.stringify({clips:sources.length,frames:sources.reduce((s,c)=>s+c.frames,0),windows:windows.length,controls:inputs.length,largestJumps:events.slice(0,8)},null,2));
