// Offline video-driven controller probe. No devices or runtime writes.
#include "../vision_controller_study_20261020/record.h"
#include "controller_native/incident_fixture_support.h"
#include "runtime_app/vision_controller_adapter.h"
#include <deque>
#include <iostream>
using namespace controller_native;
void translate(vision_native::VisionResult& r,float x,float y) {
 r.target_x+=x;r.target_y+=y;r.dx+=x;r.dy+=y;
 r.body_x1+=x;r.body_x2+=x;r.body_y1+=y;r.body_y2+=y;
 r.aim_region_x1+=x;r.aim_region_x2+=x;r.aim_region_y1+=y;r.aim_region_y2+=y;
 for(auto& d:r.detections){d.x1+=x;d.x2+=x;d.y1+=y;d.y2+=y;d.cue_x+=x;d.cue_y+=y;d.motion_anchor_x+=x;d.motion_anchor_y+=y;}
#ifdef STUDY_MOTION_CUE
 r.motion_cue_x+=x;r.motion_cue_y+=y;
#endif
}
int main(int argc,char** argv) {try {
 if(argc!=4&&argc!=5)throw std::runtime_error("profile records output.csv [camera-flow.txt]");
 auto base=load_runtime_config(argv[1]).gamepad;
 auto rows=study::read(argv[2]);const double origin=rows.front().captured_at_ns*1e-9;
 const bool closed=argc==5;std::vector<pipeline_contract::Vec2f> flow(rows.size());
 if(closed){std::ifstream in(argv[4]);int id,n;float x,y;while(in>>id>>x>>y>>n)if(id>0&&id<=int(flow.size()))flow[id-1]={x,y};}
 std::ofstream out(argv[3]);
 out<<"case,scene,latency,late_ads,fire,feedback,t,frame,source,generation,target,mode,aim_x,aim_y,error_y,position_y,motion_y,motion_rate_y,pre_y,recoil_y,out_y,dy,lt,rb,requested_y,probe_y,no_sustain_y\n";
 int id=0;
 for(int scene=0;scene<2;++scene)for(int latency:{6,28})for(int late:{0,1})for(int fire:{0,1})for(int feedback:{0,1}) {
  auto cfg=base;cfg.auto_fire.enabled=false;
  AdsAcquisitionControllerConfig ac{};
  ac.fallback_response_px_per_stick_second=cfg.ai_aim.adapter_response_px_per_second;
  ac.arrival_speed=cfg.ai_aim.adapter_ads_speed;ac.authority_budget_scale=cfg.ai_aim.adapter_force_budget_scale;
  ac.max_force_x=cfg.ai_aim.ads_output_limit_x;ac.max_force_y=cfg.ai_aim.ads_output_limit_y;
  ac.range_position_response=true;ac.minimum_position_stick=cfg.ai_aim.minimum_position_stick;
  ac.force_headroom=1;ac.arrival_horizon_seconds=cfg.ai_aim.ads_response_time_ms/1000.f;ac.response_curve=cfg.aim_response_curve;
  AdsAcquisitionController probe(ac);
  cfg.tracker.max_observation_age_ms=40;
  double now=10;NativeGamepadController controller(cfg,&now);
  const double begin=closed?(scene?4.5:1.45):(scene?2.658334:0),end=closed?(scene?5.54:2.65):(scene?5.57:2.658334);
  const double ads=late?(closed?.02:(scene?1.65:1.25)):0;
  size_t index=0;while(index<rows.size()&&rows[index].captured_at_ns*1e-9-origin<begin-1e-7)++index;
  std::deque<std::pair<double,ControllerVisionSnapshot>> pending;
  const auto original_camera=flow[index];pipeline_contract::Vec2f camera{};
  std::deque<pipeline_contract::Vec2f> plant(9);
  float prev=0;
  for(int tick=0;tick<(end-begin)*1000+35;++tick) {
   const double local=tick*.001,t=begin+local;now=10+local;
   while(index<rows.size()) {
    auto r=rows[index];const double rt=r.captured_at_ns*1e-9-origin;
    if(rt>t+1e-8||rt>=end)break;
    if(closed)translate(r,original_camera.x-flow[index].x-camera.x,original_camera.y-flow[index].y-camera.y);
    ++index;r.captured_at_ns=std::uint64_t(std::llround((10+rt-begin)*1e9));r.result_at_ns=r.captured_at_ns+latency*1000000ull;
    pending.emplace_back(rt-begin+latency*.001,runtime_app::adapt_vision_result(r));
   }
   while(!pending.empty()&&pending.front().first<=local+1e-9){controller.submit_vision_snapshot(pending.front().second);pending.pop_front();}
   auto input=incident_fixture::ads_input();input.left_trigger=local>=ads?1:0;
   input.rb=fire&&local>=(closed?.05:(scene?1.82:1.5));
   auto result=controller.build_output(input);const auto&p=controller.last_target_plan();const auto&d=controller.last_output_components();
   if(!std::isfinite(result.right_y)||std::fabs(result.right_y)>1)throw std::runtime_error("invalid output");
   const auto ri=p.source_frame_id&&p.source_frame_id<=rows.size()?p.source_frame_id-1:rows.size();
   auto no_sustain=p;no_sustain.bodylock_target_motion_valid=false;no_sustain.bodylock_target_motion_px_per_sec={};
   out<<id<<','<<scene<<','<<latency<<','<<late<<','<<fire<<','<<feedback<<','<<t<<','<<p.source_frame_id<<','<<(ri<rows.size()?study::source_id(rows[ri].target_source):0)<<','<<p.selector_target_generation<<','<<p.target_id<<','<<int(p.mode)<<','<<p.source_aim_px.x<<','<<p.source_aim_px.y<<','<<p.error_px.y<<','<<d.bodylock_position_stick.y<<','<<d.bodylock_motion_stick.y<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<d.before_recoil_stick.y<<','<<d.recoil_stick.y<<','<<result.right_y<<','<<result.right_y-prev<<','<<input.left_trigger<<','<<input.rb<<','<<d.requested_assist_stick.y<<','<<probe.compute(p).y<<','<<probe.compute(no_sustain).y<<'\n';
   // Sensitivity only: video pixels do not respond to these hypothetical commands.
   if(feedback)controller.observe_delivered_output(result,true,now);
   if(closed){
    const auto v=plant.front();plant.pop_front();
    const auto total=forward_aim_response_curve({result.right_x,result.right_y},cfg.aim_response_curve);
    const auto kick=forward_aim_response_curve({d.recoil_stick.x,d.recoil_stick.y},cfg.aim_response_curve);
    plant.push_back({total.x-kick.x,total.y-kick.y});
    camera.x+=1400*v.x*.001f;camera.y-=1400*v.y*.001f;
   }
   prev=result.right_y;
  }
  ++id;
 }
 std::cout<<"cases="<<id<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
