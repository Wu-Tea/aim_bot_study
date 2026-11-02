#include "vision_native/cue_identity.h"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){
 if(argc!=2)return 2;
 std::ifstream input(argv[1],std::ios::binary);
 std::vector<unsigned char> pixels(640*512*4);
 vision_native::CueDetector detector;
 for(unsigned frame=0;input.read(reinterpret_cast<char*>(pixels.data()),pixels.size());++frame){
  const auto& cues=detector.detect(pixels.data(),640,512,2560,vision_native::PixelFormat::BGRA8,0,0);
  std::cout<<frame;
  for(const auto& c:cues)std::cout<<';'<<c.x<<','<<c.y<<','<<c.size<<','<<c.score;
  std::cout<<'\n';
 }
 return input.gcount()==0 ? 0 : 3;
}
