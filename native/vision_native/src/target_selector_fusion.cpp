#include "vision_native/target_selector.h"
#include "vision_native/cue_acquisition_policy.h"
#include "vision_native/person_cue_association.h"
#include "vision_native/cue_aim_geometry.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vision_native {
namespace {
using namespace cue_association;
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
    person.cue_size=cue.size;
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
bool same_body_shape(const VisionTargetSelector::Rect& box,const VisionTargetSelector::Rect& calibrated) {
    const float w=box.right-box.left,h=box.bottom-box.top;
    const float rw=calibrated.right-calibrated.left,rh=calibrated.bottom-calibrated.top;
    // Both extents must agree with the original body, not the last fragment.
    // A coherent current body can move relative to the UI marker; the marker's
    // initial top gap is not an immutable anatomical measurement.
    return rw>0 && rh>0 && w>=.85f*rw && w<=1.18f*rw && h>=.85f*rh && h<=1.18f*rh &&
        std::abs(w/rw-h/rh)<=.12f;
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
    if(recent_person_ && (batch.captured_at_ns<recent_person_->captured_at_ns ||
        batch.captured_at_ns-recent_person_->captured_at_ns>180'000'000ull ||
        recent_person_->generation!=selection_.generation()))recent_person_.reset();
    std::optional<Detection> projected_person;
    if(recent_person_) {
        projected_person=recent_person_->detection;
        // The same short motion horizon as person association. Extrapolation
        // only locates a cue correspondence; it never authorizes an empty frame.
        const float dt=std::min(.04f,float(batch.captured_at_ns-recent_person_->captured_at_ns)*1e-9f);
        const float dx=recent_person_->velocity_x*dt,dy=recent_person_->velocity_y*dt;
        projected_person->x1+=dx;projected_person->x2+=dx;
        projected_person->y1+=dy;projected_person->y2+=dy;
    }
    // A missing marker revokes current coordinates, not recent anatomical
    // evidence. Retention is bounded by real person observations, never holds.
    const bool recent_reference=cue_body_reference_ && cue_body_reference_->last_person_ns &&
        batch.captured_at_ns>=cue_body_reference_->last_person_ns &&
        batch.captured_at_ns-cue_body_reference_->last_person_ns<=180'000'000ull &&
        cue_body_reference_->generation==selection_.generation();
    std::optional<CueObservation> recovery_cue;
    if(!cue_identity_.active() && recent_reference) {
        recovery_cue=cue_body_reference_->cue;
        const auto shift=cue_reference_translation(*cue_body_reference_);
        recovery_cue->x+=shift.first;recovery_cue->y+=shift.second;
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
        if(projected_person && !cue_body_reference_)
            if(const auto region=association_region(*projected_person))cue_search_regions_.push_back(*region);
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

    // Person admission is independent of marker presence. Unique cue pairing
    // supplies continuity and geometry, never extra person weight.
    DetectionBatch evidence=batch;
    evidence.has_external_cue=false;
    // ROI color centroids are not independent identities and must not bypass
    // the mutual correspondence decision below.
    for (auto& person:evidence.detections) {
        person.has_cue_point=false;person.color_bonus=0;person.cue_score=0;person.cue_size=0;
    }
    for (auto& person:evidence.detections)
        if (const auto* cue=associated_cue(cues,person,evidence.detections)) annotate_cue(person,*cue);
    for (std::size_t i=0;i<diagnostics_.count;++i) {
        const auto& person=evidence.detections[i];
        auto& box=diagnostics_.boxes[i];
        box.has_cue=person.has_cue_point; box.cue_x=person.cue_x;
        box.cue_y=person.cue_y; box.cue_score=person.cue_score;
    }
    build_candidates(evidence,last_target_center_);
    diagnostics_.admitted=static_cast<std::uint32_t>(candidate_scratch_.size());
    const auto selected=select_candidate_targets(candidate_scratch_,last_target_center_,intent);
    auto transition=selected.first ? resolve_active_target_transition(*selected.first,selected.second,intent) : std::nullopt;
    if(selected.first && cue_body_reference_ && !cue_body_reference_->simultaneous_pair &&
        cue_identity_.fresh() && selected.first->candidate.has_source_detection) {
        const auto& person=evidence.detections[selected.first->candidate.source_detection_index];
        const auto* cue=associated_cue(cues,person,evidence.detections);
        const auto& active=*cue_identity_.active();
        if(cue && cue->x==active.x && cue->y==active.y &&
            calibration_supported(selected.first->candidate.body_box,*cue,frame_height_)) {
            // The provisional displacement cannot veto the first real pair.
            // Unique current cue correspondence supplies identity, while the
            // directly observed body replaces estimated geometry immediately.
            transition=*selected.first;
            selection_.observe(*transition,true);
        }
    }
    bool retain_body_geometry=false;
    bool independent_recovery=false;
    if(transition && cue_body_reference_ && cue_body_reference_->simultaneous_pair && cue_identity_.fresh() &&
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
        const bool current_pair_supported=cue && same_cue &&
            calibration_supported(transition->candidate.body_box,*cue,frame_height_);
        const bool current_body_supported=current_pair_supported &&
            same_body_shape(transition->candidate.body_box,ref.calibrated_box);
        const auto& body=transition->candidate.body_box;
        // A box that cannot refresh anatomical displacement cannot supply the
        // live aim point either. Otherwise partial frames pull upward before
        // cue-only frames restore the saved offset, creating a round trip.
        const bool contracts_toward_cue=same_cue && !supports_cue_reference_refresh(
            body.right-body.left,body.bottom-body.top,transition->candidate.target_y-active.y,
            ref.aim_reference_size.first,ref.aim_reference_size.second,ref.aim_offset.second);
        const bool current_geometry_supported=current_body_supported ||
            (same_cue && compatible_body_extent(body,active,
                ref.calibrated_box,ref.calibrated_cue,ref.calibrated_aspect));
        retain_body_geometry=!explicit_handover && !independent_recovery &&
            (contracts_toward_cue || !current_geometry_supported);
        // Keep current correspondence for identity/recovery, independently of
        // the anatomical offset. Freezing the whole reference would make a
        // returning weak/current body match the old full extent instead of
        // the actually observed partial body when the marker briefly vanishes.
        if(retain_body_geometry && current_geometry_supported && current_pair_supported &&
            std::strcmp(transition->candidate.source,"observed")==0) {
            cue_body_reference_->target=*transition;
            cue_body_reference_->cue=*cue;
            cue_body_reference_->last_person_ns=batch.captured_at_ns;
        }
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
                const bool calibrated=cue_body_reference_ && cue_body_reference_->simultaneous_pair;
                const float aspect=calibrated ? cue_body_reference_->calibrated_aspect :
                    (box.right-box.left)/(box.bottom-box.top);
                const auto calibrated_box=calibrated ? cue_body_reference_->calibrated_box : box;
                const auto calibrated_cue=calibrated ? cue_body_reference_->calibrated_cue : *cue;
                cue_identity_.seed(*cue,batch.captured_at_ns);
                auto offset=calibrated ? cue_body_reference_->aim_offset :
                    std::make_pair(target.candidate.target_x-cue->x,target.candidate.target_y-cue->y);
                auto reference_size=calibrated ? cue_body_reference_->aim_reference_size :
                    std::make_pair(box.right-box.left,box.bottom-box.top);
                if (!calibrated || supports_cue_reference_refresh(
                    box.right-box.left,box.bottom-box.top,target.candidate.target_y-cue->y,
                    reference_size.first,reference_size.second,offset.second)) {
                    offset={target.candidate.target_x-cue->x,target.candidate.target_y-cue->y};
                    reference_size={box.right-box.left,box.bottom-box.top};
                    diagnostics_.cue_reason="person_cue_reference_recorded";
                } else {
                    diagnostics_.cue_reason="person_preserves_calibrated_cue_offset";
                }
                cue_body_reference_=CueBodyReference{target,*cue,selection_.generation(),aspect,calibrated_box,
                    calibrated_cue,batch.captured_at_ns,true,offset,reference_size};
            }
        }
        return result;
    }

