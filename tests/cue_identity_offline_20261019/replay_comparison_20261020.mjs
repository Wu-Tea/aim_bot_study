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
for(const clip of clips) for(const mode of ['person','cue']) {
  const file=path.join(out,`${clip.id}-${mode}.jsonl`);
  const decoder=spawn('ffmpeg',['-v','error','-ss',String(clip.start),'-i',clip.file,'-t',String(clip.duration),'-vf','crop=640:512:640:284','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
  const args=[path.join(root,'models/best_480x384.engine'),file,'120','190','.2','.65','1'];
  if(mode==='cue')args.push('cue-owned');
  // The current executable now implements OR/reference. Preserve the previous
  // cue-mandatory policy by explicitly using its archived executable.
  const executable=mode==='cue'?path.join(out,'cue-owned-before.exe'):path.join(root,'native/build/Release/vision_cue_video_review.exe');
  if(!fs.existsSync(executable)){decoder.kill();throw Error(`Missing baseline executable: ${executable}`);}
  const native=spawn(executable,args,{env,windowsHide:true,stdio:['pipe','ignore','pipe']});
  const pending=Promise.all([done(decoder),done(native)]);
  native.stdin.on('error',e=>{if(e.code!=='EPIPE')console.error(e);});decoder.stdout.pipe(native.stdin);
  try{await pending;}catch(e){decoder.kill();native.kill();throw e;}
  console.log(`${clip.id} ${mode} complete`);
}
const summaries=[];
for(const clip of clips) {
  const read=mode=>fs.readFileSync(path.join(out,`${clip.id}-${mode}.jsonl`),'utf8').trim().split('\n').map(JSON.parse);
  const person=read('person'),cue=read('cue');
  if(person.length!==cue.length)throw Error('mismatched frames');
  const summary={clip:clip.id,start:clip.start,frames:person.length,personTargets:0,cueTargets:0,personOnly:0,cueOnly:0,both:0,neither:0,cueReasonsWhenPersonSelected:{},examples:[],bodyPointDisagreements:0};
  for(let i=0;i<person.length;++i) {
    const p=person[i],c=cue[i];
    if(p.detections.length!==c.detections.length)throw Error('detector mismatch');
    for(let j=0;j<p.detections.length;++j) {
      const a=p.detections[j],b=c.detections[j];
      if(a.conf!==b.conf || a.box.some((v,k)=>v!==b.box[k]))throw Error('raw person detections differ');
    }
    if(p.has_target)++summary.personTargets;if(c.has_target)++summary.cueTargets;
    ++summary[p.has_target?(c.has_target?'both':'personOnly'):(c.has_target?'cueOnly':'neither')];
    if(p.has_target&&!c.has_target) {
      summary.cueReasonsWhenPersonSelected[c.outcome]=(summary.cueReasonsWhenPersonSelected[c.outcome]??0)+1;
      if(p.selected>=0 && (!summary.examples.length || clip.start+p.t-summary.examples.at(-1).t>2))
        summary.examples.push({t:clip.start+p.t,frame:i,person:p.detections[p.selected],cueOutcome:c.outcome,cues:c.independent_cues});
    }
    if(p.has_target&&c.has_target&&p.selected>=0&&p.selected===c.selected&&Math.hypot(p.target[0]-c.target[0],p.target[1]-c.target[1])>.01)++summary.bodyPointDisagreements;
  }
  summaries.push(summary);console.log(JSON.stringify({...summary,examples:summary.examples.slice(0,3)}));
}
fs.writeFileSync(path.join(out,'comparison-summary.json'),JSON.stringify({clips,summaries,limitations:'Offline compressed footage, no live request resets or manual intent. Matching person selections should have the same anatomical point. Output counts are not target accuracy.'},null,2));
