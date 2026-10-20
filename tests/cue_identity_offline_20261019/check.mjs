import assert from 'node:assert/strict';
import fs from 'node:fs';
import {CueLock,attachPerson} from './cue_lock.mjs';
const cue=(x,y,id,color=1,size=10)=>({xy:[x,y],box:[x-size/2,y-size/2,x+size/2,y+size/2],score:.9,color,truth:id});
const rng=s=>()=>{s=(s+0x6D2B79F5)>>>0;let t=s;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return ((t^t>>>14)>>>0)/4294967296;};
let worlds=0,ticks=0,closerCompetitorFrames=0,missingPersonFrames=0;
for(const seed of [2026101902,32452937,49979807,104395489])for(let scene=0;scene<32;++scene)for(const seconds of [2,20]){
 const rand=rng(seed+scene*7919+seconds),phase=rand()*6.28,amp=35+rand()*55,speed=.3+rand(),tracker=new CueLock();
 const a0=cue(320+amp*Math.sin(phase),256+20*Math.cos(phase),'A');tracker.seed(a0,0);
 for(let frame=1;frame<=seconds*60;++frame){
  const t=frame/60,a=cue(320+amp*Math.sin(phase+t*speed),256+20*Math.cos(phase+t*speed),'A');
  const b=cue(a.xy[0]+(a.xy[0]>320?-60:60),a.xy[1]+20,'B');
  const distractor=cue(40+rand()*30,40+rand()*30,'texture',2,6);
  const observations=rand()<.5?[a,b,distractor]:[b,distractor,a];
  // Deliberately make competing cue confidence higher. Held identity ignores it.
  b.score=1;a.score=.7+rand()*.2;
  if(Math.hypot(b.xy[0]-320,b.xy[1]-256)<Math.hypot(a.xy[0]-320,a.xy[1]-256))++closerCompetitorFrames;
  const lock=tracker.step(observations,t);assert.equal(lock.fresh,true);assert.equal(lock.id,1);assert.equal(observations[lock.cue].truth,'A');
  const before=JSON.stringify(tracker.active);
  let persons=[];
  if(rand()>.7)persons=[{class:0,friendly:false,conf:rand(),box:[a.xy[0]-20,a.xy[1]+10,a.xy[0]+20,a.xy[1]+100]},
   {class:0,friendly:false,conf:1,box:[b.xy[0]-20,b.xy[1]+10,b.xy[0]+20,b.xy[1]+100]}];else ++missingPersonFrames;
  attachPerson(lock,observations,persons);assert.equal(JSON.stringify(tracker.active),before);++ticks;
 }
 ++worlds;
}
// Discrete lifecycle checks: missing observations never count as fresh or pin old geometry.
const tr=new CueLock(),a=cue(320,240,'A');tr.seed(a,0);
assert.deepEqual(tr.step([],1/60),{id:1,fresh:false,reason:'no_match',cue:-1});
assert.equal(tr.step([a],2/60).id,1);
assert.equal(tr.step([],5/60,{acquire:false}).fresh,false);
assert.equal(tr.step([],6/60,{acquire:false}).id,0);
tr.seed(a,1);assert.equal(tr.step([a],1.01,{exit:true}).id,0);
const ambiguous=new CueLock();ambiguous.seed(a,0);
const result=ambiguous.step([cue(318,240,'A'),cue(322,240,'B')],1/60);
assert.equal(result.fresh,false);assert.equal(result.id,1);assert.equal(result.reason,'ambiguous');
// User-confirmed COD nemesis glyphs are valid observations, not death evidence.
const nemesisExamples=[];
for(const [clip,frame,xy] of [['v2',631,[316,233]],['v3',150,[296,210]]]){
 const r=fs.readFileSync(`runs/cue-person-offline-20261019/${clip}.jsonl`,'utf8').trim().split('\n').map(JSON.parse)[frame];
 const i=r.cues.findIndex(c=>Math.hypot(c.xy[0]-xy[0],c.xy[1]-xy[1])<=6);assert.ok(i>=0,'nemesis marker must survive common cue proposal rules');
 const tracker=new CueLock();tracker.seed(r.cues[i],r.t);const lock=tracker.step(r.cues,r.t);
 assert.equal(lock.fresh,true);assert.equal(lock.cue,i);assert.equal(lock.id,1);
 nemesisExamples.push({clip,frame,detected_and_retained:true});
}
const output={worlds,ticks,closerCompetitorFrames,missingPersonFrames,wrong_cue_switches:0,
 nemesisExamples,
 limits:'Idealized marker positions with >60px separation, smooth motion, fixed marker scale and known initial target. Does not prove real-video identity or detection quality; no physically ambiguous same-looking crossings are scored as successful.'};
fs.writeFileSync('runs/cue-identity-offline-20261019/checks.json',JSON.stringify(output,null,2));console.log(JSON.stringify(output));
