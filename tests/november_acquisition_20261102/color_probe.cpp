#include "vision_native/cue_identity.h"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){if(argc!=6)return 2;std::vector<unsigned char> p(640*512*4);std::ifstream f(argv[1],std::ios::binary);f.read((char*)p.data(),p.size());
 int l=std::stoi(argv[2]),t=std::stoi(argv[3]),r=std::stoi(argv[4]),b=std::stoi(argv[5]);vision_native::CueDetector d;d.set_enemy_color(pipeline_contract::MarkerPalette{}.friendly);
 for(const auto& c:d.detect(p.data()+(t*640+l)*4,r-l,b-t,640*4,vision_native::PixelFormat::BGRA8,l,t))std::cout<<c.x<<','<<c.y<<','<<c.size<<','<<c.score<<'\n';}
