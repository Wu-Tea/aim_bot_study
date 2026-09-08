#include "mouse_native/mouse_diagnostics.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mouse_native {
namespace {
template<std::size_t N> void label(char (&to)[N], const char* from) noexcept {
    if (!from) return;
    std::strncpy(to, from, N - 1); to[N - 1] = 0;
}
double finite(double value) { return std::isfinite(value) ? value : 0; }
template<class T, std::size_t N> void array(std::ostream& out, const T (&values)[N]) {
    out << '[';
    for (std::size_t i = 0; i < N; ++i) { if (i) out << ','; out << finite(values[i]); }
    out << ']';
}
void counts(std::ostream& out, MouseSourceCounts v) { out << '[' << v.dx << ',' << v.dy << ']'; }
std::string serialize(const MouseDiagnosticRecord& r) {
    std::ostringstream o; o << std::setprecision(8);
    o << "{\"type\":\"tick\",\"tick\":" << r.clock.tick
      << ",\"ns\":" << r.clock.begin_ns << ",\"end_ns\":" << r.clock.end_ns
      << ",\"dt_ms\":" << finite(r.dt_ms)
      << ",\"vision_sequence\":" << r.clock.vision_sequence
      << ",\"vision_frame\":" << r.clock.vision_frame
      << ",\"capture_ns\":" << r.clock.capture_ns << ",\"result_ns\":" << r.clock.result_ns
      << ",\"accepted_frame\":" << r.clock.accepted_frame << ",\"vision_accepted\":" << r.clock.vision_accepted
      << ",\"control_frame\":" << r.control_frame << ",\"target\":" << r.target
      << ",\"generation\":" << r.generation << ",\"source_age_ms\":" << finite(r.source_age_ms)
      << ",\"mode\":" << std::quoted(r.mode) << ",\"phase\":" << std::quoted(r.phase)
      << ",\"lifecycle\":" << std::quoted(r.lifecycle) << ",\"limit\":" << std::quoted(r.limit)
      << ",\"aim_authority\":" << finite(r.aim_authority)
      << ",\"bodylock_range_px\":" << finite(r.bodylock_range_px)
      << ",\"point_tolerance_px\":" << finite(r.point_tolerance_px)
      << ",\"selector_generation\":" << r.selector_generation
      << ",\"decision_reason\":" << r.decision_reason
      << ",\"source\":"; counts(o,r.source); o << ",\"aim\":"; counts(o,r.aim);
    o << ",\"final\":"; counts(o,r.final);
    o << ",\"manual_u\":"; array(o,r.manual); o << ",\"filtered_u\":"; array(o,r.filtered);
    o << ",\"requested_u\":"; array(o,r.requested); o << ",\"shaped_u\":"; array(o,r.shaped);
    o << ",\"total_u\":"; array(o,r.total_u); o << ",\"retention\":"; array(o,r.retention);
    o << ",\"conflict\":[" << std::quoted(r.conflict_x) << ',' << std::quoted(r.conflict_y) << ']';
    o << ",\"error_px\":"; array(o,r.error); o << ",\"desired_px\":"; array(o,r.desired);
    o << ",\"source_aim_px\":"; array(o,r.source_aim);
    o << ",\"desired_point_source\":" << std::quoted(r.desired_point_source);
    o << ",\"point_inside\":"; array(o,r.point_inside);
    o << ",\"bodylock_position_u\":"; array(o,r.position_u);
    o << ",\"bodylock_motion_u\":"; array(o,r.motion_u);
    o << ",\"bodylock_effective_motion_u\":"; array(o,r.effective_motion_u);
    o << ",\"box_xywh\":"; array(o,r.box); o << ",\"authority\":" << finite(r.authority);
    o << ",\"correction\":"; array(o,r.correction); o << ",\"handover\":" << r.handover;
    o << ",\"residual\":"; array(o,r.residual); o << ",\"px_per_count\":"; array(o,r.px_per_count);
    o << ",\"profile_generation\":" << r.profile_generation
      << ",\"rmb\":" << r.rmb << ",\"lmb\":" << r.lmb << ",\"auto_fire\":" << r.auto_fire
      << ",\"recoil\":" << r.recoil << ",\"recoil_dy\":" << r.recoil_dy
      << ",\"recoil_requested\":" << finite(r.recoil_requested)
      << ",\"recoil_remainder\":" << finite(r.recoil_remainder) << ",\"recoil_clock_gap\":" << r.recoil_clock_gap
      << ",\"controller\":" << r.controller_used << ",\"transparent\":" << r.transparent
      << ",\"calibration\":" << r.calibration << ",\"calibration_failure\":" << r.calibration_failure
      << ",\"output_kind\":" << r.output_kind << ",\"delivered\":" << r.delivered
      << ",\"failed\":" << r.failed << ",\"transport_error\":" << r.transport_error;
    const auto& w = r.window;
    o << ",\"window\":[" << w.token.epoch << ',' << w.token.sequence << ',' << w.source_begin << ',' << w.source_end << ']'
      << ",\"window_ns\":[" << w.first_source_ns << ',' << w.cutoff_ns << ',' << w.submitted_ns << ']'
      << ",\"submitted\":"; counts(o,w.submitted_counts);
    o << ",\"cancelled\":"; counts(o,w.cancelled_counts);
    o << ",\"reports\":" << w.reports_submitted << ",\"committed\":" << w.committed
      << ",\"window_cancelled\":" << w.cancelled << ",\"buttons\":" << w.physical_buttons
      << ",\"wheel\":[" << w.wheel << ',' << w.hwheel << "]}\n";
    return o.str();
}
}

