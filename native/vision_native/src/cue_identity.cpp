#include "vision_native/cue_identity.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vision_native {
namespace {
int enemy_color(const unsigned char* p, PixelFormat format) {
    const float r = p[format == PixelFormat::BGRA8 ? 2 : 0], g = p[1];
    const float b = p[format == PixelFormat::BGRA8 ? 0 : 2];
    const float hi = std::max({r,g,b}), lo = std::min({r,g,b}), delta = hi-lo;
    if (delta == 0 || hi == 0 || 255*delta/hi < 120) return 0;
    float hue = 30*(hi == r ? (g-b)/delta : hi == g ? 2+(b-r)/delta : 4+(r-g)/delta);
    if (hue < 0) hue += 180;
    if (hue >= 20 && hue <= 35 && hi >= 120) return 1;
    if ((hue <= 10 || hue >= 170) && hi >= 80) return 2;
    return 0;
}
}

const std::vector<CueObservation>& CueDetector::detect(const unsigned char* pixels,
    int width, int height, int pitch, PixelFormat format, int origin_x, int origin_y) {
    observations_.clear();
    const int channels = format == PixelFormat::BGRA8 ? 4 : 3;
    if (!pixels || width <= 0 || height <= 0 || pitch < std::int64_t(width)*channels ||
        std::int64_t(width)*height > std::numeric_limits<int>::max()) return observations_;
    mask_.resize(std::size_t(width)*height);
    for (int y=0; y<height; ++y)
        for (int x=0; x<width; ++x)
            mask_[y*width+x] = static_cast<unsigned char>(enemy_color(pixels+std::size_t(y)*pitch+x*channels,format));
    for (int start=0; start<width*height; ++start) {
        const int color = mask_[start];
        if (!color) continue;
        component_.clear(); component_.push_back(start); mask_[start]=0;
        int l=width,t=height,r=0,b=0;
        double sx=0,sy=0;
        for (std::size_t j=0; j<component_.size(); ++j) {
            const int pos=component_[j],x=pos%width,y=pos/width;
            l=std::min(l,x); r=std::max(r,x); t=std::min(t,y); b=std::max(b,y); sx+=x; sy+=y;
            for (int yy=std::max(0,y-1); yy<=std::min(height-1,y+1); ++yy)
                for (int xx=std::max(0,x-1); xx<=std::min(width-1,x+1); ++xx) {
                    const int next=yy*width+xx;
                    if (mask_[next]==color) { mask_[next]=0; component_.push_back(next); }
                }
        }
        const int w=r-l+1,h=b-t+1,n=static_cast<int>(component_.size());
        // Compact colored markers include circle and COD nemesis skull. These
        // are proposals, not semantic proof; bars and large pings are excluded.
        if (w<4 || h<4 || w>18 || h>18 || n<8 || l==0 || t==0 || r==width-1 || b==height-1) continue;
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
        observations_.push_back({cx+origin_x,cy+origin_y,float(std::max(w,h)),score,color});
    }
    // A hit marker is a screen-centred composition of four tips. Individual
    // tips can satisfy the compact cue test; reject the composition without
    // masking legitimate circles or skulls close to the reticle.
    std::vector<bool> hit_marker(observations_.size(), false);
    const float center_x=origin_x+width*.5f, center_y=origin_y+height*.5f;
    for (std::size_t i=0;i<observations_.size();++i) {
        const auto& a=observations_[i];
        const float dx=a.x-center_x,dy=a.y-center_y;
        if (dx>=-4 || dy>=-4 || dx<-32 || dy<-32 || std::abs(dx-dy)>4) continue;
        std::size_t partners[3]={observations_.size(),observations_.size(),observations_.size()};
        for (std::size_t j=0;j<observations_.size();++j) {
            const auto& b=observations_[j];
            if (b.color!=a.color || b.size/a.size<.6f || b.size/a.size>1.67f) continue;
            const float expected_x[3]={center_x-dx,center_x+dx,center_x-dx};
            const float expected_y[3]={center_y+dy,center_y-dy,center_y-dy};
            for(int k=0;k<3;++k)
                if(std::hypot(b.x-expected_x[k],b.y-expected_y[k])<=4.f) partners[k]=j;
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
        if (cue.color!=active_->color || std::hypot(cue.x-active_->x,cue.y-active_->y)>42) continue;
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
