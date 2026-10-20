// Offline, same-frame experiment. No controller, capture or configuration access.
#include "vision_native/tensorrt_engine.h"
#include "vision_native/target_selector.h"
#include <cuda_runtime.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

constexpr int W=640,H=512;
struct Cue {float x,y,score;int l,t,r,b,n,color;};
struct Pair {int d,c;float cost;};
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
void check(cudaError_t e){if(e!=cudaSuccess)throw std::runtime_error(cudaGetErrorString(e));}
struct Array {cudaArray_t p=nullptr;~Array(){if(p)cudaFreeArray(p);}};

// Same HSV ranges as production. Connected components are independent of person boxes.
int color_of(const unsigned char* p){
    float r=p[2],g=p[1],b=p[0],hi=std::max({r,g,b}),lo=std::min({r,g,b}),v=hi;
    if(hi==lo||hi==0)return 0;
    float s=255*(hi-lo)/hi,h=hi==r?(g-b)/(hi-lo):hi==g?2+(b-r)/(hi-lo):4+(r-g)/(hi-lo);
    h*=30;if(h<0)h+=180;
    if(s<120)return 0;
    if(h>=20&&h<=35&&v>=120)return 1;
    if((h<=10||h>=170)&&v>=80)return 2;
    return 0;
}
std::vector<Cue> detect_cues(const std::vector<unsigned char>& pixels){
    std::vector<unsigned char> mask(W*H);
    for(int i=0;i<W*H;++i)mask[i]=static_cast<unsigned char>(color_of(&pixels[4*i]));
    std::vector<int> queue;queue.reserve(4096);
    std::vector<Cue> out;
    for(int start=0;start<W*H;++start){
        int col=mask[start];if(!col)continue;
        queue.clear();queue.push_back(start);mask[start]=0;
        int l=W,t=H,r=0,b=0;double sx=0,sy=0;
        for(size_t j=0;j<queue.size();++j){
            int pos=queue[j],x=pos%W,y=pos/W;l=std::min(l,x);r=std::max(r,x);t=std::min(t,y);b=std::max(b,y);sx+=x;sy+=y;
            for(int yy=std::max(0,y-1);yy<=std::min(H-1,y+1);++yy)
                for(int xx=std::max(0,x-1);xx<=std::min(W-1,x+1);++xx){
                    int next=yy*W+xx;if(mask[next]==col){mask[next]=0;queue.push_back(next);}
                }
        }
        int w=r-l+1,h=b-t+1,n=static_cast<int>(queue.size());
        // Development hypothesis: compact small marker, not health bar/text/large ping.
        if(w<4||h<4||w>18||h>18||n<8||l==0||t==0||r==W-1||b==H-1)continue;
        float aspect=float(std::min(w,h))/std::max(w,h),fill=float(n)/(w*h);
        if(aspect<.65f||fill<.22f||fill>.94f)continue;
        float cx=(l+r)*.5f,cy=(t+b)*.5f,rx=w*.5f,ry=h*.5f;
        int sectors=0,outer=0,corner=0;
        for(int pos:queue){
            float x=(pos%W-cx)/rx,y=(pos/W-cy)/ry,rr=x*x+y*y;
            if(rr>.25f){float a=std::atan2(y,x)+3.14159265f;int k=std::min(7,int(a*8/6.2831853f));sectors|=1<<k;++outer;}
            if(rr>1.25f)++corner;
        }
        int coverage=0;for(int k=0;k<8;++k)coverage+=(sectors>>k)&1;
        if(coverage<7||float(corner)/n>.12f||float(outer)/n<.5f)continue;
        float centering=std::hypot(float(sx/n)-cx,float(sy/n)-cy)/std::max(w,h);
        if(centering>.16f)continue;
        float score=.4f*aspect+.3f*(coverage/8.f)+.3f*(1-std::min(1.f,centering*5));
        out.push_back({cx,cy,score,l,t,r+1,b+1,n,col});
    }
    return out;
}

// Mutual best matching with an ambiguity margin: one cue and one person per pair.
// Missing cues do not remove persons; unmatched cues never create persons.
std::vector<Pair> associate(const std::vector<Cue>& cues,const std::vector<vision_native::Detection>& ds){
    std::vector<Pair> edges;
    for(int d=0;d<int(ds.size());++d){
        const auto& p=ds[d];float w=p.x2-p.x1,h=p.y2-p.y1;
        if(p.class_id!=0||p.is_friendly||w<8||h<12)continue;
        for(int c=0;c<int(cues.size());++c){
            auto q=cues[c];float cx=(p.x1+p.x2)*.5f;
            if(q.x<p.x1-.2f*w||q.x>p.x2+.2f*w||q.y<p.y1-std::max(36.f,.6f*h)||q.y>p.y1+.4f*h)continue;
            float dx=std::abs(q.x-cx)/std::max(16.f,w*.65f);
            float dy=std::abs(q.y-(p.y1-.08f*h))/std::max(24.f,h*.45f);
            float cost=dx*.55f+dy*.35f+(1-q.score)*.1f;
            if(cost<=.95f)edges.push_back({d,c,cost});
        }
    }
    std::vector<Pair> pairs;
    for(auto e:edges){bool clear=true;
        for(auto other:edges){if(e.d==other.d&&e.c==other.c)continue;
            if((e.d==other.d||e.c==other.c)&&other.cost<e.cost+.12f){clear=false;break;}}
        if(clear)pairs.push_back(e);
    }
    return pairs;
}

