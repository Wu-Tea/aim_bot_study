import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,'../..'),out=path.join(root,'runs/cue-person-offline-20261019');
const read=n=>JSON.parse(fs.readFileSync(path.join(out,n)));
const labels=JSON.parse(fs.readFileSync(path.join(here,'labels.json')));
const clips=read('sources.json'),samples=read('samples.json');
const rows=Object.fromEntries(clips.map(c=>[c.id,fs.readFileSync(path.join(out,c.id+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse)]));
const distance=(a,b)=>Math.hypot(a[0]-b[0],a[1]-b[1]);
const quantile=(a,q)=>a.length?[...a].sort((a,b)=>a-b)[Math.floor((a.length-1)*q)]:null;
function dedup(pred){const kept=[];for(const p of pred)if(!kept.some(k=>distance(k.xy,p.xy)<=3))kept.push(p);return kept;}
function score(pred,truth,pairing=false){
  const edges=[];pred.forEach((p,i)=>truth.forEach((t,j)=>{
    const error=distance(p.xy,t.xy);if(error<=labels.tolerance_px&&(!pairing||t.persons.includes(p.d)))edges.push({i,j,error});
  }));edges.sort((a,b)=>a.error-b.error);
  const ps=new Set(),ts=new Set(),errors=[];
  for(const e of edges)if(!ps.has(e.i)&&!ts.has(e.j)){ps.add(e.i);ts.add(e.j);errors.push(e.error);}
  return {tp:ts.size,fp:pred.length-ps.size,fn:truth.length-ts.size,errors};
}
const cases=[];
for(const s of samples){
  const r=rows[s.clip][s.frame];if(!r||r.frame!==s.frame)throw Error('sample mismatch');
  if(labels.excluded[s.id]){cases.push({...s,excluded:labels.excluded[s.id]});continue;}
  const truth=labels.positives[s.id]||[],pairTruth=truth.filter(t=>t.persons.length);
  const old=r.detections.flatMap((d,i)=>d.has_cue&&!d.friendly?[{xy:d.cue,d:i}]:[]);
  const proposed=r.cues.map((c,i)=>({xy:c.xy,c:i}));
  const paired=r.pairs.map(p=>({xy:r.cues[p.c].xy,d:p.d}));
  // For duplicate detector boxes over one person, keep all allowed indices in truth,
  // but count spatially identical old cue outputs once for cue-point precision.
  const oldDedup=dedup(old);
  cases.push({...s,truth,person_available:pairTruth.length,
    old_cue:score(oldDedup,truth),independent_cue:score(proposed,truth),
    old_pair:score(oldDedup,pairTruth,true),new_pair:score(paired,pairTruth,true),
    old:oldDedup,independent:proposed,paired,
    nearest:truth.map(t=>({xy:t.xy,old:oldDedup.length?Math.min(...oldDedup.map(p=>distance(p.xy,t.xy))):null,
      candidate:proposed.length?Math.min(...proposed.map(p=>distance(p.xy,t.xy))):null}))});
}
function sum(key){const result={tp:0,fp:0,fn:0,errors:[]};for(const c of cases)if(!c.excluded){const m=c[key];for(const k of ['tp','fp','fn'])result[k]+=m[k];result.errors.push(...m.errors);}return {...result,precision:result.tp/(result.tp+result.fp)||0,recall:result.tp/(result.tp+result.fn)||0,median_error_px:quantile(result.errors,.5)};}
const descriptions=clips.map(c=>{
 const rs=rows[c.id];if(rs.length!==c.frames)throw Error(`${c.id} frame count ${rs.length}/${c.frames}`);
 rs.forEach((r,i)=>{if(r.frame!==i)throw Error('discontinuous frames');
   const ds=new Set(),cs=new Set();for(const p of r.pairs){if(ds.has(p.d)||cs.has(p.c))throw Error('nonunique pairing');ds.add(p.d);cs.add(p.c);if(!r.cues[p.c]||!r.detections[p.d])throw Error('bad reference');}});
 return {id:c.id,frames:rs.length,old_cue_frames:rs.filter(r=>r.detections.some(d=>d.has_cue&&!d.friendly)).length,
   candidate_cue_frames:rs.filter(r=>r.cues.length).length,candidate_pair_frames:rs.filter(r=>r.pairs.length).length,
   candidate_pairs:rs.reduce((s,r)=>s+r.pairs.length,0),baseline_target_frames:rs.filter(r=>r.has_target).length,
   baseline_cue_hold_frames:rs.filter(r=>r.source==='cue_hold').length,
   cue_ms_median:quantile(rs.slice(10).map(r=>r.cue_ms),.5),cue_ms_p95:quantile(rs.slice(10).map(r=>r.cue_ms),.95),
   assoc_ms_p95:quantile(rs.slice(10).map(r=>r.assoc_ms),.95)};
});
const dev=[];
for(const [id,truth] of Object.entries(labels.development)){
  const r=JSON.parse(fs.readFileSync(path.join(out,id+'.jsonl'),'utf8').trim());
  const historic=JSON.parse(fs.readFileSync(path.join(root,'output/cue-review-20261014/summary.json'))).examples;
  const [,clip,frame]=id.match(/dev-clip(\d+)-f(\d+)/),h=historic.find(e=>e.clip===+clip&&e.frame_zero_based===+frame);
  dev.push({id,truth:truth[0].xy,historical_old_error:h?.cue_distance_to_manual_dot_px??null,
    candidate_nearest_error:Math.min(...r.cues.map(c=>distance(c.xy,truth[0].xy))),pairs:r.pairs.map(p=>({d:p.d,xy:r.cues[p.c].xy,error:distance(r.cues[p.c].xy,truth[0].xy)}))});
}
const summary={scope:'Frozen first candidate. This experiment did not change production. 9 entire videos, 46 predetermined sample frames, provisional assistant visual reference labels without independent human review. No controller or proposed selector lifecycle; metrics describe cue observation and same-frame association only.',
  total_frames:descriptions.reduce((s,c)=>s+c.frames,0),sample_frames:cases.length,evaluated_frames:cases.filter(c=>!c.excluded).length,
  positive_cues:cases.reduce((s,c)=>s+(c.truth?.length||0),0),pairable_cues:cases.reduce((s,c)=>s+(c.person_available||0),0),
  old_cue:sum('old_cue'),independent_cue:sum('independent_cue'),old_pair:sum('old_pair'),new_pair:sum('new_pair'),descriptions,development:dev};
fs.writeFileSync(path.join(out,'metrics.json'),JSON.stringify(summary,null,2));
fs.writeFileSync(path.join(out,'scored-samples.json'),JSON.stringify(cases,null,2));
console.log(JSON.stringify(summary,null,2));
