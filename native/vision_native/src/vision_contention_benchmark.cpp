#include "bench_support.h"
#include "vision_native/tensorrt_engine.h"
#include "vision_native/cuda_graphics_mapping.h"
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
        if (argc != 5) throw std::runtime_error("Usage: vision_contention_benchmark model frames.bgra output-prefix baseline|no-flush|no-graph");
        const std::string variant=argv[4];
        if (variant!="baseline" && variant!="no-flush" && variant!="no-graph") throw std::runtime_error("Unknown variant");
        constexpr int w=640,h=512; constexpr size_t bytes=w*h*4;
        std::ifstream frames(argv[2], std::ios::binary|std::ios::ate);
        const auto size=frames.tellg();
        if (size<=0 || size%bytes || size/bytes>4096) throw std::runtime_error("Invalid raw BGRA fixture");
        frames.seekg(0); Device d;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=w; desc.Height=h; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<Ptr<ID3D11Texture2D>> sources(static_cast<size_t>(size)/bytes);
        std::vector<unsigned char> host(bytes);
        for (auto& source:sources) {
            if (!frames.read(reinterpret_cast<char*>(host.data()), bytes)) throw std::runtime_error("Short fixture read");
            D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=host.data(); data.SysMemPitch=w*4;
            check(d.device->CreateTexture2D(&desc, &data, &source));
        }
        Ptr<ID3D11Texture2D> destination;
        check(d.device->CreateTexture2D(&desc, nullptr, &destination));
        Registration resource;
        check(cudaGraphicsD3D11RegisterResource(&resource.value, destination.Get(), cudaGraphicsRegisterFlagsNone));
        vision_native::TensorRTEngineOptions options;
        options.use_cuda_graph=variant!="no-graph";
        vision_native::TensorRTEngine engine(argv[1], options);
        if (engine.input_width()!=480 || engine.input_height()!=384) throw std::runtime_error("Requires frozen 480x384 model");
        runtime_app::PrecisionTickScheduler scheduler(0);
        std::cout << "READY " << GetCurrentProcessId() << " " << sources.size() << std::endl;
        double hz, seconds; int phase=0;
        while (std::cin >> hz >> seconds && hz>0) {
            validate_phase(hz, seconds);
            struct Row { double qpc,t,wall,copy,map,unmap; size_t source; vision_native::DetectionBatch batch; };
            std::vector<Row> rows; rows.reserve(static_cast<size_t>(hz*seconds+1));
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
                const auto inferred=Clock::now(); mapping.unmap(); const auto end=Clock::now();
                rows.push_back({qpc,t,milliseconds(start,end),milliseconds(start,copied),milliseconds(copied,mapped),milliseconds(inferred,end),index,std::move(batch)});
                // Production-style serial service: newest fixture selected when service starts,
                // no queued frames or catch-up bursts. This is not DXGI present-to-result age.
                next=start+period;
            }
            const auto prefix=std::string(argv[3])+"-"+std::to_string(phase++);
            std::ofstream out(prefix+".csv"), boxes(prefix+"-detections.csv");
            out.exceptions(std::ios::badbit|std::ios::failbit); boxes.exceptions(std::ios::badbit|std::ios::failbit);
            out << "qpc_s,t,source,wall_ms,copy_submit_ms,map_ms,unmap_ms,preprocess_ms,infer_ms,output_sync_ms,decode_ms\n" << std::setprecision(17);
            boxes << "frame,source,signature\n";
            size_t frame=0;
            for (const auto& r:rows) {
                out<<r.qpc<<','<<r.t<<','<<r.source<<','<<r.wall<<','<<r.copy<<','<<r.map<<','<<r.unmap<<','<<r.batch.preprocess_ms<<','<<r.batch.infer_ms<<','<<r.batch.output_copy_sync_ms<<','<<r.batch.decode_ms<<'\n';
                boxes<<frame++<<','<<r.source<<','<<detections(r.batch)<<'\n';
            }
            out.close(); boxes.close(); std::cout << "DONE " << rows.size() << std::endl;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<std::endl; return 1; }
}