MouseDiagnosticRecord mouse_diagnostic_record(const MouseControllerSession& session,
    const MouseControllerSessionTickResult& tick, MouseDiagnosticContext clock) noexcept {
    MouseDiagnosticRecord r; r.clock = clock; r.window = session.transport_window();
    const auto& c = tick.runtime.controller;
    const auto& controller = session.runtime().facade().controller();
    const auto& p = controller.last_target_plan();
    const auto& d = controller.last_output_components();
    r.source = tick.source_counts; r.aim = c.aim_counts; r.final = tick.runtime.output.counts;
    r.target = p.target_id; r.generation = p.generation; r.control_frame = p.source_frame_id;
    r.dt_ms = c.dt_seconds * 1000; r.source_age_ms = p.source_capture_age_ms;
    r.manual[0] = c.manual_input.x; r.manual[1] = c.manual_input.y;
    r.filtered[0] = d.filtered_manual_stick.x; r.filtered[1] = d.filtered_manual_stick.y;
    r.requested[0] = d.requested_assist_stick.x; r.requested[1] = d.requested_assist_stick.y;
    r.shaped[0] = d.shaped_assist_stick.x; r.shaped[1] = d.shaped_assist_stick.y;
    r.total_u[0] = c.final_u_x; r.total_u[1] = c.final_u_y;
    r.retention[0] = d.mouse_manual_retention.x; r.retention[1] = d.mouse_manual_retention.y;
    r.error[0] = p.error_px.x; r.error[1] = p.error_px.y;
    r.desired[0] = p.aim_px.x; r.desired[1] = p.aim_px.y;
    r.box[0] = p.aim_region_px.x; r.box[1] = p.aim_region_px.y;
    r.box[2] = p.aim_region_px.w; r.box[3] = p.aim_region_px.h;
    r.authority = p.visual_authority;
    r.aim_authority=p.aim_authority;
    r.bodylock_range_px=session.runtime().facade().bodylock_effective_range_px();
    r.point_tolerance_px=session.runtime().facade().bodylock_point_tolerance_px();
    r.selector_generation=p.selector_target_generation;
    r.source_aim[0]=p.source_aim_px.x; r.source_aim[1]=p.source_aim_px.y;
    r.position_u[0]=d.bodylock_position_stick.x; r.position_u[1]=d.bodylock_position_stick.y;
    r.motion_u[0]=d.bodylock_motion_stick.x; r.motion_u[1]=d.bodylock_motion_stick.y;
    r.effective_motion_u[0]=d.bodylock_effective_motion_stick.x;
    r.effective_motion_u[1]=d.bodylock_effective_motion_stick.y;
    for(int axis=0;axis<2;++axis) r.point_inside[axis]=
        p.mode==pipeline_contract::ControlMode::BodyLockFollow && r.point_tolerance_px>0 &&
        std::fabs(r.error[axis])<=r.point_tolerance_px;
    label(r.desired_point_source,d.desired_point_source.c_str());
    r.decision_reason=static_cast<int>(p.ads_decision_reason);
    r.correction[0] = p.manual_correction_x; r.correction[1] = p.manual_correction_y;
    r.handover = d.handover_requested;
    r.residual[0] = c.aim_residual_x; r.residual[1] = c.aim_residual_y;
    const auto& profile = session.runtime().effective_profile(tick.runtime.mode);
    r.profile_generation = profile.generation;
    r.px_per_count[0] = profile.px_per_count_x; r.px_per_count[1] = profile.px_per_count_y;
    r.rmb = session.right_button_down(); r.lmb = session.left_button_down(); r.auto_fire = c.auto_fire_active;
    r.recoil = c.recoil.active; r.recoil_dy = c.recoil.dy;
    r.recoil_requested = c.recoil.requested_counts; r.recoil_remainder = c.recoil.remainder;
    r.recoil_clock_gap = c.recoil.clock_discontinuity;
    r.controller_used = c.controller_used; r.transparent = c.transparent;
    r.delivered = tick.output_delivered; r.failed = tick.transport_failed; r.transport_error = session.transport_error();
    r.calibration = tick.runtime.calibration_active; r.calibration_failure = static_cast<int>(tick.runtime.calibration_failure);
    r.output_kind = static_cast<int>(tick.runtime.output.kind);
    label(r.mode, d.aim_mode.c_str()); label(r.phase, d.assist_control_phase.c_str());
    label(r.lifecycle, d.bodylock_lifecycle.c_str()); label(r.limit, d.bodylock_constraint_reason.c_str());
    label(r.conflict_x, d.mouse_manual_conflict_x); label(r.conflict_y, d.mouse_manual_conflict_y);
    return r;
}

