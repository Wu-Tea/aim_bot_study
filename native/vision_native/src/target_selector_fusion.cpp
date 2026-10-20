#include "vision_native/target_selector.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vision_native {
namespace {
float association_cost(const CueObservation& cue, const Detection& person) {
    const float w=person.x2-person.x1,h=person.y2-person.y1;
    if (person.class_id!=0 || person.is_friendly || w<8 || h<12 ||
        cue.x<person.x1-.2f*w || cue.x>person.x2+.2f*w ||
        cue.y<person.y1-std::max(36.f,.6f*h) || cue.y>person.y1+.4f*h) return 1e9f;
    return .55f*std::abs(cue.x-(person.x1+person.x2)*.5f)/std::max(16.f,.65f*w) +
        .35f*std::abs(cue.y-(person.y1-.08f*h))/std::max(24.f,.45f*h);
}
const CueObservation* associated_cue(const std::vector<CueObservation>& cues, const Detection& person) {
    const CueObservation* best=nullptr;
    float cost=1e9f,second=1e9f;
    for (const auto& cue:cues) {
        const float value=association_cost(cue,person);
        if (value>.95f) continue;
        if (value<cost) { second=cost; cost=value; best=&cue; }
        else second=std::min(second,value);
    }
    return best && second-cost>=.12f ? best : nullptr;
}
void annotate_cue(Detection& person,const CueObservation& cue) {
    person.has_cue_point=true; person.cue_x=cue.x; person.cue_y=cue.y;
    person.cue_score=cue.score; person.color_bonus=10000.f;
}
VisionTargetSelector::Rect translated(VisionTargetSelector::Rect box,float dx,float dy) {
    box.left+=dx; box.right+=dx; box.top+=dy; box.bottom+=dy; return box;
}
}

