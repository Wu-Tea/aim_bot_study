import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const root='tests/vision_controller_study_20261020/trajectories';
const output=process.argv[2]??'runs/vision-controller-study-20261020/trajectory-matrix';
const seeds=process.argv.slice(3).map(Number);if(!seeds.length)seeds.push(71020503,71020513,71020527,71020543);
const manifest=JSON.parse(fs.readFileSync(root+'/manifest.json','utf8'));
const executable='runs/vision-controller-study-20261020/build/Release/controller_study.exe';
fs.mkdirSync(output,{recursive:true});
const combined=[];
for(const seed of seeds){
 const dir=path.join(output,String(seed));
 const run=spawnSync(executable,['tests/ads_cooperation_20261019/fixture.toml',root+'/replay.txt',dir,String(seed),'closed','-'],{windowsHide:true,encoding:'utf8'});
 if(run.error)throw run.error;process.stdout.write(run.stdout??'');process.stderr.write(run.stderr??'');if(run.status!==0)throw Error(`seed ${seed} exited ${run.status}`);
 const lines=fs.readFileSync(path.join(dir,'cases.csv'),'utf8').trim().split(/\r?\n/),header=lines.shift().split(',');
 for(const line of lines)combined.push(Object.fromEntries(line.split(',').map((v,i)=>[header[i],isNaN(+v)?v:+v])));
}
const summary={seeds,trajectories:manifest.trajectories.length,sourceFrames:manifest.totalFrames,cases:combined.length,ticks:combined.reduce((s,r)=>s+r.ticks,0),violations:combined.reduce((s,r)=>s+r.violations,0),note:'Recorded screen geometry used as exogenous disturbance. No game hit-rate verdict. Fault trajectories are separately labelled in the fixture manifest.'};
fs.writeFileSync(path.join(output,'summary.json'),JSON.stringify(summary,null,2));console.log(JSON.stringify(summary,null,2));
