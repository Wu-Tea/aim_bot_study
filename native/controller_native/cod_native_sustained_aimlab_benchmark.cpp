#include "native_benchmark_controller_adapter.h"
#include "pid_benchmark_controller.h"
#include "runtime_config.h"
#include "sustained_aimlab_scenario.h"
#include "sustained_aimlab_simulator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace controller_native;
using namespace controller_native::benchmark_adapter;
using namespace controller_native::pid_benchmark;
using namespace controller_native::sustained_aimlab;

std::vector<std::string> split_nonempty(
    const std::string& value,
    char delimiter,
    const char* label) {
    if (value.empty()) {
        throw std::invalid_argument(std::string(label) + " cannot be empty");
    }
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t end = value.find(delimiter, begin);
        const std::string token = value.substr(
            begin,
            end == std::string::npos ? std::string::npos : end - begin);
        if (token.empty()) {
            throw std::invalid_argument(
                std::string(label) + " contains an empty field");
        }
        result.push_back(token);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

int parse_integer(const std::string& value, const char* label) {
    std::size_t consumed = 0;
    int result = 0;
    try {
        result = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    if (consumed != value.size()) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    return result;
}

double parse_number(const std::string& value, const char* label) {
    std::size_t consumed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    if (consumed != value.size() || !std::isfinite(result)) {
        throw std::invalid_argument(std::string("invalid ") + label);
    }
    return result;
}

std::vector<RuntimeObservationSample> parse_observation_pattern(
    const std::string& value) {
    std::vector<RuntimeObservationSample> result;
    for (const auto& token : split_nonempty(
             value, ',', "runtime observation pattern")) {
        const auto fields = split_nonempty(
            token, ':', "runtime observation sample");
        if (fields.size() != 2) {
            throw std::invalid_argument(
                "runtime observation samples require delivery:capture-age");
        }
        result.push_back({
            parse_integer(fields[0], "runtime delivery interval"),
            parse_integer(fields[1], "runtime capture age"),
        });
    }
    return result;
}

std::vector<RuntimeManualSegment> parse_manual_pattern(
    const std::string& value) {
    std::vector<RuntimeManualSegment> result;
    for (const auto& segment_token : split_nonempty(
             value, '|', "runtime manual pattern")) {
        RuntimeManualSegment segment;
        std::string sample_payload = segment_token;
        const std::size_t mode_separator = segment_token.find('@');
        if (mode_separator != std::string::npos) {
            const std::string mode = segment_token.substr(0, mode_separator);
            sample_payload = segment_token.substr(mode_separator + 1);
            if (mode == "ads") {
                segment.aim_mode = RuntimeManualAimMode::Ads;
            } else if (mode == "bodylock") {
                segment.aim_mode = RuntimeManualAimMode::BodyLock;
            } else if (mode != "any") {
                throw std::invalid_argument(
                    "runtime manual segment mode must be any, ads, or bodylock");
            }
        }
        for (const auto& sample_token : split_nonempty(
                 sample_payload, ';', "runtime manual segment")) {
            const auto fields = split_nonempty(
                sample_token, ':', "runtime manual sample");
            if (fields.size() != 3) {
                throw std::invalid_argument(
                    "runtime manual samples require duration:radial:tangential");
            }
            segment.samples.push_back({
                parse_integer(fields[0], "runtime manual duration"),
                parse_number(fields[1], "runtime manual radial input"),
                parse_number(fields[2], "runtime manual tangential input"),
            });
        }
        result.push_back(std::move(segment));
    }
    return result;
}

std::vector<RuntimeTargetSample> parse_target_pattern(
    const std::string& value) {
    std::vector<RuntimeTargetSample> result;
    for (const auto& token : split_nonempty(
             value, ';', "runtime target pattern")) {
        const auto fields = split_nonempty(
            token, ':', "runtime target sample");
        if (fields.size() != 4) {
            throw std::invalid_argument(
                "runtime target samples require error-x:error-y:width:height");
        }
        result.push_back({
            {
                parse_number(fields[0], "runtime target error x"),
                parse_number(fields[1], "runtime target error y"),
            },
            parse_number(fields[2], "runtime target width"),
            parse_number(fields[3], "runtime target height"),
        });
    }
    return result;
}

bool is_sha256(const std::string& value) {
    return value.size() == 64 && std::all_of(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

struct Options {
    std::filesystem::path config_path = "config.native.example.toml";
    std::filesystem::path output_path;
    std::string revision = "unknown";
    bool dirty = false;
    bool smoke = false;
    int duration_ms = 60'000;
    int controller_tick_hz = 1000;
    int vision_hz = 0;
    int vision_result_delay_ms = 0;
    int control_response_delay_ms = 0;
    int short_occlusion_ms = 0;
    int target_slot_ms = 0;
    double manual_input_scale = 1.0;
    double slowdown_edge = 0.50;
    double slowdown_center = 0.40;
    bool slowdown_edge_explicit = false;
    bool slowdown_center_explicit = false;
    double camera_response_px_per_stick_second = 0.0;
    double sensitivity_multiplier = 1.0;
    std::string plant_source;
    std::string runtime_profile_id;
    std::string runtime_profile_sha256;
    std::string runtime_audit_sha256;
    std::string runtime_source_runtime_sha256;
    std::string runtime_source_config_sha256;
    std::string runtime_source_engine_sha256;
    std::string config_relationship;
    std::string benchmark_config_file_sha256;
    std::string benchmark_executable_sha256;
    std::string runtime_vision_mode;
    double runtime_source_vision_hz = 0.0;
    double runtime_requested_vision_hz = 0.0;
    double runtime_realized_vision_hz = 0.0;
    double runtime_vision_age_scale = 1.0;
    double runtime_source_controller_interval_p50_ms = 0.0;
    double runtime_source_controller_interval_p95_ms = 0.0;
    std::vector<RuntimeObservationSample> runtime_observation_pattern;
    std::vector<RuntimeManualSegment> runtime_manual_segments;
    std::vector<RuntimeTargetSample> runtime_target_samples;
    std::string profile = "pure";
    std::string cohort = "both";
    std::string left_strafe = "off";
    std::string vertical_motion = "off";
    std::string target_motion = "moving";
    std::string target_motion_preset;
    std::string pov_motion_preset;
    std::string scenario = "baseline";
    std::string target_profile = "ordinary";
    std::string vision_disturbance = "off";
    std::string benchmark_recoil = "off";
    std::string controller = "production";
    PidBenchmarkConfig pid;
    std::vector<std::uint32_t> seeds;
};

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&]() -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing value for " + argument);
            }
            return argv[++index];
        };
        if (argument == "--config") options.config_path = value();
        else if (argument == "--output") options.output_path = value();
        else if (argument == "--revision") options.revision = value();
        else if (argument == "--duration-ms") options.duration_ms = std::stoi(value());
        else if (argument == "--controller-tick-hz") {
            options.controller_tick_hz = std::stoi(value());
        }
        else if (argument == "--vision-hz") options.vision_hz = std::stoi(value());
        else if (argument == "--vision-result-delay-ms") {
            options.vision_result_delay_ms = std::stoi(value());
        } else if (argument == "--control-response-delay-ms") {
            options.control_response_delay_ms = std::stoi(value());
        } else if (argument == "--short-occlusion-ms") {
            options.short_occlusion_ms = std::stoi(value());
        } else if (argument == "--target-slot-ms") {
            options.target_slot_ms = std::stoi(value());
        } else if (argument == "--manual-input-scale") {
            options.manual_input_scale = std::stod(value());
        } else if (argument == "--slowdown-edge") {
            options.slowdown_edge = std::stod(value());
            options.slowdown_edge_explicit = true;
        } else if (argument == "--slowdown-center") {
            options.slowdown_center = std::stod(value());
            options.slowdown_center_explicit = true;
        } else if (argument == "--camera-response-px-per-stick-second") {
            options.camera_response_px_per_stick_second = std::stod(value());
        } else if (argument == "--sensitivity-multiplier") {
            options.sensitivity_multiplier = std::stod(value());
        } else if (argument == "--plant-source") {
            options.plant_source = value();
        } else if (argument == "--runtime-profile-id") {
            options.runtime_profile_id = value();
        } else if (argument == "--runtime-profile-sha256") {
            options.runtime_profile_sha256 = value();
        } else if (argument == "--runtime-audit-sha256") {
            options.runtime_audit_sha256 = value();
        } else if (argument == "--runtime-source-runtime-sha256") {
            options.runtime_source_runtime_sha256 = value();
        } else if (argument == "--runtime-source-config-sha256") {
            options.runtime_source_config_sha256 = value();
        } else if (argument == "--runtime-source-engine-sha256") {
            options.runtime_source_engine_sha256 = value();
        } else if (argument == "--config-relationship") {
            options.config_relationship = value();
        } else if (argument == "--benchmark-config-file-sha256") {
            options.benchmark_config_file_sha256 = value();
        } else if (argument == "--benchmark-executable-sha256") {
            options.benchmark_executable_sha256 = value();
        } else if (argument == "--runtime-vision-mode") {
            options.runtime_vision_mode = value();
        } else if (argument == "--runtime-source-vision-hz") {
            options.runtime_source_vision_hz = std::stod(value());
        } else if (argument == "--runtime-requested-vision-hz") {
            options.runtime_requested_vision_hz = std::stod(value());
        } else if (argument == "--runtime-realized-vision-hz") {
            options.runtime_realized_vision_hz = std::stod(value());
        } else if (argument == "--runtime-vision-age-scale") {
            options.runtime_vision_age_scale = std::stod(value());
        } else if (argument ==
                   "--runtime-source-controller-interval-p50-ms") {
            options.runtime_source_controller_interval_p50_ms =
                std::stod(value());
        } else if (argument ==
                   "--runtime-source-controller-interval-p95-ms") {
            options.runtime_source_controller_interval_p95_ms =
                std::stod(value());
        } else if (argument == "--observation-pattern-ms") {
            options.runtime_observation_pattern =
                parse_observation_pattern(value());
        } else if (argument == "--manual-pattern") {
            options.runtime_manual_segments = parse_manual_pattern(value());
        } else if (argument == "--target-sample-pattern") {
            options.runtime_target_samples = parse_target_pattern(value());
        } else if (argument == "--profile") options.profile = value();
        else if (argument == "--cohort") options.cohort = value();
        else if (argument == "--left-strafe") options.left_strafe = value();
        else if (argument == "--vertical-motion") options.vertical_motion = value();
        else if (argument == "--target-motion") options.target_motion = value();
        else if (argument == "--target-motion-preset") {
            options.target_motion_preset = value();
        } else if (argument == "--pov-motion-preset") {
            options.pov_motion_preset = value();
        }
        else if (argument == "--scenario") options.scenario = value();
        else if (argument == "--target-profile") options.target_profile = value();
        else if (argument == "--vision-disturbance") {
            options.vision_disturbance = value();
        } else if (argument == "--benchmark-recoil") {
            options.benchmark_recoil = value();
        } else if (argument == "--controller") {
            options.controller = value();
        } else if (argument == "--pid-kp") {
            options.pid.kp = std::stod(value());
        } else if (argument == "--pid-ki") {
            options.pid.ki = std::stod(value());
        } else if (argument == "--pid-kd") {
            options.pid.kd = std::stod(value());
        } else if (argument == "--pid-d-filter-ms") {
            options.pid.derivative_filter_tau_ms = std::stod(value());
        } else if (argument == "--pid-output-filter-ms") {
            options.pid.output_filter_tau_ms = std::stod(value());
        } else if (argument == "--pid-max-assist") {
            options.pid.max_assist = std::stod(value());
        } else if (argument == "--seed") {
            options.seeds.push_back(
                static_cast<std::uint32_t>(std::stoul(value())));
        } else if (argument == "--dirty") options.dirty = true;
        else if (argument == "--smoke") options.smoke = true;
        else if (argument == "--help") {
            std::cout
                << "cod_native_sustained_aimlab_benchmark "
                << "[--config PATH] [--output PATH] [--revision HASH] "
                << "[--dirty] [--duration-ms N] [--seed N ...] "
                << "[--controller-tick-hz N] "
                << "[--sensitivity-multiplier N] "
                << "[--profile pure|mixed|scripted|wrong-then-correct|"
                   "arc-recovery|micro-correction|obsolete-after-crossing|runtime] "
                 << "[--cohort ads|bodylock|both] "
                 << "[--vision-hz N] [--vision-result-delay-ms N] "
                 << "[--control-response-delay-ms N] [--short-occlusion-ms N] "
                 << "[--runtime-profile-id ID --runtime-profile-sha256 SHA256 "
                 << "--runtime-audit-sha256 SHA256 "
                 << "--runtime-source-runtime-sha256 SHA256 "
                 << "--runtime-source-config-sha256 SHA256 "
                 << "--runtime-source-engine-sha256 SHA256 "
                 << "--config-relationship matched|counterfactual "
                 << "--benchmark-config-file-sha256 SHA256 "
                 << "--benchmark-executable-sha256 SHA256 "
                 << "--runtime-vision-mode audited_runtime_profile|scaled_runtime_profile "
                 << "--runtime-source-vision-hz N "
                 << "--runtime-requested-vision-hz N "
                 << "--runtime-realized-vision-hz N "
                 << "--runtime-vision-age-scale N "
                 << "--runtime-source-controller-interval-p50-ms N "
                 << "--runtime-source-controller-interval-p95-ms N "
                 << "--observation-pattern-ms DELIVERY:AGE,... "
                 << "--manual-pattern DURATION:RADIAL:TANGENTIAL;...|... "
                 << "--target-sample-pattern X:Y:WIDTH:HEIGHT;... "
                 << "--camera-response-px-per-stick-second N "
                 << "--slowdown-edge N --slowdown-center N "
                 << "--plant-source measured|inferred|assumption] "
                << "[--target-motion stationary|moving] "
                << "[--target-motion-preset stationary|seeded-legacy] "
                << "[--pov-motion-preset off|seeded-strafe|seeded-vertical|seeded-combined] "
                << "[--left-strafe off|full-reversal|both] "
                 << "[--vertical-motion off|slide|jump|random|all] "
                 << "[--controller production|pid] "
                 << "[--pid-kp N] [--pid-ki N] [--pid-kd N] "
                 << "[--pid-d-filter-ms N] [--pid-output-filter-ms N] "
                 << "[--pid-max-assist N] "
                 << "[--benchmark-recoil off|on] [--smoke]\n";
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument(
                "unknown or retired benchmark option: " + argument);
        }
    }
    if (options.seeds.empty()) options.seeds.push_back(2026072301u);
    if (options.duration_ms <= 0) {
        throw std::invalid_argument("duration-ms must be positive");
    }
    if (options.controller_tick_hz <= 0 ||
        options.controller_tick_hz > 1000 ||
        1000 % options.controller_tick_hz != 0) {
        throw std::invalid_argument(
            "controller-tick-hz must be a positive divisor of 1000");
    }
    if (options.vision_hz < 0 || options.vision_hz > 1000) {
        throw std::invalid_argument("vision-hz must be in [0, 1000]");
    }
    if (options.short_occlusion_ms < 0 ||
        options.vision_result_delay_ms < 0 ||
        options.control_response_delay_ms < 0 ||
        options.target_slot_ms < 0) {
        throw std::invalid_argument("timing options cannot be negative");
    }
    if (!std::isfinite(options.manual_input_scale) ||
        !std::isfinite(options.slowdown_edge) ||
        !std::isfinite(options.slowdown_center) ||
        !std::isfinite(options.sensitivity_multiplier) ||
        options.manual_input_scale < 0.0 || options.slowdown_edge <= 0.0 ||
        options.slowdown_center <= 0.0 ||
        options.sensitivity_multiplier <= 0.0) {
        throw std::invalid_argument("scale values must be positive");
    }
    if (options.controller != "production" && options.controller != "pid") {
        throw std::invalid_argument("controller must be production or pid");
    }
    if (options.pid.kp < 0.0 || options.pid.ki < 0.0 ||
        options.pid.kd < 0.0 || options.pid.derivative_filter_tau_ms < 0.0 ||
        options.pid.output_filter_tau_ms < 0.0 ||
        options.pid.max_assist <= 0.0 || options.pid.max_assist > 1.0) {
        throw std::invalid_argument("PID parameters are outside supported bounds");
    }
    if (options.controller == "pid" && options.benchmark_recoil == "on") {
        throw std::invalid_argument(
            "benchmark recoil is not part of the PID-only baseline");
    }
    const bool runtime_requested = options.profile == "runtime";
    const bool any_runtime_option =
        !options.runtime_profile_id.empty() ||
        !options.runtime_profile_sha256.empty() ||
        !options.runtime_audit_sha256.empty() ||
        !options.runtime_source_runtime_sha256.empty() ||
        !options.runtime_source_config_sha256.empty() ||
        !options.runtime_source_engine_sha256.empty() ||
        !options.config_relationship.empty() ||
        !options.benchmark_config_file_sha256.empty() ||
        !options.benchmark_executable_sha256.empty() ||
        !options.runtime_vision_mode.empty() ||
        options.runtime_source_vision_hz != 0.0 ||
        options.runtime_requested_vision_hz != 0.0 ||
        options.runtime_realized_vision_hz != 0.0 ||
        options.runtime_vision_age_scale != 1.0 ||
        options.runtime_source_controller_interval_p50_ms != 0.0 ||
        options.runtime_source_controller_interval_p95_ms != 0.0 ||
        !options.runtime_observation_pattern.empty() ||
        !options.runtime_manual_segments.empty() ||
        !options.runtime_target_samples.empty() ||
        options.camera_response_px_per_stick_second != 0.0 ||
        !options.plant_source.empty();
    if (!runtime_requested && any_runtime_option) {
        throw std::invalid_argument(
            "runtime profile options require --profile runtime");
    }
    if (runtime_requested) {
        if (options.runtime_profile_id.empty() ||
            !is_sha256(options.runtime_profile_sha256) ||
            !is_sha256(options.runtime_audit_sha256) ||
            !is_sha256(options.runtime_source_runtime_sha256) ||
            !is_sha256(options.runtime_source_config_sha256) ||
            !is_sha256(options.runtime_source_engine_sha256) ||
            !is_sha256(options.benchmark_config_file_sha256) ||
            !is_sha256(options.benchmark_executable_sha256) ||
            options.runtime_observation_pattern.empty() ||
            options.runtime_manual_segments.empty() ||
            options.runtime_target_samples.empty()) {
            throw std::invalid_argument(
                "runtime profile identity and all three patterns are required");
        }
        if ((options.runtime_vision_mode != "audited_runtime_profile" &&
             options.runtime_vision_mode != "scaled_runtime_profile") ||
            !std::isfinite(options.runtime_source_vision_hz) ||
            !std::isfinite(options.runtime_requested_vision_hz) ||
            !std::isfinite(options.runtime_realized_vision_hz) ||
            !std::isfinite(options.runtime_vision_age_scale) ||
            !std::isfinite(
                options.runtime_source_controller_interval_p50_ms) ||
            !std::isfinite(
                options.runtime_source_controller_interval_p95_ms) ||
            options.runtime_source_vision_hz <= 0.0 ||
            options.runtime_requested_vision_hz <= 0.0 ||
            options.runtime_realized_vision_hz <= 0.0 ||
            options.runtime_vision_age_scale <= 0.0 ||
            options.runtime_source_controller_interval_p50_ms <= 0.0 ||
            options.runtime_source_controller_interval_p95_ms <
                options.runtime_source_controller_interval_p50_ms) {
            throw std::invalid_argument(
                "runtime mode requires complete positive timing transform metadata");
        }
        if (options.config_relationship != "matched" &&
            options.config_relationship != "counterfactual") {
            throw std::invalid_argument(
                "config-relationship must be matched or counterfactual");
        }
        if (options.vision_hz != 0 || options.vision_result_delay_ms != 0) {
            throw std::invalid_argument(
                "runtime observation timing cannot be combined with scalar Vision timing");
        }
        if (options.manual_input_scale != 1.0) {
            throw std::invalid_argument(
                "runtime manual input must retain its extracted scale");
        }
        if (!std::isfinite(options.camera_response_px_per_stick_second) ||
            options.camera_response_px_per_stick_second <= 0.0 ||
            !options.slowdown_edge_explicit ||
            !options.slowdown_center_explicit) {
            throw std::invalid_argument(
                "runtime mode requires explicit positive camera and slowdown plant values");
        }
        if (options.plant_source != "measured" &&
            options.plant_source != "inferred" &&
            options.plant_source != "assumption") {
            throw std::invalid_argument(
                "plant-source must be measured, inferred, or assumption");
        }
    }
    return options;
}

