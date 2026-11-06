// Original logged boxes with nearest decoded video frame. Offline CPU selector only.
#include "vision_native/target_selector.h"
#include <fcntl.h>
#include <io.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
template<class T>void read(T& v){std::cin.read(reinterpret_cast<char*>(&v),sizeof(v));if(!std::cin)throw std::runtime_error("truncated input");}
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("output.csv");_setmode(_fileno(stdin),_O_BINARY);
 vision_native::VisionTargetSelector s(640,512,190,true,.25f,.8f);s.set_cue_identity_enabled(true);
 std::vector<unsigned char> pixels(640*512*4);std::ofstream out(argv[1]);out<<std::setprecision(9);
 out<<"frame,ns,has_target,x,y,source,selected,fire,generation,reason,friendly,geometry,confidence,ms,outcome,admitted,gates\n";
 unsigned frame=0;
 while(std::cin.peek()!=EOF){vision_native::DetectionBatch b;unsigned n;read(b.captured_at_ns);read(n);if(n>1000)throw std::runtime_error("count");b.frame_id=++frame;
 for(unsigned i=0;i<n;++i){vision_native::Detection d;read(d.x1);read(d.y1);read(d.x2);read(d.y2);read(d.conf);b.detections.push_back(d);}
 std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size());if(!std::cin)throw std::runtime_error("pixels");
 auto start=std::chrono::steady_clock::now();auto r=s.select_with_frame(b,{pixels.data(),640,512,2560,0,0,640,512,vision_native::PixelFormat::BGRA8});
 auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();int friendly=0,geometry=0,confidence=0;
 for(unsigned i=0;i<r.selection_diagnostics.count;++i){std::string gate=r.selection_diagnostics.boxes[i].gate;friendly+=gate=="friendly_color";geometry+=gate=="body_geometry_gate";confidence+=gate=="pickup_confidence";}
 out<<frame<<','<<b.captured_at_ns<<','<<r.has_target<<','<<r.target_x<<','<<r.target_y<<','<<r.target_source<<','<<r.has_selected_detection<<','<<r.fire_authority<<','<<r.selector_target_generation<<','<<r.selection_diagnostics.cue_reason<<','<<friendly<<','<<geometry<<','<<confidence<<','<<ms<<','<<r.selection_diagnostics.outcome<<','<<r.selection_diagnostics.admitted<<',';
 for(unsigned i=0;i<r.selection_diagnostics.count;++i)out<<i<<':'<<r.selection_diagnostics.boxes[i].gate<<';';
 out<<'\n';
 }return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
