#include "bench_support.h"
#include <d3dcompiler.h>
#include <fstream>
#include <iomanip>
#include <vector>
using namespace vision_bench;

// Offscreen graphics competition, not a game simulation. One frame in flight.
// Fixed work per frame: no feedback throttling or priority changes.
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: vision_graphics_load output-prefix");
        Device d;
        constexpr char shader[] = R"(
struct Vertex { float4 position : SV_POSITION; };
Vertex vs(uint id : SV_VertexID) {
    Vertex v; v.position = float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1); return v;
}
float4 ps(Vertex v) : SV_TARGET {
    float4 a = frac(v.position.xyxy * float4(.013, .017, .019, .023)) + .01;
    [loop] for (uint i=0; i<96; ++i) a = frac(a.wxyz * 1.731 + a.zwxy * .319 + .017);
    return a;
})";
        Ptr<ID3DBlob> vs_blob, ps_blob, error;
        check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "vs", "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs_blob, &error));
        check(D3DCompile(shader, sizeof(shader)-1, nullptr, nullptr, nullptr, "ps", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps_blob, &error));
        Ptr<ID3D11VertexShader> vs; Ptr<ID3D11PixelShader> ps;
        check(d.device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs));
        check(d.device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps));
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=1920; desc.Height=1080; desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Ptr<ID3D11Texture2D> texture; Ptr<ID3D11RenderTargetView> target;
        check(d.device->CreateTexture2D(&desc, nullptr, &texture));
        check(d.device->CreateRenderTargetView(texture.Get(), nullptr, &target));
        desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Texture2D> readback; check(d.device->CreateTexture2D(&desc, nullptr, &readback));
        auto* rtv=target.Get(); d.context->OMSetRenderTargets(1, &rtv, nullptr);
        D3D11_VIEWPORT viewport{0,0,1920,1080,0,1}; d.context->RSSetViewports(1, &viewport);
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID;
        raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        Ptr<ID3D11RasterizerState> rs; check(d.device->CreateRasterizerState(&raster, &rs));
        d.context->RSSetState(rs.Get());
        d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        d.context->VSSetShader(vs.Get(), nullptr, 0); d.context->PSSetShader(ps.Get(), nullptr, 0);
        Ptr<ID3D11Query> disjoint, start_query, end_query;
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        check(d.device->CreateQuery(&query, &disjoint)); query.Query=D3D11_QUERY_TIMESTAMP;
        check(d.device->CreateQuery(&query, &start_query)); check(d.device->CreateQuery(&query, &end_query));
        runtime_app::PrecisionTickScheduler scheduler(0);
        std::cout << "READY " << GetCurrentProcessId() << std::endl;
        double hz, seconds; unsigned passes; int phase=0;
        while (std::cin >> hz >> seconds >> passes && hz > 0) {
            validate_phase(hz, seconds);
            if (passes < 1 || passes > 128) throw std::runtime_error("Passes must be 1..128");
            struct Row { double qpc, wall, gpu; uint64_t skipped; bool valid; };
            std::vector<Row> rows; rows.reserve(static_cast<size_t>(hz*seconds+1));
            const auto begin=Clock::now();
            const auto period=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1/hz));
            runtime_app::AbsoluteDeadlineState deadlines(begin, period);
            while (std::chrono::duration<double>(Clock::now()-begin).count() < seconds) {
                scheduler.wait_until(deadlines.next_deadline());
                const auto start=Clock::now();
                if (std::chrono::duration<double>(start-begin).count() >= seconds) break;
                const double qpc=qpc_seconds();
                d.context->Begin(disjoint.Get()); d.context->End(start_query.Get());
                for (unsigned i=0; i<passes; ++i) d.context->Draw(3, 0);
                d.context->End(end_query.Get()); d.context->End(disjoint.Get()); d.context->Flush();
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
                HRESULT result;
                // Bounded queue and bounded polling. A hung device is a failed run.
                while ((result=d.context->GetData(disjoint.Get(), &frequency, sizeof(frequency), D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
                    if (milliseconds(start, Clock::now()) > 5000) throw std::runtime_error("Graphics query timeout");
                    scheduler.wait_until(Clock::now()+std::chrono::microseconds(100));
                }
                check(result);
                UINT64 first=0,last=0;
                const auto first_result=d.context->GetData(start_query.Get(), &first, sizeof(first), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                const auto last_result=d.context->GetData(end_query.Get(), &last, sizeof(last), D3D11_ASYNC_GETDATA_DONOTFLUSH);
                check(first_result); check(last_result);
                const bool valid=first_result==S_OK && last_result==S_OK && !frequency.Disjoint && frequency.Frequency && last>=first;
                const auto end=Clock::now();
                const auto skipped=deadlines.advance_after_tick(end);
                rows.push_back({qpc, milliseconds(start,end), valid ? 1000.0*(last-first)/frequency.Frequency : -1, skipped, valid});
            }
            std::ofstream out(std::string(argv[1])+"-"+std::to_string(phase++)+".csv");
            out.exceptions(std::ios::badbit|std::ios::failbit);
            out << "qpc_s,wall_ms,gpu_ms,skipped,valid\n" << std::setprecision(17);
            for (const auto& row:rows) out<<row.qpc<<','<<row.wall<<','<<row.gpu<<','<<row.skipped<<','<<row.valid<<'\n';
            out.close();
            // Outside the timed region: prove that the pixel shader produced an image.
            d.context->CopyResource(readback.Get(),texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(d.context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
            uint64_t hash=14695981039346656037ull; bool nonconstant=false;
            const auto first_pixel=*static_cast<const uint32_t*>(mapped.pData);
            for (UINT y=0;y<1080;++y) {
                const auto* pixels=reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);
                for (UINT x=0;x<1920;++x) {
                    nonconstant |= pixels[x]!=first_pixel;
                    hash=(hash^pixels[x])*1099511628211ull;
                }
            }
            d.context->Unmap(readback.Get(),0);
            if (!nonconstant) throw std::runtime_error("Graphics load produced a constant/blank image");
            std::cout << "RENDER_CHECK " << hash << std::endl;
            std::cout << "DONE " << rows.size() << std::endl;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << std::endl; return 1; }
}
