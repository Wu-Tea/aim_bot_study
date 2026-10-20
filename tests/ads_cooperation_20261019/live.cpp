#include "controller_native/ads_acquisition_controller.h"
#include "controller_native/assist_control_state_machine.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
using namespace controller_native;
using namespace pipeline_contract;
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("live.csv output.csv");
 std::ifstream f(argv[1]);std::ofstream out(argv[2]);std::string line;std::getline(f,line);
 out<<"session_s,line,manual_x,logged_x,replayed_x,error_x,age_ms\n";
 AdsAcquisitionControllerConfig cfg;cfg.range_position_response=true;
 cfg.response_curve.algorithm=AimResponseCurveAlgorithm::CodDynamicLegacyLut;
 AdsAcquisitionController solver(cfg);
 AssistControlStateMachineConfig ac;ac.use_gamepad_intent_for_arbitration=true;
 unsigned count=0,changed=0;
 while(std::getline(f,line)){
  std::istringstream s(line);std::string col;std::vector<double> v;while(std::getline(s,col,','))v.push_back(std::stod(col));
  TargetPlan p;p.mode=ControlMode::AdsAcquire;p.lifecycle=TargetLifecycle::Observed;p.aim_authority=1;
  p.source_capture_age_ms=float(v[2]);p.error_px={float(v[3]),float(v[4])};p.response_scale=float(v[7]);p.position_arrival_radius_px=2;
#ifdef AFTER
  p.position_stopping_reserve_px={float(v[5]),float(v[6])};
#endif
  AssistControlStateMachine a(ac);AssistControlStateMachineInput in;
  in.activation=AssistActivation::Engaged;in.target_authoritative=in.fresh_observation=true;in.target_id=1;
  in.mode=p.mode;in.ads_acquisition_state=AdsAcquisitionState::AcquiringNominal;in.target_error_px=p.error_px;in.visual_authority=1;
  in.manual_stick=in.filtered_manual_stick={float(v[8]),float(v[9])};in.ai_stick={float(v[10]),float(v[11])};in.firing=v[14]!=0;
  auto weight=[](float value){float t=std::clamp((std::fabs(value)-.15f)/.15f,0.f,1.f);return t*t*(3-2*t);};
  in.manual_axis_activity={weight(in.manual_stick.x),weight(in.manual_stick.y)};
#ifdef AFTER
  in.ads_manual_work=solver.cooperative_manual_work(p,in.manual_stick,in.ai_stick);
#endif
  auto r=a.update(in).stick;
#ifndef AFTER
  if(std::fabs(r.x-v[12])>2e-5)throw std::runtime_error("old X arbitration does not match logged output");
#else
  if(std::fabs(r.x-v[12])>1e-5)++changed;
  if(std::fabs(r.x)>std::max(std::fabs(float(v[12])),std::fabs(in.manual_stick.x))+1e-5)throw std::runtime_error("created additional manual authority");
#endif
  out<<v[0]<<','<<v[1]<<','<<v[8]<<','<<v[12]<<','<<r.x<<','<<v[3]<<','<<v[2]<<'\n';++count;
 }
 if(!count)throw std::runtime_error("no live samples");
 std::cout<<count<<" frozen samples, "<<changed<<" X cooperation changes\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