int main(int argc,char**argv){try{
    if(argc!=4)throw std::runtime_error("model out.jsonl fps; stdin 640x512 BGRA");
    double fps=std::stod(argv[3]);if(!std::isfinite(fps)||fps<1||fps>1000)throw std::runtime_error("invalid fps");
    _setmode(_fileno(stdin),_O_BINARY);
    vision_native::TensorRTEngine engine(argv[1]);
    if(engine.input_width()!=480||engine.input_height()!=384)throw std::runtime_error("expected 480x384 model");
    vision_native::VisionTargetSelector baseline(W,H,190,true,.2f,.65f);
    std::ofstream out(argv[2]);out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(7)<<std::boolalpha;
    std::vector<unsigned char> pixels(W*H*4),color(W*H*4);
    Array input;auto desc=cudaCreateChannelDesc<uchar4>();check(cudaMallocArray(&input.p,&desc,W,H));
    uint64_t frame=0;
    while(true){
        std::cin.read(reinterpret_cast<char*>(pixels.data()),pixels.size());
        if(std::cin.gcount()==0&&std::cin.eof())break;
        if(size_t(std::cin.gcount())!=pixels.size())throw std::runtime_error("truncated frame");
        auto ts=Clock::now();auto cues=detect_cues(pixels);double cue_ms=ms(ts);
        check(cudaMemcpy2DToArray(input.p,0,0,pixels.data(),W*4,W*4,H,cudaMemcpyHostToDevice));
        auto batch=engine.infer_bgra_array(input.p,W,H,.20f);
        batch.frame_id=frame+1;batch.captured_at_ns=1'000'000'000ull+uint64_t(std::llround(frame*1e9/fps));
        auto region=baseline.required_color_region(batch);vision_native::VisionResult result;
        ts=Clock::now();
        if(region){
            int rw=region->right-region->left,rh=region->bottom-region->top;
            check(cudaMemcpy2DFromArrayAsync(color.data(),rw*4,input.p,region->left*4,region->top,rw*4,rh,cudaMemcpyDeviceToHost,engine.cuda_stream()));
            check(cudaStreamSynchronize(engine.cuda_stream()));
            result=baseline.select_with_frame(batch,{color.data(),rw,rh,rw*4,region->left,region->top,W,H,vision_native::PixelFormat::BGRA8});
        }else result=baseline.select(batch);
        double baseline_ms=ms(ts);ts=Clock::now();
        auto pairs=associate(cues,result.detections);double assoc_ms=ms(ts);
        out<<"{\"frame\":"<<frame<<",\"t\":"<<frame/fps<<",\"has_target\":"<<result.has_target
           <<",\"source\":\""<<result.target_source<<"\",\"generation\":"<<result.selector_target_generation
           <<",\"selected\":"<<(result.has_selected_detection?int(result.selected_detection_index):-1)
           <<",\"target\":["<<result.target_x<<','<<result.target_y<<"],\"cue_ms\":"<<cue_ms
           <<",\"assoc_ms\":"<<assoc_ms<<",\"baseline_post_ms\":"<<baseline_ms<<",\"detections\":[";
        for(size_t i=0;i<result.detections.size();++i){if(i)out<<',';auto d=result.detections[i];
            out<<"{\"box\":["<<d.x1<<','<<d.y1<<','<<d.x2<<','<<d.y2<<"],\"conf\":"<<d.conf<<",\"class\":"<<d.class_id
               <<",\"friendly\":"<<d.is_friendly<<",\"has_cue\":"<<d.has_cue_point<<",\"cue\":["<<d.cue_x<<','<<d.cue_y<<"]}";}
        out<<"],\"cues\":[";
        for(size_t i=0;i<cues.size();++i){if(i)out<<',';auto q=cues[i];out<<"{\"xy\":["<<q.x<<','<<q.y<<"],\"box\":["<<q.l<<','<<q.t<<','<<q.r<<','<<q.b<<"],\"n\":"<<q.n<<",\"color\":"<<q.color<<",\"score\":"<<q.score<<'}';}
        out<<"],\"pairs\":[";for(size_t i=0;i<pairs.size();++i){if(i)out<<',';auto p=pairs[i];out<<"{\"d\":"<<p.d<<",\"c\":"<<p.c<<",\"cost\":"<<p.cost<<'}';}
        out<<"]}\n";++frame;
    }
    out.close();std::cerr<<"DONE frames="<<frame<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
