#include "vision_native/cue_acquisition_policy.h"
#include "vision_native/person_cue_association.h"
#include "vision_native/cue_aim_geometry.h"
#include <algorithm>
#include <cmath>

namespace vision_native {
CueSearchRegion cue_acquisition_search_region(const CueAcquisitionContext& c) {
    return {c.center_x-c.pickup_radius,c.center_y-c.pickup_radius-c.downward_offset,
        c.center_x+c.pickup_radius,c.center_y+c.pickup_radius-c.downward_offset};
}
std::optional<CueAcquisitionPoint> choose_cue_acquisition(
    const std::vector<CueObservation>& cues,const std::vector<Detection>& people,
    const CueAcquisitionContext& c) {
    std::optional<CueAcquisitionPoint> selected;
    for(const auto& cue:cues) {
        const float x=cue.x,y=cue.y+c.downward_offset;
        if(!cue_aim_is_below_marker(y,cue.y,cue.size))continue;
        if(x<0 || x>=c.width || y<0 || y>=c.height ||
            std::hypot(x-c.center_x,y-c.center_y)>c.pickup_radius)continue;
        const bool friendly=std::any_of(people.begin(),people.end(),[&](Detection person){
            if(!person.is_friendly)return false;
            person.is_friendly=false;
            return cue_association::association_cost(cue,person)<=.95f;
        });
        if(friendly)continue;
        if(selected)return std::nullopt; // No arbitrary identity when ambiguous.
        selected=CueAcquisitionPoint{cue,x,y,std::max(2.f,cue.size*.25f)};
    }
    return selected;
}
}
