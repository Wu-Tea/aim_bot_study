// Offline identity policy only. The tracker never reads a person detection.
// A cue is a fresh observation, not a persistent semantic identity by itself.
const hypot=(a,b)=>Math.hypot(a[0]-b[0],a[1]-b[1]);
const diameter=c=>Math.sqrt((c.box[2]-c.box[0])*(c.box[3]-c.box[1]));
export class CueLock {
  constructor(){this.active=null;this.serial=0;}
  seed(cue,t){this.active={id:++this.serial,xy:[...cue.xy],v:[0,0],size:diameter(cue),color:cue.color,last:t};}
  step(cues,t,{exit=false,acquire=true}={}){
    if(exit){this.active=null;return {id:0,fresh:false,reason:'explicit_exit',cue:-1};}
    let expired=false;
    if(this.active&&t-this.active.last>0.05000001){this.active=null;expired=true;}
    if(!this.active){
      if(!acquire)return {id:0,fresh:false,reason:expired?'expired':'empty',cue:-1};
      let index=-1,best=190;
      cues.forEach((c,i)=>{const d=hypot(c.xy,[320,256]);if(d<best){best=d;index=i;}});
      if(index<0)return {id:0,fresh:false,reason:expired?'expired':'empty',cue:-1};
      this.seed(cues[index],t);
      return {id:this.active.id,fresh:true,reason:expired?'reacquired_after_expiry':'acquired',cue:index,xy:[...this.active.xy]};
    }
    const a=this.active,dt=Math.max(0,t-a.last);
    let displacement=a.v.map(v=>v*dt),length=Math.hypot(...displacement);
    if(length>36)displacement=displacement.map(v=>v*36/length);
    const expected=a.xy.map((v,i)=>v+displacement[i]);
    const candidates=[];
    cues.forEach((c,i)=>{
      if(c.color!==a.color||hypot(c.xy,a.xy)>42)return;
      const size=diameter(c),ratio=size/a.size,residual=hypot(c.xy,expected);
      if(ratio<.5||ratio>2||residual>36)return;
      candidates.push({i,cost:residual+8*Math.abs(Math.log(ratio))});
    });
    candidates.sort((x,y)=>x.cost-y.cost);
    if(!candidates.length)return {id:a.id,fresh:false,reason:'no_match',cue:-1};
    if(candidates.length>1&&candidates[1].cost-candidates[0].cost<4)
      return {id:a.id,fresh:false,reason:'ambiguous',cue:-1};
    const i=candidates[0].i,c=cues[i];
    if(dt>0){const measured=c.xy.map((x,j)=>(x-a.xy[j])/dt);a.v=a.v.map((v,j)=>.5*v+.5*measured[j]);}
    a.xy=[...c.xy];a.size=.8*a.size+.2*diameter(c);a.last=t;
    return {id:a.id,fresh:true,reason:'held',cue:i,xy:[...a.xy]};
  }
}

// Body geometry can attach to the owned cue, but cannot choose or revoke it.
// This experiment reports a correspondence only: no chest prediction/output.
export function attachPerson(lock,cues,persons){
  if(!lock.fresh||lock.cue<0)return {person:-1,reason:'no_fresh_cue'};
  const c=cues[lock.cue],edges=[];
  persons.forEach((p,d)=>{
    const [l,t,r,b]=p.box,w=r-l,h=b-t,[x,y]=c.xy;
    if(p.class!==0||p.friendly||w<8||h<12||x<l-.2*w||x>r+.2*w||y<t-Math.max(36,.6*h)||y>t+.4*h)return;
    const cost=.55*Math.abs(x-(l+r)/2)/Math.max(16,.65*w)+.35*Math.abs(y-(t-.08*h))/Math.max(24,.45*h);
    if(cost<=.95)edges.push({d,cost});
  });
  edges.sort((a,b)=>a.cost-b.cost);
  if(!edges.length)return {person:-1,reason:'no_person'};
  const iou=(a,b)=>{const area=(r)=>Math.max(0,r[2]-r[0])*Math.max(0,r[3]-r[1]);const inter=area([Math.max(a[0],b[0]),Math.max(a[1],b[1]),Math.min(a[2],b[2]),Math.min(a[3],b[3])]);return inter/(area(a)+area(b)-inter)||0;};
  // A best person may supply geometry; competing overlapping boxes are recorded
  // as duplicate-like evidence. This does not merge or relabel target identities.
  const first=edges[0];
  const competing=edges.slice(1).filter(e=>e.cost-first.cost<.12&&iou(persons[e.d].box,persons[first.d].box)<.5);
  if(competing.length)return {person:-1,reason:'ambiguous_person'};
  return {person:first.d,reason:edges.length>1?'attached_with_alternatives':'attached'};
}
