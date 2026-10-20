// Run after replay_comparison_20261020.mjs with the frozen previous-policy build.
// Keep those baselines: this script writes a separate OR/reference result.
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
const root=process.cwd(),out=path.join(root,'runs/cue-review-20261020');
const env={...process.env},pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`${root}/native/build/Release;D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
const clips=[
  {id:'clean-tail',file:'E:/迅雷下载/2026-10-20 00-29-16.mp4',start:110,duration:19.775},
  {id:'dev-first60',file:'E:/迅雷下载/2026-10-20 00-25-46.mp4',start:0,duration:60},
];
function done(p){let err='';p.stderr.on('data',x=>err+=x);return new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',c=>c===0?resolve():reject(Error(err)));});}
if(!process.argv.includes('--summarize-only')) for(const clip of clips) {
  const file=path.join(out,`${clip.id}-or-reference.jsonl`);
  const decoder=spawn('ffmpeg',['-v','error','-ss',String(clip.start),'-i',clip.file,'-t',String(clip.duration),'-vf','crop=640:512:640:284','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const native=spawn(path.join(root,'native/build/Release/vision_cue_video_review.exe'),[path.join(root,'models/best_480x384.engine'),file,'120','190','.2','.65','1','cue-owned'],{env,windowsHide:true,stdio:['pipe','ignore','pipe']});
  const pending=Promise.all([done(decoder),done(native)]);
  native.stdin.on('error',e=>{if(e.code!=='EPIPE')console.error(e);});decoder.stdout.pipe(native.stdin);
  try{await pending;}catch(e){decoder.kill();native.kill();throw e;}
  console.log(`${clip.id} OR reference complete`);
}
const summaries=[];
for(const clip of clips) {
  const read=mode=>fs.readFileSync(path.join(out,`${clip.id}-${mode}.jsonl`),'utf8').trim().split('\n').map(JSON.parse);
  const original=read('person'),previous=read('cue'),current=read('or-reference');
  if(original.length!==current.length || previous.length!==current.length)throw Error('mismatched frames');
  function summarize(indices) {
    const result={frames:indices.length,original:0,previous:0,current:0,currentCueOnly:0,originalOnly:0,currentOnly:0,sameBodyPointDifferences:0};
    for(const i of indices) {
      const p=original[i],c=current[i];
      for(const key of ['conf','class']) if(p.detections.some((d,j)=>d[key]!==c.detections[j]?.[key]))throw Error('detector mismatch');
      if(p.detections.length!==c.detections.length || p.detections.some((d,j)=>d.box.some((v,k)=>v!==c.detections[j].box[k])))throw Error('detector box mismatch');
      result.original+=+p.has_target;result.previous+=+previous[i].has_target;result.current+=+c.has_target;
      result.currentCueOnly+=+(c.has_target&&c.selected<0);
      result.originalOnly+=+(p.has_target&&!c.has_target);result.currentOnly+=+(!p.has_target&&c.has_target);
      if(p.has_target&&c.has_target&&p.selected>=0&&p.selected===c.selected&&Math.hypot(p.target[0]-c.target[0],p.target[1]-c.target[1])>.01)++result.sameBodyPointDifferences;
      if(c.selected<0 && c.fire_authority)throw Error('cue fire authority');
    }
    return result;
  }
  const summary={clip:clip.id,all:summarize(current.map((_,i)=>i))};
  if(clip.id==='clean-tail')summary.incident122_2to122_6=summarize(current.flatMap((_,i)=>i/120+clip.start>=122.2&&i/120+clip.start<122.6?[i]:[]));
  summaries.push(summary);console.log(JSON.stringify(summary));
}
fs.writeFileSync(path.join(out,'or-reference-summary.json'),JSON.stringify({clips,summaries,limitations:'Compressed video at 120 fps, no live manual intent/request resets. Output availability is not target accuracy or live-game acceptance.'},null,2));
