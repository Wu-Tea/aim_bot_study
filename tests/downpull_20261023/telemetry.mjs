import fs from 'node:fs';
import readline from 'node:readline';
const session=process.argv[2]??'20261022T023527Z_30888_1';
const dir='D:/work/AI/yolo-study-001-clean/runs/native_perf/sessions/'+session;
const out='runs/downpull-20261023/'+session;
fs.mkdirSync(out,{recursive:true});
let metadata;const types={},examples={},rows=[];
for(const file of fs.readdirSync(dir).filter(f=>f.endsWith('.jsonl')).sort()) {
 for await(const line of readline.createInterface({input:fs.createReadStream(dir+'/'+file)})) {
  const r=JSON.parse(line);types[r.type]=(types[r.type]??0)+1;
  if(!examples[r.type])examples[r.type]=r;
  if(r.type==='session_metadata')metadata=r;
  if(r.type==='controller_sample'&&r.sample_ns) {
   const t=(r.sample_ns-metadata.clock_steady_ns)/1e9+metadata.clock_utc_ns/1e9-Date.parse('2026-10-22T02:40:12Z')/1000;
   rows.push({video_utc_t:t,...r});
  }
 }
}
fs.writeFileSync(out+'/telemetry-examples.json',JSON.stringify({types,examples},null,2));
fs.writeFileSync(out+'/telemetry-controller.json',JSON.stringify(rows));
const events=[];let previous;
for(const r of rows) {
 if(previous && r.final_y<-.35 && r.final_y-previous.final_y<-.08)events.push({t:r.video_utc_t,dy:r.final_y-previous.final_y,
  physical:r.physical_y,final:r.final_y,ai:r.ai_y,recoil:r.recoil_y,source:r.production_target_source,target:r.controller_target_track_id,aim:r.source_aim,frame:r.frame_id});
 previous=r;
}
fs.writeFileSync(out+'/downward-events.json',JSON.stringify(events,null,2));
console.log(JSON.stringify({types,samples:rows.length,first:rows[0]?.video_utc_t,last:rows.at(-1)?.video_utc_t,
 minY:Math.min(...rows.map(r=>r.final_y)),maxY:Math.max(...rows.map(r=>r.final_y)),
 largest:events.sort((a,b)=>a.dy-b.dy).slice(0,15)},null,2));
