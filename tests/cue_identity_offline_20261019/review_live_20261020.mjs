import fs from 'node:fs';
import readline from 'node:readline';
import path from 'node:path';
const out=path.resolve('runs/cue-review-20261020');
const directory='D:/work/AI/yolo-study-001-clean/runs/native_perf/sessions/20261019T162914Z_43900_1';
const file=path.join(directory,fs.readdirSync(directory).find(x=>x.endsWith('.jsonl')));
const lines=readline.createInterface({input:fs.createReadStream(file)});
let metadata;
const bins={},examples={},excerpt=[];
for await(const line of lines) {
  const r=JSON.parse(line);
  if(r.type==='session_metadata'){metadata=r;continue;}
  if(!['selection_decision','controller_sample','ads_acquisition_trace'].includes(r.type))continue;
  const ns=r.type==='selection_decision'?r.vision_capture_ns:r.sample_ns||r.output_sent_ns;
  if(!ns)continue;
  // Image/box alignment around the incident refines the coarse UTC clock map.
  // alignment.json: best offsets .89/.90 seconds; this is approximate, not frame sync.
  const t=(ns-metadata.clock_steady_ns)/1e9+(metadata.clock_utc_ns/1e9-Date.parse('2026-10-19T16:29:16Z')/1000)-.89;
  if(t<118||t>=130)continue;
  r.video_t=t;excerpt.push(r);
  const key=Math.floor(t*2)/2;
  const b=bins[key]??={t:key,vision:0,target:0,boxes:0,outcomes:{},gates:{},cue:{},controller:0,ads:0,aim:0,fire:0,modes:{}};
  const add=(obj,k)=>obj[k]=(obj[k]??0)+1;
  if(r.type==='selection_decision'){
    ++b.vision;if(r.vision_has_target)++b.target;if(r.selection.total_boxes)++b.boxes;
    add(b.outcomes,r.selection.outcome);add(b.cue,r.selection.cue_reason);
    for(const d of r.selection.boxes)add(b.gates,d.gate);
    const ek=`${key}/${r.selection.outcome}`;if(!examples[ek])examples[ek]=r;
  }else if(r.type==='controller_sample'){
    ++b.controller;if(r.left_trigger>.5)++b.ads;if(r.aim_authority)++b.aim;if(r.final_fire_button)++b.fire;add(b.modes,r.aim_mode);
  }
}
fs.writeFileSync(path.join(out,'clean-live-last12.jsonl'),excerpt.map(x=>JSON.stringify(x)).join('\n')+'\n');
fs.writeFileSync(path.join(out,'clean-live-examples.json'),JSON.stringify(examples,null,2));
fs.writeFileSync(path.join(out,'clean-live-summary.json'),JSON.stringify({alignment:'MP4 creation_time vs runtime UTC/steady calibration minus .89 s from image/box matching; approximate. Fire counts final_fire_button, not right trigger (profile uses RB).',bins:Object.values(bins)},null,2));
for(const b of Object.values(bins).sort((a,b)=>a.t-b.t)) console.log(JSON.stringify(b));
