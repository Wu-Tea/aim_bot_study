import fs from 'node:fs';
import {spawnSync} from 'node:child_process';
const root='runs/vision-controller-fixes-20261020';
const manifest=JSON.parse(fs.readFileSync('tests/vision_controller_study_20261020/trajectories/manifest.json','utf8'));
const sources=JSON.parse(fs.readFileSync(root+'/vision/sources.json','utf8'));
if(sources.length!==11)throw Error('Full pixel export must finish before replay');
fs.mkdirSync(root+'/fresh-trajectories',{recursive:true});
const cache=new Map();
for(const t of manifest.trajectories) {
 if(!cache.has(t.clip)) cache.set(t.clip,fs.readFileSync(`${root}/vision/${t.clip}.records`,'utf8').trim().split(/\r?\n/).map(l=>l.split(' ')));
 const rows=cache.get(t.clip).filter(r=>+r[0]>=t.firstFrame&&+r[0]<=t.lastFrame);
 if(rows.length!==t.frames)throw Error('changed trajectory capture range');
 const origin=+rows[0][1];
 fs.writeFileSync(`${root}/fresh-trajectories/${t.recordFile}`,rows.map(r=>{
  const tokens=r.slice();tokens[1]=String(+r[1]-origin+1e9);return tokens.join(' ');
 }).join('\n')+'\n');
}
fs.writeFileSync(root+'/fresh-windows.txt',manifest.trajectories.map(t=>`${t.id} ${root}/fresh-trajectories/${t.recordFile} 0`).join('\n')+'\n');
for(const seed of [71020503,71020513,71020527,71020543]) {
 const run=spawnSync('runs/vision-controller-study-20261020/build/Release/controller_study.exe',[
  'tests/ads_cooperation_20261019/fixture.toml',root+'/fresh-windows.txt',`${root}/fresh-chain/${seed}`,String(seed),'closed','-'],
  {windowsHide:true,encoding:'utf8'});
 if(run.error)throw run.error;process.stdout.write(run.stdout??'');process.stderr.write(run.stderr??'');
 if(run.status!==0)throw Error(`fresh chain failed ${seed}`);
}
