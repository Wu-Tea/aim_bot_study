// Offline diagnostic facade: no physical output, runtime/config mutation or device creation.
#include "record.h"
#include "controller_native/incident_fixture_support.h"
#include "controller_native/aim_response_curve_plugin.h"
#include "runtime_app/vision_controller_adapter.h"
#include <deque>
#include <random>
#include <iostream>
#include <algorithm>
using namespace controller_native;
using namespace pipeline_contract;
namespace f=controller_native::incident_fixture;
struct Window {std::string id,path;double start;};
struct Inputs {double t;float x,y,lt;bool fire;};
void shift(vision_native::VisionResult& r,float x,float y) {
 r.target_x+=x;r.target_y+=y;r.dx=r.target_x-320;r.dy=r.target_y-256;
 r.body_x1+=x;r.body_x2+=x;r.body_y1+=y;r.body_y2+=y;
 r.aim_region_x1+=x;r.aim_region_x2+=x;r.aim_region_y1+=y;r.aim_region_y2+=y;
 #ifdef STUDY_MOTION_CUE
 r.motion_cue_x+=x;r.motion_cue_y+=y;
 #endif
 for(auto& d:r.detections){d.x1+=x;d.x2+=x;d.y1+=y;d.y2+=y;d.cue_x+=x;d.cue_y+=y;d.motion_anchor_x+=x;d.motion_anchor_y+=y;}
}
struct Stats {long ticks=0,target=0,cue=0,ads=0,body=0,exits=0,handoffs=0,violations=0,sign_flips=0;double sum=0,deltas=0,peak=0,motion_peak=0;std::vector<float> errors;};
int main(int argc,char** argv){try{
 if(argc<6)throw std::runtime_error("profile manifest output-dir seed open|closed [inputs] [trace-case]");
 const bool closed=std::string(argv[5])=="closed";const unsigned seed=std::stoul(argv[4]);
 auto base=load_runtime_config(argv[1]).gamepad;base.auto_fire.enabled=false;base.tracker.max_observation_age_ms=40;
 std::vector<Window> windows;std::ifstream manifest(argv[2]);Window w;while(manifest>>w.id>>w.path>>w.start)windows.push_back(w);
 if(windows.empty())throw std::runtime_error("empty manifest");
 std::vector<Inputs> controls;if(argc>6&&std::string(argv[6])!="-"){std::ifstream f(argv[6]);Inputs a;while(f>>a.t>>a.x>>a.y>>a.lt>>a.fire)controls.push_back(a);if(controls.empty())throw std::runtime_error("empty controls");}
 const int trace_case=argc>7?std::stoi(argv[7]):-1;
 std::filesystem::create_directories(argv[3]);std::ofstream cases(std::string(argv[3])+"/cases.csv"),trace(std::string(argv[3])+"/trace.csv");
 cases<<"seed,case,clip,start,duration,manual,response,delay,latency,ticks,target,cue,ads,body,handoffs,exits,violations,mean_error,p95_error,max_error,mean_delta,peak_delta,peak_motion,sign_flips\n";
 trace<<"case,tick,video_t,frame,source,selector_generation,target_id,mode,reason,gap,source_age,authority,error_x,error_y,source_x,source_y,manual_x,manual_y,out_x,out_y,delta,motion_x,motion_y,motion_valid,position_x,position_y,exit,correction_x,correction_y,aim_region_w,aim_region_h,plant_x,plant_y,physical_response,response_estimate,manual_authority\n";
 int cid=0;long total=0,violations=0;
 for(const auto& window:windows){auto rows=study::read(window.path);const double origin=rows.front().captured_at_ns*1e-9;
  const auto time=[&](std::size_t i){return rows[i].captured_at_ns*1e-9-origin;};
  const auto start=std::lower_bound(rows.begin(),rows.end(),window.start,[&](const auto&r,double t){return r.captured_at_ns*1e-9-origin<t;})-rows.begin();
  if(start>=rows.size())throw std::runtime_error("window outside footage");
  for(int duration:closed?std::vector<int>{2000,20000}:std::vector<int>{int(std::ceil(time(rows.size()-1)*1000))+40})for(int manual=0;manual<(closed?6:3);++manual,++cid){
   // Keep plant parameters identical across input patterns and short/long pairs.
   std::mt19937 rng(seed^unsigned((cid/(closed?12:3))*104729));const int delay=6+rng()%25,latency=closed?4+rng()%17:(manual==2?28:6);
   const float response=closed?900+float(rng()%1601):1400;
   auto cfg=base;cfg.recoil.enabled=closed&&manual==4;
   double now=10;NativeGamepadController controller(cfg,&now);
   std::deque<Vec2f> plant(delay);std::deque<std::pair<int,ControllerVisionSnapshot>> pending;
   std::size_t index=closed?start:0,active=rows.size(),control_index=0;unsigned slot=0;std::uint64_t frame=0;
   float camera_x=0,camera_y=0;Vec2f previous{};ControlMode previous_mode=ControlMode::Manual;Stats st;
   std::uint64_t last_trace_frame=0;int previous_sign=0;bool gap_sent=false;std::vector<std::size_t> source_indices{rows.size()};
   for(int tick=0;tick<duration;++tick){const int phase=closed?tick%1625:tick;now=10+tick*.001;
    if(closed&&phase==0){index=start;active=rows.size();camera_x=camera_y=0;slot=tick/1625;gap_sent=false;}
    const double local=closed?phase*.001+window.start:tick*.001;
    auto input=f::ads_input();
    if(closed){input.left_trigger=phase>=25&&phase<1500?1:0;
     if(manual==1){input.right_x=.2f;input.right_y=.12f;}
     if(manual==2){input.right_x=phase>=300&&phase<800?.3f:phase>=800&&phase<1200?-.3f:0;input.right_y=phase>=300&&phase<1200?.21f:0;}
     if(manual==3&&phase>=700&&phase<1000)input.right_x=-.95f;
     if(manual==4)input.rb=phase>=300&&phase<1400;
     if(manual==5){input.right_x=.3f;input.right_y=-.18f;}
    }else if(manual==1&&window.id=="clean"&&!controls.empty()){
     while(control_index+1<controls.size()&&controls[control_index+1].t<=tick*.001)++control_index;
     if(controls[control_index].t<=tick*.001&&tick*.001-controls[control_index].t<.05){const auto& a=controls[control_index];input.right_x=a.x;input.right_y=a.y;input.left_trigger=a.lt;input.rb=a.fire;}
     else input.left_trigger=0;
    }else if(manual==1){input.right_x=.2f;input.right_y=.12f;}
    while(index<rows.size()&&time(index)<=local+1e-8&&(!closed||phase<1575)){
     auto r=rows[index];active=index++;
     const double capture=closed?(slot*1.625+time(active)-window.start):time(active);
     r.frame_id=++frame;source_indices.push_back(active);r.captured_at_ns=std::uint64_t(std::llround((10+capture)*1e9));r.result_at_ns=r.captured_at_ns+latency*1000000ull;
     if(closed){r.selector_target_generation+=slot*100000;shift(r,-camera_x,-camera_y);}
     pending.emplace_back(int(std::ceil(capture*1000))+latency,runtime_app::adapt_vision_result(r));
    }
    if(closed&&phase>=1575&&!gap_sent){ControllerVisionSnapshot s;s.frame_updated=true;s.frame_id=++frame;source_indices.push_back(rows.size());s.selector_identity_protocol=true;s.capture_time_seconds=now;s.ready_time_seconds=now+latency*.001;s.selector_target_generation=slot*100000+99999;pending.emplace_back(tick+latency,s);gap_sent=true;active=rows.size();}
    while(!pending.empty()&&pending.front().first<=tick){controller.submit_vision_snapshot(pending.front().second);pending.pop_front();}
    const auto out=controller.build_output(input);const auto&p=controller.last_target_plan();const auto& d=controller.last_output_components();
    auto check=[&](bool ok){if(!ok)++st.violations;};
    check(std::isfinite(out.right_x)&&std::isfinite(out.right_y)&&std::fabs(out.right_x)<=1&&std::fabs(out.right_y)<=1);
    if(p.target_id){check(p.source_capture_age_ms<=40.01);check(std::hypot(p.aim_px.x-p.source_aim_px.x,p.aim_px.y-p.source_aim_px.y)<.001);}
    if(p.observation_gap&&p.source_capture_age_ms>32.001)check(p.aim_authority==0&&!p.fire_authority);
    if(p.manual_exit_requested)check(d.manual_passthrough_x&&d.manual_passthrough_y);
    if(!input.left_trigger&&!input.rb)check(out.right_x==input.right_x&&out.right_y==input.right_y);
    if(p.cue_continuation)check(!p.fire_authority);
    check(!(p.ads_acquisition_active&&p.mode==ControlMode::BodyLockFollow));
    ++st.ticks;st.target+=p.target_id!=0;st.cue+=p.cue_continuation;st.ads+=p.mode==ControlMode::AdsAcquire;st.body+=p.mode==ControlMode::BodyLockFollow;st.exits+=p.manual_exit_requested;
    st.handoffs+=previous_mode==ControlMode::AdsAcquire&&p.mode==ControlMode::BodyLockFollow;previous_mode=p.mode;
    const float delta=std::hypot(out.right_x-previous.x,out.right_y-previous.y);st.deltas+=delta;st.peak=std::max(st.peak,double(delta));
    st.motion_peak=std::max(st.motion_peak,double(std::hypot(p.bodylock_target_motion_px_per_sec.x,p.bodylock_target_motion_px_per_sec.y)));
    if(closed&&phase>=100&&phase<1400&&active<rows.size()&&rows[active].has_target){const float err=std::hypot(rows[active].target_x-320-camera_x,rows[active].target_y-256-camera_y);st.errors.push_back(err);st.sum+=err;}
    if(std::fabs(out.right_x)>.05f){int sign=out.right_x>0?1:-1;if(sign!=previous_sign&&previous_sign&&p.target_id)++st.sign_flips;previous_sign=sign;}
    if((trace_case==cid||(!closed&&manual==0))&&(tick%5==0||p.source_frame_id!=last_trace_frame||delta>.1)){
     const auto saved_active=active;active=p.source_frame_id<source_indices.size()?source_indices[p.source_frame_id]:rows.size();
     trace<<cid<<','<<tick<<','<<local<<','<<p.source_frame_id<<','<<(active<rows.size()?study::source_id(rows[active].target_source):0)<<','<<p.selector_target_generation<<','<<p.target_id<<','<<int(p.mode)<<','<<int(p.ads_decision_reason)<<','<<p.observation_gap<<','<<p.source_capture_age_ms<<','<<p.aim_authority<<','<<p.error_px.x<<','<<p.error_px.y<<','<<p.source_aim_px.x<<','<<p.source_aim_px.y<<','<<input.right_x<<','<<input.right_y<<','<<out.right_x<<','<<out.right_y<<','<<delta<<','<<p.bodylock_target_motion_px_per_sec.x<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<p.bodylock_target_motion_valid<<','<<d.bodylock_position_stick.x<<','<<d.bodylock_position_stick.y<<','<<p.manual_exit_requested<<','<<p.manual_correction_x<<','<<p.manual_correction_y<<','<<p.aim_region_px.w<<','<<p.aim_region_px.h<<','<<camera_x<<','<<camera_y<<','<<response<<','<<p.response_scale<<','<<d.manual_authority_mode<<'\n';last_trace_frame=p.source_frame_id;
     active=saved_active;
    }
    controller.observe_delivered_output(out,true,now);previous={out.right_x,out.right_y};
    if(closed){const auto command=plant.front();plant.pop_front();plant.push_back(forward_aim_response_curve(previous,cfg.aim_response_curve));camera_x+=response*command.x*.001f;camera_y-=response*command.y*.001f;if(input.rb)camera_y-=.2f*response*.001f;}
   }
   std::sort(st.errors.begin(),st.errors.end());
   cases<<seed<<','<<cid<<','<<window.id<<','<<window.start<<','<<duration<<','<<manual<<','<<response<<','<<delay<<','<<latency<<','<<st.ticks<<','<<st.target<<','<<st.cue<<','<<st.ads<<','<<st.body<<','<<st.handoffs<<','<<st.exits<<','<<st.violations<<','<<(st.errors.empty()?0:st.sum/st.errors.size())<<','<<(st.errors.empty()?0:st.errors[st.errors.size()*95/100])<<','<<(st.errors.empty()?0:st.errors.back())<<','<<st.deltas/st.ticks<<','<<st.peak<<','<<st.motion_peak<<','<<st.sign_flips<<'\n';
   total+=st.ticks;violations+=st.violations;
  }
 }
 std::cout<<"cases="<<cid<<" ticks="<<total<<" violations="<<violations<<'\n';return violations?2:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
