import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {CueLock,attachPerson} from './cue_lock.mjs';
const here=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(here,'../..');
const input=path.join(root,'runs/cue-person-offline-20261019'),out=path.join(root,'runs/cue-identity-offline-20261019');
fs.mkdirSync(out,{recursive:true});
const read=n=>JSON.parse(fs.readFileSync(path.join(input,n)));
const clips=read('sources.json'),all=Object.fromEntries(clips.map(c=>[c.id,fs.readFileSync(path.join(input,c.id+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse)]));
const summary=[];
for(const c of clips){
  const tracker=new CueLock(),rs=all[c.id],events=[];let acquisitions=0,fresh=0,attached=0;
  for(const r of rs){const lock=tracker.step(r.cues,r.t),body=attachPerson(lock,r.cues,r.detections);
    if(lock.reason.includes('acquired'))++acquisitions;if(lock.fresh)++fresh;if(body.person>=0)++attached;
    events.push({frame:r.frame,t:r.t,lock,body});
  }
  fs.writeFileSync(path.join(out,c.id+'.jsonl'),events.map(r=>JSON.stringify(r)).join('\n')+'\n');
  summary.push({clip:c.id,frames:rs.length,cue_owned_episodes:acquisitions,fresh_cue_frames:fresh,person_attached_frames:attached,ambiguous_frames:events.filter(e=>e.lock.reason==='ambiguous').length});
}
// Seeded diagnostics isolate keeping a known visible cue from automatic acquisition.
const seeds=[{clip:'v1',frame:150,xy:[304,207]},{clip:'v2',frame:631,xy:[316,233]},
 {clip:'v3',frame:630,xy:[318,186]},{clip:'v5',frame:30,xy:[310,229]},{clip:'v6',frame:150,xy:[299,207]}];
for(const seed of seeds){
 const rs=all[seed.clip],start=rs[seed.frame],matches=start.cues.map((c,i)=>({i,error:Math.hypot(c.xy[0]-seed.xy[0],c.xy[1]-seed.xy[1])})).filter(c=>c.error<=6).sort((a,b)=>a.error-b.error);
 if(!matches.length)throw Error('seed cue missing');const tracker=new CueLock();tracker.seed(start.cues[matches[0].i],start.t);
 const events=[];
 for(const r of rs.slice(seed.frame,seed.frame+91)){
   const lock=r.frame===seed.frame?{id:1,fresh:true,reason:'visual_seed',cue:matches[0].i,xy:start.cues[matches[0].i].xy}:tracker.step(r.cues,r.t,{acquire:false});
   events.push({frame:r.frame,t:r.t,lock,body:attachPerson(lock,r.cues,r.detections)});
 }
 fs.writeFileSync(path.join(out,`${seed.clip}-seed${seed.frame}.jsonl`),events.map(r=>JSON.stringify(r)).join('\n')+'\n');
}
const samples=read('samples.json');
const labels=JSON.parse(fs.readFileSync(path.join(root,'tests/cue_person_offline_20261019/labels.json')));
const evaluated=[];
for(const s of samples){if(labels.excluded[s.id])continue;
 const e=fs.readFileSync(path.join(out,s.clip+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse)[s.frame];
 const truth=labels.positives[s.id]||[];
 const correct=e.lock.fresh&&truth.some(t=>Math.hypot(t.xy[0]-e.lock.xy[0],t.xy[1]-e.lock.xy[1])<=6);
 evaluated.push({id:s.id,truth,lock:e.lock,body:e.body,correct});
}
const result={scope:'Offline cue-owned identity on frozen cached detector observations. No production controller, no new perception training. True-cue seeded diagnostics are separate from automatic acquisition.',summary,seeds,
 sample_frames:evaluated.length,visible_cue_frames:evaluated.filter(e=>e.truth.length).length,
 fresh_lock_samples:evaluated.filter(e=>e.lock.fresh).length,correct_visible_cue_locks:evaluated.filter(e=>e.correct).length,
 false_visible_cue_locks:evaluated.filter(e=>e.lock.fresh&&!e.correct).length};
fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify(result,null,2));
fs.writeFileSync(path.join(out,'scored-samples.json'),JSON.stringify(evaluated,null,2));
console.log(JSON.stringify(result,null,2));
