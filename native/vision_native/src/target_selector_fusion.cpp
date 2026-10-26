#include "vision_native/target_selector.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vision_native {
namespace {
float cue_lateral_support(float width,float height) {
    // A side-facing head can sit near a shoulder. Limit that displacement by
    // both dimensions so a short, wide false box cannot borrow a distant cue.
    return std::max(12.f,std::min(.6f*width,.35f*height));
}
std::optional<CueSearchRegion> association_region(const Detection& person) {
    const float w=person.x2-person.x1,h=person.y2-person.y1;
    if(person.class_id!=0 || person.is_friendly || w<8 || h<12)return std::nullopt;
    return CueSearchRegion{person.x1-.2f*w,person.y1-std::max(36.f,.6f*h),
        person.x2+.2f*w,person.y1+.4f*h};
}
float association_cost(const CueObservation& cue, const Detection& person) {
    const auto region=association_region(person);
    if(!region || !region->contains(cue.x,cue.y))return 1e9f;
    const float w=person.x2-person.x1,h=person.y2-person.y1;
    // A side marker outside the body's supported horizontal anatomy cannot
    // grant marked-person confidence or initialize its chest displacement.
    if(std::abs(cue.x-(person.x1+person.x2)*.5f)>cue_lateral_support(w,h)) return 1e9f;
    return .55f*std::abs(cue.x-(person.x1+person.x2)*.5f)/std::max(16.f,.65f*w) +
        .35f*std::abs(cue.y-(person.y1-.08f*h))/std::max(24.f,.45f*h);
}
const CueObservation* associated_cue(const std::vector<CueObservation>& cues, const Detection& person,
    const std::vector<Detection>& people) {
    const CueObservation* best=nullptr;
    float cost=1e9f,second=1e9f;
    for (const auto& cue:cues) {
        const float value=association_cost(cue,person);
        if (value>.95f) continue;
        if (value<cost) { second=cost; cost=value; best=&cue; }
        else second=std::min(second,value);
    }
    if (!best || second-cost<.12f) return nullptr;
    // A cue has one body owner as well as a body having one cue. Geometry
    // wins; confidence only resolves duplicate/equal-geometry detections.
    for (const auto& other:people) {
        if (&other==&person) continue;
        const float other_cost=association_cost(*best,other);
        if (other_cost<cost-.001f || (std::abs(other_cost-cost)<=.001f &&
            (other.conf>person.conf || (other.conf==person.conf && &other<&person)))) return nullptr;
    }
    return best;
}
void annotate_cue(Detection& person,const CueObservation& cue) {
    person.has_cue_point=true; person.cue_x=cue.x; person.cue_y=cue.y;
    person.cue_score=cue.score; person.color_bonus=10000.f;
}
VisionTargetSelector::Rect translated(VisionTargetSelector::Rect box,float dx,float dy) {
    box.left+=dx; box.right+=dx; box.top+=dy; box.bottom+=dy; return box;
}
bool compatible_body_extent(const VisionTargetSelector::Rect& box,const CueObservation& cue,
    const VisionTargetSelector::Rect& reference,const CueObservation& reference_cue,float aspect) {
    const float w=box.right-box.left,h=box.bottom-box.top;
    const float rh=reference.bottom-reference.top;
    // A person confidence is not a completeness score. Upper-body fragments
    // become disproportionately wide; compare with the calibrated shape rather
    // than the last frame so a series of small contractions cannot relearn it.
    if(h<=0 || rh<=0 || w>h*aspect*1.15f) return false;
    // Missing a pairing must not bypass its spatial support. A head fragment
    // below/aside the tracked marker cannot silently become the whole body.
    if(cue.x<box.left-.2f*w || cue.x>box.right+.2f*w || cue.y>box.top+.4f*h) return false;
    // Whole-body scale can change with distance/ADS. The UI's head gap may be
    // fixed in screen pixels or scale with the body; allow either, plus detector
    // edge noise. A box growing above the same cue is neither kind of scaling.
    const float gap=box.top-cue.y,old_gap=reference.top-reference_cue.y;
    const float scaled_gap=old_gap*h/rh,tolerance=std::max(8.f,.12f*std::min(rh,h));
    return gap>=std::min(old_gap,scaled_gap)-tolerance &&
        gap<=std::max(old_gap,scaled_gap)+tolerance;
}
bool calibration_supported(const VisionTargetSelector::Rect& box,const CueObservation& cue,float frame_height) {
    const float w=box.right-box.left,h=box.bottom-box.top;
    // A clipped/inflated box may still identify a person, but a marker near
    // its side or deep inside it cannot calibrate an anatomical displacement.
    const bool close_cropped_body=h>=.6f*frame_height && box.bottom>=frame_height-2;
    return std::abs((box.left+box.right)*.5f-cue.x)<=cue_lateral_support(w,h) &&
        (box.top-cue.y>=-std::max(8.f,.08f*h) || close_cropped_body);
}
bool recovered_body(const VisionTargetSelector::Rect& box,const VisionTargetSelector::Rect& previous) {
    const float w=box.right-box.left,h=box.bottom-box.top;
    const float pw=previous.right-previous.left,ph=previous.bottom-previous.top;
    // A taller box alone cannot disprove a body: the same torso must remain
    // laterally supported and keep its lower extent (not engulf background).
    return h>1.15f*ph && w>=.65f*pw && w<=1.35f*pw && w<=.8f*h &&
        std::abs((box.left+box.right-previous.left-previous.right)*.5f)<=std::max(8.f,.2f*pw) &&
        std::abs(box.bottom-previous.bottom)<=std::max(12.f,.2f*ph);
}
bool recovers_head_gap(const VisionTargetSelector::Rect& box,const CueObservation& cue,
    const VisionTargetSelector::Rect& reference,const CueObservation& reference_cue) {
    // Recovering an initially truncated upper extent moves its top towards the
    // marker from below. A larger box farther below it is scale/pose change,
    // not evidence that can replace the immutable head-gap calibration.
    const float gap=box.top-cue.y,old_gap=reference.top-reference_cue.y;
    return gap>=0 && gap<old_gap-8;
}
bool translated_body(const VisionTargetSelector::Rect& box,const VisionTargetSelector::Rect& previous) {
    const float w=box.right-box.left,h=box.bottom-box.top;
    const float pw=previous.right-previous.left,ph=previous.bottom-previous.top;
    return w>=.8f*pw && w<=1.25f*pw && h>=.8f*ph && h<=1.25f*ph &&
        std::abs((box.left+box.right-previous.left-previous.right)*.5f)<=std::max(12.f,.5f*pw) &&
        std::abs((box.top+box.bottom-previous.top-previous.bottom)*.5f)<=std::max(12.f,.5f*ph);
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
    // A missing marker revokes current coordinates, not recent anatomical
    // evidence. Retention is bounded by real person observations, never holds.
    const bool recent_reference=cue_body_reference_ && cue_body_reference_->last_person_ns &&
        batch.captured_at_ns>=cue_body_reference_->last_person_ns &&
        batch.captured_at_ns-cue_body_reference_->last_person_ns<=180'000'000ull &&
        cue_body_reference_->generation==selection_.generation();
    std::optional<CueObservation> recovery_cue;
    if(!cue_identity_.active() && recent_reference) {
        recovery_cue=cue_body_reference_->cue;
        if(last_target_center_) {
            recovery_cue->x+=last_target_center_->first-cue_body_reference_->target.candidate.target_x;
            recovery_cue->y+=last_target_center_->second-cue_body_reference_->target.candidate.target_y;
        }
    }
    const bool complete_frame=frame && frame->data && frame->origin_x==0 && frame->origin_y==0 &&
        frame->width==static_cast<int>(frame_width_) && frame->height==static_cast<int>(frame_height_) &&
        frame->row_pitch>=frame->width*(frame->format==PixelFormat::BGRA8 ? 4 : 3);
    static const std::vector<CueObservation> none;
    cue_search_regions_.clear();
    if(complete_frame) {
        // Share the actual consumer bounds: no confidence shortcut may exclude
        // a person that association_cost would otherwise accept this frame.
        for(const auto& person:batch.detections)
            if(const auto region=association_region(person))cue_search_regions_.push_back(*region);
        if(const auto region=cue_identity_.search_region())cue_search_regions_.push_back(*region);
        else if(recovery_cue)cue_search_regions_.push_back({recovery_cue->x-42,recovery_cue->y-42,
            recovery_cue->x+42,recovery_cue->y+42});
    }
    const auto& cues=complete_frame ? cue_detector_.detect(frame->data,frame->width,frame->height,
        frame->row_pitch,frame->format,frame->origin_x,frame->origin_y,&cue_search_regions_) : none;
    cue_identity_.update(cues,batch.captured_at_ns);
    if(!cue_identity_.active() && recovery_cue && cue_identity_.reacquire(*recovery_cue,cues,batch.captured_at_ns)) {
        if(!selection_.has_identity()) {
            selection_.commit(cue_body_reference_->target,true);
            cue_body_reference_->generation=selection_.generation();
            active_generation_had_enemy_evidence_=true;
        }
    }
    if (!cue_identity_.active() && !recent_reference) cue_body_reference_.reset();

    // OR admission: an independently credible person never needs a cue. A
    // uniquely associated cue can also support an otherwise lower-confidence
    // body under the existing marked-person threshold, without moving its aim.
    DetectionBatch evidence=batch;
    evidence.has_external_cue=false;
    // ROI color centroids are not independent identities and must not bypass
    // the mutual correspondence decision below.
    for (auto& person:evidence.detections) {
        person.has_cue_point=false;person.color_bonus=0;person.cue_score=0;
    }
    for (auto& person:evidence.detections)
        if (const auto* cue=associated_cue(cues,person,evidence.detections)) annotate_cue(person,*cue);
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
    bool retain_body_geometry=false;
    bool independent_recovery=false;
    if(transition && cue_body_reference_ && cue_identity_.fresh() &&
        cue_body_reference_->generation==selection_.generation() &&
        targets_match(*selection_.active(),*transition) &&
        transition->candidate.has_source_detection) {
        const auto& person=evidence.detections[transition->candidate.source_detection_index];
        const auto* cue=associated_cue(cues,person,evidence.detections);
        const auto& active=*cue_identity_.active();
        // This is anatomy validation for the already tracked, uniquely paired
        // cue, not a veto of independent person acquisition or explicit handover.
        const auto& ref=*cue_body_reference_;
        const auto projected=translated(ref.target.candidate.body_box,active.x-ref.cue.x,active.y-ref.cue.y);
        const auto region=association_region(person);
        const bool moved_off_cue=region && !region->contains(active.x,active.y);
        independent_recovery=!cue && std::strcmp(transition->candidate.source,"observed")==0 &&
            ((calibration_supported(transition->candidate.body_box,active,frame_height_) &&
              recovers_head_gap(transition->candidate.body_box,active,ref.calibrated_box,ref.calibrated_cue) &&
              !compatible_body_extent(transition->candidate.body_box,active,ref.calibrated_box,ref.calibrated_cue,ref.calibrated_aspect) &&
              recovered_body(transition->candidate.body_box,projected)) ||
             (!cue && moved_off_cue && selection_.active()->candidate.has_source_detection &&
              translated_body(transition->candidate.body_box,selection_.active()->candidate.body_box)));
        const bool explicit_handover=intent && intent->valid && intent->aiming &&
            intent->purpose==pipeline_contract::UserAimIntentPurpose::HandoverTarget && transition->intent_applied;
        const bool same_cue=!cue || (cue->x==active.x && cue->y==active.y);
        retain_body_geometry=!explicit_handover && !independent_recovery &&
            (!same_cue || !compatible_body_extent(transition->candidate.body_box,active,
                ref.calibrated_box,ref.calibrated_cue,ref.calibrated_aspect));
    }
    if (transition && !retain_body_geometry) {
        auto result=finalize_selected_target(*transition,boxes_seen,candidate_scratch_.size()==1,batch.captured_at_ns);
        if (result.selector_target_changed) {
            cue_body_reference_.reset(); cue_identity_.reset();
        }
        if (result.has_selected_detection && selection_.active()) {
            const auto& target=*selection_.active();
            const auto& person=evidence.detections[result.selected_detection_index];
            const auto* cue=associated_cue(cues,person,evidence.detections);
            // Current strong body evidence can disprove an old cue pairing.
            // Do not let an unrelated persistent HUD mark retain that body's
            // geometry after the independently observed person has moved away.
            if (cue_body_reference_ && cue_identity_.fresh() &&
                std::strcmp(target.candidate.source,"observed")==0) {
                const auto& active_cue=*cue_identity_.active();
                const auto& reference=*cue_body_reference_;
                const auto projected=translated(reference.target.candidate.body_box,
                    active_cue.x-reference.cue.x,active_cue.y-reference.cue.y);
                // An unpaired, independently observed body can also disprove
                // the old extent while still overlapping it (e.g. a complete
                // torso returning above a former lower-body fragment). Retire
                // that calibration now; it must not veto the next real body.
                const auto& body=target.candidate.body_box;
                // A contracting unpaired box can itself be an occluded
                // fragment; only recovered extent is contrary body evidence.
                const bool recovers_extent=calibration_supported(body,active_cue,frame_height_) &&
                    recovers_head_gap(body,active_cue,reference.calibrated_box,reference.calibrated_cue) && recovered_body(body,projected);
                if (independent_recovery || !boxes_match(projected,body) ||
                    (!cue && recovers_extent && !compatible_body_extent(body,active_cue,
                        reference.calibrated_box,reference.calibrated_cue,reference.calibrated_aspect))) {
                    cue_body_reference_.reset();
                    cue_identity_.reset();
                    diagnostics_.cue_reason="person_disproves_cue_reference";
                }
            }
            // Only a current, strong person/cue pair calibrates geometry. A
            // weak partial box or cue-derived output cannot train its own offset.
            if (cue && std::strcmp(target.candidate.source,"observed")==0 &&
                calibration_supported(target.candidate.body_box,*cue,frame_height_)) {
                const auto& box=target.candidate.body_box;
                const float aspect=cue_body_reference_ ? cue_body_reference_->calibrated_aspect :
                    (box.right-box.left)/(box.bottom-box.top);
                const auto calibrated_box=cue_body_reference_ ? cue_body_reference_->calibrated_box : box;
                const auto calibrated_cue=cue_body_reference_ ? cue_body_reference_->calibrated_cue : *cue;
                cue_identity_.seed(*cue,batch.captured_at_ns);
                cue_body_reference_=CueBodyReference{target,*cue,selection_.generation(),aspect,calibrated_box,calibrated_cue,batch.captured_at_ns};
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
            diagnostics_.cue_reason=retain_body_geometry ? "cue_preserves_body_extent" : "fresh_cue_body_reference";
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
    if (association_ambiguous_) {
        diagnostics_.outcome="person_association_ambiguous";
        selection_.mark_missing();
        return empty_result(boxes_seen);
    }
    auto result=select_impl_core(evidence,nullptr,intent);
    // clear_tracking_state inside the legacy lifecycle may reset this watermark.
    last_cue_frame_ns_=batch.captured_at_ns;
    return result;
}
} // namespace vision_native
