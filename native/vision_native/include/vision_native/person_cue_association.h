#pragma once
#include "vision_native/cue_identity.h"
#include "vision_native/types.h"
#include <algorithm>
#include <cmath>
#include <optional>
namespace vision_native::cue_association {
inline float cue_lateral_support(float width,float height) {
    // A side-facing head can sit near a shoulder. Limit that displacement by
    // both dimensions so a short, wide false box cannot borrow a distant cue.
    return std::max(12.f,std::min(.6f*width,.35f*height));
}
inline std::optional<CueSearchRegion> association_region(const Detection& person) {
    const float w=person.x2-person.x1,h=person.y2-person.y1;
    if(person.class_id!=0 || person.is_friendly || w<8 || h<12)return std::nullopt;
    return CueSearchRegion{person.x1-.2f*w,person.y1-std::max(36.f,.6f*h),
        person.x2+.2f*w,person.y1+.4f*h};
}
inline float association_cost(const CueObservation& cue, const Detection& person) {
    const auto region=association_region(person);
    if(!region || !region->contains(cue.x,cue.y))return 1e9f;
    const float w=person.x2-person.x1,h=person.y2-person.y1;
    // A side marker outside the body's supported horizontal anatomy cannot
    // establish a person/cue pair or initialize its chest displacement.
    if(std::abs(cue.x-(person.x1+person.x2)*.5f)>cue_lateral_support(w,h)) return 1e9f;
    return .55f*std::abs(cue.x-(person.x1+person.x2)*.5f)/std::max(16.f,.65f*w) +
        .35f*std::abs(cue.y-(person.y1-.08f*h))/std::max(24.f,.45f*h);
}
}