ManualProfile parse_profile(const std::string& value) {
    if (value == "pure") return ManualProfile::Pure;
    if (value == "mixed") return ManualProfile::Mixed;
    if (value == "scripted") return ManualProfile::Scripted;
    if (value == "runtime") return ManualProfile::RuntimeProfile;
    if (value == "wrong-then-correct") return ManualProfile::WrongThenCorrect;
    if (value == "arc-recovery") return ManualProfile::ArcRecovery;
    if (value == "micro-correction") return ManualProfile::MicroCorrection;
    if (value == "recoil-controller") return ManualProfile::RecoilController;
    if (value == "recoil-flail") return ManualProfile::RecoilFlail;
    if (value == "obsolete-after-crossing") {
        return ManualProfile::ObsoleteAfterCrossing;
    }
    throw std::invalid_argument("invalid profile: " + value);
}

std::vector<BenchmarkCohort> parse_cohorts(const std::string& value) {
    if (value == "ads") return {BenchmarkCohort::AdsAcquire};
    if (value == "bodylock") return {BenchmarkCohort::BodyLockFollow};
    if (value == "both") {
        return {
            BenchmarkCohort::AdsAcquire,
            BenchmarkCohort::BodyLockFollow,
        };
    }
    throw std::invalid_argument("invalid cohort: " + value);
}

