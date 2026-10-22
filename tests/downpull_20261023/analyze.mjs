import fs from 'node:fs';
const root='runs/downpull-20261023';
for(const variant of ['clean','dev']) {
 const [head,...lines]=fs.readFileSync(`${root}/replay-${variant}.csv`,'utf8').trim().split(/\r?\n/),keys=head.split(',');
 const rows=lines.map(l=>Object.fromEntries(l.split(',').map((v,i)=>[keys[i],+v])));
 const summaries=[];
 for(const id of [...new Set(rows.map(r=>r.case))]) {
  const rr=rows.filter(r=>r.case===id&&r.t<(r.scene?5.54:2.65)),valid=rr.filter(r=>r.target),min=rr.reduce((a,b)=>a.out_y<b.out_y?a:b),jump=rr.reduce((a,b)=>a.dy<b.dy?a:b);
  summaries.push({case:id,scene:min.scene,fire:min.fire,late:min.late_ads,latency:min.latency,feedback:min.feedback,min_y:min.out_y,min_t:min.t,jump:jump.dy,jump_t:jump.t,jump_source:jump.source,wrong_way_ms:valid.filter(r=>r.error_y< -10&&r.pre_y< -.15).length});
 }
 fs.writeFileSync(`${root}/summary-${variant}.json`,JSON.stringify(summaries,null,2));
 console.log(variant,JSON.stringify(summaries.filter(r=>r.latency===6&&r.late===1)));
}
