import fs from 'node:fs';
import {spawn,spawnSync} from 'node:child_process';
const root=process.argv[2]??'runs/cue-opt-20261023/videos';fs.mkdirSync(root,{recursive:true});
const executable=process.argv[3]??'runs/cue-opt-20261023/build/Release/replay.exe';
const env={...process.env},pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
const videos=[
 ['downpull','E:/videoCut/2026-10-22 10-40-12-cut-merged-1792637433471.mp4'],
 ['dev','E:/迅雷下载/2026-10-20 00-25-46.mp4'],
 ['clean','E:/迅雷下载/2026-10-20 00-29-16.mp4'],
];
function done(p){let output='';p.stderr.on('data',x=>output+=x);p.stdout?.on('data',x=>output+=x);return new Promise((resolve,reject)=>{p.on('error',reject);p.on('exit',code=>code===0?resolve(output):reject(Error(output)));});}
for(const [id,file] of videos){
 if(process.argv[4] && process.argv[4]!==id)continue;
 const output=`${root}/${id}.csv`;if(fs.existsSync(output))throw Error('Refusing to overwrite '+output);
 const probe=spawnSync('ffprobe',['-v','error','-select_streams','v','-show_entries','stream=r_frame_rate','-of','json',file],{windowsHide:true,encoding:'utf8'});
 if(probe.status!==0)throw Error(probe.stderr);const rate=JSON.parse(probe.stdout).streams[0].r_frame_rate.split('/').map(Number);const fps=rate[0]/rate[1];
 const decode=spawn('ffmpeg',['-v','error','-i',file,'-vf','crop=640:512','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe','pipe']});
 const check=spawn(executable,['D:/work/AI/yolo-study-001-clean/models/best_480x384.engine',output,String(fps)],{env,windowsHide:true,stdio:['pipe','pipe','pipe']});
 // Keep decoder stdout exclusively for binary input, never collect it as text.
 const decoding=new Promise((resolve,reject)=>{let err='';decode.stderr.on('data',x=>err+=x);decode.on('error',reject);decode.on('exit',c=>c===0?resolve():reject(Error(err)));});
 const checking=done(check);check.stdin.on('error',()=>{});decode.stdout.pipe(check.stdin);
 try{const result=await Promise.all([decoding,checking]);console.log(id+': '+result[1]);fs.writeFileSync(`${root}/${id}.txt`,result[1]);}
 catch(e){decode.kill();check.kill();throw e;}
}