std::vector<PlayerStrafeMode> parse_strafe_modes(const std::string& value) {
    if (value == "off") return {PlayerStrafeMode::Off};
    if (value == "full-reversal") return {PlayerStrafeMode::FullReversal};
    if (value == "both") {
        return {PlayerStrafeMode::Off, PlayerStrafeMode::FullReversal};
    }
    throw std::invalid_argument("invalid left-strafe mode: " + value);
}

std::vector<PlayerVerticalMotionMode> parse_vertical_modes(
    const std::string& value) {
    if (value == "off") return {PlayerVerticalMotionMode::Off};
    if (value == "slide") return {PlayerVerticalMotionMode::Slide};
    if (value == "jump") return {PlayerVerticalMotionMode::Jump};
    if (value == "random") return {PlayerVerticalMotionMode::Random};
    if (value == "all") {
        return {
            PlayerVerticalMotionMode::Off,
            PlayerVerticalMotionMode::Slide,
            PlayerVerticalMotionMode::Jump,
            PlayerVerticalMotionMode::Random,
        };
    }
    throw std::invalid_argument("invalid vertical-motion mode: " + value);
}

VisionDisturbanceProfile parse_vision_disturbance(
    const std::string& value) {
    if (value == "off") return VisionDisturbanceProfile::Off;
    if (value == "gun-kick") return VisionDisturbanceProfile::GunKick;
    if (value == "gun-kick-adversarial") {
        return VisionDisturbanceProfile::GunKickAdversarial;
    }
    if (value == "camera-recoil") {
        return VisionDisturbanceProfile::CameraRecoil;
    }
    if (value == "gun-kick-plus-recoil") {
        return VisionDisturbanceProfile::GunKickAndCameraRecoil;
    }
    if (value == "body-box-deformation") {
        return VisionDisturbanceProfile::BodyBoxDeformation;
    }
    if (value == "body-box-plus-recoil") {
        return VisionDisturbanceProfile::BodyBoxDeformationAndCameraRecoil;
    }
    if (value == "horizontal-aim-bias") {
        return VisionDisturbanceProfile::HorizontalAimBiasRecovery;
    }
    if (value == "target-dropout-decoy") {
        return VisionDisturbanceProfile::TargetDropoutDecoy;
    }
    throw std::invalid_argument("invalid vision-disturbance: " + value);
}

