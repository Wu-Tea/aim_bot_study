#include "vision_native/cue_identity.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vision_native {
namespace {
constexpr int kMinimumCueExtent=4;
constexpr int kMaximumCueExtent=18;
constexpr float kCueMatchDistance=42;
constexpr float kHitMarkerRadius=32;
constexpr float kHitMarkerTolerance=4;
int enemy_color(const unsigned char* p, PixelFormat format, const pipeline_contract::MarkerColorBand& color) {
    return color.matches(p[format == PixelFormat::BGRA8 ? 2 : 0], p[1],
        p[format == PixelFormat::BGRA8 ? 0 : 2]) ? 1 : 0;
}
}

const std::vector<CueObservation>& CueDetector::detect(const unsigned char* pixels,
    int width, int height, int pitch, PixelFormat format, int origin_x, int origin_y,
    const std::vector<CueSearchRegion>* regions) {
    observations_.clear();
    ordered_observations_.clear();
    const int channels = format == PixelFormat::BGRA8 ? 4 : 3;
    if (!pixels || width <= 0 || height <= 0 || pitch < std::int64_t(width)*channels ||
        std::int64_t(width)*height > std::numeric_limits<int>::max()) return observations_;
    if(regions && regions->empty())return observations_;
    constexpr unsigned char unknown=255;
    mask_.assign(std::size_t(width)*height,unknown);
    const auto color_at = [&](int x,int y) {
        auto& value=mask_[y*width+x];
        if (value==unknown) value=static_cast<unsigned char>(enemy_color(
            pixels+std::size_t(y)*pitch+std::size_t(x)*channels,format,enemy_color_));
        return value;
    };
    // An accepted 8-connected component is at least four rows high. It must
    // intersect one of these seed rows: no accepted circle/skull can lie
    // wholly between them. Expand every seed at full resolution and consume
    // the complete component, even after it grows too large to be a cue.
    for (int seed_y=0; seed_y<height; seed_y+=kMinimumCueExtent) {
      seed_spans_.clear();
      if(!regions)seed_spans_.push_back({0,width});
      else {
        const auto add_span=[&](const CueSearchRegion& region) {
            constexpr float padding=kMaximumCueExtent*.5f;
            if(seed_y+origin_y<region.top-padding || seed_y+origin_y>region.bottom+padding)return;
            // Clamp before integer conversion; regions can extend beyond the frame.
            const int left=static_cast<int>(std::floor(std::clamp(region.left-origin_x-padding,0.f,float(width))));
            const int right=static_cast<int>(std::ceil(std::clamp(region.right-origin_x+padding+1,0.f,float(width))));
            if(left<right)seed_spans_.push_back({left,right});
        };
        for(const auto& region:*regions)add_span(region);
        const float cx=origin_x+width*.5f,cy=origin_y+height*.5f;
        constexpr float extent=kHitMarkerRadius+kHitMarkerTolerance;
        add_span({cx-extent,cy-extent,cx+extent,cy+extent});
        std::sort(seed_spans_.begin(),seed_spans_.end());
        std::size_t merged=0;
        for(const auto span:seed_spans_) {
            if(merged && span.first<=seed_spans_[merged-1].second)
                seed_spans_[merged-1].second=std::max(seed_spans_[merged-1].second,span.second);
            else seed_spans_[merged++]=span;
        }
        seed_spans_.resize(merged);
      }
      for(const auto span:seed_spans_) for(int seed_x=span.first;seed_x<span.second;++seed_x) {
        const int start=seed_y*width+seed_x;
        const int color = color_at(seed_x,seed_y);
        if (!color) continue;
        component_.clear(); runs_.clear();
        int l=width,t=height,r=0,b=0;
        int first_pixel=start;
        int n=0;
        double sx=0,sy=0;
        const auto take_run = [&](int x,int y) {
            int left=x,right=x;
            while(left>0 && color_at(left-1,y)==color) --left;
            while(right+1<width && color_at(right+1,y)==color) ++right;
            std::fill(mask_.begin()+y*width+left,mask_.begin()+y*width+right+1,0);
            runs_.push_back({left,right,y});
            first_pixel=std::min(first_pixel,y*width+left);
            l=std::min(l,left);r=std::max(r,right);t=std::min(t,y);b=std::max(b,y);
            const int count=right-left+1;n+=count;
            sx+=(double(left)+right)*count*.5;sy+=double(y)*count;
            // Bounds only grow. Once this component cannot fit a valid cue,
            // stop materializing its per-pixel shape; still consume its runs
            // so the remaining background cannot be rediscovered as fragments.
            if(r-l+1<=kMaximumCueExtent && b-t+1<=kMaximumCueExtent) {
                for(int xx=left;xx<=right;++xx)component_.push_back(y*width+xx);
            } else component_.clear();
            return right;
        };
        take_run(seed_x,seed_y);
        for(std::size_t j=0;j<runs_.size();++j) {
            const auto run=runs_[j]; // queue growth can reallocate runs_
            for(int yy:{run.y-1,run.y+1}) {
                if(yy<0 || yy>=height)continue;
                const int end=std::min(width-1,run.right+1);
                for(int xx=std::max(0,run.left-1);xx<=end;++xx) {
                    if(color_at(xx,yy)==color)xx=take_run(xx,yy);
                }
            }
        }
        const int w=r-l+1,h=b-t+1;
        // Compact markers include circles, small diamonds and COD nemesis skulls. These
        // are proposals, not semantic proof; bars and large pings are excluded.
        if (w<kMinimumCueExtent || h<kMinimumCueExtent || w>kMaximumCueExtent || h>kMaximumCueExtent || n<8 || l==0 || t==0 || r==width-1 || b==height-1) continue;
        const float aspect=float(std::min(w,h))/std::max(w,h),fill=float(n)/(w*h);
        if (aspect<.65f || fill<.22f || fill>.94f) continue;
        const float cx=(l+r)*.5f,cy=(t+b)*.5f,rx=w*.5f,ry=h*.5f;
        int sectors=0,outer=0,corner=0;
        for (int pos:component_) {
            const float x=(pos%width-cx)/rx,y=(pos/width-cy)/ry,rr=x*x+y*y;
            if (rr>.25f) {
                const float angle=std::atan2(y,x)+3.14159265f;
                sectors |= 1<<std::min(7,int(angle*8/6.2831853f)); ++outer;
            }
            if (rr>1.25f) ++corner;
        }
        int coverage=0;
        for (int k=0;k<8;++k) coverage+=(sectors>>k)&1;
        if (coverage<7 || float(corner)/n>.12f || float(outer)/n<.5f) continue;
        const float centering=std::hypot(float(sx/n)-cx,float(sy/n)-cy)/std::max(w,h);
        if (centering>.16f) continue;
        const float score=.4f*aspect+.3f*(coverage/8.f)+.3f*(1-std::min(1.f,centering*5));
        ordered_observations_.push_back({first_pixel,{cx+origin_x,cy+origin_y,float(std::max(w,h)),score,color}});
      }
    }
    // Sparse seed discovery can visit neighbouring components in a different
    // order. Restore the original raster order before association/tie handling.
    std::sort(ordered_observations_.begin(),ordered_observations_.end(),
        [](const auto& a,const auto& b){return a.first<b.first;});
    for (const auto& entry:ordered_observations_) observations_.push_back(entry.second);
    // A hit marker is a screen-centred composition of four tips. Individual
    // tips can satisfy the compact cue test; reject the composition without
    // masking legitimate circles or skulls close to the reticle.
    std::vector<bool> hit_marker(observations_.size(), false);
    const float center_x=origin_x+width*.5f, center_y=origin_y+height*.5f;
    for (std::size_t i=0;i<observations_.size();++i) {
        const auto& a=observations_[i];
        const float dx=a.x-center_x,dy=a.y-center_y;
        if (dx>=-kHitMarkerTolerance || dy>=-kHitMarkerTolerance || dx<-kHitMarkerRadius || dy<-kHitMarkerRadius || std::abs(dx-dy)>kHitMarkerTolerance) continue;
        std::size_t partners[3]={observations_.size(),observations_.size(),observations_.size()};
        for (std::size_t j=0;j<observations_.size();++j) {
            const auto& b=observations_[j];
            if (b.color!=a.color || b.size/a.size<.6f || b.size/a.size>1.67f) continue;
            const float expected_x[3]={center_x-dx,center_x+dx,center_x-dx};
            const float expected_y[3]={center_y+dy,center_y-dy,center_y-dy};
            for(int k=0;k<3;++k)
                if(std::hypot(b.x-expected_x[k],b.y-expected_y[k])<=kHitMarkerTolerance) partners[k]=j;
        }
        if (partners[0]<observations_.size() && partners[1]<observations_.size() && partners[2]<observations_.size()) {
            hit_marker[i]=true;
            for(auto j:partners) hit_marker[j]=true;
        }
    }
    std::size_t kept=0;
    for(std::size_t i=0;i<observations_.size();++i)
        if(!hit_marker[i]) observations_[kept++]=observations_[i];
    observations_.resize(kept);
    return observations_;
}

