#pragma once
#include "vision_native/types.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace study {
inline int source_id(const char* s) {
    const std::string source=s?s:"";
    return source=="observed"?1:source=="cue_hold"?2:source=="associated_weak"?3:source=="weak_observed"?4:0;
}
inline const char* source_name(int i) {
    static const char* names[]={"","observed","cue_hold","associated_weak","weak_observed"};
    if(i<0||i>4)throw std::runtime_error("bad source");return names[i];
}
inline void write(std::ostream& o,const vision_native::VisionResult& r) {
    o<<std::setprecision(9)<<r.frame_id<<' '<<r.captured_at_ns<<' '
     <<r.selector_target_generation<<' '<<r.selector_target_changed<<' '<<r.has_target<<' '
     <<source_id(r.target_source)<<' '<<r.has_selected_detection<<' '<<r.selected_detection_index<<' '
     <<r.target_x<<' '<<r.target_y<<' '<<r.has_body_box<<' '
     <<r.body_x1<<' '<<r.body_y1<<' '<<r.body_x2<<' '<<r.body_y2<<' '
     <<r.has_aim_region<<' '<<r.aim_region_x1<<' '<<r.aim_region_y1<<' '<<r.aim_region_x2<<' '<<r.aim_region_y2<<' '
     <<r.aim_authority<<' '<<r.fire_authority<<' '<<r.auto_fire<<' '<<r.enemy_cue_current<<' '
     <<r.enemy_identity_confirmed<<' '<<r.target_confidence<<' '<<r.detections.size();
    for(const auto& d:r.detections)o<<' '<<d.x1<<' '<<d.y1<<' '<<d.x2<<' '<<d.y2<<' '<<d.conf<<' '
     <<d.class_id<<' '<<d.color_bonus<<' '<<d.is_friendly<<' '<<d.color_classified<<' '<<d.has_cue_point<<' '
     <<d.cue_x<<' '<<d.cue_y<<' '<<d.cue_score<<' '<<d.has_motion_anchor<<' '
     <<d.motion_anchor_x<<' '<<d.motion_anchor_y<<' '<<d.motion_anchor_score;
#ifdef STUDY_MOTION_CUE
    o<<' '<<r.motion_cue_id<<' '<<r.motion_cue_x<<' '<<r.motion_cue_y;
#endif
    o<<'\n';
}
inline std::vector<vision_native::VisionResult> read(const std::string& path) {
    std::ifstream file(path);if(!file)throw std::runtime_error("missing records");
    std::vector<vision_native::VisionResult> rows;std::string line;
    while(std::getline(file,line)) {
        std::istringstream s(line);vision_native::VisionResult r;int source;std::size_t count;
        s>>r.frame_id>>r.captured_at_ns>>r.selector_target_generation>>r.selector_target_changed>>r.has_target
         >>source>>r.has_selected_detection>>r.selected_detection_index>>r.target_x>>r.target_y>>r.has_body_box
         >>r.body_x1>>r.body_y1>>r.body_x2>>r.body_y2>>r.has_aim_region
         >>r.aim_region_x1>>r.aim_region_y1>>r.aim_region_x2>>r.aim_region_y2
         >>r.aim_authority>>r.fire_authority>>r.auto_fire>>r.enemy_cue_current>>r.enemy_identity_confirmed>>r.target_confidence>>count;
        if(!s||count>10000)throw std::runtime_error("invalid vision record");
        r.target_source=r.association_stage=source_name(source);
        r.target_tier=source==1?"observed_strong":source==2?"cue_hold":source?"associated_weak":"none";
        r.detections.resize(count);
        for(auto& d:r.detections)s>>d.x1>>d.y1>>d.x2>>d.y2>>d.conf>>d.class_id>>d.color_bonus
          >>d.is_friendly>>d.color_classified>>d.has_cue_point>>d.cue_x>>d.cue_y>>d.cue_score
          >>d.has_motion_anchor>>d.motion_anchor_x>>d.motion_anchor_y>>d.motion_anchor_score;
        if(!s)throw std::runtime_error("truncated detection record");
#ifdef STUDY_MOTION_CUE
        // Old fixtures remain readable; the optional tail carries current
        // native selector correspondence in new pixel exports.
        s>>r.motion_cue_id>>r.motion_cue_x>>r.motion_cue_y;
#endif
        r.screen_center_x=320;r.screen_center_y=256;r.dx=r.target_x-320;r.dy=r.target_y-256;
        r.selector_identity_protocol=true;r.frame_updated=true;r.result_at_ns=r.captured_at_ns;
        rows.push_back(std::move(r));
    }
    if(rows.empty())throw std::runtime_error("empty recording");return rows;
}
}
