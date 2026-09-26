// Standalone diagnostic; build against the frozen production controller core.
#include "controller_native/incident_fixture_support.h"
#include "controller_native/ds4_output_report.h"
#include "controller_native/aim_response_curve_plugin.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace controller_native;
using pipeline_contract::Vec2f;
struct Spec { int id, vision, segment; float angle, speed; bool ads, fire, control; unsigned seed; };
struct Result {
    int first_source=-1, first_output=-1, body_entry=-1, first_close=-1;
    int body=0, observed=0, fresh=0, target=0, moved=0, outside=0, zeros=0, crossings=0;
    int identity_break_ms=-1; bool finite=true; int warm_body=0;
    float peak=0, track_peak=0, stop_peak=0, capacity=0, passthrough=0, max_manual=0;
    bool trigger=false, o1=true, o2=true, o3=true, o4=true;
};
float norm(Vec2f v) { return std::hypot(v.x,v.y); }
Result run(const Spec& s, GamepadRuntimeConfig config, std::ostream* trace) {
    config.recoil.enabled=false; config.recoil.profile_playback_enabled=false;
    config.recoil.selection_log_enabled=false;
    double now=1; NativeGamepadController controller(config,&now);
    incident_fixture::TargetSpec target; target.observation_id=target.selector_generation=1;
    target.aim_height_ratio=config.tracker.aim_height_ratio;
    const Vec2f dir{std::cos(s.angle),std::sin(s.angle)};
    const int onset=s.ads?0:500, stop=onset+2*s.segment, duration=stop+600;
    Vec2f error{s.ads?dir.x*60:0,s.ads?dir.y*60:0};
    std::deque<Vec2f> pending(9,Vec2f{});
    Result r; int warm_body=0, last_sign=0; std::uint64_t frame=0, persistent_id=0;
    bool finite=true, identity=true;
    const auto need=inverse_aim_response_curve({dir.x*s.speed/650,dir.y*s.speed/650},config.aim_response_curve);
    r.capacity=std::hypot(need.x/config.ai_aim.body_lock_max_ai_force,need.y/config.ai_aim.body_lock_max_ai_force_y);
    for(int ms=0;ms<duration;++ms) {
        now=1+ms*.001;
        const float sign=ms<onset||ms>=stop?0:ms<onset+s.segment?1:-1;
        const Vec2f velocity{s.control?0:dir.x*s.speed*sign,s.control?0:dir.y*s.speed*sign};
        if(norm(velocity)>0) ++r.moved;
        const bool fresh=ms%s.vision==0;
        if(fresh) {
            controller.submit_vision_snapshot(incident_fixture::observed_snapshot(target,++frame,now,error.x,error.y,ms==0));
            ++r.fresh;
            if(!s.ads&&!s.control&&ms>onset&&r.first_source<0&&error.x*dir.x+error.y*dir.y>=.25f) r.first_source=ms;
        }
        auto physical=incident_fixture::ads_input(0,0,s.fire);
        physical.left_x=-dir.x*sign; // same button script in the no-motion control
        r.max_manual=std::max(r.max_manual,std::hypot(physical.right_x,physical.right_y));
        const auto out=controller.build_output(physical);
        const auto& p=controller.last_target_plan(); const auto& c=controller.last_output_components();
        if(p.target_id) { if(!persistent_id) persistent_id=p.target_id; ++r.target; }
        // Runtime publishes generation only with a source batch. An absent
        // generation on a held tick is not a target identity replacement.
        if(p.target_id!=persistent_id||(fresh&&p.selector_target_generation!=1)) {
            identity=false; if(r.identity_break_ms<0)r.identity_break_ms=ms;
        }
        const bool body=p.mode==pipeline_contract::ControlMode::BodyLockFollow;
        if(body) {++r.body; if(ms<onset) ++warm_body; if(r.body_entry<0) r.body_entry=ms;}
        if(p.bodylock_target_motion_valid) ++r.observed;
        if(norm(error)<=8&&r.first_close<0) r.first_close=ms;
        const Vec2f final_screen{out.right_x,-out.right_y};
        finite=finite&&std::isfinite(norm(final_screen))&&std::fabs(out.right_x)<=1.00001f&&std::fabs(out.right_y)<=1.00001f;
        if(!s.ads&&!s.control&&ms>=onset&&r.first_output<0&&final_screen.x*dir.x+final_screen.y*dir.y>.01f) r.first_output=ms;
        if(ms>=onset) r.peak=std::max(r.peak,norm(error));
        const int settling=s.ads?355:100;
        const bool first_steady=ms>=onset+settling&&ms<onset+s.segment;
        const bool reverse_steady=ms>=onset+s.segment+100&&ms<stop;
        if(first_steady||reverse_steady) {
            r.track_peak=std::max(r.track_peak,norm(error));
            r.outside+=norm(error)>8;
            r.zeros+=norm(final_screen)<.01f;
        }
        if(ms>=stop+100) r.stop_peak=std::max(r.stop_peak,norm(error));
        if(ms>=stop&&std::fabs(error.x*dir.x+error.y*dir.y)>3) {
            const int side=error.x*dir.x+error.y*dir.y>0?1:-1;
            if(last_sign&&side!=last_sign)++r.crossings;last_sign=side;
        }
        const auto bytes=to_ds4_report(out);
        auto delivered=out; delivered.right_x=ds4_axis_value(bytes.bytes[2]); delivered.right_y=-ds4_axis_value(bytes.bytes[3]);
        controller.observe_delivered_output(delivered,true,now);
        const Vec2f decoded{delivered.right_x,-delivered.right_y};
        pending.push_back(forward_aim_response_curve(decoded,config.aim_response_curve));
        const auto applied=pending.front(); pending.pop_front();
        if(trace) *trace<<s.id<<','<<ms<<','<<fresh<<','<<error.x<<','<<error.y<<','<<velocity.x<<','<<velocity.y<<','
            <<out.right_x<<','<<-out.right_y<<','<<c.requested_assist_stick.x<<','<<-c.requested_assist_stick.y<<','
            <<c.shaped_assist_stick.x<<','<<-c.shaped_assist_stick.y<<','<<c.bodylock_position_stick.x<<','<<-c.bodylock_position_stick.y<<','
            <<c.bodylock_motion_stick.x<<','<<-c.bodylock_motion_stick.y<<','<<c.bodylock_effective_motion_stick.x<<','<<-c.bodylock_effective_motion_stick.y<<','
            <<p.bodylock_target_motion_px_per_sec.x<<','<<p.bodylock_target_motion_px_per_sec.y<<','<<p.bodylock_target_motion_valid<<','
            <<p.response_scale<<','<<static_cast<int>(p.mode)<<','<<p.error_px.x<<','<<p.error_px.y<<','<<p.reliability<<','<<p.target_id<<','<<p.selector_target_generation<<'\n';
        error.x+=(velocity.x-650*applied.x)*.001f;error.y+=(velocity.y-650*applied.y)*.001f;
    }
    now+=.001; auto released=incident_fixture::ads_input(.12f,-.12f);released.left_trigger=0;released.right_trigger=0;
    const auto raw=controller.build_output(released);
    r.passthrough=std::max(std::fabs(raw.right_x-.12f),std::fabs(raw.right_y+.12f));
    r.finite=finite;r.warm_body=warm_body;
    r.trigger=finite&&identity&&r.target==duration&&r.fresh>10&&r.observed>10&&r.max_manual==0&&r.passthrough<1e-6f&&
        (s.ads||warm_body>=400)&&(s.control?r.moved==0:r.moved==2*s.segment)&&r.capacity<.9f;
    r.o1=s.ads||s.control||(r.first_source>=0&&r.first_output>=0&&std::max(0,r.first_output-r.first_source)<=1);
    r.o2=r.track_peak<=8; r.o3=!s.ads||(r.body_entry>=0&&r.body_entry<=355&&r.first_close>=0&&r.first_close<=355);
    r.o4=r.stop_peak<=8;
    return r;
}
int main(int argc,char**argv) { try {
    if(argc!=4&&argc!=6)throw std::runtime_error("usage: probe config report trace-case-id [trace.csv case-id]");
    const auto config=load_runtime_config(argv[1]).gamepad;
    if(std::filesystem::exists(argv[2]))throw std::runtime_error("report exists");
    std::ofstream out(argv[2]);out<<std::setprecision(9);
    int trace_case=std::stoi(argv[3]);std::ofstream trace;
    if(argc==6) { if(std::filesystem::exists(argv[4]))throw std::runtime_error("trace exists");trace.open(argv[4]);trace_case=std::stoi(argv[5]);
        trace<<"case,ms,fresh,ex,ey,vx,vy,finalx,finaly,requestx,requesty,shapedx,shapedy,positionx,positiony,motionx,motiony,boundedx,boundedy,observerx,observery,observer_valid,response,mode,planex,planey,reliability,target_id,generation\n"; }
    std::vector<Spec> specs;int id=0;
    for(bool ads:{false,true})for(int vision:{5,2})for(bool fire:{false,true})for(float speed:{120.f,240.f})for(int d=0;d<8;++d)
        for(bool control:{false,true}) specs.push_back({id++,vision,700,d*3.14159265358979323846f/4,speed,ads,fire,control,0});
    for(unsigned seed:{20260926u,20260927u}) { std::mt19937 rng(seed);
        for(int i=0;i<64;++i) {const int vision=2+rng()%7,segment=rng()%2?500:2500;const float angle=(rng()/4294967295.f)*6.283185307f,speed=80+(rng()/4294967295.f)*180;
            const bool ads=rng()%2,fire=rng()%2;for(bool control:{false,true})specs.push_back({id++,vision,segment,angle,speed,ads,fire,control,seed});}}
    int failures=0,invalid=0,controls_failed=0;out<<"{\"schema\":1,\"plant\":\"synthetic_matched_650_delay9_ds4\",\"cases\":[";
    for(const auto&s:specs) {
        const auto r=run(s,config,trace.is_open()&&s.id==trace_case?&trace:nullptr);
        const bool ok=r.o1&&r.o2&&r.o3&&r.o4;
        invalid+=!r.trigger;failures+=!s.control&&r.trigger&&!ok;controls_failed+=s.control&&(!r.trigger||!ok);
        out<<(s.id?",":"")<<"{\"id\":"<<s.id<<",\"seed\":"<<s.seed<<",\"angle\":"<<s.angle<<",\"speed\":"<<s.speed<<",\"vision_ms\":"<<s.vision
            <<",\"segment_ms\":"<<s.segment<<",\"ads\":"<<s.ads<<",\"fire\":"<<s.fire<<",\"control\":"<<s.control<<",\"trigger\":"<<r.trigger
            <<",\"identity_break_ms\":"<<r.identity_break_ms<<",\"finite\":"<<r.finite<<",\"warm_body_ticks\":"<<r.warm_body
            <<",\"capacity_fraction\":"<<r.capacity<<",\"first_source_ms\":"<<r.first_source<<",\"first_output_ms\":"<<r.first_output<<",\"body_entry_ms\":"<<r.body_entry
            <<",\"first_close_ms\":"<<r.first_close<<",\"body_ticks\":"<<r.body<<",\"observer_ticks\":"<<r.observed<<",\"fresh\":"<<r.fresh<<",\"target_ticks\":"<<r.target
            <<",\"moving_ticks\":"<<r.moved<<",\"max_manual\":"<<r.max_manual<<",\"passthrough_loss\":"<<r.passthrough<<",\"peak_error\":"<<r.peak
            <<",\"tracking_peak\":"<<r.track_peak<<",\"outside_8px_ms\":"<<r.outside<<",\"steady_zero_ms\":"<<r.zeros<<",\"stop_peak\":"<<r.stop_peak<<",\"stop_crossings\":"<<r.crossings
            <<",\"O1\":"<<r.o1<<",\"O2\":"<<r.o2<<",\"O3\":"<<r.o3<<",\"O4\":"<<r.o4<<"}";
    }
    out<<"],\"failed_cases\":"<<failures<<",\"invalid_cases\":"<<invalid<<",\"failed_controls\":"<<controls_failed<<"}\n";
    std::cout<<"cases="<<specs.size()<<" failed="<<failures<<" invalid="<<invalid<<" failed_controls="<<controls_failed<<'\n';
    return invalid||controls_failed?2:failures?1:0;
} catch(const std::exception&e) {std::cerr<<e.what()<<'\n';return 3;} }
