import fs from 'node:fs';
const root='runs/downpull-20261023';
const telemetry=JSON.parse(fs.readFileSync(root+'/20261022T020004Z_26152_1/telemetry-controller.json','utf8'));
const rows=fs.readFileSync(root+'/clean.records','utf8').trim().split(/\r?\n/).map(l=>l.split(' ').map(Number));
function nearest(t){let l=0,h=telemetry.length;while(l<h){let m=(l+h)>>1;if(telemetry[m].video_utc_t<t)l=m+1;else h=m;}if(l>0&&(l===telemetry.length||Math.abs(telemetry[l-1].video_utc_t-t)<Math.abs(telemetry[l].video_utc_t-t)))l--;return telemetry[l];}
const result=[];
for(const [name,start,end,anchorTime] of [['case1',1.25,2.25,1.35],['case2',4.35,5.45,4.55]]) {
 const sample=rows.filter(r=>r[5]===1&&(r[1]-1e9)/1e9>=start&&(r[1]-1e9)/1e9<=end&&r[0]%2===0);
 const anchor=sample.reduce((a,b)=>Math.abs((a[1]-1e9)/1e9-anchorTime)<Math.abs((b[1]-1e9)/1e9-anchorTime)?a:b);
 const offsets=telemetry.filter(r=>r.has_target&&Math.hypot(r.source_aim[0]-anchor[8],r.source_aim[1]-anchor[9])<10).map(r=>r.video_utc_t-(anchor[1]-1e9)/1e9);
 const ranked=[];
 for(const offset of offsets) {
  const errors=sample.map(r=>{const p=nearest((r[1]-1e9)/1e9+offset);return p?.has_target?Math.min(250,Math.hypot(p.source_aim[0]-r[8],p.source_aim[1]-r[9])):250;}).sort((a,b)=>a-b);
  ranked.push({offset,median:errors[Math.floor(errors.length/2)],mean:errors.reduce((a,b)=>a+b,0)/errors.length,within15:errors.filter(x=>x<15).length,total:sample.length});
 }
 ranked.sort((a,b)=>a.mean-b.mean);const distinct=[];for(const item of ranked)if(!distinct.some(p=>Math.abs(p.offset-item.offset)<.5))distinct.push(item);
 result.push({name,anchor:{t:(anchor[1]-1e9)/1e9,x:anchor[8],y:anchor[9]},candidates:offsets.length,best:distinct.slice(0,5)});
}
fs.writeFileSync(root+'/alignment-candidates.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