VisionResult VisionTargetSelector::select_cue_identity(const DetectionBatch& batch,
    const ColorFrameView* frame,const pipeline_contract::UserAimIntent* intent) {
    selection_.begin_frame();
    clear_auto_fire_state();
    const float boxes_seen=static_cast<float>(batch.detections.size());
    if (!batch.captured_at_ns || batch.captured_at_ns<=last_cue_frame_ns_) {
        diagnostics_.outcome="capture_not_fresh";
        selection_.mark_missing();
        return empty_result(boxes_seen);
    }
    last_cue_frame_ns_=batch.captured_at_ns;
    const bool complete_frame=frame && frame->data && frame->origin_x==0 && frame->origin_y==0 &&
        frame->width==static_cast<int>(frame_width_) && frame->height==static_cast<int>(frame_height_) &&
        frame->row_pitch>=frame->width*(frame->format==PixelFormat::BGRA8 ? 4 : 3);
    static const std::vector<CueObservation> none;
    const auto& cues=complete_frame ? cue_detector_.detect(frame->data,frame->width,frame->height,
        frame->row_pitch,frame->format,frame->origin_x,frame->origin_y) : none;
    cue_identity_.update(cues,batch.captured_at_ns);
    if (!cue_identity_.active()) cue_body_reference_.reset();

    // OR admission: an independently credible person never needs a cue. A
    // uniquely associated cue can also support an otherwise lower-confidence
    // body under the existing marked-person threshold, without moving its aim.
    DetectionBatch evidence=batch;
    evidence.has_external_cue=false;
    for (auto& person:evidence.detections)
        if (const auto* cue=associated_cue(cues,person)) annotate_cue(person,*cue);
    for (std::size_t i=0;i<diagnostics_.count;++i) {
        const auto& person=evidence.detections[i];
        auto& box=diagnostics_.boxes[i];
        box.has_cue=person.has_cue_point; box.cue_x=person.cue_x;
        box.cue_y=person.cue_y; box.cue_score=person.cue_score;
    }
    build_candidates(evidence,last_target_center_,intent);
    diagnostics_.admitted=static_cast<std::uint32_t>(candidate_scratch_.size());
    const auto selected=select_candidate_targets(candidate_scratch_,last_target_center_,intent);
    const auto transition=selected.first ? resolve_active_target_transition(*selected.first,selected.second,intent) : std::nullopt;
    if (transition) {
        auto result=finalize_selected_target(*transition,boxes_seen,candidate_scratch_.size()==1,batch.captured_at_ns);
        if (result.selector_target_changed) {
            cue_body_reference_.reset(); cue_identity_.reset();
        }
        if (result.has_selected_detection && selection_.active()) {
            const auto& target=*selection_.active();
            const auto& person=evidence.detections[result.selected_detection_index];
            const auto* cue=associated_cue(cues,person);
            // Only a current, strong person/cue pair calibrates geometry. A
            // weak partial box or cue-derived output cannot train its own offset.
            if (cue && std::strcmp(target.candidate.source,"observed")==0) {
                cue_identity_.seed(*cue,batch.captured_at_ns);
                cue_body_reference_=CueBodyReference{target,*cue,selection_.generation()};
                diagnostics_.cue_reason="person_cue_reference_recorded";
            }
        }
        return result;
    }

    const bool reference_matches=cue_body_reference_ && selection_.has_identity() &&
        cue_body_reference_->generation==selection_.generation();
    if (reference_matches && cue_identity_.fresh()) {
        const auto& cue=*cue_identity_.active();
        const auto& reference=*cue_body_reference_;
        const float dx=cue.x-reference.cue.x,dy=cue.y-reference.cue.y;
        TargetState held=reference.target;
        auto& candidate=held.candidate;
        candidate.target_x+=dx; candidate.target_y+=dy;
        candidate.body_box=translated(candidate.body_box,dx,dy);
        candidate.aim_region=translated(candidate.aim_region,dx,dy);
        candidate.fire_zone=translated(candidate.fire_zone,dx,dy);
        candidate.has_source_detection=false;
        candidate.has_cue=true; candidate.cue_x=cue.x; candidate.cue_y=cue.y; candidate.cue_score=cue.score;
        candidate.source="cue_hold";
        held.intent_applied=false; held.intent_decision="cue_reference_continuation"; held.intent_score=0;
        // 2026-10-20 user authorization: a continuous current cue renews motion
        // evidence beyond 180 ms. The recorded body/chest geometry is translated,
        // never recursively estimated. Output is still aim-only and per-frame.
        if (candidate.target_x>=0 && candidate.target_x<frame_width_ &&
            candidate.target_y>=0 && candidate.target_y<frame_height_) {
            selection_.observe(held,true);
            last_target_center_=std::make_pair(candidate.target_x,candidate.target_y);
            diagnostics_.cue_reason="fresh_cue_body_reference";
            diagnostics_.cue_best_score=cue.score;
            return result_from_target(held,boxes_seen);
        }
        diagnostics_.cue_reason="cue_reference_outside_frame";
        selection_.mark_missing();
        return empty_result(boxes_seen);
    }
    if (reference_matches && cue_identity_.active()) {
        // Ambiguity/gap: retain only identity for the existing 50 ms matching
        // window. Current weak person evidence can still supply its own point.
        if (const auto weak=select_weak_association(evidence)) {
            selection_.observe(*weak,false);
            last_target_center_=std::make_pair(weak->candidate.target_x,weak->candidate.target_y);
            return result_from_target(*weak,boxes_seen);
        }
        diagnostics_.outcome="cue_missing_or_ambiguous";
        selection_.mark_missing();
        return empty_result(boxes_seen);
    }
    // Keep the existing person acquisition/confirmation and explicit handover
    // lifecycle. No color frame or external cue here: independent current-cue
    // matching above is the sole cue continuation owner in this policy.
    auto result=select_impl_core(evidence,nullptr,intent);
    // clear_tracking_state inside the legacy lifecycle may reset this watermark.
    last_cue_frame_ns_=batch.captured_at_ns;
    return result;
}
} // namespace vision_native
