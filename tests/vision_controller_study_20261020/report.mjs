import fs from 'node:fs';
const out='runs/vision-controller-study-20261020';
function csv(file){const a=fs.readFileSync(file,'utf8').trim().split(/\r?\n/),keys=a.shift().split(',');return a.map(l=>Object.fromEntries(l.split(',').map((v,i)=>[keys[i],isNaN(+v)?v:+v])));}
const seeds=[71020261,71020279,71020303,71020321];
const cases=seeds.flatMap(s=>csv(`${out}/final-${s}/cases.csv`));
const open=csv(out+'/open/cases.csv'),trace=csv(out+'/open/trace.csv');
const cutoffs=[];
for(let i=1;i<trace.length;i++){
 const a=trace[i-1],b=trace[i];
 if(a.case===b.case&&a.target_id&&a.target_id===b.target_id&&a.mode===2&&b.mode===2&&a.source===1&&b.source===2&&a.authority>0&&b.authority===0&&!b.gap){
  cutoffs.push({case:b.case,t:b.video_t,frame:b.frame,previousAuthority:a.authority,error:Math.hypot(b.error_x,b.error_y),sourceStep:Math.hypot(b.source_x-a.source_x,b.source_y-a.source_y),previousOutput:Math.hypot(a.out_x,a.out_y),output:Math.hypot(b.out_x,b.out_y)});
 }
}
fs.writeFileSync(out+'/radius-events.json',JSON.stringify(cutoffs,null,2));
const summary={seeds,cases:cases.length,ticks:cases.reduce((s,c)=>s+c.ticks,0),violations:cases.reduce((s,c)=>s+c.violations,0),openCases:open.length,openTicks:open.reduce((s,c)=>s+c.ticks,0),openViolations:open.reduce((s,c)=>s+c.violations,0),sourceCutoffs:cutoffs.length,smallStepCutoffs:cutoffs.filter(c=>c.sourceStep<5).length,incident:cases.filter(c=>c.clip==='clean'&&c.start===121.5&&c.manual===0),cueColdStart:cases.filter(c=>c.clip==='clean'&&c.start===122&&c.manual===0).map(c=>({seed:c.seed,duration:c.duration,target:c.target})),cutoffExamples:cutoffs.filter(c=>c.sourceStep<5).slice(0,8)};
fs.writeFileSync(out+'/summary.json',JSON.stringify(summary,null,2));
console.log(JSON.stringify(summary,null,2));
// Standalone scientific trace; fixed scales make the source-only cutoff visible.
const a=trace.filter(r=>r.case===6&&r.tick>=10700&&r.tick<=10880);
const x=t=>70+(t-10700)/180*780,y=v=>245-v*170;
const path=key=>a.map((r,i)=>`${i?'L':'M'}${x(r.tick).toFixed(2)},${y(key(r)).toFixed(2)}`).join(' ');
let svg=`<svg xmlns="http://www.w3.org/2000/svg" width="920" height="340" viewBox="0 0 920 340"><rect width="920" height="340" fill="#fff"/><g font-family="Arial,sans-serif" fill="#182230"><text x="45" y="32" font-size="20">Same target: person → cue → person</text><text x="45" y="54" font-size="13">v1 video 10.700–10.880 s · neutral input · open-loop policy/output replay</text><rect x="${x(10780)}" y="75" width="${x(10796)-x(10780)}" height="170" fill="#fff0cb"/>`;
for(const v of [0,.5,1])svg+=`<line x1="70" y1="${y(v)}" x2="850" y2="${y(v)}" stroke="#ddd"/><text x="32" y="${y(v)+4}" font-size="12">${v}</text>`;
for(const t of [10700,10740,10780,10820,10860])svg+=`<text x="${x(t)-20}" y="267" font-size="12">${(t/1000).toFixed(3)}</text>`;
svg+=`<path d="${path(r=>r.authority)}" fill="none" stroke="#1459b5" stroke-width="2.5"/><path d="${path(r=>Math.hypot(r.out_x,r.out_y))}" fill="none" stroke="#c34b2b" stroke-width="1.8"/><text x="70" y="300" font-size="14" fill="#1459b5">Blue: aim authority</text><text x="270" y="300" font-size="14" fill="#c34b2b">Orange: output magnitude</text><text x="535" y="300" font-size="14">Yellow: cue evidence</text><text x="70" y="323" font-size="12">Cue frame: point moves 2.06 px; authority 0.845 → 0; person returns after 16 ms.</text></g></svg>`;
fs.writeFileSync(out+'/radius-transition.svg',svg);
