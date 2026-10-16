#include "vision_native/preprocess.h"
#include "vision_native/cuda_graphics_mapping.h"
#include "bench_support.h"
#include <cstring>
#include <random>
#include <vector>
using namespace vision_bench;

// Hardware test, no controller. Compare every float bit against the existing
// preprocessor for independent seeds, edge ROIs and both resized/direct inputs.
int main(){
    try{
        Device device; size_t checked=0,cases=0;
        for(unsigned seed:{78123u,946817u}){
            std::mt19937 rng(seed);
            for(int c=0;c<40;++c){
                int sw=c%5==0?480:1+int(rng()%700),sh=c%5==0?384:1+int(rng()%550);
                if(c==1)sw=sh=1;
                if(c==2){sw=640;sh=512;}
                int dw=c%4==0?sw:480,dh=c%4==0?sh:384;
                int left=int(rng()%19),top=int(rng()%17),aw=left+sw+13,ah=top+sh+11;
                std::vector<unsigned char> pixels(size_t(aw)*ah*4);
                for(auto& x:pixels)x=static_cast<unsigned char>(rng());
                if(c==3)std::fill(pixels.begin(),pixels.end(),255);
                if(c==4)std::fill(pixels.begin(),pixels.end(),0);
                D3D11_TEXTURE2D_DESC desc{};desc.Width=aw;desc.Height=ah;
                desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
                desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=aw*4;
                Ptr<ID3D11Texture2D> image;check(device.device->CreateTexture2D(&desc,&data,&image));
                cudaGraphicsResource_t resource=nullptr;cudaStream_t stream=nullptr;
                check(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
                check(cudaGraphicsD3D11RegisterResource(&resource,image.Get(),cudaGraphicsRegisterFlagsNone));
                unsigned char* linear=nullptr;float *a=nullptr,*b=nullptr;size_t n=size_t(dw)*dh*3;
                check(cudaMalloc(reinterpret_cast<void**>(&linear),size_t(sw)*sh*4));
                check(cudaMalloc(reinterpret_cast<void**>(&a),n*sizeof(float)));
                check(cudaMalloc(reinterpret_cast<void**>(&b),n*sizeof(float)));
                {
                    vision_native::CudaGraphicsMapping mapping(resource,stream);
                    check(cudaMemcpy2DFromArrayAsync(linear,sw*4,mapping.array(),left*4,top,sw*4,sh,cudaMemcpyDeviceToDevice,stream));
                    cudaResourceDesc rd{};rd.resType=cudaResourceTypeArray;rd.res.array.array=mapping.array();
                    cudaTextureDesc td{};td.filterMode=cudaFilterModePoint;td.readMode=cudaReadModeElementType;
                    td.addressMode[0]=td.addressMode[1]=cudaAddressModeClamp;
                    cudaTextureObject_t texture;check(cudaCreateTextureObject(&texture,&rd,&td,nullptr));
                    vision_native::launch_bgra_hwc_to_chw_float(linear,sw,sh,sw*4,dw,dh,a,stream);
                    vision_native::launch_bgra_texture_to_chw_float(texture,left,top,sw,sh,dw,dh,b,stream);
                    check(cudaGetLastError());check(cudaStreamSynchronize(stream));
                    std::vector<float> ha(n),hb(n);
                    check(cudaMemcpy(ha.data(),a,n*sizeof(float),cudaMemcpyDeviceToHost));
                    check(cudaMemcpy(hb.data(),b,n*sizeof(float),cudaMemcpyDeviceToHost));
                    check(cudaDestroyTextureObject(texture));
                    if(std::memcmp(ha.data(),hb.data(),n*sizeof(float))!=0){
                        size_t mismatches=0;double max_error=0;
                        for(size_t i=0;i<n;++i){mismatches+=std::memcmp(&ha[i],&hb[i],sizeof(float))!=0;max_error=std::max(max_error,double(std::abs(ha[i]-hb[i])));}
                        std::cerr<<"seed="<<seed<<" case="<<c<<" mismatches="<<mismatches<<" max_error="<<max_error<<std::endl;
                        throw std::runtime_error("Input tensor is not bit-identical");
                    }
                    checked+=n;++cases;
                }
                check(cudaFree(linear));check(cudaFree(a));check(cudaFree(b));
                check(cudaGraphicsUnregisterResource(resource));check(cudaStreamDestroy(stream));
            }
        }
        std::cout<<"PASS cases="<<cases<<" floats_bit_equal="<<checked<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
