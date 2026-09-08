#pragma once
#include "mouse_native/mouse_controller_session.h"
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace mouse_native {
struct MouseDiagnosticContext {
    std::uint64_t tick = 0, begin_ns = 0, end_ns = 0;
    std::uint64_t vision_sequence = 0, vision_frame = 0, capture_ns = 0, result_ns = 0;
    std::uint64_t accepted_frame = 0;
    bool vision_accepted = false;
};
struct MouseDiagnosticRecord {
    MouseDiagnosticContext clock{};
    MouseVirtualHidTransport::WindowInfo window{};
    std::uint64_t target = 0, generation = 0, control_frame = 0, profile_generation = 0;
    MouseSourceCounts source{}, aim{}, final{};
    float manual[2]{}, filtered[2]{}, requested[2]{}, shaped[2]{}, total_u[2]{}, retention[2]{};
    float error[2]{}, desired[2]{}, box[4]{}, authority = 0, source_age_ms = 0, dt_ms = 0;
    float aim_authority = 0, bodylock_range_px = 0;
    float point_tolerance_px = 0;
    float source_aim[2]{}, position_u[2]{}, motion_u[2]{}, effective_motion_u[2]{};
    std::uint64_t selector_generation = 0;
    bool point_inside[2]{};
    int decision_reason = 0;
    double residual[2]{}, px_per_count[2]{}, recoil_requested = 0, recoil_remainder = 0;
    int recoil_dy = 0;
    unsigned long transport_error = 0;
    bool rmb = false, lmb = false, auto_fire = false, recoil = false, recoil_clock_gap = false;
    bool controller_used = false, transparent = false, delivered = false, failed = false;
    bool correction[2]{}, handover = false, calibration = false;
    int calibration_failure = 0, output_kind = 0;
    char mode[32]{}, phase[32]{}, lifecycle[40]{}, limit[64]{}, conflict_x[24]{}, conflict_y[24]{};
    char desired_point_source[32]{};
};
MouseDiagnosticRecord mouse_diagnostic_record(const MouseControllerSession& session,
    const MouseControllerSessionTickResult& tick, MouseDiagnosticContext clock) noexcept;

struct MouseDiagnosticOptions {
    bool enabled = true;
    std::filesystem::path directory = "runs/mouse";
    std::size_t capacity = 8192;
    std::uint64_t max_bytes = 2048ull * 1024 * 1024;
    std::uint64_t segment_bytes = 64ull * 1024 * 1024;
    bool start_writer = true; // Deterministic queue overflow test, never a runtime knob.
};
struct MouseDiagnosticCounters {
    std::uint64_t accepted = 0, written = 0, dropped = 0, discarded = 0;
    bool failed = false, budget_exhausted = false;
};
// One controller producer, one file writer. Producer only copies a fixed-size
// record into a preallocated SPSC ring; no file I/O, allocation or wait there.
class MouseDiagnostics {
public:
    MouseDiagnostics(MouseDiagnosticOptions options, const std::string& metadata_json);
    ~MouseDiagnostics();
    bool enqueue(const MouseDiagnosticRecord& record) noexcept;
    void stop(bool clean = false);
    MouseDiagnosticCounters counters() const noexcept;
    const std::filesystem::path& directory() const noexcept { return directory_; }
private:
    void writer_loop() noexcept;
    void open_segment();
    MouseDiagnosticOptions options_;
    std::filesystem::path directory_;
    std::ofstream output_;
    std::vector<MouseDiagnosticRecord> queue_;
    std::atomic<std::uint64_t> head_{0}, tail_{0}, dropped_{0}, written_{0}, discarded_{0};
    std::atomic<bool> stopping_{false}, failed_{false}, exhausted_{false};
    std::thread writer_;
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::uint64_t bytes_ = 0, segment_size_ = 0, segment_ = 0;
    bool stopped_ = false;
};
}
