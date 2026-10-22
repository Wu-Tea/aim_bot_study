#include "vision_native/cue_identity.h"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){
 if(argc!=2)return 1;std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> p(640*512*4);file.read(reinterpret_cast<char*>(p.data()),p.size());if(file.gcount()!=p.size())return 2;
 vision_native::CueDetector detector;
 for(const auto& c:detector.detect(p.data(),640,512,640*4,vision_native::PixelFormat::BGRA8,0,0))std::cout<<c.x<<','<<c.y<<','<<c.size<<','<<c.score<<','<<c.color<<'\n';
}
