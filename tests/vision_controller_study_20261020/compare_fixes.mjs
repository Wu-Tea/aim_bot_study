import fs from 'node:fs';
const before='runs/vision-controller-study-20261020', after='runs/vision-controller-fixes-20261020';
const matrix=process.argv[2]??'trajectory',prefix=matrix==='trajectory'?'case':matrix;
const csv=p=>{const [h,...ls]=fs.readFileSync(p,'utf8').trim().split(/\r?\n/);const keys=h.split(',');return ls.map(l=>Object.fromEntries(l.split(',').map((v,i)=>[keys[i],v!==''&&!isNaN(+v)?+v:v])));};
const deltas=[];
for(const seed of [71020503,71020513,71020527,71020543]) {
 const a=csv(`${before}/trajectory-final/${seed}/cases.csv`), b=csv(`${after}/${matrix}/${seed}/cases.csv`);
 if(a.length!==b.length)throw Error('case mismatch');
 for(let i=0;i<a.length;++i) {
  for(const k of ['seed','case','clip','duration','manual','response','delay','latency'])if(a[i][k]!==b[i][k])throw Error('scenario changed: '+k);
  deltas.push({...b[i],before_mean:a[i].mean_error,mean_delta_px:b[i].mean_error-a[i].mean_error,
   before_p95:a[i].p95_error,p95_delta_px:b[i].p95_error-a[i].p95_error,
   before_handoffs:a[i].handoffs,before_target:a[i].target});
 }
}
const mean=(a,k)=>a.reduce((s,r)=>s+r[k],0)/a.length;
const groups=[...new Set(deltas.map(r=>r.manual))].map(manual=>{const rows=deltas.filter(r=>r.manual===manual);return {manual,cases:rows.length,
 before_mean:mean(rows,'before_mean'),after_mean:mean(rows,'mean_error'),before_p95:mean(rows,'before_p95'),after_p95:mean(rows,'p95_error'),
 improved:rows.filter(r=>r.mean_delta_px<-.1).length,worsened:rows.filter(r=>r.mean_delta_px>.1).length};});
const summary={cases:deltas.length,byManual:groups,
 worstMeanRegressions:[...deltas].sort((a,b)=>b.mean_delta_px-a.mean_delta_px).slice(0,12),
 largestMeanImprovements:[...deltas].sort((a,b)=>a.mean_delta_px-b.mean_delta_px).slice(0,12)};
const manifest=JSON.parse(fs.readFileSync('tests/vision_controller_study_20261020/trajectories/manifest.json','utf8'));
summary.byKind=[...new Set(manifest.trajectories.map(t=>t.kind))].map(kind=>{
 const ids=manifest.trajectories.filter(t=>t.kind===kind).map(t=>t.id),rows=deltas.filter(r=>ids.includes(r.clip)&&r.manual===0);
 return {kind,cases:rows.length,beforeMean:mean(rows,'before_mean'),afterMean:mean(rows,'mean_error'),
   beforeP95:mean(rows,'before_p95'),afterP95:mean(rows,'p95_error')};
});
fs.writeFileSync(`${after}/${prefix}-comparison.json`,JSON.stringify(summary,null,2));
fs.writeFileSync(`${after}/${prefix}-deltas.json`,JSON.stringify(deltas,null,2));
console.log(JSON.stringify({cases:summary.cases,byManual:groups,worst:summary.worstMeanRegressions.slice(0,3)},null,2));
if(fs.existsSync(after+'/vision/sources.json')) {
 const sources=JSON.parse(fs.readFileSync(after+'/vision/sources.json','utf8')),vision=[];
 for(const clip of sources) {
  const read=p=>fs.readFileSync(p,'utf8').trim().split(/\r?\n/).map(l=>l.split(' ').map(Number));
  const a=read(`${before}/${clip.id}.records`),b=read(`${after}/vision/${clip.id}.records`);
  if(a.length!==b.length)throw Error('frame mismatch');
  let lost=0,gained=0,sameBody=0,maxSameBodyPointDelta=0,sameGenerationJumpsBefore=0,sameGenerationJumpsAfter=0;
  for(let i=0;i<a.length;++i) {
   if(a[i][4]&&!b[i][4])++lost;if(!a[i][4]&&b[i][4])++gained;
   if(a[i][5]===1&&b[i][5]===1&&a[i].slice(11,15).every((v,j)=>v===b[i][11+j])) {
    ++sameBody;maxSameBodyPointDelta=Math.max(maxSameBodyPointDelta,Math.hypot(a[i][8]-b[i][8],a[i][9]-b[i][9]));
   }
   if(i) for(const [rows,isAfter] of [[a,false],[b,true]]) {
    const p=rows[i-1],r=rows[i];
    if(p[4]&&r[4]&&p[2]===r[2]&&Math.hypot(p[8]-r[8],p[9]-r[9])>100) {
     if(isAfter)++sameGenerationJumpsAfter;else ++sameGenerationJumpsBefore;
    }
   }
  }
  const incident=clip.id==='dev'?b.filter(r=>r[0]>=22809&&r[0]<=22813).map(r=>({frame:r[0],generation:r[2],target:r[4],source:r[5],x:r[8],y:r[9]})):undefined;
  const coverage=(rows,start,end)=>rows.filter(r=>(r[1]-rows[0][1])/1e9>=start&&(r[1]-rows[0][1])/1e9<end&&r[4]).length;
  vision.push({clip:clip.id,frames:b.length,beforeTargets:a.filter(r=>r[4]).length,afterTargets:b.filter(r=>r[4]).length,
   beforeCue:a.filter(r=>r[5]===2).length,afterCue:b.filter(r=>r[5]===2).length,lost,gained,sameBody,maxSameBodyPointDelta,
   sameGenerationJumpsBefore,sameGenerationJumpsAfter,incident,
   reportedWindow:clip.id==='clean'?{before:coverage(a,122.2,122.6),after:coverage(b,122.2,122.6)}:undefined});
 }
 fs.writeFileSync(after+'/vision-comparison.json',JSON.stringify(vision,null,2));
 console.log(JSON.stringify(vision,null,2));
}