const char* name(ManualProfile value) {
    switch (value) {
    case ManualProfile::Pure: return "pure";
    case ManualProfile::Mixed: return "mixed";
    case ManualProfile::Scripted: return "scripted";
    case ManualProfile::ObsoleteAfterCrossing: return "obsolete-after-crossing";
    case ManualProfile::WrongThenCorrect: return "wrong-then-correct";
    case ManualProfile::ArcRecovery: return "arc-recovery";
    case ManualProfile::MicroCorrection: return "micro-correction";
    case ManualProfile::RecoilController: return "recoil-controller";
    case ManualProfile::RecoilFlail: return "recoil-flail";
    case ManualProfile::RuntimeProfile: return "runtime";
    }
    return "unknown";
}

const char* name(BenchmarkCohort value) {
    return value == BenchmarkCohort::AdsAcquire ? "ads" : "bodylock";
}

const char* name(PlayerStrafeMode value) {
    return value == PlayerStrafeMode::Off ? "off" : "full-reversal";
}

const char* name(PlayerVerticalMotionMode value) {
    switch (value) {
    case PlayerVerticalMotionMode::Off: return "off";
    case PlayerVerticalMotionMode::Slide: return "slide";
    case PlayerVerticalMotionMode::Jump: return "jump";
    case PlayerVerticalMotionMode::Random: return "random";
    }
    return "unknown";
}

