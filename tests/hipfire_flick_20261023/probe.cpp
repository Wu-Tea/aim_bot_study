// Offline only. Recorded input/geometry replay + explicit camera-translation
// counterfactual, not reconstructed game pixels or hidden controller state.
#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_dynamics_shaper.h"
#include <chrono>
#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
using namespace controller_native;
using V=pipeline_contract::Vec2f;
struct Row {
 double t;float mx,my,lt,fire,id,valid,ex,ey,w,h,cue,rx,ry,ax,ay,ox,oy,confidence;std::string mode;float continuation;
};
std::vector<Row> read(const std::filesystem::path& p) {
 std::ifstream f(p);std::string line;std::getline(f,line);std::vector<Row> rows;
 while(std::getline(f,line)) {
  std::replace(line.begin(),line.end(),',',' ');std::istringstream in(line);Row r{};
  if(!(in>>r.t>>r.mx>>r.my>>r.lt>>r.fire>>r.id>>r.valid>>r.ex>>r.ey>>r.w>>r.h>>r.cue>>r.rx>>r.ry>>r.ax>>r.ay>>r.ox>>r.oy>>r.confidence>>r.mode>>r.continuation))throw std::runtime_error("fixture row");
  rows.push_back(r);
 }
 return rows;
}
float blend(float a,float b,float t){return a+(b-a)*t;}
float activity(float m){float v=std::clamp((std::fabs(m)-.15f)/.15f,0.f,1.f);return v*v*(3-2*v);}
void component(const std::vector<Row>& rows,const std::string& name,std::ofstream& trace) {
 AimDynamicsShaper shaper;pipeline_contract::TargetPlan plan{};
 plan.target_id=1;plan.lifecycle=pipeline_contract::TargetLifecycle::Observed;
 plan.mode=pipeline_contract::ControlMode::BodyLockFollow;plan.aim_authority=plan.reliability=1;
 // Numerical ownership isolate: use the recorded AI proposal and exact
 // carried-acquisition algebra, with 1 ms interpolated physical input.
 size_t index=0;bool initialized=false;
 for(int tick=0;tick<=int(rows.back().t*1000);++tick) {
  double t=tick*.001;while(index+1<rows.size()&&rows[index+1].t<=t)++index;
  const auto&r=rows[index];const auto&next=rows[std::min(index+1,rows.size()-1)];
  float u=next.t>r.t?float((t-r.t)/(next.t-r.t)):0;
  V manual{blend(r.mx,next.mx,u),blend(r.my,next.my,u)};
  V ai{r.ax,r.ay};V desired{ai.x+(manual.x-ai.x)*activity(manual.x),ai.y+(manual.y-ai.y)*activity(manual.y)};
  V out=desired;
#ifdef STUDY_AFTER
  out=shaper.shape_ownership(desired,manual,ai,.001f,r.valid!=0,!initialized);
#endif
  initialized=true;
  trace<<name<<','<<t<<','<<manual.x<<','<<manual.y<<','<<ai.x<<','<<ai.y<<','<<out.x<<','<<out.y<<','<<r.ox<<','<<r.oy<<'\n';
 }
}
int main(int argc,char**argv){try {
 if(argc!=4)throw std::runtime_error("profile fixtures output-dir");
 auto cfg=load_runtime_config(argv[1]).gamepad;cfg.auto_fire.enabled=false;
 std::filesystem::create_directories(argv[3]);const std::string dest=argv[3];
 std::ofstream summary(dest+"/summary.csv"),trace(dest+"/trace.csv"),handoff(dest+"/ownership.csv"),scope(dest+"/scope.csv");
 summary<<"case,plant,latency,ticks,mean_error,late_error,peak_step,owned,acquired_tick,source_violations,manual_violations,controller_us,controller_p95_us,controller_p99_us\n";
 trace<<"case,plant,t,ex,ey,mx,my,lt,ox,oy,id,mode,aimx,aimy,requestedx,aix\n";
 handoff<<"case,t,mx,my,aix,aiy,ox,oy,recorded_ox,recorded_oy\n";
 scope<<"tick,ads,output\n";
 {
  AimDynamicsShaper s;pipeline_contract::TargetPlan p{};p.target_id=1;p.lifecycle=pipeline_contract::TargetLifecycle::Observed;p.mode=pipeline_contract::ControlMode::BodyLockFollow;p.aim_authority=p.reliability=1;
  for(int tick=0;tick<80;++tick){float ratio=tick<40?.7f:1;V v;
#ifdef STUDY_AFTER
   v=s.shape({-.78f*ratio,0},p,.001f);
#else
   v=s.shape({-.78f,0},p,.001f);v.x*=ratio;
#endif
   scope<<tick<<','<<(tick>=40)<<','<<v.x<<'\n';
  }
 }
 for(const auto& entry:std::filesystem::directory_iterator(argv[2])) {
  if(entry.path().extension()!=".csv")continue;
  const auto rows=read(entry.path());if(rows.size()<2)throw std::runtime_error("empty fixture");
  const auto name=entry.path().stem().string();component(rows,name,handoff);
  // plant=0 fixed-observation sensitivity; 1..3 translated closed loops.
  for(int plant=0;plant<4;++plant) {
   const int delay=plant==1?6:plant==2?12:24;const float response=plant==1?900:plant==2?1400:2200;
   auto config=cfg;config.tracker.max_observation_age_ms=40;
   double now=10;NativeGamepadController controller(config,&now);
   V reference_camera{},camera{},previous{};
   std::deque<V> reference_delay(delay),camera_delay(delay);
   size_t index=0;int last_index=-1,acquired=-1,owned=0,source_bad=0,manual_bad=0;
   double error_sum=0,late_sum=0,time_us=0;int late_count=0;float peak=0;
   const int ticks=int(rows.back().t*1000)+1;
   std::vector<double> timings;timings.reserve(ticks);
   for(int tick=0;tick<ticks;++tick){
    double t=tick*.001;now=10+t;while(index+1<rows.size()&&rows[index+1].t<=t)++index;
    const auto&r=rows[index];const auto&next=rows[std::min(index+1,rows.size()-1)];
    float u=next.t>r.t?float((t-r.t)/(next.t-r.t)):0;
    auto physical=incident_fixture::ads_input(blend(r.mx,next.mx,u),blend(r.my,next.my,u));
    physical.left_trigger=r.lt;physical.rb=r.fire!=0;
    const float dx=r.ex+(plant?reference_camera.x-camera.x:0),dy=r.ey+(plant?reference_camera.y-camera.y:0);
    if(int(index)!=last_index){
     last_index=int(index);ControllerVisionSnapshot snapshot;
     if(r.valid&&r.id){
      incident_fixture::TargetSpec spec;spec.observation_id=index+1;spec.selector_generation=std::uint64_t(r.id);
      spec.has_enemy_cue=spec.enemy_identity_confirmed=r.cue!=0;spec.body_width=r.w;spec.body_height=r.h;spec.confidence=std::max(.1f,r.confidence);
      snapshot=r.continuation?incident_fixture::cue_snapshot(spec,index+1,10+r.t,dx,dy):incident_fixture::observed_snapshot(spec,index+1,10+r.t,dx,dy);
      if(r.cue){snapshot.motion_cue_id=std::uint64_t(r.id);snapshot.motion_cue_px={320+dx,256+dy-r.h*.35f};}
     } else {snapshot.frame_updated=true;snapshot.frame_id=index+1;snapshot.capture_time_seconds=10+r.t;snapshot.selector_identity_protocol=true;}
     snapshot.ready_time_seconds=now;controller.submit_vision_snapshot(snapshot);
    }
    const auto begin=std::chrono::steady_clock::now();auto out=controller.build_output(physical);
    const double cost=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
    time_us+=cost;timings.push_back(cost);
    const auto&p=controller.last_target_plan();const auto&d=controller.last_output_components();
    if(!std::isfinite(out.right_x)||!std::isfinite(out.right_y)||std::hypot(out.right_x,out.right_y)>1.415f)throw std::runtime_error("invalid output");
    if(p.target_id){if(acquired<0)acquired=tick;++owned;source_bad+=std::hypot(p.aim_px.x-p.source_aim_px.x,p.aim_px.y-p.source_aim_px.y)>.001f;}
    if((!physical.left_trigger&&!physical.rb)||p.manual_exit_requested)
     manual_bad+=out.right_x!=physical.right_x||std::fabs(d.before_recoil_stick.y-physical.right_y)>1e-6;
    if(r.valid){error_sum+=std::hypot(dx,dy);if(tick>ticks/2){late_sum+=std::hypot(dx,dy);++late_count;}}
    peak=std::max(peak,std::hypot(out.right_x-previous.x,out.right_y-previous.y));
    trace<<name<<','<<plant<<','<<t<<','<<dx<<','<<dy<<','<<physical.right_x<<','<<physical.right_y<<','<<physical.left_trigger<<','<<out.right_x<<','<<out.right_y<<','<<p.target_id<<','<<int(p.mode)<<','<<p.aim_px.x<<','<<p.aim_px.y<<','<<d.requested_assist_stick.x<<','<<d.shaped_assist_stick.x<<'\n';
    controller.observe_delivered_output(out,true,now);previous={out.right_x,out.right_y};
    auto v=camera_delay.front();camera_delay.pop_front();camera_delay.push_back(forward_aim_response_curve(previous,config.aim_response_curve));
    auto ref=reference_delay.front();reference_delay.pop_front();reference_delay.push_back(forward_aim_response_curve({r.ox,r.oy},config.aim_response_curve));
    camera.x+=response*v.x*.001f;camera.y-=response*v.y*.001f;
    reference_camera.x+=response*ref.x*.001f;reference_camera.y-=response*ref.y*.001f;
   }
   std::sort(timings.begin(),timings.end());
   summary<<name<<','<<plant<<','<<delay<<','<<ticks<<','<<error_sum/ticks<<','<<(late_count?late_sum/late_count:0)<<','<<peak<<','<<owned<<','<<acquired<<','<<source_bad<<','<<manual_bad<<','<<time_us/ticks<<','<<timings[timings.size()*95/100]<<','<<timings[timings.size()*99/100]<<'\n';
   if(source_bad||manual_bad)throw std::runtime_error("source or manual invariant");
  }
 }
 std::cout<<"8 log windows; ownership isolate, scope isolate, 32 native controller trajectory scenarios complete\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
