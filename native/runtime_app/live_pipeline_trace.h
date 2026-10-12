#pragma once

#include "vision_native/types.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <thread>

namespace runtime_app {

// Versioned, opt-in diagnostics. Zero timestamps mean unavailable, never zero
// latency. V=every producer poll, C=every controller tick. Join by sequence.
#define LIVE_TRACE_UINT_FIELDS(X) \
 X(sequence) X(active_sequence) X(frame_id) X(tick_id) X(input_sequence) \
 X(poll_enter_ns) X(poll_due_ns) X(dispatch_ns) X(poll_return_ns) \
 X(capture_begin_ns) X(capture_acquired_ns) X(capture_copied_ns) \
 X(source_present_ns) X(source_qpc) X(source_qpc_frequency) X(clock_calibration_id) X(clock_uncertainty_ns) \
 X(result_ready_ns) X(publish_wait_ns) X(publish_lock_ns) X(published_ns) X(publish_unlock_ns) \
 X(mailbox_read_begin_ns) X(mailbox_read_end_ns) X(consume_ns) X(controller_submit_ns) \
 X(control_begin_ns) X(control_end_ns) X(relay_submit_ns) X(relay_dequeue_ns) X(device_begin_ns) X(device_end_ns) X(receipt_ns) \
 X(physical_read_ns) X(input_scope) X(request) X(epoch) X(policy_revision) X(producer_updated) X(epoch_current) \
 X(status) X(accumulated_frames) X(boxes) X(selected) X(generation) X(target_changed) \
 X(color_bytes) X(aiming) X(output_enabled) X(output_delivered) X(relay_accepted) X(relay_input_current) X(relay_visual_fresh) X(visual_expired)
#define LIVE_TRACE_REAL_FIELDS(X) \
 X(requested_fps) X(capture_acquire_ms) X(capture_copy_ms) X(cuda_map_ms) X(inference_wall_ms) \
 X(preprocess_gpu_ms) X(infer_gpu_ms) X(enqueue_cpu_ms) X(output_copy_sync_ms) X(output_copy_gpu_ms) X(output_wait_ms) \
 X(decode_ms) X(color_copy_ms) X(cuda_unmap_ms) X(selector_ms) X(target_x) X(target_y) X(left_trigger) X(right_trigger)

struct LiveTraceRecord {
    char lane='V';
#define FIELD(name) std::uint64_t name=0;
    LIVE_TRACE_UINT_FIELDS(FIELD)
#undef FIELD
#define FIELD(name) double name=0;
    LIVE_TRACE_REAL_FIELDS(FIELD)
#undef FIELD
};

inline std::uint64_t trace_now_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

inline void trace_vision_fields(LiveTraceRecord& t, const vision_native::VisionResult& r) noexcept {
    t.frame_id=r.frame_id;t.capture_begin_ns=r.capture_acquire_begin_ns;
    t.capture_acquired_ns=r.capture_acquire_complete_ns;t.capture_copied_ns=r.capture_copy_complete_ns;
    t.source_present_ns=r.source_present_steady_available?r.source_present_steady_ns:0;
    t.source_qpc=r.source_present_qpc;t.source_qpc_frequency=r.source_present_qpc_frequency;
    t.clock_calibration_id=r.source_present_calibration_id;t.clock_uncertainty_ns=r.source_present_calibration_uncertainty_ns;
    t.result_ready_ns=r.result_at_ns;t.accumulated_frames=r.accumulated_frames;
    t.boxes=r.detections.size();t.selected=r.has_target;t.generation=r.selector_target_generation;t.target_changed=r.selector_target_changed;
    t.color_bytes=r.color_copy_bytes;t.capture_acquire_ms=r.capture_acquire_ms;t.capture_copy_ms=r.capture_copy_ms;
    t.cuda_map_ms=r.cuda_map_ms;t.inference_wall_ms=r.inference_wall_ms;
    t.preprocess_gpu_ms=r.preprocess_ms;t.infer_gpu_ms=r.infer_ms;t.enqueue_cpu_ms=r.enqueue_cpu_ms;
    t.output_copy_sync_ms=r.output_copy_sync_ms;t.output_copy_gpu_ms=r.output_copy_ms;t.output_wait_ms=r.output_wait_ms;
    t.decode_ms=r.decode_ms;t.color_copy_ms=r.color_copy_ms;t.cuda_unmap_ms=r.cuda_unmap_ms;t.selector_ms=r.selector_ms;
    t.target_x=r.target_x;t.target_y=r.target_y;
}

template<std::size_t Capacity> class LiveTraceQueue {
public:
    // Each lane has exactly one producer; the writer is its sole consumer.
    bool push(const LiveTraceRecord& record) noexcept {
        const auto head=head_.load(std::memory_order_relaxed);
        if(head-tail_.load(std::memory_order_acquire)>=Capacity){++dropped_;return false;}
        records_[head%Capacity]=record;head_.store(head+1,std::memory_order_release);return true;
    }
    bool pop(LiveTraceRecord& record) noexcept {
        const auto tail=tail_.load(std::memory_order_relaxed);
        if(tail==head_.load(std::memory_order_acquire))return false;
        record=records_[tail%Capacity];tail_.store(tail+1,std::memory_order_release);return true;
    }
    std::uint64_t dropped() const noexcept{return dropped_.load();}
private:
    std::array<LiveTraceRecord,Capacity> records_{};
    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

class LivePipelineTrace {
public:
    static std::unique_ptr<LivePipelineTrace> from_environment() {
        const char* path=std::getenv("AIMLAB_LIVE_TRACE_DIR");
        return path && *path ? std::make_unique<LivePipelineTrace>(path) : nullptr;
    }
    explicit LivePipelineTrace(const std::filesystem::path& root) {
        directory_=root/ std::to_string(trace_now_ns());
        std::filesystem::create_directories(directory_);
        stream_.open(directory_/"pipeline.csv");
        if(!stream_)throw std::runtime_error("cannot open requested live pipeline trace");
        stream_<<"lane";
#define COLUMN(name) stream_<<"," #name;
        LIVE_TRACE_UINT_FIELDS(COLUMN)
        LIVE_TRACE_REAL_FIELDS(COLUMN)
#undef COLUMN
        stream_<<'\n'<<std::setprecision(17);
        const auto before=trace_now_ns();
        const auto utc=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto after=trace_now_ns();
        std::ofstream metadata(directory_/"clock.json");
        metadata<<"{\"schema\":\"live-pipeline-v1\",\"utc_ns\":"<<utc<<",\"steady_ns\":"<<before+(after-before)/2<<",\"uncertainty_ns\":"<<(after-before)/2<<"}\n";
        writer_=std::thread([this]{write_loop();});
    }
    ~LivePipelineTrace(){stop();}
    void vision(const LiveTraceRecord& r) noexcept{vision_.push(r);}
    void controller(const LiveTraceRecord& r) noexcept{controller_.push(r);}
    const std::filesystem::path& directory() const noexcept{return directory_;}
    void stop() noexcept {
        stopping_.store(true);if(writer_.joinable())writer_.join();
    }
private:
    void serialize(const LiveTraceRecord& r) {
        stream_<<r.lane;
#define CELL(name) stream_<<','<<r.name;
        LIVE_TRACE_UINT_FIELDS(CELL)
        LIVE_TRACE_REAL_FIELDS(CELL)
#undef CELL
        stream_<<'\n';++written_;
    }
    void write_loop() noexcept {
        try {
            auto flushed=std::chrono::steady_clock::now();
            for(;;){
                LiveTraceRecord r;bool work=false;
                // Bounded interleaving prevents a busy lane starving another.
                for(unsigned i=0;i<256;++i){bool any=false;if(vision_.pop(r)){serialize(r);any=true;}if(controller_.pop(r)){serialize(r);any=true;}work|=any;if(!any)break;}
                if(!stream_){failed_=true;break;}
                const auto now=std::chrono::steady_clock::now();
                if(now-flushed>=std::chrono::milliseconds(250)){stream_.flush();flushed=now;}
                if(stopping_.load()&&!work)break;
                if(!work)std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            stream_.flush();if(!stream_)failed_=true;
            std::ofstream completion(directory_/"completion.json");
            completion<<"{\"schema\":\"live-pipeline-v1\",\"written\":"<<written_
                <<",\"vision_dropped\":"<<vision_.dropped()<<",\"controller_dropped\":"<<controller_.dropped()
                <<",\"writer_failed\":"<<(failed_?"true":"false")<<",\"complete\":"<<(failed_?"false":"true")<<"}\n";
        }catch(...){failed_=true;} // diagnostic writer never changes control ownership
    }
    LiveTraceQueue<4096> vision_,controller_;
    std::filesystem::path directory_;
    std::ofstream stream_;
    std::thread writer_;
    std::atomic<bool> stopping_{false};
    std::uint64_t written_=0;
    bool failed_=false;
};

} // namespace runtime_app
