import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
const sourceRoot='runs/vision-controller-study-20261020';
const destination=process.argv[2]??'tests/vision_controller_study_20261020/trajectories';
fs.mkdirSync(destination,{recursive:true});
const sources=JSON.parse(fs.readFileSync(sourceRoot+'/sources.json','utf8'));
const cache=new Map();
function load(id){if(!cache.has(id)){const source=sources.find(s=>s.id===id);const rows=fs.readFileSync(`${sourceRoot}/${id}.records`,'utf8').trim().split(/\r?\n/).map(line=>({tokens:line.split(' '),r:line.split(' ').map(Number)}));cache.set(id,{source,rows,origin:rows[0].r[1]});}return cache.get(id);}
const time=(data,row)=>(row.r[1]-data.origin)/1e9;
function window(id,start){const data=load(id);return data.rows.filter(row=>time(data,row)>=start-1e-8&&time(data,row)<start+1.575-1e-8);}
function metrics(rows){
 const targets=rows.filter(v=>v.r[4]),xs=targets.map(v=>v.r[8]),ys=targets.map(v=>v.r[9]);
 let maxJump=0,sourceChanges=0,gaps=0;for(let i=1;i<rows.length;i++){const a=rows[i-1].r,b=rows[i].r;if(a[4]&&b[4]&&a[2]===b[2])maxJump=Math.max(maxJump,Math.hypot(b[8]-a[8],b[9]-a[9]));if(a[5]!==b[5])sourceChanges++;if(a[4]&&!b[4])gaps++;}
 const rangeX=xs.length?Math.max(...xs)-Math.min(...xs):0,rangeY=ys.length?Math.max(...ys)-Math.min(...ys):0;
 const netX=xs.length?xs.at(-1)-xs[0]:0,netY=ys.length?ys.at(-1)-ys[0]:0;
 return {coverage:targets.length/rows.length,cueFrames:targets.filter(v=>v.r[5]===2).length,generations:[...new Set(targets.map(v=>v.r[2]))],rangeX,rangeY,netX,netY,maxJump,sourceChanges,gaps};
}
const definitions=[];
for(const clip of ['clean','dev']){
 const data=load(clip),candidates=[];
 for(let start=0;start<time(data,data.rows.at(-1))-1.575;start+=.125){const rows=window(clip,start),m=metrics(rows);if(m.coverage>=.9&&m.generations.length===1&&m.maxJump<=35&&rows.slice(0,4).some(v=>v.r[5]===1))candidates.push({start,m});}
 for(const [kind,score] of [
  ['lateral',m=>Math.abs(m.netX)-.4*m.rangeY],
  ['turn',m=>m.rangeX-Math.abs(m.netX)-.2*m.rangeY],
  ['vertical',m=>m.rangeY-.3*m.rangeX],
 ]){
  const available=candidates.filter(c=>!definitions.some(d=>d.clip===clip&&Math.abs(d.start-c.start)<1.575));available.sort((a,b)=>score(b.m)-score(a.m));assert(available.length,'no suitable motion window');
  definitions.push({id:`${clip}_${kind}`,clip,start:available[0].start,kind:'recorded_motion',feature:kind});
 }
}
for(const [id,clip,start,kind,feature] of [
 ['clean_long_cue','clean',121.5,'recorded_evidence','person then long cue'],
 ['clean_cue_cold_start','clean',122,'negative_control','cue cannot establish a new body reference'],
 ['dev_radius_transition','dev',87.5,'diagnostic_fault','person/cue BodyLock radius'],
 ['v1_radius_transition','v1',10,'diagnostic_fault','person/cue BodyLock radius'],
 ['dev_hitmarker','dev',189,'diagnostic_fault','HUD association, not a real actor trajectory'],
 ['v3_mixed_cue','v3',3.5,'recorded_evidence','source changes and gaps'],
 ['v2_detection_gap','v2',.25,'recorded_evidence','detection availability'],
 ['v5_short_target','v5',0,'recorded_evidence','short target observation'],
 ['v6_large_jump','v6',2.75,'diagnostic_fault','identity/geometry discontinuity, not ground truth'],
 ['v9_reacquisition','v9',3.5,'recorded_evidence','loss and reacquisition'],
 ['dev_cue_jitter','dev',170.5,'diagnostic_fault','unequal capture intervals and reversing cue displacement'],
])definitions.push({id,clip,start,kind,feature});
const names=['none','person','cue','associated_weak','weak_observed'],manifest=[];
let totalFrames=0;
for(const def of definitions){
 const data=load(def.clip),rows=window(def.clip,def.start);assert(rows.length>50);const first=rows[0].r[1];
 const csv=['elapsed_ms,video_time_s,original_frame,selector_generation,tracklet,has_target,source,x_px,y_px,dx_px,dy_px,body_width_px,body_height_px,confidence,has_motion_anchor,motion_anchor_x,motion_anchor_y,pair_dt_ms,vx_px_s,vy_px_s'];
 const packed=[];let last=null,tracklet=0,previous=null;
 for(const row of rows){const r=row.r,t=row.tokens.slice();t[1]=String(r[1]-first+1e9);packed.push(t.join(' '));
  if(r[4]&&(!last||last[2]!==r[2]||r[3]||r[1]-last[1]>40000000))tracklet++;
  const pair=previous&&previous[4]&&r[4]&&previous[2]===r[2]&&!r[3],dt=pair?(r[1]-previous[1])/1e9:0;
  const d=r[6]&&r[7]<r[26]?r.slice(27+r[7]*17,44+r[7]*17):null;
  csv.push([(r[1]-first)/1e6,time(data,row),r[0],r[2],r[4]?tracklet:'',r[4],names[r[5]],
   ...(r[4]?[r[8],r[9],r[8]-320,r[9]-256,r[13]-r[11],r[14]-r[12],r[25]]:Array(7).fill('')),
   d?.[13]??0,...(d?.[13]?[d[14],d[15]]:['','']),...(pair?[dt*1000,(r[8]-previous[8])/dt,(r[9]-previous[9])/dt]:['','',''])].join(','));
  if(r[4])last=r;previous=r;
 }
 const recordFile=`${def.id}.records`,csvFile=`${def.id}.csv`;
 fs.writeFileSync(path.join(destination,recordFile),packed.join('\n')+'\n');fs.writeFileSync(path.join(destination,csvFile),csv.join('\n')+'\n');
 // Exact field fidelity, with capture origin as the sole intentional change.
 const roundtrip=fs.readFileSync(path.join(destination,recordFile),'utf8').trim().split(/\r?\n/);
 assert.equal(roundtrip.length,rows.length);roundtrip.forEach((line,i)=>{const tokens=line.split(' ');assert.equal(+tokens[1]-1e9,rows[i].r[1]-first);tokens[1]=rows[i].tokens[1];assert.deepEqual(tokens,rows[i].tokens);});
 totalFrames+=rows.length;
 manifest.push({...def,video:data.source.file,videoStartSeconds:time(data,rows[0]),videoEndSeconds:time(data,rows.at(-1)),fps:data.source.fps,firstFrame:rows[0].r[0],lastFrame:rows.at(-1).r[0],frames:rows.length,recordFile,csvFile,metrics:metrics(rows)});
}
fs.writeFileSync(path.join(destination,'manifest.json'),JSON.stringify({version:1,coordinateSystem:'center 640x512 screen pixels; includes original camera movement; no world-motion ground truth',productionRevision:'8a7f36a',slotMs:1575,gapMs:50,interpolation:'none; missing coordinates blank in CSV; finite differences never cross missing frames or identity changes',totalFrames,trajectories:manifest},null,2));
fs.writeFileSync(path.join(destination,'replay.txt'),manifest.map(m=>`${m.id} ${destination.replaceAll('\\','/')}/${m.recordFile} 0`).join('\n')+'\n');
const chartHeight=90+Math.ceil(manifest.length/4)*258;
let svg=`<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="${chartHeight}"><rect width="1200" height="${chartHeight}" fill="white"/><g font-family="Arial,sans-serif"><text x="20" y="28" font-size="20">Recorded screen trajectories · person blue / cue orange</text><text x="20" y="50" font-size="13">Same 640×512 coordinate scale in every panel. Gaps, identity changes and >40px jumps break lines.</text>`;
manifest.forEach((m,k)=>{const left=20+(k%4)*295,top=80+Math.floor(k/4)*258;const rows=window(m.clip,m.start);svg+=`<text x="${left}" y="${top}" font-size="14">${m.id}</text><text x="${left}" y="${top+18}" font-size="11" fill="#555">${m.clip} ${m.videoStartSeconds.toFixed(3)}s · ${m.kind}</text><rect x="${left}" y="${top+25}" width="256" height="204.8" fill="#f4f6f8" stroke="#ccd2d9"/><path d="M${left+128},${top+25}v204.8 M${left},${top+127.4}h256" stroke="#ccd2d9"/>`;
 let previous;for(const {r} of rows){if(previous&&previous[4]&&r[4]&&previous[2]===r[2]&&!r[3]&&Math.hypot(r[8]-previous[8],r[9]-previous[9])<=40)svg+=`<path d="M${left+previous[8]*.4},${top+25+previous[9]*.4}L${left+r[8]*.4},${top+25+r[9]*.4}" stroke="${r[5]===2?'#dc8a08':'#1766aa'}" stroke-width="1.5"/>`;previous=r;}
});svg+='</g></svg>';fs.writeFileSync(path.join(destination,'overview.svg'),svg);
console.log(JSON.stringify({trajectories:manifest.length,totalFrames,output:destination,items:manifest.map(m=>({id:m.id,start:m.videoStartSeconds,frames:m.frames,kind:m.kind}))},null,2));
