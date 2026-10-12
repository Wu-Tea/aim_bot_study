#pragma once
#include <array>
#include <cstdint>

namespace pipeline_contract {
// Diagnostic-only, bounded POD. All reason pointers refer to static literals.
// Never use these fields as control inputs; truncation is explicit.
struct DetectionDecision {
    float x1=0, y1=0, x2=0, y2=0, confidence=0;
    int class_id=0;
    bool color_checked=false, friendly=false, has_cue=false, active_match=false, within_pickup=false;
    float cue_x=0, cue_y=0, cue_score=0, rank_score=0;
    const char* gate="not_evaluated";
};
struct SelectionDiagnostics {
    std::uint32_t total=0, count=0, admitted=0;
    std::int32_t selected_index=-1;
    std::uint64_t generation_before=0, generation_after=0;
    float previous_target_x=0, previous_target_y=0;
    const char* outcome="not_evaluated";
    const char* cue_reason="not_attempted";
    float cue_best_score=-1, cue_threshold=0, cue_direct_age_ms=-1, cue_age_ms=-1;
    std::array<DetectionDecision,8> boxes{};
};
struct IdentityDiagnostics {
    std::uint64_t previous_id=0, resulting_id=0, input_generation=0;
    std::uint32_t friendly_rejections=0, reliability_rejections=0;
    bool fresh_capture=false, continuation_valid=false;
    float source_age_ms=0;
    const char* transition="unchanged";
    const char* gap_reason="not_needed";
};
} // namespace pipeline_contract
