import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const input='runs/cue-person-offline-20261019',out='runs/cue-identity-offline-20261019';
const clips=JSON.parse(fs.readFileSync(path.join(input,'sources.json')));
const requests=[
 {clip:'v2',frames:[620,625,630,635,640,645,650,655,660,665,670,675]},
 {clip:'v1',frames:[150,155,160,165,170,175,180,185]},
 {clip:'v6',frames:[150,155,160,165,170,175,180,185]}
];
function run(args){const r=spawnSync('ffmpeg',args,{encoding:'utf8',windowsHide:true});if(r.status!==0)throw Error(r.stderr||r.error?.message);}
const samples=[];
for(const req of requests){const c=clips.find(c=>c.id===req.clip);
 for(const frame of req.frames){const id=`${req.clip}-f${frame}`,dest=path.join(out,id+'.png');
  if(!fs.existsSync(dest))run(['-v','error','-i',c.file,'-vf',`select=eq(n\\,${frame}),crop=640:512:640:284`,'-frames:v','1','-fps_mode','passthrough',dest]);
  samples.push({id,clip:req.clip,frame});
 }
}
for(let i=0;i<samples.length;i+=4){const group=samples.slice(i,i+4),args=['-v','error'];for(const s of group)args.push('-i',path.join(out,s.id+'.png'));
 const filters=group.map((s,k)=>`[${k}:v]drawtext=fontfile='C\\:/Windows/Fonts/arial.ttf':text='${s.id}':x=8:y=478:fontsize=20:fontcolor=white:box=1:boxcolor=black[l${k}]`);
 filters.push(group.map((s,k)=>`[l${k}]`).join('')+`xstack=inputs=4:layout=0_0|640_0|0_512|640_512[out]`);
 const dest=path.join(out,`sheet-${i/4+1}.png`);if(!fs.existsSync(dest))run([...args,'-filter_complex',filters.join(';'),'-map','[out]','-frames:v','1',dest]);
}
fs.writeFileSync(path.join(out,'samples.json'),JSON.stringify(samples,null,2));console.log('extracted '+samples.length+' frames');
