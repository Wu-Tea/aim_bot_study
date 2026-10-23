// Same translation unit exposes the private old/new classifiers solely here.
#include "../../native/vision_native/src/cue_identity.cpp"
#include "reference.cpp"
#include <iostream>
#include <random>
#include <stdexcept>
void same(const std::vector<vision_native::CueObservation>& a,const std::vector<vision_native::CueObservation>& b) {
 if(a.size()!=b.size())throw std::runtime_error("cue count differs");
 for(size_t i=0;i<a.size();++i)if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].size!=b[i].size||a[i].score!=b[i].score||a[i].color!=b[i].color)throw std::runtime_error("cue value/order differs");
}
int main(){try{
 using namespace vision_native;
 for(int r=0;r<256;++r)for(int g=0;g<256;++g)for(int b=0;b<256;++b) {
  unsigned char bgra[]={static_cast<unsigned char>(b),static_cast<unsigned char>(g),static_cast<unsigned char>(r),255};
  unsigned char rgb[]={static_cast<unsigned char>(r),static_cast<unsigned char>(g),static_cast<unsigned char>(b)};
  for(auto format:{PixelFormat::BGRA8,PixelFormat::RGB8}) {
   const auto* p=format==PixelFormat::BGRA8?bgra:rgb;
   if(enemy_color(p,format)!=reference_enemy_color(p,format)) {
    std::cerr<<"color mismatch "<<r<<','<<g<<','<<b<<'\n';return 1;
   }
  }
 }
 std::cout<<"all 16777216 RGB colors in both formats identical\n";
 CueDetector actual;ReferenceCueDetector reference;
 unsigned cases=0,accepted=0;
 for(unsigned seed:{23102311u,98102731u}) {
  std::mt19937 rng(seed);
  for(int c=0;c<2400;++c) {
   int w=32+rng()%97,h=24+rng()%105,channels=c%2?3:4,pitch=w*channels+int(rng()%8);
   auto format=channels==4?PixelFormat::BGRA8:PixelFormat::RGB8;
   std::vector<unsigned char> p(pitch*h);
   for(auto& byte:p)byte=c%4?0:static_cast<unsigned char>(rng());
   const auto paint=[&](int x,int y,int red,int green,int blue) {if(x<0||x>=w||y<0||y>=h)return;auto* q=p.data()+y*pitch+x*channels;q[channels==4?2:0]=static_cast<unsigned char>(red);q[1]=static_cast<unsigned char>(green);q[channels==4?0:2]=static_cast<unsigned char>(blue);};
   // Small/border/over-size rings at all seed phases, mixed colors and ordering.
   for(int j=0;j<6;++j) {
    int x=rng()%w,y=rng()%h,rad=2+rng()%10;
    for(int dy=-rad;dy<=rad;++dy)for(int dx=-rad;dx<=rad;++dx) {
     int d=dx*dx+dy*dy;
     if(d<=rad*rad&&d>=(rad-2)*(rad-2))paint(x+dx,y+dy,255,j%2?0:220,0);
    }
   }
   // Large connected clutter, thin diagonal bridges and dense adversarial masks.
   if(c%7==0)for(int y=0;y<h;++y)for(int x=0;x<w/2;++x)paint(x,y,255,220,0);
   if(c%11==0)for(int y=0;y<h;++y)paint(y%w,y,255,0,0);
   if(c%13==0)for(int y=0;y<h;++y)for(int x=0;x<w;++x)if((x+y)%2==0)paint(x,y,255,0,0);
   int ox=int(rng()%81)-40,oy=int(rng()%81)-40;
   const auto& a=actual.detect(p.data(),w,h,pitch,format,ox,oy);
   const auto& b=reference.detect(p.data(),w,h,pitch,format,ox,oy);
   try{same(a,b);}catch(...){std::cerr<<"seed="<<seed<<" case="<<c<<'\n';throw;}
   accepted+=static_cast<unsigned>(a.size());++cases;
  }
 }
 std::cout<<"randomized frames="<<cases<<" accepted cues="<<accepted<<" all identical\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