const char* name(AimResponseCurveAlgorithm value) {
    return value == AimResponseCurveAlgorithm::Linear
        ? "linear"
        : "cod_dynamic_legacy_lut";
}

std::string json_escape(const std::string& value) {
    std::string result;
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') result.push_back('\\');
        result.push_back(ch);
    }
    return result;
}

void write_result(std::ostream& out, const BenchmarkResult& value) {
    out << std::setprecision(12)
        << "{\"seed\":" << value.seed
        << ",\"profile\":\"" << name(value.manual_profile) << "\""
        << ",\"cohort\":\"" << name(value.cohort) << "\""
        << ",\"left_strafe\":\"" << name(value.player_strafe_mode) << "\""
        << ",\"vertical_motion\":\"" << name(value.player_vertical_motion_mode) << "\""
        << ",\"script_hash\":\"" << value.script_hash << "\""
        << ",\"ticks\":" << value.ticks
        << ",\"controller_updates\":" << value.controller_updates
        << ",\"left_strafe_active_ms\":" << value.left_strafe_active_ms
        << ",\"left_strafe_reversals\":" << value.left_strafe_reversals
        << ",\"max_abs_left_x\":" << value.max_abs_left_x
        << ",\"min_sampled_player_top_speed_px_per_second\":"
        << value.min_sampled_player_top_speed_px_per_second
        << ",\"max_sampled_player_top_speed_px_per_second\":"
        << value.max_sampled_player_top_speed_px_per_second
        << ",\"max_abs_player_speed_px_per_second\":" << value.max_abs_player_speed_px_per_second
        << ",\"player_vertical_active_ms\":"
        << value.player_vertical_active_ms
        << ",\"player_slide_events\":" << value.player_slide_events
        << ",\"player_jump_events\":" << value.player_jump_events
        << ",\"max_abs_player_vertical_offset_px\":"
        << value.max_abs_player_vertical_offset_px
        << ",\"max_abs_player_vertical_speed_px_per_second\":"
        << value.max_abs_player_vertical_speed_px_per_second
        << ",\"acquire_points\":" << value.acquire_points
        << ",\"tracking_points\":" << value.tracking_points
        << ",\"smooth_bonus\":" << value.smooth_bonus
        << ",\"targets_spawned\":" << value.targets_spawned
        << ",\"targets_acquired\":" << value.targets_acquired
        << ",\"targets_missed\":" << value.targets_missed
        << ",\"over_events\":" << value.over_events
        << ",\"undertrack_events\":" << value.undertrack_events
        << ",\"false_interruption_events\":" << value.false_interruption_events
        << ",\"false_stop_events\":" << value.false_stop_events
        << ",\"stale_output_after_stop_events\":" << value.stale_output_after_stop_events
        << ",\"bodylock_entry_failures\":" << value.bodylock_entry_failures
        << ",\"bodylock_active_ms\":" << value.bodylock_active_ms
        << ",\"unexpected_mode_ms\":" << value.unexpected_mode_ms
        << ",\"settled_targets\":" << value.settled_targets
        << ",\"unsettled_targets\":" << value.unsettled_targets
        << ",\"center_cross_events\":" << value.center_cross_events
        << ",\"max_post_cross_error_px\":" << value.max_post_cross_error_px
        << ",\"p95_post_cross_error_px\":" << value.p95_post_cross_error_px
        << ",\"overshoot_area_px_ms\":" << value.overshoot_area_px_ms
        << ",\"continued_push_after_cross_ms\":" << value.continued_push_after_cross_ms
        << ",\"correction_reversal_events\":" << value.correction_reversal_events
        << ",\"circle_exit_events\":" << value.circle_exit_events
        << ",\"stall_ring_ms\":" << value.stall_ring_ms
        << ",\"direction_discontinuities\":" << value.direction_discontinuities
        << ",\"fresh_vision_direction_discontinuities\":" << value.fresh_vision_direction_discontinuities
        << ",\"controller_residual_kick_events\":" << value.controller_residual_kick_events
        << ",\"requested_assist_discontinuities\":" << value.requested_assist_discontinuities
        << ",\"shaped_assist_discontinuities\":" << value.shaped_assist_discontinuities
        << ",\"near_center_shaped_wrong_way_ms\":" << value.near_center_shaped_wrong_way_ms
        << ",\"oscillation_episodes\":" << value.oscillation_episodes
        << ",\"oscillation_active_ms\":" << value.oscillation_active_ms
        << ",\"oscillation_output_area\":" << value.oscillation_output_area
        << ",\"max_error_px\":" << value.max_error_px
        << ",\"median_first_entry_to_settle_ms\":" << value.median_first_entry_to_settle_ms
        << ",\"p95_first_entry_to_settle_ms\":" << value.p95_first_entry_to_settle_ms
        << ",\"median_first_assist_output_ms\":" << value.median_first_assist_output_ms
        << ",\"p95_first_assist_output_ms\":" << value.p95_first_assist_output_ms
        << ",\"handoff_count\":" << value.handoff_count
        << ",\"handoff_defect_rate\":" << value.handoff_defect_rate
        << ",\"p95_post_handoff_rebound_px\":" << value.p95_post_handoff_rebound_px
        << ",\"max_post_handoff_rebound_px\":" << value.max_post_handoff_rebound_px
        << ",\"post_occlusion_error_area_px_ms\":" << value.post_occlusion_error_area_px_ms
        << ",\"max_post_occlusion_error_px\":" << value.max_post_occlusion_error_px
        << ",\"p95_reveal_to_stable_ms\":" << value.p95_reveal_to_stable_ms
        << ",\"mean_error_px\":" << value.mean_error_px
        << ",\"p95_error_px\":" << value.p95_error_px
        << ",\"p95_output_delta\":" << value.p95_output_delta
        << ",\"p95_jerk\":" << value.p95_jerk
        << ",\"p95_controller_residual_delta\":" << value.p95_controller_residual_delta
        << ",\"p95_controller_residual_jerk\":" << value.p95_controller_residual_jerk
        << ",\"p99_controller_residual_delta\":" << value.p99_controller_residual_delta
        << ",\"max_controller_residual_delta\":" << value.max_controller_residual_delta
        << '}';
}