void CueIdentity::reset() { active_.reset(); last_ns_=0; velocity_x_=velocity_y_=0; fresh_=false; }
std::optional<CueSearchRegion> CueIdentity::search_region() const {
    if(!active_)return std::nullopt;
    return CueSearchRegion{active_->x-kCueMatchDistance,active_->y-kCueMatchDistance,
        active_->x+kCueMatchDistance,active_->y+kCueMatchDistance};
}
void CueIdentity::seed(const CueObservation& cue, std::uint64_t timestamp) {
    // A confirming body must not erase the correspondence established by
    // update(), nor reset its velocity every frame. A new pairing is a new id.
    if (fresh_ && active_ && last_ns_==timestamp &&
        cue.x==active_->x && cue.y==active_->y && cue.color==active_->color) return;
    ++identity_;
    active_=cue; last_ns_=timestamp; velocity_x_=velocity_y_=0; fresh_=true;
}
void CueIdentity::update(const std::vector<CueObservation>& cues, std::uint64_t timestamp) {
    fresh_=false;
    if (!active_) return;
    if (!timestamp || timestamp<last_ns_ || timestamp-last_ns_>50'000'000ull) { reset(); return; }
    // Reprocessing a capture cannot prolong identity or body authority.
    if (timestamp==last_ns_) return;
    const float dt=float(timestamp-last_ns_)*1e-9f;
    float dx=velocity_x_*dt,dy=velocity_y_*dt;
    const float length=std::hypot(dx,dy);
    if (length>36) { dx*=36/length; dy*=36/length; }
    const CueObservation* best=nullptr;
    float best_cost=1e9f,second_cost=1e9f;
    for (const auto& cue:cues) {
        if (cue.color!=active_->color || std::hypot(cue.x-active_->x,cue.y-active_->y)>kCueMatchDistance) continue;
        const float ratio=cue.size/active_->size;
        const float residual=std::hypot(cue.x-active_->x-dx,cue.y-active_->y-dy);
        if (ratio<.5f || ratio>2 || residual>36) continue;
        const float cost=residual+8*std::abs(std::log(ratio));
        if (cost<best_cost) { second_cost=best_cost; best_cost=cost; best=&cue; }
        else second_cost=std::min(second_cost,cost);
    }
    if (!best || second_cost-best_cost<4) return;
    velocity_x_=.5f*velocity_x_+.5f*(best->x-active_->x)/dt;
    velocity_y_=.5f*velocity_y_+.5f*(best->y-active_->y)/dt;
    const float size=.8f*active_->size+.2f*best->size;
    active_=*best; active_->size=size; last_ns_=timestamp; fresh_=true;
}
} // namespace vision_native
