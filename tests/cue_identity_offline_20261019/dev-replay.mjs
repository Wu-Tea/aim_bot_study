// Replays the native dev selection policy, without capture or controller output.
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
const output=path.join(root,'runs/cue-dev-20261019');
fs.mkdirSync(output,{recursive:true});
const env={...process.env};
const pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`${root}/native/build/Release;D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
function completion(child) {
  let error=''; child.stderr.on('data',chunk=>error+=chunk);
  return new Promise((resolve,reject)=>{
    child.on('error',reject);
    child.on('exit',code=>code===0?resolve(error):reject(Error(`${child.spawnfile}: ${code}: ${error}`)));
  });
}
async function replay(file,id,fps,still=false) {
  const args=['-v','error','-i',file,'-vf','crop=640:512'];
  if(still)args.push('-frames:v','1');
  args.push('-fps_mode','passthrough','-f','rawvideo','-pix_fmt','bgra','pipe:1');
  const decoder=spawn('ffmpeg',args,{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const native=spawn(path.join(root,'native/build/Release/vision_cue_video_review.exe'),[
    path.join(root,'models/best_480x384.engine'),path.join(output,id+'.jsonl'),String(fps),'190','.2','.65','1','cue-owned'],
    {env,windowsHide:true,stdio:['pipe','ignore','pipe']});
  const done=Promise.all([completion(decoder),completion(native)]);
  native.stdin.on('error',error=>{if(error.code!=='EPIPE')console.error(error);});
  decoder.stdout.pipe(native.stdin);
  try {await done;} catch(error) {decoder.kill();native.kill();throw error;}
  const rows=fs.readFileSync(path.join(output,id+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
  for(const r of rows) {
    if(!r.has_target && (r.aim_authority || r.fire_authority))throw Error(`${id}/${r.frame}: empty authority`);
    if(r.source==='cue_hold' && r.fire_authority)throw Error(`${id}/${r.frame}: cue fire`);
  }
  const summary={id,frames:rows.length,targets:rows.filter(r=>r.has_target).length,
    observed:rows.filter(r=>r.selected>=0).length,cueHold:rows.filter(r=>r.source==='cue_hold').length,
    lastGeneration:rows.at(-1).generation,outcomes:{}};
  for(const r of rows)summary.outcomes[r.outcome]=(summary.outcomes[r.outcome]??0)+1;
  console.log(JSON.stringify(summary));return {summary,rows};
}
const summaries=[];
for(const clip of JSON.parse(fs.readFileSync(path.join(root,'runs/cue-person-offline-20261019/sources.json')))) {
  const {summary}=await replay(clip.file,clip.id,clip.fps);
  if(summary.frames!==clip.frames)throw Error(`${clip.id}: frame count mismatch`);
  summaries.push(summary);
}
// User-confirmed COD nemesis glyphs must be included by the production detector.
// Automatic ownership is reported separately: colored effects can still win.
const skulls=[];
for(const [id,xy] of [['v2-f631',[316,233]],['v3-f150',[296,210]]]) {
  const {rows}=await replay(path.join(root,'runs/cue-person-offline-20261019',id+'.png'),id,60,true);
  const r=rows[0],d=r.detections[r.selected];
  const marker=r.independent_cues.find(c=>Math.hypot(c.xy[0]-xy[0],c.xy[1]-xy[1])<=2);
  if(!marker)throw Error(`${id}: nemesis glyph excluded by production detector`);
  skulls.push({id,detectedCue:marker.xy,selectedCue:d?.cue??null,hasTarget:r.has_target,
    nemesisPaired:!!d?.has_cue && Math.hypot(d.cue[0]-xy[0],d.cue[1]-xy[1])<=2,
    selected:r.selected,generation:r.generation});
}
fs.writeFileSync(path.join(output,'replay-summary.json'),JSON.stringify({summaries,skulls,
  note:'Unannotated automatic replay: counts are coverage, not accuracy or live-game acceptance.'},null,2)+'\n');