void write_report(
    std::ostream& out,
    const Options& options,
    const BenchmarkConfig& config,
    const std::vector<BenchmarkResult>& results) {
    const bool runtime_profile = !options.runtime_profile_sha256.empty();
    std::size_t runtime_manual_sample_count = 0;
    for (const auto& segment : config.runtime_manual_segments) {
        runtime_manual_sample_count += segment.samples.size();
    }
    out << std::setprecision(12)
        << "{\n"
        << "  \"schema\": \"sustained-aimlab-"
        << (options.controller == "production" ? "production" : "ab")
        << "-v1\",\n"
        << "  \"revision\": \"" << json_escape(options.revision) << "\",\n"
        << "  \"dirty\": " << (options.dirty ? "true" : "false") << ",\n"
        << "  \"config_path\": \"" << json_escape(options.config_path.string()) << "\",\n"
        << "  \"simulator\": {\"duration_ms\":" << config.duration_ms
        << ",\"tick_ms\":" << config.tick_ms
        << ",\"plant_tick_ms\":1"
        << ",\"controller_tick_ms\":" << config.tick_ms
        << ",\"controller_tick_hz\":" << options.controller_tick_hz
        << ",\"controller\":{\"mode\":\"fixed_requested\""
        << ",\"output_hold\":\"zero_order_hold\""
        << ",\"source_interval_p50_ms\":"
        << (runtime_profile
                ? options.runtime_source_controller_interval_p50_ms : 0.0)
        << ",\"source_interval_p95_ms\":"
        << (runtime_profile
                ? options.runtime_source_controller_interval_p95_ms : 0.0)
        << ",\"source_hz_estimate\":"
        << (runtime_profile
                ? 1000.0 /
                    options.runtime_source_controller_interval_p50_ms
                : 0.0)
        << ",\"requested_hz\":" << options.controller_tick_hz
        << ",\"tick_ms\":" << config.tick_ms << "}"
        << ",\"manual_sample_hz\":" << options.controller_tick_hz
        << ",\"final_arbitration_hz\":" << options.controller_tick_hz
        << ",\"recoil_hz\":" << options.controller_tick_hz
        << ",\"output_hz\":" << options.controller_tick_hz
        << ",\"ai_proposal\":{\"scope\":\"controller_pipeline\""
        << ",\"mode\":\"lockstep\""
        << ",\"requested_hz\":" << options.controller_tick_hz
        << ",\"update_stage\":\"assist_solver_and_dynamics\""
        << ",\"dynamics_hz\":" << options.controller_tick_hz
        << ",\"hold\":\"none\""
        << ",\"target_plan_and_safety_hz\":"
        << options.controller_tick_hz
        << "}"
        << ",\"vision_interval_ms\":" << config.vision_interval_ms
        << ",\"vision_hz_requested\":"
        << (runtime_profile
                ? options.runtime_requested_vision_hz
                : static_cast<double>(options.vision_hz))
        << ",\"vision\":{"
        << "\"mode\":\""
        << (runtime_profile
                ? json_escape(options.runtime_vision_mode)
                : "synthetic_fixture")
        << "\",\"source_hz\":"
        << (runtime_profile ? options.runtime_source_vision_hz : 0.0)
        << ",\"requested_hz\":"
        << (runtime_profile ? options.runtime_requested_vision_hz
                            : static_cast<double>(options.vision_hz))
        << ",\"realized_hz\":"
        << (runtime_profile ? options.runtime_realized_vision_hz
                            : static_cast<double>(options.vision_hz))
        << ",\"capture_age_scale\":"
        << (runtime_profile ? options.runtime_vision_age_scale : 1.0)
        << ",\"controller_delivery\":\"latest_only_latched\""
        << "}"
        << ",\"vision_result_delay_ms\":" << config.vision_result_delay_ms
        << ",\"control_response_delay_ms\":"
        << config.control_response_delay_ms
        << ",\"short_occlusion_ms\":" << config.short_occlusion_duration_ms
        << ",\"target_motion\":\"" << options.target_motion << "\""
        << ",\"target_motion_preset\":\""
        << (options.target_motion_preset.empty()
                ? (options.target_motion == "moving"
                    ? "seeded-legacy" : "stationary")
                : json_escape(options.target_motion_preset))
        << "\""
        << ",\"pov_motion_preset\":\""
        << (options.pov_motion_preset.empty()
                ? "custom" : json_escape(options.pov_motion_preset))
        << "\""
        << ",\"target_trajectory_source\":\"algorithmic_preset\""
        << ",\"left_strafe_request\":\"" << options.left_strafe << "\""
        << ",\"observation_source\":\""
        << (runtime_profile
                ? json_escape(options.runtime_vision_mode)
                : "synthetic_fixture")
        << "\""
        << ",\"manual_source\":\""
        << (runtime_profile ? "audited_runtime_profile" : "named_fixture")
        << "\""
        << ",\"plant\":{\"base_camera_response_px_per_stick_second\":"
        << config.camera_response_px_per_stick_second
        << ",\"sensitivity_multiplier\":"
        << config.sensitivity_multiplier
        << ",\"camera_response_px_per_stick_second\":"
        << config.camera_response_px_per_stick_second *
            config.sensitivity_multiplier
        << ",\"response_curve\":\""
        << name(config.camera_response_curve.algorithm) << "\""
        << ",\"body_aim_height_ratio\":"
        << config.body_aim_height_ratio
        << ",\"slowdown_edge_multiplier\":"
        << config.slowdown_edge_multiplier
        << ",\"slowdown_center_multiplier\":"
        << config.slowdown_center_multiplier
        << ",\"source\":\""
        << (runtime_profile
                ? json_escape(options.plant_source)
                : "historical_synthetic_default")
        << "\",\"telemetry_status\":\""
        << (runtime_profile ? "unavailable" : "not_applicable")
        << "\"}},\n"
        << "  \"runtime_profile\": ";
    if (runtime_profile) {
        out << "{\"id\":\"" << json_escape(options.runtime_profile_id)
            << "\",\"profile_payload_sha256\":\""
            << options.runtime_profile_sha256
            << "\",\"audit_artifact_sha256\":\""
            << options.runtime_audit_sha256
            << "\",\"source_runtime_sha256\":\""
            << options.runtime_source_runtime_sha256
            << "\",\"source_config_sha256\":\""
            << options.runtime_source_config_sha256
            << "\",\"source_engine_sha256\":\""
            << options.runtime_source_engine_sha256
            << "\",\"config_relationship\":\""
            << options.config_relationship
            << "\",\"benchmark_config_file_sha256\":\""
            << options.benchmark_config_file_sha256
            << "\",\"benchmark_executable_sha256\":\""
            << options.benchmark_executable_sha256
            << "\",\"observation_samples\":"
            << config.runtime_observation_pattern.size()
            << ",\"manual_segments\":"
            << config.runtime_manual_segments.size()
            << ",\"manual_samples\":" << runtime_manual_sample_count
            << ",\"target_samples\":"
            << config.runtime_target_samples.size()
            << ",\"not_exact_replay\":true}";
    } else {
        out << "null";
    }
    out << ",\n"
        << "  \"control_path\": \"" << options.controller << "\",\n"
        << "  \"pid\": {\"kp\":" << options.pid.kp
        << ",\"ki\":" << options.pid.ki
        << ",\"kd\":" << options.pid.kd
        << ",\"derivative_filter_tau_ms\":"
        << options.pid.derivative_filter_tau_ms
        << ",\"output_filter_tau_ms\":"
        << options.pid.output_filter_tau_ms
        << ",\"max_assist\":" << options.pid.max_assist << "},\n"
        << "  \"runs\": [\n";
    for (std::size_t index = 0; index < results.size(); ++index) {
        out << "    ";
        write_result(out, results[index]);
        out << (index + 1 == results.size() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
}

void validate_smoke(
    const BenchmarkResult& result,
    const AssistedModeCoverage& coverage,
    int expected_ticks,
    int expected_controller_updates) {
    if (result.ticks != expected_ticks ||
        result.controller_updates != expected_controller_updates ||
        !std::isfinite(result.acquire_points) ||
        !std::isfinite(result.tracking_points) ||
        !std::isfinite(result.mean_error_px) ||
        !coverage.saw_assisted_mode) {
        throw std::runtime_error("production-path smoke validation failed");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_options(argc, argv);
        if (!options.target_motion_preset.empty()) {
            if (options.target_motion_preset == "stationary") {
                options.target_motion = "stationary";
            } else if (options.target_motion_preset == "seeded-legacy") {
                options.target_motion = "moving";
            } else {
                throw std::invalid_argument(
                    "invalid target-motion-preset: " +
                    options.target_motion_preset);
            }
        }
        if (!options.pov_motion_preset.empty()) {
            if (options.pov_motion_preset == "off") {
                options.left_strafe = "off";
                options.vertical_motion = "off";
            } else if (options.pov_motion_preset == "seeded-strafe") {
                options.left_strafe = "full-reversal";
                options.vertical_motion = "off";
            } else if (options.pov_motion_preset == "seeded-vertical") {
                options.left_strafe = "off";
                options.vertical_motion = "random";
            } else if (options.pov_motion_preset == "seeded-combined") {
                options.left_strafe = "full-reversal";
                options.vertical_motion = "random";
            } else {
                throw std::invalid_argument(
                    "invalid pov-motion-preset: " +
                    options.pov_motion_preset);
            }
        }
        const ManualProfile profile = parse_profile(options.profile);
        const auto cohorts = parse_cohorts(options.cohort);
        const auto strafe_modes = parse_strafe_modes(options.left_strafe);
        const auto vertical_modes = parse_vertical_modes(options.vertical_motion);
        if (options.target_motion != "moving" &&
            options.target_motion != "stationary") {
            throw std::invalid_argument(
                "invalid target-motion: " + options.target_motion);
        }
        if (options.benchmark_recoil != "on" &&
            options.benchmark_recoil != "off") {
            throw std::invalid_argument(
                "invalid benchmark-recoil: " + options.benchmark_recoil);
        }

        const RuntimeConfig runtime = load_runtime_config(options.config_path);
        BenchmarkConfig benchmark;
        benchmark.duration_ms = options.duration_ms;
        benchmark.tick_ms = 1000 / options.controller_tick_hz;
        benchmark.sensitivity_multiplier = options.sensitivity_multiplier;
        benchmark.scenario_profile = options.scenario == "compound_directional"
            ? ScenarioProfile::CompoundDirectional : ScenarioProfile::Baseline;
        benchmark.target_profile = options.target_profile == "small"
            ? TargetProfile::SmallVisible
            : options.target_profile == "near"
                ? TargetProfile::NearCrosshair : TargetProfile::Ordinary;
        benchmark.vision_interval_ms = options.vision_hz > 0
            ? std::max(1, static_cast<int>(std::lround(
                1000.0 / static_cast<double>(options.vision_hz))))
            : 0;
        benchmark.vision_result_delay_ms = options.vision_result_delay_ms;
        benchmark.control_response_delay_ms = options.control_response_delay_ms;
        benchmark.short_occlusion_duration_ms = options.short_occlusion_ms;
        benchmark.fixed_target_slot_ms = options.target_slot_ms;
        benchmark.manual_input_scale = options.manual_input_scale;
        benchmark.slowdown_edge_multiplier = options.slowdown_edge;
        benchmark.slowdown_center_multiplier = options.slowdown_center;
        benchmark.body_aim_height_ratio =
            runtime.gamepad.tracker.aim_height_ratio;
        if (profile == ManualProfile::RuntimeProfile) {
            benchmark.camera_response_px_per_stick_second =
                options.camera_response_px_per_stick_second;
            benchmark.runtime_observation_pattern =
                options.runtime_observation_pattern;
            benchmark.runtime_manual_segments = options.runtime_manual_segments;
            benchmark.runtime_target_samples = options.runtime_target_samples;
        }
        benchmark.target_motion_enabled = options.target_motion == "moving";
        benchmark.vision_disturbance =
            parse_vision_disturbance(options.vision_disturbance);
        benchmark.camera_response_curve = runtime.gamepad.aim_response_curve;

        std::vector<BenchmarkResult> results;
        for (const std::uint32_t seed : options.seeds) {
            const ScenarioScript script = generate_script(seed, benchmark);
            for (const BenchmarkCohort cohort : cohorts) {
                for (const PlayerStrafeMode strafe : strafe_modes) {
                    for (const PlayerVerticalMotionMode vertical : vertical_modes) {
                        auto coverage = std::make_shared<AssistedModeCoverage>();
                        const auto controller = options.controller == "pid"
                            ? make_pid_controller(options.pid, cohort)
                            : make_native_controller(
                                runtime.gamepad,
                                cohort,
                                coverage,
                                options.benchmark_recoil == "on");
                        BenchmarkResult result = run_simulation(
                            script,
                            profile,
                            controller,
                            cohort,
                            {},
                            strafe,
                            vertical);
                        if (options.smoke && options.controller == "production") {
                            validate_smoke(
                                result,
                                *coverage,
                                options.duration_ms,
                                (options.duration_ms + benchmark.tick_ms - 1) /
                                    benchmark.tick_ms);
                        }
                        if (profile == ManualProfile::RecoilController ||
                            profile == ManualProfile::RecoilFlail) {
                            const auto& c = *coverage;
                            std::cerr
                                << "  [op-diag] profile=" << name(profile)
                                << " frames=" << c.total_frames
                                << " firing=" << c.firing_frames
                                << " pull_class=" << c.recoil_pull_frames
                                << " unreliable_class=" << c.unreliable_frames
                                << " recoil_active=" << c.recoil_active_frames
                                << "\n";
                        }
                        std::cout
                            << "seed=" << seed
                            << " controller=" << options.controller
                            << " cohort=" << name(cohort)
                            << " strafe=" << name(strafe)
                            << " score="
                            << result.acquire_points + result.tracking_points +
                                result.smooth_bonus
                            << " mean_error_px=" << result.mean_error_px
                            << " p95_error_px=" << result.p95_error_px
                            << "\n";
                        results.push_back(std::move(result));
                    }
                }
            }
        }

        if (options.output_path.empty()) {
            write_report(std::cout, options, benchmark, results);
        } else {
            if (!options.output_path.parent_path().empty()) {
                std::filesystem::create_directories(
                    options.output_path.parent_path());
            }
            std::ofstream output(options.output_path);
            if (!output) {
                throw std::runtime_error(
                    "failed to open output: " + options.output_path.string());
            }
            write_report(output, options, benchmark, results);
        }
        std::cout << "cod_native_sustained_aimlab_benchmark PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "cod_native_sustained_aimlab_benchmark FAIL: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