    if(!transition && !cue_body_reference_ && recent_person_ && !association_ambiguous_) {
        auto person=*projected_person;
        const auto* cue=associated_cue(cues,person,evidence.detections);
        bool contested=false;
        if(cue)for(const auto& current:evidence.detections) {
            auto competitor=current;competitor.is_friendly=false;
            if(current.is_friendly && association_cost(*cue,competitor)<=.95f)contested=true;
        }
        if(cue && !contested && calibration_supported(to_rect(person),*cue,frame_height_)) {
            annotate_cue(person,*cue);
            // Reuse ordinary geometry/confidence/envelope admission. The body
            // is historical, so it cannot be exposed as a current detection or
            // confer fire authority. No coordinates are produced without cue.
            auto candidate=build_candidate(person,~std::uint32_t(0),last_target_center_);
            if(candidate) {
                candidate->has_source_detection=false;candidate->source="cue_hold";
                if(const auto target=select_single_candidate(*candidate,intent)) {
                    if(!selection_.has_identity() && recent_person_->confirmed)
                        selection_.commit(*target,true);
                    auto result=finalize_selected_target(*target,boxes_seen,true,batch.captured_at_ns);
                    if(result.has_target && selection_.active()) {
                        const auto body_ns=recent_person_->captured_at_ns;
                        cue_identity_.seed(*cue,batch.captured_at_ns);
                        const auto& box=candidate->body_box;
                        cue_body_reference_=CueBodyReference{*selection_.active(),*cue,selection_.generation(),
                            (box.right-box.left)/(box.bottom-box.top),box,*cue,body_ns,false,
                            {candidate->target_x-cue->x,candidate->target_y-cue->y}};
                        diagnostics_.cue_reason="recent_person_first_cue";
                        recent_person_.reset();
                    }
                    // Preserve ordinary two-frame confirmation for an
                    // unselected proposal; the empty legacy path would erase it.
                    return result;
                }
            }
        }
    }