MouseDiagnostics::MouseDiagnostics(MouseDiagnosticOptions options, const std::string& metadata)
    : options_(std::move(options)) {
    if (!options_.enabled) return;
    if (!options_.capacity || !options_.max_bytes || !options_.segment_bytes)
        throw std::invalid_argument("mouse diagnostic bounds must be positive");
    queue_.resize(options_.capacity);
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::filesystem::create_directories(options_.directory);
    directory_ = options_.directory / (std::to_string(stamp) + "-" + std::to_string(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(directory_)) throw std::runtime_error("mouse diagnostic session already exists");
    std::ofstream manifest(directory_ / "session.json");
    manifest << metadata << '\n'; manifest.flush();
    if (!manifest) throw std::runtime_error("cannot write mouse diagnostic session.json");
    open_segment();
    if (options_.start_writer) writer_ = std::thread([this] { writer_loop(); });
}
MouseDiagnostics::~MouseDiagnostics() { stop(); }
void MouseDiagnostics::open_segment() {
    output_.close();
    std::ostringstream name; name << "ticks-" << std::setfill('0') << std::setw(4) << segment_++ << ".jsonl";
    output_.open(directory_ / name.str(), std::ios::binary);
    if (!output_) throw std::runtime_error("cannot open mouse tick log");
    segment_size_ = 0;
}
bool MouseDiagnostics::enqueue(const MouseDiagnosticRecord& record) noexcept {
    if (!options_.enabled) return false;
    if (stopping_.load() || failed_.load() || exhausted_.load()) { ++dropped_; return false; }
    const auto head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) >= queue_.size()) { ++dropped_; return false; }
    queue_[head % queue_.size()] = record;
    head_.store(head + 1, std::memory_order_release);
    // The writer drains every 20 ms. No mutex or OS notification in the tick.
    return true;
}
void MouseDiagnostics::writer_loop() noexcept {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    auto next_flush = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    try {
        for (;;) {
            auto tail = tail_.load(std::memory_order_relaxed);
            const auto head = head_.load(std::memory_order_acquire);
            if (tail == head) {
                if (stopping_.load()) break;
                std::unique_lock<std::mutex> lock(wait_mutex_);
                wake_.wait_for(lock, std::chrono::milliseconds(20));
            }
            for (; tail < head; ++tail) {
                const auto line = serialize(queue_[tail % queue_.size()]);
                tail_.store(tail + 1, std::memory_order_release);
                if (bytes_ + line.size() > options_.max_bytes) { exhausted_ = true; ++discarded_; continue; }
                if (segment_size_ && segment_size_ + line.size() > options_.segment_bytes) open_segment();
                output_ << line;
                if (!output_) throw std::runtime_error("mouse diagnostic write failed");
                bytes_ += line.size(); segment_size_ += line.size(); ++written_;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_flush) {
                output_.flush(); if (!output_) throw std::runtime_error("mouse diagnostic flush failed");
                next_flush = now + std::chrono::seconds(1);
            }
        }
        output_.flush(); if (!output_) failed_ = true;
    } catch (...) { failed_ = true; }
}
MouseDiagnosticCounters MouseDiagnostics::counters() const noexcept {
    return {head_.load(), written_.load(), dropped_.load(), discarded_.load(), failed_.load(), exhausted_.load()};
}
void MouseDiagnostics::stop(bool clean) {
    if (stopped_ || !options_.enabled) return;
    stopping_ = true; wake_.notify_one();
    if (writer_.joinable()) writer_.join();
    discarded_ = head_.load() - written_.load();
    output_.close();
    const auto c = counters();
    std::ofstream summary(directory_ / "summary.json");
    summary << "{\"schema\":1,\"clean_shutdown\":" << clean << ",\"accepted\":" << c.accepted
        << ",\"written\":" << c.written << ",\"dropped\":" << c.dropped << ",\"discarded\":" << c.discarded
        << ",\"writer_failed\":" << c.failed << ",\"budget_exhausted\":" << c.budget_exhausted
        << ",\"bytes\":" << bytes_ << "}\n";
    summary.flush(); if (!summary) failed_ = true;
    stopped_ = true;
}
}
