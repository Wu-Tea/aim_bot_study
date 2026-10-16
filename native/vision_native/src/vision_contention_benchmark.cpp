#include "bench_support.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/cuda_graphics_mapping.h"
#include "color_readback.h"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
using namespace vision_bench;

struct Registration {
    cudaGraphicsResource_t value=nullptr;
    ~Registration() { if (value) cudaGraphicsUnregisterResource(value); }
};
std::string detections(const vision_native::DetectionBatch& batch) {
    // Nine digits round-trip float32; preserve order and all decoded box fields.
    std::ostringstream out; out << std::setprecision(9) << batch.detections.size();
    for (const auto& d:batch.detections)
        out << ';' << d.class_id << ':' << d.conf << ':' << d.x1 << ':' << d.y1 << ':' << d.x2 << ':' << d.y2;
    return out.str();
}
int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Usage: vision_contention_benchmark model frames.bgra output-prefix baseline|no-flush|no-graph|wait|direct|both|color-pageable|color-pinned|map-readonly");
        const std::string variant=argv[4];
        if (variant!="baseline" && variant!="no-flush" && variant!="no-graph" && variant!="wait" && variant!="direct" && variant!="both" && variant!="color-pageable" && variant!="color-pinned" && variant!="map-readonly") throw std::runtime_error("Unknown variant");
        const bool color_test=variant=="color-pageable" || variant=="color-pinned";
        constexpr int w=640,h=512; constexpr size_t bytes=w*h*4;
        std::ifstream frames(argv[2], std::ios::binary|std::ios::ate);
        const auto size=frames.tellg();
        if (size<=0 || size%bytes || size/bytes>4096) throw std::runtime_error("Invalid raw BGRA fixture");
        frames.seekg(0); Device d;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=w; desc.Height=h; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<Ptr<ID3D11Texture2D>> sources(static_cast<size_t>(size)/bytes);
        std::vector<std::vector<unsigned char>> reference;
        std::vector<unsigned char> host(bytes);
        for (auto& source:sources) {
            if (!frames.read(reinterpret_cast<char*>(host.data()), bytes)) throw std::runtime_error("Short fixture read");
            D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=host.data(); data.SysMemPitch=w*4;
            check(d.device->CreateTexture2D(&desc, &data, &source));
            if(color_test) reference.push_back(host);
        }
        Ptr<ID3D11Texture2D> destination;
        check(d.device->CreateTexture2D(&desc, nullptr, &destination));
        Registration resource;
        check(cudaGraphicsD3D11RegisterResource(&resource.value, destination.Get(), cudaGraphicsRegisterFlagsNone));
        // CUDA only reads this texture. This optional driver hint changes no
        // ownership boundaries: D3D still writes only while it is unmapped.
        if(variant=="map-readonly")
            check(cudaGraphicsResourceSetMapFlags(resource.value,cudaGraphicsMapFlagsReadOnly));
        vision_native::TensorRTEngineOptions options;
        options.use_cuda_graph=variant!="no-graph";
        options.direct_array_preprocess=variant=="direct" || variant=="both";
        options.blocking_output_wait=variant!="wait" && variant!="both";
        vision_native::TensorRTEngine engine(argv[1], options);
        if (engine.input_width()!=480 || engine.input_height()!=384) throw std::runtime_error("Requires frozen 480x384 model");
        vision_native::ColorReadbackBuffer color_buffer(variant=="color-pinned");
        if(color_test && (!color_buffer.ensure(bytes) ||
            (variant=="color-pinned" && color_buffer.mode()!=vision_native::ColorReadbackMode::Pinned)))
            throw std::runtime_error("Requested color buffer mode unavailable");
        runtime_app::PrecisionTickScheduler scheduler(0);
        std::cout << "READY " << GetCurrentProcessId() << " " << sources.size() << std::endl;
        double hz, seconds; int phase=0;
        while (std::cin >> hz >> seconds && hz>0) {
            validate_phase(hz, seconds);
            struct Row { double qpc,t,wall,copy,map,unmap,color; size_t source,color_bytes; vision_native::DetectionBatch batch; };
            std::vector<Row> rows; rows.reserve(static_cast<size_t>(hz*seconds+1));
            auto cpu_seconds=[](){FILETIME c,e,k,u; if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))throw std::runtime_error("CPU accounting failed");
                return (double((uint64_t(k.dwHighDateTime)<<32)|k.dwLowDateTime)+double((uint64_t(u.dwHighDateTime)<<32)|u.dwLowDateTime))/1e7;};
            const double cpu_begin=cpu_seconds();
            const auto begin=Clock::now(); auto next=begin;
            const auto period=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1/hz));
            std::cout << "START " << std::setprecision(17) << qpc_seconds() << std::endl;
            while (std::chrono::duration<double>(Clock::now()-begin).count()<seconds) {
                scheduler.wait_until(next); const auto start=Clock::now();
                const double t=std::chrono::duration<double>(start-begin).count();
                if (t>=seconds) break;
                const double qpc=qpc_seconds(); const auto index=rows.size()%sources.size();
                // Match the production capture owner's CopySubresourceRegion + Flush.
                D3D11_BOX roi{0,0,0,w,h,1};
                d.context->CopySubresourceRegion(destination.Get(),0,0,0,0,sources[index].Get(),0,&roi);
                // Exploratory removal of explicit submission, confined to this executable.
                if (variant!="no-flush") d.context->Flush();
                const auto copied=Clock::now();
                vision_native::CudaGraphicsMapping mapping(resource.value, engine.cuda_stream());
                const auto mapped=Clock::now();
                auto batch=engine.infer_bgra_array_roi(mapping.array(), w,h,0,0,w,h,.20f);
                const auto inferred=Clock::now();
                // Bounded transfer-size matrix, not a replacement selector. Offsets exercise
                // cropped reads; both color variants perform exactly the same work.
                constexpr int widths[]{64,160,320,640}, heights[]{64,128,256,512};
                const int rw=widths[index%4],rh=heights[index%4];
                const int left=int(index*17)%(w-rw+1),top=int(index*23)%(h-rh+1);
                const size_t color_bytes=color_test ? size_t(rw)*rh*4 : 0;
                if(color_test){
                    check(cudaMemcpy2DFromArrayAsync(color_buffer.data(),size_t(rw)*4,
                        mapping.array(),left*4,top,size_t(rw)*4,rh,cudaMemcpyDeviceToHost,engine.cuda_stream()));
                    check(cudaStreamSynchronize(engine.cuda_stream()));
                }
                const auto color_done=Clock::now();
                mapping.unmap(); const auto end=Clock::now();
                // Validate every byte after the timed GPU path. CPU accounting includes this
                // identical comparison in both color variants; no acceptance by checksum.
                if(color_test) for(int y=0;y<rh;++y)
                    if(std::memcmp(color_buffer.data()+size_t(y)*rw*4,
                        reference[index].data()+(size_t(top+y)*w+left)*4,size_t(rw)*4)!=0)
                        throw std::runtime_error("Color ROI pixels differ from source fixture");
                rows.push_back({qpc,t,milliseconds(start,end),milliseconds(start,copied),milliseconds(copied,mapped),milliseconds(color_done,end),color_test?milliseconds(inferred,color_done):0,index,color_bytes,std::move(batch)});
                // Production-style serial service: newest fixture selected when service starts,
                // no queued frames or catch-up bursts. This is not DXGI present-to-result age.
                next=start+period;
            }
            const double phase_wall=milliseconds(begin,Clock::now())/1000;
            const double cpu_used=cpu_seconds()-cpu_begin;
            const auto prefix=std::string(argv[3])+"-"+std::to_string(phase++);
            std::ofstream meta(prefix+"-cpu.json");
            meta.exceptions(std::ios::badbit|std::ios::failbit);
            meta<<std::setprecision(17)<<"{\"cpu_seconds\":"<<cpu_used<<",\"wall_seconds\":"<<phase_wall<<",\"frames\":"<<rows.size()<<"}\n";
            meta.close();
            std::ofstream out(prefix+".csv"), boxes(prefix+"-detections.csv");
            out.exceptions(std::ios::badbit|std::ios::failbit); boxes.exceptions(std::ios::badbit|std::ios::failbit);
            out << "qpc_s,t,source,wall_ms,copy_submit_ms,map_ms,unmap_ms,preprocess_ms,infer_ms,output_sync_ms,decode_ms,color_ms,color_bytes\n" << std::setprecision(17);
            boxes << "frame,source,signature\n";
            size_t frame=0;
            for (const auto& r:rows) {
                out<<r.qpc<<','<<r.t<<','<<r.source<<','<<r.wall<<','<<r.copy<<','<<r.map<<','<<r.unmap<<','<<r.batch.preprocess_ms<<','<<r.batch.infer_ms<<','<<r.batch.output_copy_sync_ms<<','<<r.batch.decode_ms<<','<<r.color<<','<<r.color_bytes<<'\n';
                boxes<<frame++<<','<<r.source<<','<<detections(r.batch)<<'\n';
            }
            out.close(); boxes.close(); std::cout << "DONE " << rows.size() << std::endl;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
}
