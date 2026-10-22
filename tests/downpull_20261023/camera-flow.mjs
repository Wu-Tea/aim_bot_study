import fs from 'node:fs';
// FFmpeg vidstab local motions are reverse correspondences (verified with
// a known +4,+3 px translation). Reject weapon/HUD regions before medians.
const lines=fs.readFileSync('runs/downpull-20261023/camera-motion.txt','utf8').split(/\r?\n/);
let cx=0,cy=0;const rows=[];
const median=a=>a.sort((x,y)=>x-y)[Math.floor(a.length/2)]??0;
for(const line of lines) {
 const id=+line.match(/^Frame (\d+)/)?.[1];if(!id)continue;
 const fields=[...line.matchAll(/\(LM ([^)]+)\)/g)].map(m=>m[1].split(' ').map(Number))
  .filter(([dx,dy,x,y,size,contrast,error])=>x>180&&x<850&&y>100&&y<380&&
    !(x>350&&x<620&&y>245)&&error<4&&contrast>.3);
 const dx=fields.length>=5?-2*median(fields.map(f=>f[0])):0;
 const dy=fields.length>=5?-2*median(fields.map(f=>f[1])):0;
 cx+=dx;cy+=dy;rows.push(`${id} ${cx} ${cy} ${fields.length}`);
}
fs.writeFileSync('runs/downpull-20261023/camera-flow.txt',rows.join('\n')+'\n');
console.log('estimated frames',rows.length,'insufficient',rows.filter(r=>+r.split(' ')[3]<5).length);
