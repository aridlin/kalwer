#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include "live_backdrop.hpp"
namespace kalwer {
struct WindowsDither {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11ComputeShader> shader;Ptr<ID3D11Buffer> constants;
    Ptr<ID3D11Texture2D> input,output,errors;
    Ptr<ID3D11ShaderResourceView> source,view;
    Ptr<ID3D11UnorderedAccessView> destination,error_view;
    std::shared_ptr<const LiveBackdrop::Frame> uploaded;
    int width=0,height=0,mode=-1,theme=-1,scale=-1;
    bool bw=false;
    bool update(ID3D11Device* device,ID3D11DeviceContext* context,const std::shared_ptr<const LiveBackdrop::Frame>& frame,int selected_mode=-1){
        int m=selected_mode<0?appearance.mode:selected_mode;
        if(!frame || m==0)return false;
        if(uploaded==frame && mode==m && theme==appearance.theme && scale==appearance.scale && bw==appearance.bw)return true;
        if(!shader){
            const char* code=R"(
Texture2D<float4> sourceImage:register(t0);RWTexture2D<float4> resultImage:register(u0);globallycoherent RWTexture2D<float> errorImage:register(u1);
cbuffer Options:register(b0){int2 size;int method;int spacing;float4 lightColor;float4 darkColor;};
static const int bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
float errorAt(int2 p){if(any(p<0)||any(p>=size))return 0.;return errorImage[p];}
void pixel(int2 p){uint w,h;sourceImage.GetDimensions(w,h);float3 rgb=sourceImage.Load(int3(min(p*spacing,int2(w,h)-1),0)).rgb;
precise float value=dot(rgb,float3(.2126,.7152,.0722));float threshold=.5;
if(method==1){value+=errorAt(p+int2(0,-2))/8.;value+=errorAt(p+int2(-1,-1))/8.;value+=errorAt(p+int2(0,-1))/8.;value+=errorAt(p+int2(1,-1))/8.;value+=errorAt(p+int2(-2,0))/8.;value+=errorAt(p+int2(-1,0))/8.;}
if(method==2){value+=errorAt(p+int2(-1,-1))/16.;value+=errorAt(p+int2(0,-1))*5./16.;value+=errorAt(p+int2(1,-1))*3./16.;value+=errorAt(p+int2(-1,0))*7./16.;}
if(method==3)threshold=(bayer[(p.y%4)*4+p.x%4]+.5)/16.;
if(method==4)threshold=(4.*bayer[(p.y%4)*4+p.x%4]+bayer[(p.y/4%2)*4+p.x/4%2]/4.+.5)/64.;
float bit=value>=threshold?1.:0.;errorImage[p]=value-bit;resultImage[p]=float4(lerp(darkColor.rgb,lightColor.rgb,bit),1);}
[numthreads(256,1,1)]void main(uint3 local:SV_GroupThreadID,uint3 globalId:SV_DispatchThreadID){
if(method>2){int i=globalId.x;if(i<size.x*size.y)pixel(int2(i%size.x,i/size.x));return;}
for(int diagonal=0;diagonal<size.x+2*size.y-2;++diagonal){for(int y=local.x;y<size.y;y+=256){int x=diagonal-2*y;if(x>=0&&x<size.x)pixel(int2(x,y));}AllMemoryBarrierWithGroupSync();}}
)";
            Ptr<ID3DBlob> blob,log;
            if(FAILED(D3DCompile(code,strlen(code),"KalwerDither",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&log)))return false;
            if(FAILED(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader)))return false;
            D3D11_BUFFER_DESC b{};b.ByteWidth=48;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(FAILED(device->CreateBuffer(&b,nullptr,&constants)))return false;
        }
        int w=(frame->width+appearance.scale-1)/appearance.scale,h=(frame->height+appearance.scale-1)/appearance.scale;
        if(!input || !uploaded || uploaded->width!=frame->width || uploaded->height!=frame->height){
            input.Reset();source.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=frame->width;d.Height=frame->height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&input)) || FAILED(device->CreateShaderResourceView(input.Get(),nullptr,&source)))return false;
        }
        if(uploaded!=frame)context->UpdateSubresource(input.Get(),0,nullptr,frame->pixels.data(),frame->width*4,0);
        if(width!=w || height!=h){
            width=w;height=h;output.Reset();errors.Reset();view.Reset();destination.Reset();error_view.Reset();
            D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&output)) || FAILED(device->CreateShaderResourceView(output.Get(),nullptr,&view)) || FAILED(device->CreateUnorderedAccessView(output.Get(),nullptr,&destination)))return false;
            d.Format=DXGI_FORMAT_R32_FLOAT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
            if(FAILED(device->CreateTexture2D(&d,nullptr,&errors)) || FAILED(device->CreateUnorderedAccessView(errors.Get(),nullptr,&error_view)))return false;
        }
        struct Params{int w,h,mode,scale;float light[4],dark[4];} params{w,h,m,appearance.scale,{},{}};
        for(int i=0;i<3;i++){params.light[i]=(((appearance.bw || m==5?0xffffff:themes[appearance.theme].text)>>(16-i*8))&255)/255.f;params.dark[i]=(((appearance.bw || m==5?0:themes[appearance.theme].background)>>(16-i*8))&255)/255.f;}
        context->UpdateSubresource(constants.Get(),0,nullptr,&params,0,0);ID3D11Buffer* buffer=constants.Get();context->CSSetConstantBuffers(0,1,&buffer);
        ID3D11ShaderResourceView* src=source.Get();context->CSSetShaderResources(0,1,&src);ID3D11UnorderedAccessView* dst[]={destination.Get(),error_view.Get()};context->CSSetUnorderedAccessViews(0,2,dst,nullptr);context->CSSetShader(shader.Get(),nullptr,0);
        context->Dispatch(m<=2?1:(w*h+255)/256,1,1);
        ID3D11UnorderedAccessView* empty[2]={};context->CSSetUnorderedAccessViews(0,2,empty,nullptr);ID3D11ShaderResourceView* nullView=nullptr;context->CSSetShaderResources(0,1,&nullView);context->CSSetShader(nullptr,nullptr,0);
        uploaded=frame;mode=m;theme=appearance.theme;scale=appearance.scale;bw=appearance.bw;return true;
    }
};
}
