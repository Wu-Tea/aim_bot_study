#pragma once

// Offline deterministic plant. Included by the incident test translation unit.
// Same schedules/seeds are run against the before and after controller binaries.
void firing_gap_motion_scenarios(const native_test::TestContext& context, bool validation, int ablation = -1, bool independent = false) {
    using namespace controller_native;
    std::filesystem::create_directories(context.artifact_directory);
    std::ofstream report(context.artifact_path(ablation >= 0 ? "ablation_" + std::to_string(ablation) + ".csv" : validation ? "firing_gap_motion_validation.csv" : "firing_gap_motion.csv"));
    report << "seed,duration_ms,hz,case,miss_ticks,short_identity_losses,blind_growth,blind_reverse,expired_authority,fire_on_gap,mean_error,max_error,wrong_way_px,gap_ai_px,mean_desired_error,max_desired_shift,max_gap_ai_px,response,target_speed,latency_ms,effect_delay_ms,nonlinear,outside_capture_ticks,outside_control_range_ticks\n";
    std::ofstream trace;
    if (ablation >= 0) {
        trace.open(context.artifact_path("trace_" + std::to_string(ablation) + ".csv"));
        trace << "tick,x,y,id,mode,authority,radius,ex,ey,mx,my,mvalid,ai_x,ai_y,out_x,out_y,gap,missing,reason,source_reason,response\n";
    }
    unsigned failures = 0;
    const auto seeds = independent ? std::array<unsigned,2>{570233u,808901u} : validation ? std::array<unsigned,2>{928517u, 410629u} :
        std::array<unsigned,2>{185235u,719031u};
    for (unsigned seed : seeds) for (int duration : {1200, 8000})
    for (int hz : {150, 180, 220}) for (int scenario = 0; scenario < 16; ++scenario) {
        if (ablation >= 0 && (seed != 410629u || duration != 8000 || hz != 150 || scenario != (ablation >= 10 ? 7 : 3))) continue;
        std::mt19937 rng(seed + scenario);
        auto config = incident_fixture::base_config(40, 180);
        config.ai_aim.normalized_assist_parameters = true;
        config.ai_aim.ads_completion_fresh_frames = 1;
        config.ai_aim.aim_response_learning_enabled = true;
        config.ai_aim.aim_response_initial_scale = 500;
        config.ai_aim.aim_response_effect_delay_ms = 9;
        config.ai_aim.body_lock_max_ai_force = .3f;
        config.ai_aim.body_lock_max_ai_force_y = .3f;
        config.auto_fire.manual_fire_input = "RB";
        config.recoil.enabled = true;
        config.recoil.feedback_amount = .20f;
        config.recoil.feedback_min_amount = 0;
        config.recoil.feedback_max_amount = .34f;
        config.aim_response_curve.algorithm = validation && scenario%2 ?
            AimResponseCurveAlgorithm::CodDynamicLegacyLut : AimResponseCurveAlgorithm::Linear;
        const float response = validation ? 350.f + rng()%501 : 500.f;
        const float target_speed = validation ? static_cast<int>(rng()%241)-120.f : 65.f;
        const float impulse = validation ? 1.f + rng()%6 : 3.f;
        double now = 10, next_capture = 10;

        incident_fixture::TargetSpec spec;
        spec.observation_id = 1; spec.selector_generation = 86;
        spec.enemy_identity_confirmed = spec.has_enemy_cue = true;
        const int kind = scenario % 8;
        const int latency = validation ? 2+rng()%9 : scenario < 8 ? 2 : 7;
        const int effect_delay = validation ? 5+rng()%11 : scenario < 8 ? 7 : 15;
        const int gap_length = kind == 3 ? 65 : 8 + int(rng() % 15);
        if (ablation == 2 || ablation == 3 || ablation == 14) {
            config.ai_aim.aim_response_initial_scale = response;
            config.ai_aim.aim_response_effect_delay_ms = float(effect_delay);
        }
        NativeGamepadController controller(config, &now);
        struct Queued { int due; ControllerVisionSnapshot snapshot; bool miss; };
        std::vector<Queued> queue;
        std::array<pipeline_contract::Vec2f, 16> lag{};
        float x = 0, y = 0, maximum = 0;
        double sum = 0, wrong_way = 0, gap_work = 0, desired_sum = 0, current_gap_work = 0, max_gap_work = 0;
        float max_desired_shift = 0;
        unsigned frame = 0, miss_ticks = 0, losses = 0, growth = 0, reversals = 0, expired = 0, fires = 0;
        unsigned outside_capture_ticks=0, outside_control_range_ticks=0;
        bool missing = false;
        double last_capture = now;
        std::uint64_t visible_id = 0;
        pipeline_contract::Vec2f previous_ai{};
        for (int tick = 0; tick < duration; ++tick) {
            const int slot = tick % 240;
            const bool gap = ablation != 1 && ablation != 3 && ablation != 13 && tick >= 300 && slot >= 100 && slot < 100 + gap_length;
            const bool disappeared = kind == 4 && tick > duration - 150;
            const bool outside_capture = std::abs(x)>spec.center_x+spec.body_width*.5f ||
                y-spec.body_height*spec.aim_height_ratio>spec.center_y ||
                y+spec.body_height*(1-spec.aim_height_ratio)<-spec.center_y;
            outside_capture_ticks += outside_capture;
            auto physical = incident_fixture::ads_input();
            physical.rb = tick >= 150;
            // Independent lateral POV and manual yaw, including reversals during a gap.
            physical.left_x = tick >= 150 ? .35f * std::sin(tick * .008f) : 0;
            physical.right_x = tick >= 150 ? .06f * std::sin(tick * .013f) : 0;
            physical.right_y = tick >= 150 ? -.025f : 0;
            if (kind == 1 && gap) physical.right_x = slot < 110 ? .2f : -.2f;
            if (kind == 5 && gap) physical.right_x = .75f;
            if (ablation == 11) physical.right_x = physical.right_y = 0;
            if (now + 1e-9 >= next_capture) {
                next_capture += 1. / hz;
                const bool miss = gap || disappeared || outside_capture;
                if (kind == 6 && tick >= duration / 2 && spec.selector_generation == 86) {
                    ++spec.selector_generation; x += 25;
                }
                auto snapshot = miss ? incident_fixture::empty_snapshot(spec, ++frame, now) :
                    incident_fixture::observed_snapshot(spec, ++frame, now, x, y);
                snapshot.selector_target_generation = spec.selector_generation;
                if (kind == 7 && miss) snapshot.selector_target_generation = 0;
                queue.push_back({tick + latency, snapshot, miss});
            }
            while (!queue.empty() && queue.front().due <= tick) {
                const auto packet = queue.front(); queue.erase(queue.begin());
                controller.submit_vision_snapshot(packet.snapshot);
                missing = packet.miss;
                if (!missing) last_capture = packet.snapshot.capture_time_seconds;
                if (kind == 2 && missing) controller.observe_delivered_output({}, false, 0);
            }
            const auto prior_id = controller.last_target_plan().target_id;
            const auto learning_before = controller.learning_snapshot();
            const auto output = controller.build_output(physical);
            const auto& plan = controller.last_target_plan();
            outside_control_range_ticks += plan.mode == pipeline_contract::ControlMode::BodyLockFollow &&
                std::hypot(plan.error_px.x,plan.error_px.y) > plan.position_response_radius_px;
            const auto components = controller.last_output_components();
            const pipeline_contract::Vec2f ai{components.shaped_assist_stick.x, components.shaped_assist_stick.y};
            if (trace.is_open()) trace << tick << ',' << x << ',' << y << ',' << plan.target_id << ',' << int(plan.mode) << ','
                << plan.aim_authority << ',' << plan.position_response_radius_px << ',' << plan.error_px.x << ',' << plan.error_px.y << ','
                << plan.bodylock_target_motion_px_per_sec.x << ',' << plan.bodylock_target_motion_px_per_sec.y << ',' << plan.bodylock_target_motion_valid << ','
                << ai.x << ',' << ai.y << ',' << output.right_x << ',' << output.right_y << ',' << plan.observation_gap << ',' << missing << ','
                << int(plan.ads_decision_reason) << ',' << int(plan.source_decision_reason) << ',' << plan.response_scale << '\n';
            const double age = (now - last_capture) * 1000;
            if (!missing && plan.direct_person_observation) visible_id = plan.target_id;
            if (missing) {
                const auto learning_after = controller.learning_snapshot();
                for (std::size_t i=0;i<learning_after.size();++i)
                    failures += learning_before[i].accepted_samples != learning_after[i].accepted_samples;
                ++miss_ticks;
                // Strong manual/handover, generation invalidation and replacement are separate cases.
                if (kind != 5 && kind != 6 && kind != 7 && age < 31.9 && prior_id != 0 && plan.target_id == 0) ++losses;
                if (age > 32.001 && plan.aim_authority > 0) ++expired;
                if (plan.fire_authority || plan.fire_requested || plan.direct_person_observation) ++fires;
                if (plan.observation_gap) {
                    if (std::abs(ai.x) > std::abs(previous_ai.x) + 1e-5f ||
                        std::abs(ai.y) > std::abs(previous_ai.y) + 1e-5f) ++growth;
                    if (ai.x * previous_ai.x < -1e-8f || ai.y * previous_ai.y < -1e-8f) ++reversals;
                    gap_work += response * std::hypot(ai.x, ai.y) * .001;
                    current_gap_work += response * std::hypot(ai.x, ai.y) * .001;
                    max_gap_work = std::max(max_gap_work, current_gap_work);
                    if (plan.aim_authority == 0)
                        failures += components.before_recoil_stick.x != physical.right_x ||
                            components.before_recoil_stick.y != physical.right_y;
                }
            }
            if (!plan.observation_gap) current_gap_work = 0;
            failures += !incident_fixture::finite_unit(output.right_x) || !incident_fixture::finite_unit(output.right_y);
            if (tick > 200) {
                const auto error = std::hypot(x, y);
                sum += error; maximum = std::max(maximum, error);
                const float shift_x = plan.target_id ? (plan.desired_point_normalized.x-.5f)*spec.body_width : 0;
                const float shift_y = plan.target_id ? (plan.desired_point_normalized.y-spec.aim_height_ratio)*spec.body_height : 0;
                desired_sum += std::hypot(x+shift_x, y+shift_y);
                max_desired_shift = std::max(max_desired_shift, std::hypot(shift_x,shift_y));
                if (error > 1) wrong_way += response * std::max(0.f, (-ai.x*x + ai.y*y) / error) * .001;
            }
            const auto delivered = lag[tick % effect_delay];
            lag[tick % effect_delay] = forward_aim_response_curve({output.right_x, output.right_y},config.aim_response_curve);
            const float target = tick > 150 ? (kind == 1 && gap ? -target_speed-20.f : target_speed) : 0;
            const float pov = 75 * physical.left_x;
            // Weapon kick is independent of configured compensation: variable rate and impulses.
            const float kick = physical.rb ? -response * forward_aim_response_curve({0,-.2f},config.aim_response_curve).y +
                55.f * std::sin(tick * .041f) : 0;
            x += (target - pov - response * delivered.x) * .001f;
            y += (kick + response * delivered.y) * .001f;
            if (ablation != 4 && ablation != 12 && physical.rb && tick % 73 == 0) { x += (scenario % 2 ? -2.f : 2.f); y += impulse; }
            previous_ai = ai;
            now += .001;
        }
        auto manual = incident_fixture::ads_input(.031f, -.043f);
        manual.left_trigger = 0;
        const auto released = controller.build_output(manual);
        failures += released.right_x != manual.right_x || released.right_y != manual.right_y;
        (void)visible_id;
        failures += losses + growth + reversals + expired + fires;
        report << seed << ',' << duration << ',' << hz << ',' << scenario << ',' << miss_ticks << ',' << losses << ','
            << growth << ',' << reversals << ',' << expired << ',' << fires << ',' << sum / (duration-201) << ','
            << maximum << ',' << wrong_way << ',' << gap_work << ',' << desired_sum/(duration-201) << ','
            << max_desired_shift << ',' << max_gap_work << ',' << response << ',' << target_speed << ',' << latency << ','
            << effect_delay << ',' << int(config.aim_response_curve.algorithm==AimResponseCurveAlgorithm::CodDynamicLegacyLut) << ','
            << outside_capture_ticks << ',' << outside_control_range_ticks << '\n';
    }
    report.close();
    if (failures) throw std::runtime_error("firing gap identity/prediction invariant failed: " + std::to_string(failures));
}

void firing_gap_motion_simulation(const native_test::TestContext& context) {
    firing_gap_motion_scenarios(context, false);
}
void firing_gap_motion_validation(const native_test::TestContext& context) {
    firing_gap_motion_scenarios(context, true);
}

void firing_gap_causal_ablation(const native_test::TestContext& context) {
    for (int variant=0; variant<5; ++variant) firing_gap_motion_scenarios(context, true, variant);
    for (int variant=10; variant<15; ++variant) firing_gap_motion_scenarios(context, true, variant);
}

void firing_gap_composition_validation(const native_test::TestContext& context) {
    firing_gap_motion_scenarios(context, true, -1, true);
}