    const bool reference_matches=cue_body_reference_ && selection_.has_identity() &&
        cue_body_reference_->generation==selection_.generation();
    if (reference_matches && cue_identity_.fresh()) {
        const auto& cue=*cue_identity_.active();
        const auto& reference=*cue_body_reference_;
        const float dx=cue.x-reference.cue.x,dy=cue.y-reference.cue.y;
        TargetState held=reference.target;
        auto& candidate=held.candidate;
        candidate.target_x=cue.x+reference.aim_offset.first;
        candidate.target_y=cue.y+reference.aim_offset.second;
        candidate.body_box=translated(candidate.body_box,dx,dy);
        candidate.aim_region=translated(candidate.aim_region,
            candidate.target_x-reference.target.candidate.target_x,
            candidate.target_y-reference.target.candidate.target_y);
        candidate.fire_zone=translated(candidate.fire_zone,dx,dy);
        candidate.has_source_detection=false;
        candidate.has_cue=true; candidate.cue_x=cue.x; candidate.cue_y=cue.y; candidate.cue_score=cue.score;
        candidate.source="cue_hold";
        held.intent_applied=false; held.intent_decision="cue_reference_continuation"; held.intent_score=0;
        // 2026-10-20 user authorization: a continuous current cue renews motion
        // evidence beyond 180 ms. The recorded body/chest geometry is translated,
        // never recursively estimated. Output is still aim-only and per-frame.
        const bool below_marker=cue_aim_is_below_marker(candidate.target_y,cue.y,cue.size);
        if (below_marker && candidate.target_x>=0 && candidate.target_x<frame_width_ &&
            candidate.target_y>=0 && candidate.target_y<frame_height_) {
            selection_.observe(held,true);
            last_target_center_=std::make_pair(candidate.target_x,candidate.target_y);
            diagnostics_.cue_reason=retain_body_geometry ? "cue_preserves_body_extent" : "fresh_cue_body_reference";
            diagnostics_.cue_best_score=cue.score;
            return result_from_target(held,boxes_seen);
        }
        diagnostics_.cue_reason=below_marker ? "cue_reference_outside_frame" : "cue_reference_overlaps_marker";
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
    // A cold cue owns only a point and a small localization region, never an
    // invented person box. Real person admission above always takes priority.
    if(complete_frame && !transition && candidate_scratch_.empty() && !selection_.has_identity() &&
        !cue_body_reference_ && !association_ambiguous_) {
        const CueAcquisitionContext context{screen_center_x_,screen_center_y_,
            frame_width_,frame_height_,pickup_base_radius_px_,cue_pickup_offset_y_px_};
        // Person/paired-cue selection above owns the normal path. Only an
        // empty acquisition needs the broader search; no delayed sampling.
        cue_search_regions_.clear();
        cue_search_regions_.push_back(cue_acquisition_search_region(context));
        const auto& cold_cues=cue_detector_.detect(frame->data,frame->width,frame->height,
            frame->row_pitch,frame->format,frame->origin_x,frame->origin_y,&cue_search_regions_);
        const auto acquisition=choose_cue_acquisition(cold_cues,evidence.detections,context);
        if(acquisition) {
            const auto* unique=&acquisition->cue;
            Candidate candidate;candidate.has_body_geometry=false;candidate.source="cue_hold";
            candidate.source_detection_index=~std::uint32_t(0);
            candidate.target_x=acquisition->x;candidate.target_y=acquisition->y;
            candidate.conf=unique->score;candidate.has_cue=true;
            candidate.cue_x=unique->x;candidate.cue_y=unique->y;candidate.cue_score=unique->score;
            const float radius=acquisition->radius;
            candidate.aim_region={candidate.target_x-radius,candidate.target_y-radius,
                candidate.target_x+radius,candidate.target_y+radius};
            if(const auto target=select_single_candidate(candidate,intent)) {
                auto result=finalize_selected_target(*target,boxes_seen,true,batch.captured_at_ns);
                if(result.has_target && selection_.active()) {
                    cue_identity_.seed(*unique,batch.captured_at_ns);
                    cue_body_reference_=CueBodyReference{*selection_.active(),*unique,selection_.generation(),
                        0,{},*unique,0,false,{candidate.target_x-unique->x,candidate.target_y-unique->y}};
                    diagnostics_.cue_reason="cue_offset_acquisition";
                }
                return result;
            }
        }
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
