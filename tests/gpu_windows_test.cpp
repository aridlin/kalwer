#include "../gpu_dither_windows.hpp"
#include <cassert>
#include <iostream>
int main(){
    using Microsoft::WRL::ComPtr;ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context);assert(SUCCEEDED(hr));
    auto frame=std::make_shared<kalwer::LiveBackdrop::Frame>();frame->width=frame->height=32;frame->pixels.resize(1024);
    for(int i=0;i<1024;i++){unsigned v=i%32*8;frame->pixels[i]=0xff000000|v<<16|v<<8|v;}
    kalwer::WindowsDither dither;
    for(int mode=1;mode<6;mode++){
        kalwer::appearance.mode=mode;assert(dither.update(device.Get(),context.Get(),frame));
        D3D11_TEXTURE2D_DESC desc;dither.output->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
        ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging.Get(),dither.output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
        int bright=0;for(int y=0;y<32;y++)for(int x=0;x<32;x++){auto* pixel=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;bright+=pixel[0]>128;assert(pixel[3]==255);}
        context->Unmap(staging.Get(),0);assert(bright>200 && bright<800);
    }
    std::cout<<"All five Direct3D compute dithers passed\n";
}
