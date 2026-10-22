import fs from 'node:fs';
import {spawn,spawnSync} from 'node:child_process';
const out='runs/downpull-20261023',file='E:/videoCut/2026-10-22 10-40-12-cut-merged-1792637433471.mp4';
const env={...process.env},pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
const probe=spawnSync('ffprobe',['-v','error','-select_streams','v','-show_entries','frame=best_effort_timestamp_time','-of','json',file],{windowsHide:true,encoding:'utf8'});
if(probe.status!==0)throw Error(probe.stderr);const timestamps=JSON.parse(probe.stdout).frames.map(f=>+f.best_effort_timestamp_time);
fs.writeFileSync(out+'/timestamps.json',JSON.stringify(timestamps));
function done(p){let err='';p.stderr.on('data',x=>err+=x);return new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',c=>c===0?resolve(err):reject(Error(err)));});}
for(const variant of ['clean','dev']) {
 const decode=spawn('ffmpeg',['-v','error','-i',file,'-vf','crop=640:512','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
 const infer=spawn(`${out}/build/Release/export_${variant}.exe`,['D:/work/AI/yolo-study-001-clean/models/best_480x384.engine',`${out}/${variant}.records`,'120','.35','160'],{env,windowsHide:true,stdio:['pipe','ignore','pipe']});
 const pending=Promise.all([done(decode),done(infer)]);infer.stdin.on('error',()=>{});decode.stdout.pipe(infer.stdin);console.log(variant+': '+(await pending)[1]);
 const rows=fs.readFileSync(`${out}/${variant}.records`,'utf8').trim().split(/\r?\n/).map(l=>l.split(' '));
 if(rows.length!==timestamps.length)throw Error('decoded frame count mismatch');
 // Record actual presentation time, not average fps, for Controller replay.
 for(let i=0;i<rows.length;++i)rows[i][1]=String(Math.round(1e9+(timestamps[i]-timestamps[0])*1e9));
 fs.writeFileSync(`${out}/${variant}.records`,rows.map(r=>r.join(' ')).join('\n')+'\n');
}
