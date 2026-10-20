import fs from 'node:fs';
import path from 'node:path';
import {spawn,spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const root=path.resolve(here,'../..');
const out=path.join(root,'runs/cue-person-offline-20261019');
fs.mkdirSync(out,{recursive:true});
const exe=path.join(out,'build/Release/cue_person_offline.exe');
const model=path.join(root,'models/best_480x384.engine');
const env={...process.env};
const pk=Object.keys(env).find(k=>k.toLowerCase()==='path');
env[pk]=`${root}/native/build/Release;D:/env/TensorRT-10.15.1.29/lib;C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.1/bin;${env[pk]}`;
function sync(cmd,args){const r=spawnSync(cmd,args,{encoding:'utf8',windowsHide:true,maxBuffer:20*1024*1024});if(r.status!==0)throw Error(`${cmd}: ${r.stderr}`);return r.stdout;}
function probe(file){return JSON.parse(sync('ffprobe',['-v','error','-select_streams','v:0','-show_entries','stream=width,height,avg_frame_rate,nb_frames:format=duration','-of','json',file]));}
async function replay(file,id,fps,isImage=false){
  const log=fs.openSync(path.join(out,`${id}.log`),'w');
  const native=spawn(exe,[model,path.join(out,`${id}.jsonl`),String(fps)],{env,windowsHide:true,stdio:['pipe',log,log]});
  const args=['-v','error','-i',file,'-an'];if(!isImage)args.push('-vf','crop=640:512:640:284');
  args.push('-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1');
  const decode=spawn('ffmpeg',args,{windowsHide:true,stdio:['ignore','pipe',log]});
  native.stdin.on('error',()=>{});decode.stdout.pipe(native.stdin);
  const [nc,dc]=await Promise.all([new Promise(r=>native.on('close',r)),new Promise(r=>decode.on('close',r))]);
  fs.closeSync(log);if(nc||dc)throw Error(`replay ${id}: ${nc}/${dc}`);
  console.log(`${id} complete`);
}
const mode=process.argv[2];
if(mode==='prepare'){
  const dir='C:/Users/Administrator/Videos/NVIDIA/Call of Duty  Black Ops 7';
  const clips=fs.readdirSync(dir).filter(x=>x.endsWith('.mp4')).sort().map((name,i)=>{
    const file=path.join(dir,name),m=probe(file),s=m.streams[0],frac=s.avg_frame_rate.split('/').map(Number);
    return {id:`v${i+1}`,file,fps:frac[0]/frac[1],frames:Number(s.nb_frames),seconds:Number(m.format.duration),width:s.width,height:s.height};
  });
  const samples=[];
  for(const c of clips){
    if(c.width!==1920||c.height!==1080)throw Error('Unexpected source resolution');
    for(let t=.5;t<c.seconds-.1;t+=2){
      const frame=Math.round(t*c.fps),id=`${c.id}-f${frame}`;
      sync('ffmpeg',['-v','error','-i',c.file,'-vf',`select=eq(n\\,${frame}),crop=640:512:640:284`,'-frames:v','1','-fps_mode','passthrough',path.join(out,`${id}.png`)]);
      samples.push({id,clip:c.id,frame,nominal_t:t});
    }
  }
  fs.writeFileSync(path.join(out,'sources.json'),JSON.stringify(clips,null,2));
  fs.writeFileSync(path.join(out,'samples.json'),JSON.stringify(samples,null,2));
  console.log(JSON.stringify({clips:clips.length,frames:clips.reduce((s,c)=>s+c.frames,0),samples:samples.length}));
}else if(mode==='baseline-check'){
  const c=JSON.parse(fs.readFileSync(path.join(out,'sources.json')))[0];
  const log=fs.openSync(path.join(out,'baseline-reference.log'),'w');
  const native=spawn(path.join(out,'build/Release/baseline_reference.exe'),[model,path.join(out,'baseline-reference.jsonl'),String(c.fps),'190','.2','.65','1'],{env,windowsHide:true,stdio:['pipe',log,log]});
  const decode=spawn('ffmpeg',['-v','error','-i',c.file,'-an','-vf','crop=640:512:640:284','-fps_mode','passthrough','-pix_fmt','bgra','-f','rawvideo','pipe:1'],{windowsHide:true,stdio:['ignore','pipe',log]});
  native.stdin.on('error',()=>{});decode.stdout.pipe(native.stdin);
  const [nc,dc]=await Promise.all([new Promise(r=>native.on('close',r)),new Promise(r=>decode.on('close',r))]);
  fs.closeSync(log);if(nc||dc)throw Error(`reference failed ${nc}/${dc}`);
  const readRows=id=>fs.readFileSync(path.join(out,id+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse);
  const ref=readRows('baseline-reference'),exp=readRows('v1');let maxDiff=0;
  if(ref.length!==exp.length)throw Error('reference frame mismatch');
  for(let i=0;i<ref.length;++i){const a=ref[i],b=exp[i];
    for(const k of ['frame','has_target','source','generation','selected'])if(a[k]!==b[k])throw Error(`state mismatch ${i} ${k}`);
    if(a.detections.length!==b.detections.length)throw Error(`detection count ${i}`);
    for(let d=0;d<a.detections.length;++d){const x=a.detections[d],y=b.detections[d];
      for(const k of ['class','friendly','has_cue'])if(x[k]!==y[k])throw Error(`detection state ${i}/${d}/${k}`);
      for(const k of ['box','cue'])for(let j=0;j<x[k].length;++j)maxDiff=Math.max(maxDiff,Math.abs(x[k][j]-y[k][j]));
      maxDiff=Math.max(maxDiff,Math.abs(x.conf-y.conf));
    }
    for(let j=0;j<2;++j)maxDiff=Math.max(maxDiff,Math.abs(a.target[j]-b.target[j]));
  }
  if(maxDiff>0.0001)throw Error(`numerical mismatch ${maxDiff}`);
  const result={frames:ref.length,all_states_equal:true,all_detection_states_equal:true,max_serialized_coordinate_difference:maxDiff,tolerance:0.0001,note:'Different JSON print precision: reference 9 significant digits, experiment 7.'};
  fs.writeFileSync(path.join(out,'baseline-check.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}else if(mode==='development'){
  for(const name of fs.readdirSync(path.join(root,'output/cue-review-20261014')).filter(n=>n.endsWith('.png')).sort())
    await replay(path.join(root,'output/cue-review-20261014',name),`dev-${name.slice(0,-4)}`,60,true);
}else if(mode==='videos'){
  const clips=JSON.parse(fs.readFileSync(path.join(out,'sources.json')));
  for(const c of clips)await replay(c.file,c.id,c.fps);
}else if(mode==='visuals'){
  const cases=[['dev-clip2-f731','blood bar vs dot'],['dev-clip4-f735','blood bar vs dot'],['v1-f150','duplicate person boxes - missed pair'],['v4-f0','false pair on gold equipment']];
  const paths=[];
  for(const [id,title] of cases){
    const dev=id.startsWith('dev-'),image=dev?path.join(root,'output/cue-review-20261014',id.slice(4)+'.png'):path.join(out,id+'.png');
    const r=dev?JSON.parse(fs.readFileSync(path.join(out,id+'.jsonl'),'utf8').trim()):fs.readFileSync(path.join(out,id.split('-')[0]+'.jsonl'),'utf8').trim().split('\n').map(JSON.parse)[+id.split('-f')[1]];
    const filters=[];
    const box=(x,y,w,h,color)=>filters.push(`drawbox=x=${Math.max(0,x)}:y=${Math.max(0,y)}:w=${w}:h=${h}:color=${color}:t=2`);
    for(const d of r.detections){box(d.box[0],d.box[1],d.box[2]-d.box[0],d.box[3]-d.box[1],'violet');if(d.has_cue)box(d.cue[0]-5,d.cue[1]-5,10,10,'red');}
    for(const c of r.cues)box(c.xy[0]-3,c.xy[1]-3,6,6,'blue');
    for(const p of r.pairs){const c=r.cues[p.c];box(c.xy[0]-9,c.xy[1]-9,18,18,'cyan');}
    filters.push(`drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='${id} - ${title}':x=8:y=478:fontsize=16:fontcolor=white:box=1:boxcolor=black`);
    filters.push(`drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='RED old - BLUE candidates - CYAN paired':x=8:y=8:fontsize=15:fontcolor=white:box=1:boxcolor=black`);
    const p=path.join(out,id+'-overlay.png');if(!fs.existsSync(p))sync('ffmpeg',['-v','error','-i',image,'-vf',filters.join(','),'-frames:v','1',p]);paths.push(p);
  }
  const args=['-v','error'];for(const p of paths)args.push('-i',p);
  args.push('-filter_complex','[0:v][1:v][2:v][3:v]xstack=inputs=4:layout=0_0|640_0|0_512|640_512[out]','-map','[out]','-frames:v','1',path.join(out,'comparison.png'));sync('ffmpeg',args);
}else if(mode==='supplementary'){
  const samples=JSON.parse(fs.readFileSync(path.join(out,'supplementary.json'))),clips=JSON.parse(fs.readFileSync(path.join(out,'sources.json')));
  for(const s of samples){
    if(fs.existsSync(path.join(out,`${s.id}.png`)))continue;
    const c=clips.find(c=>c.id===s.clip);
    sync('ffmpeg',['-v','error','-i',c.file,'-vf',`select=eq(n\\,${s.frame}),crop=640:512:640:284`,'-frames:v','1','-fps_mode','passthrough',path.join(out,`${s.id}.png`)]);
  }
}else if(mode==='sheets'){
  const samples=JSON.parse(fs.readFileSync(path.join(out,'samples.json')));
  for(let i=0;i<samples.length;i+=4){
    const group=samples.slice(i,i+4),args=['-v','error'];for(const s of group)args.push('-i',path.join(out,`${s.id}.png`));
    const filters=group.map((s,k)=>`[${k}:v]drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='${s.id}':x=10:y=478:fontsize=22:fontcolor=white:box=1:boxcolor=black[l${k}]`);
    if(group.length>1)filters.push(group.map((s,k)=>`[l${k}]`).join('')+`xstack=inputs=${group.length}:layout=${group.map((s,k)=>`${k%2*640}_${Math.floor(k/2)*512}`).join('|')}:fill=black[out]`);
    args.push('-filter_complex',filters.join(';'),'-map',group.length>1?'[out]':'[l0]','-frames:v','1',path.join(out,`sheet-${1+i/4}.png`));
    sync('ffmpeg',args);
  }
}else throw Error('prepare|development|videos|sheets');
