#include "reference.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace vision_native;
bool equal(const CueObservation&a,const CueObservation&b){return a.x==b.x&&a.y==b.y&&a.size==b.size&&a.score==b.score&&a.color==b.color;}
int main(){try {
 ReferenceCueDetector full,greedy;CueDetector production;size_t checked=0,frames=0;
 for(unsigned seed:{23102329u,78102331u}) {
  std::mt19937 rng(seed);
  for(int frame=0;frame<2400;++frame){
   const int w=80+int(rng()%160),h=80+int(rng()%120),ch=frame%2?4:3,pitch=w*ch+int(rng()%20);
   const auto fmt=ch==4?PixelFormat::BGRA8:PixelFormat::RGB8;
   std::vector<unsigned char> pixels(pitch*h,0);
   auto pixel=[&](int x,int y,int color){if(x<0||x>=w||y<0||y>=h)return;auto*p=pixels.data()+y*pitch+x*ch;p[ch==4?2:0]=255;p[1]=color==1?230:0;p[ch==4?0:2]=0;};
   for(int i=0;i<20;++i){int x=int(rng()%w),y=int(rng()%h),radius=2+int(rng()%7),color=1+int(rng()%2);
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){const int d=dx*dx+dy*dy;if(d<=radius*radius&&d>=(radius-1)*(radius-1))pixel(x+dx,y+dy,color);}
   }
   if(frame%3==0)for(int y=h/2;y<h;++y)for(int x=0;x<20;++x)pixel(x,y,1);
   region_rects.clear();
   for(int i=0;i<3;++i){float x=float(rng()%w)+.25f,y=float(rng()%h)+.5f;region_rects.push_back({x-10,y-7,x+10,y+7});}
   // Ensure boundary cases contain a full-detector cue whose centre is
   // precisely on a region edge, including every seed-row/column phase.
   region_relevance_enabled=false;
   const auto all=full.detect(pixels.data(),w,h,pitch,fmt,0,0);
   if(!all.empty()){auto c=all[frame%all.size()];region_rects.push_back({c.x,c.y,c.x+1,c.y+1});}
   region_rects.push_back({w*.5f-36,h*.5f-36,w*.5f+36,h*.5f+36});
   region_relevance_enabled=true;
   const auto& subset=greedy.detect(pixels.data(),w,h,pitch,fmt,0,0);
   const int ox=frame%2?37:0,oy=frame%2?-19:0;
   std::vector<CueSearchRegion> regions;
   for(auto&r:region_rects)regions.push_back({r[0]+ox,r[1]+oy,r[2]+ox,r[3]+oy});
   const auto& native=production.detect(pixels.data(),w,h,pitch,fmt,ox,oy,&regions);
   if(native.size()!=subset.size())throw std::runtime_error("production region count differs");
   for(std::size_t i=0;i<native.size();++i){auto c=native[i];c.x-=ox;c.y-=oy;if(!equal(c,subset[i]))throw std::runtime_error("production region value/order differs");}
   for(auto&c:all){bool relevant=false;for(auto&r:region_rects)relevant|=c.x>=r[0]&&c.x<=r[2]&&c.y>=r[1]&&c.y<=r[3];
    if(relevant){++checked;if(std::none_of(subset.begin(),subset.end(),[&](auto&d){return equal(c,d);}))throw std::runtime_error("required cue missed");}
   }
   for(auto&c:subset)if(std::none_of(all.begin(),all.end(),[&](auto&d){return equal(c,d);}))throw std::runtime_error("spurious cue created");
   ++frames;
  }
 }
 std::cout<<"frames="<<frames<<" required_cues="<<checked<<" all preserved; no added cues\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
