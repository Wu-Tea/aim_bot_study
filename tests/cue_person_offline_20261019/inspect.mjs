import fs from 'node:fs';
const out='runs/cue-person-offline-20261019/';
const read=id=>fs.readFileSync(out+id+'.jsonl','utf8').trim().split('\n').map(JSON.parse);
for(const [id,f] of [['v1',150],['v6',150]]){
 const r=read(id)[f],edges=[];
 r.detections.forEach((p,d)=>r.cues.forEach((q,c)=>{
   const [l,t,rr,b]=p.box,w=rr-l,h=b-t,[x,y]=q.xy;
   if(p.class!==0||p.friendly||w<8||h<12||x<l-.2*w||x>rr+.2*w||y<t-Math.max(36,.6*h)||y>t+.4*h)return;
   const cost=Math.abs(x-(l+rr)/2)/Math.max(16,w*.65)*.55+Math.abs(y-(t-.08*h))/Math.max(24,h*.45)*.35+(1-q.score)*.1;
   if(cost<=.95)edges.push({d,c,xy:q.xy,cost});
 }));
 console.log(JSON.stringify({id,frame:f,edges}));
}
const supplementary=[];
for(let n=1;n<=9;++n){
 const id='v'+n,rs=read(id);
 const r=rs.find(r=>r.pairs.length&&!r.detections.some(d=>d.has_cue&&!d.friendly));
 if(r)supplementary.push({id:`${id}-f${r.frame}`,clip:id,frame:r.frame,reason:'First frame in clip with candidate pair and no baseline per-person cue; diagnostic selection, excluded from fixed-sample accuracy.'});
}
fs.writeFileSync(out+'supplementary.json',JSON.stringify(supplementary,null,2));
console.log(JSON.stringify(supplementary));
