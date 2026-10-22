import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
const root=process.cwd(),out=path.resolve(process.argv[2]??'runs/vision-controller-study-20261020');
fs.mkdirSync(out,{recursive:true});
const env={...process.env},pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`${root}/native/build/Release;D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
const clips=[
 {id:'clean',file:'E:/迅雷下载/2026-10-20 00-29-16.mp4',fps:120,frames:15573,seconds:129.775},
 {id:'dev',file:'E:/迅雷下载/2026-10-20 00-25-46.mp4',fps:120,frames:23199,seconds:193.325},
 ...JSON.parse(fs.readFileSync('runs/cue-person-offline-20261019/sources.json','utf8')),
];
function done(p){let err='';p.stderr.on('data',x=>err+=x);return new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',c=>c===0?resolve(err):reject(Error(err)));});}
const completed=[];
for(const clip of clips){
 const file=path.join(out,clip.id+'.records');
 const metadata=file+'.json';
 if(fs.existsSync(metadata)){const item=JSON.parse(fs.readFileSync(metadata,'utf8'));completed.push(item);fs.writeFileSync(path.join(out,'sources.json'),JSON.stringify(completed,null,2));console.log('cached '+clip.id);continue;}
 const decoder=spawn('ffmpeg',['-v','error','-i',clip.file,'-vf','crop=640:512','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
 const native=spawn(path.join(root,'runs/vision-controller-study-20261020/build/Release/export_vision.exe'),[path.join(root,'models/best_480x384.engine'),file,String(clip.fps)],{env,windowsHide:true,stdio:['pipe','ignore','pipe']});
 const pending=Promise.all([done(decoder),done(native)]);native.stdin.on('error',e=>{if(e.code!=='EPIPE')console.error(e);});decoder.stdout.pipe(native.stdin);
 let message;try{message=(await pending)[1].trim();}catch(e){decoder.kill();native.kill();throw e;}
 const match=message.match(/frames=(\d+) targets=(\d+) cues=(\d+)/);if(!match||Math.abs(+match[1]-clip.frames)>1)throw Error('frame count mismatch '+clip.id+' '+message);
 const item={...clip,containerFrames:clip.frames,frames:+match[1],targets:+match[2],cues:+match[3]};completed.push(item);console.log(JSON.stringify(item));
 fs.writeFileSync(metadata,JSON.stringify(item,null,2));
 fs.writeFileSync(path.join(out,'sources.json'),JSON.stringify(completed,null,2));
}
