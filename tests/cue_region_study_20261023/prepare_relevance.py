"""Test-only semantic search regions, based on existing consumer geometry."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
base = root / (sys.argv[1] if len(sys.argv)>1 else 'runs/cue-region-20261023/reference')
out = root / (sys.argv[2] if len(sys.argv)>2 else 'runs/cue-relevance-20261023/reference')
out.mkdir(parents=True, exist_ok=False)
for name in ['reference.h', 'reference_selector.h', 'reference_selector.cpp', 'reference_selector_fusion.cpp', 'reference.cpp']:
    (out / name).write_text((base / name).read_text(encoding='utf-8'), encoding='utf-8')
header = (out / 'reference.h').read_text(encoding='utf-8')
header = header.replace('#include <optional>', '#include <optional>\n#include <array>')
header = header.replace('extern int region_seed_bottom;', '''extern int region_seed_bottom;
extern bool region_relevance_enabled;
extern std::vector<std::array<float,4>> region_rects;
extern double region_cue_ms;
extern std::size_t region_cue_count;''')
(out / 'reference.h').write_text(header, encoding='utf-8')
source = (out / 'reference.cpp').read_text(encoding='utf-8').replace('#include <limits>', '#include <limits>\n#include <chrono>')
source = source.replace('int region_seed_bottom=512;', '''int region_seed_bottom=512;
bool region_relevance_enabled=false;
std::vector<std::array<float,4>> region_rects;
double region_cue_ms=0;
std::size_t region_cue_count=0;''')
source = source.replace('    observations_.clear();', '    const auto cue_start=std::chrono::steady_clock::now();\n    observations_.clear();', 1)
needle = 'for (int seed_y=0; seed_y<std::min(height,region_seed_bottom); seed_y+=kMinimumCueExtent) for (int seed_x=0; seed_x<width; ++seed_x) {'
assert source.count(needle) == 1
source = source.replace(needle, '''std::vector<std::pair<int,int>> spans;
    for (int seed_y=0; seed_y<height; seed_y+=kMinimumCueExtent) {
      spans.clear();
      if(!region_relevance_enabled)spans.push_back({0,width});
      else for(const auto& rect:region_rects) {
        // A valid cue is at most 18 pixels wide/high. Include its full shape,
        // so all original seed phases and original image boundaries survive.
        if(seed_y<rect[1]-9 || seed_y>rect[3]+9)continue;
        const int left=std::max(0,int(std::floor(rect[0]-9)));
        const int right=std::min(width,int(std::ceil(rect[2]+9))+1);
        if(left<right)spans.push_back({left,right});
      }
      std::sort(spans.begin(),spans.end());
      std::size_t merged=0;
      for(auto span:spans) {
        if(merged && span.first<=spans[merged-1].second)spans[merged-1].second=std::max(spans[merged-1].second,span.second);
        else spans[merged++]=span;
      }
      spans.resize(merged);
      for(auto span:spans) for(int seed_x=span.first;seed_x<span.second;++seed_x) {''')
source = source.replace('    // Sparse seed discovery', '    }\n    // Sparse seed discovery', 1)
last = source.rindex('    return observations_;')
source = source[:last] + '''    region_cue_count=observations_.size();
    region_cue_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cue_start).count();
''' + source[last:]
(out / 'reference.cpp').write_text(source, encoding='utf-8')
source = (out / 'reference_selector_fusion.cpp').read_text(encoding='utf-8')
needle = '    const auto& cues=complete_frame ?'
assert source.count(needle) == 1
source = source.replace(needle, '''    region_rects.clear();
    if(region_relevance_enabled && complete_frame) {
        // Exactly the outer admission bounds of association_cost(). Do not
        // confidence-filter persons here; every downstream association matters.
        for(const auto& person:batch.detections) {
            const float w=person.x2-person.x1,h=person.y2-person.y1;
            if(person.class_id!=0 || person.is_friendly || w<8 || h<12)continue;
            region_rects.push_back({person.x1-.2f*w,person.y1-std::max(36.f,.6f*h),person.x2+.2f*w,person.y1+.4f*h});
        }
        // CueIdentity::update excludes every cue farther than 42 pixels from
        // the previous position, regardless of its velocity prediction.
        if(const auto& active=cue_identity_.active())
            region_rects.push_back({active->x-42,active->y-42,active->x+42,active->y+42});
        // Preserve the entire four-tip hit-marker composition whenever any
        // semantic search is needed. Counterpart centres can be 36 px away.
        if(!region_rects.empty())region_rects.push_back({frame_width_*.5f-36,frame_height_*.5f-36,frame_width_*.5f+36,frame_height_*.5f+36});
    }
    const auto& cues=complete_frame ?''')
(out / 'reference_selector_fusion.cpp').write_text(source, encoding='utf-8')
print(out)
