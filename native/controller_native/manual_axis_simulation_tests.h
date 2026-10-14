#pragma once

// Production controller in a deterministic delayed plant. This is not a game
// replay: camera/recoil disturbance and target motion are explicit assumptions.
inline void manual_axis_motion_simulation(const native_test::TestContext& context) {
    using namespace controller_native;
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path("manual_axis_motion.csv"));
    report << "seed,ms,hz,scenario,mean_source_error,mean_desired_error,max_error,max_d_shift,release_d_shift,body_ticks,correction_x_ticks,correction_y_ticks,identity_changes,gap_ticks,invalid_output,expired_authority,release_passthrough_failures\n";
    unsigned failures=0;
    for(unsigned seed : {140026u,985721u}) for(int duration : {1200,8000})
    for(int hz : {150,180,220}) for(int scenario=0;scenario<8;++scenario) {
        std::mt19937 rng(seed+scenario);
        auto config=incident_fixture::base_config(40,180);
        config.ai_aim.normalized_assist_parameters=true;
        config.ai_aim.ads_completion_fresh_frames=1;
        config.ai_aim.aim_response_initial_scale=500;
        config.ai_aim.aim_response_effect_delay_ms=9;
        config.ai_aim.body_lock_max_ai_force=config.ai_aim.body_lock_max_ai_force_y=.3f;
        config.auto_fire.manual_fire_input="RB";
        config.recoil.enabled=true;config.recoil.feedback_amount=.2f;
        config.aim_response_curve.algorithm=scenario%2 ? AimResponseCurveAlgorithm::CodDynamicLegacyLut : AimResponseCurveAlgorithm::Linear;
        const float response=400.f+rng()%301;
        const int delay=4+rng()%9, latency=2+rng()%7;
        double now=10,next_capture=10,last_capture=10;
        NativeGamepadController controller(config,&now);
        incident_fixture::TargetSpec spec;
        spec.observation_id=1;spec.selector_generation=72;
        spec.has_enemy_cue=spec.enemy_identity_confirmed=true;
        struct Packet {int due;ControllerVisionSnapshot snapshot;bool missing;};
        std::vector<Packet> queue;
        std::array<pipeline_contract::Vec2f,16> lag{};
        float x=0,y=0,max_error=0,max_shift=0,release_shift=0;
        double sum=0,desired_sum=0;
        unsigned frames=0,body=0,cx=0,cy=0,changes=0,gaps=0,invalid=0,expired=0,passthrough=0,samples=0;
        std::uint64_t prior_id=0;
        bool missing=false;
        for(int tick=0;tick<duration;++tick){
            const int slot=tick%1000;
            auto physical=incident_fixture::ads_input();
            physical.rb=tick>=150;
            // Carry starts before ownership. New orthogonal corrections,
            // small pulse, threshold chatter, deliberate persistent edit,
            // wrong-way manual motion, and strong explicit escape.
            physical.right_x=(scenario==0||scenario==1||scenario==4) ? .36f : 0;
            if(scenario==0 && tick>=300) physical.right_y=-.23f;
            if(scenario==1 && tick>=300) physical.right_y=.15f+.018f*std::sin(tick*.7f);
            if(scenario==2 && slot>=300&&slot<330) physical.right_y=-.23f;
            if(scenario==3 && slot>=300&&slot<450) physical.right_y=-.45f;
            if(scenario==4) physical.right_y=tick<300?.6f:-.6f;
            if(scenario==5 && tick>=300){physical.right_x=.23f*std::sin(tick*.017f);physical.right_y=.23f*std::cos(tick*.013f);}
            if(scenario==6 && slot>=300&&slot<400) physical.right_x=slot<350?.8f:-.8f;
            if(scenario==7 && slot>=300&&slot<400) physical.right_y=-.2f;
            if(tick>=600 && (scenario==0||scenario==1||scenario==4)){physical.right_x=0;physical.right_y=0;}
            physical.left_x=.4f*std::sin(tick*.006f);
            const bool release=tick>=duration-100;
            if(release){physical.left_trigger=0;physical.rb=false;physical.right_x=.17f;physical.right_y=-.12f;}
            const bool gap=tick>500 && slot>=650 && slot<(scenario==7?735:670);
            const bool outside=std::abs(x)>320+spec.body_width*.5f || std::abs(y)>256+spec.body_height;
            if(now+1e-9>=next_capture){
                next_capture+=1./hz;
                bool miss=gap||outside;
                queue.push_back({tick+latency,miss?incident_fixture::empty_snapshot(spec,++frames,now):incident_fixture::observed_snapshot(spec,++frames,now,x,y),miss});
            }
            while(!queue.empty()&&queue.front().due<=tick){
                auto packet=queue.front();queue.erase(queue.begin());
                controller.submit_vision_snapshot(packet.snapshot);missing=packet.missing;
                if(!missing)last_capture=packet.snapshot.capture_time_seconds;
            }
            const auto output=controller.build_output(physical);
            const auto& plan=controller.last_target_plan();
            const auto components=controller.last_output_components();
            body+=plan.mode==pipeline_contract::ControlMode::BodyLockFollow;
            cx+=plan.manual_correction_x;cy+=plan.manual_correction_y;gaps+=missing;
            if(prior_id&&plan.target_id&&prior_id!=plan.target_id)++changes;
            if(plan.target_id)prior_id=plan.target_id;
            invalid+=!incident_fixture::finite_unit(output.right_x)||!incident_fixture::finite_unit(output.right_y);
            expired+=missing&&(now-last_capture)>.040001&&plan.aim_authority>0;
            if(release)passthrough+=std::abs(output.right_x-physical.right_x)>1e-5f||std::abs(output.right_y-physical.right_y)>1e-5f;
            if(tick>=200&&!release){
                float sx=plan.target_id?(plan.desired_point_normalized.x-.5f)*spec.body_width:0;
                float sy=plan.target_id?(plan.desired_point_normalized.y-spec.aim_height_ratio)*spec.body_height:0;
                float error=std::hypot(x,y);sum+=error;desired_sum+=std::hypot(x+sx,y+sy);++samples;
                max_error=std::max(max_error,error);max_shift=std::max(max_shift,std::hypot(sx,sy));
                if(slot>=500&&slot<600)release_shift=std::max(release_shift,std::hypot(sx,sy));
            }
            const auto delivered=lag[tick%delay];
            lag[tick%delay]=forward_aim_response_curve({output.right_x,output.right_y},config.aim_response_curve);
            const float tx=tick>=150 ? 60.f*std::sin(tick*.004f)+30.f : 0;
            const float ty=tick>=150 ? 65.f*std::sin(tick*.007f) : 0;
            const float povx=75*physical.left_x;
            const float povy=tick>=200 ? 90.f*std::sin(tick*.011f) : 0;
            const float kick=physical.rb ? -response*forward_aim_response_curve({0,-.2f},config.aim_response_curve).y+35*std::sin(tick*.041f):0;
            x+=(tx+povx-response*delivered.x)*.001f;
            y+=(ty+povy+kick+response*delivered.y)*.001f;
            if(tick>200&&tick%173==0)y+=3;
            now+=.001;
        }
        failures+=invalid+expired+passthrough;
        report<<seed<<','<<duration<<','<<hz<<','<<scenario<<','<<sum/std::max(1u,samples)<<','<<desired_sum/std::max(1u,samples)<<','<<max_error<<','<<max_shift<<','<<release_shift<<','<<body<<','<<cx<<','<<cy<<','<<changes<<','<<gaps<<','<<invalid<<','<<expired<<','<<passthrough<<'\n';
    }
    if(failures)throw std::runtime_error("manual-axis plant violated output/expiry/manual release invariants");
}
