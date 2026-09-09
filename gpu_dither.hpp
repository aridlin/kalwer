#pragma once
#include <epoxy/gl.h>
#include "live_backdrop.hpp"
namespace kalwer {
// Exact error diffusion in dependency order on the GPU. Each diagonal is parallel;
// no tiled approximation, CPU pixel loop, readback, or enlarged bitmap is used.
struct GpuDither {
    GLuint input=0,output=0,errors=0,program=0;
    int width=0,height=0,mode=-1,theme=-1,scale=-1;
    bool bw=false;
    std::shared_ptr<const LiveBackdrop::Frame> uploaded;
    bool init(){
        const char* source=R"(#version 430 core
layout(local_size_x=256) in;
layout(binding=0,rgba8) readonly uniform image2D sourceImage;
layout(binding=1,rgba8) writeonly uniform image2D resultImage;
layout(binding=2,r32f) coherent uniform image2D errorImage;
uniform ivec2 size;uniform int method;uniform int spacing;uniform vec3 lightColor;uniform vec3 darkColor;
const int bayer[16]=int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
float errorAt(ivec2 p){if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,size)))return 0.;return imageLoad(errorImage,p).r;}
void pixel(ivec2 p){
 vec3 rgb=imageLoad(sourceImage,min(p*spacing,imageSize(sourceImage)-1)).rgb;
 precise float value=dot(rgb,vec3(.2126,.7152,.0722));float threshold=.5;
 if(method==1){value+=errorAt(p+ivec2(0,-2))/8.;value+=errorAt(p+ivec2(-1,-1))/8.;value+=errorAt(p+ivec2(0,-1))/8.;value+=errorAt(p+ivec2(1,-1))/8.;value+=errorAt(p+ivec2(-2,0))/8.;value+=errorAt(p+ivec2(-1,0))/8.;}
 if(method==2){value+=errorAt(p+ivec2(-1,-1))/16.;value+=errorAt(p+ivec2(0,-1))*5./16.;value+=errorAt(p+ivec2(1,-1))*3./16.;value+=errorAt(p+ivec2(-1,0))*7./16.;}
 if(method==3)threshold=(float(bayer[(p.y%4)*4+p.x%4])+.5)/16.;
 if(method==4)threshold=(4.*float(bayer[(p.y%4)*4+p.x%4])+float(bayer[(p.y/4%2)*4+p.x/4%2])/4.+.5)/64.;
 float bit=value>=threshold?1.:0.;imageStore(errorImage,p,vec4(value-bit));imageStore(resultImage,p,vec4(mix(darkColor,lightColor,bit),1));
}
void main(){
 if(method>2){int i=int(gl_GlobalInvocationID.x);if(i<size.x*size.y)pixel(ivec2(i%size.x,i/size.x));return;}
 for(int diagonal=0;diagonal<size.x+2*size.y-2;++diagonal){
  for(int y=int(gl_LocalInvocationID.x);y<size.y;y+=256){int x=diagonal-2*y;if(x>=0&&x<size.x)pixel(ivec2(x,y));}
  memoryBarrierImage();barrier();
 }
})";
        GLuint shader=glCreateShader(GL_COMPUTE_SHADER);glShaderSource(shader,1,&source,nullptr);glCompileShader(shader);
        program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glDeleteShader(shader);GLint okay=0;glGetProgramiv(program,GL_LINK_STATUS,&okay);
        if(!okay){char log[2048];glGetProgramInfoLog(program,sizeof(log),nullptr,log);g_warning("Dither shader: %s",log);return false;}
        glGenTextures(1,&input);glGenTextures(1,&output);glGenTextures(1,&errors);return true;
    }
    void release(){glDeleteTextures(1,&input);glDeleteTextures(1,&output);glDeleteTextures(1,&errors);glDeleteProgram(program);program=0;uploaded.reset();}
    bool update(const std::shared_ptr<const LiveBackdrop::Frame>& frame,int selected_mode=-1){
        int m=selected_mode<0?appearance.mode:selected_mode;
        if(!frame || m==0)return false;
        if(uploaded==frame && mode==m && theme==appearance.theme && scale==appearance.scale && bw==appearance.bw)return true;
        if(!program && !init())return false;
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,input);
        if(uploaded!=frame)glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame->width,frame->height,0,GL_BGRA,GL_UNSIGNED_BYTE,frame->pixels.data());
        int w=(frame->width+appearance.scale-1)/appearance.scale,h=(frame->height+appearance.scale-1)/appearance.scale;
        if(width!=w || height!=h){width=w;height=h;
            glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D,errors);glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,w,h,0,GL_RED,GL_FLOAT,nullptr);
        }
        glUseProgram(program);glUniform2i(glGetUniformLocation(program,"size"),w,h);glUniform1i(glGetUniformLocation(program,"method"),m);glUniform1i(glGetUniformLocation(program,"spacing"),appearance.scale);
        auto color=[&](const char* name,unsigned c){glUniform3f(glGetUniformLocation(program,name),((c>>16)&255)/255.f,((c>>8)&255)/255.f,(c&255)/255.f);};
        color("lightColor",(appearance.bw || m==5?0xffffff:themes[appearance.theme].text));color("darkColor",(appearance.bw || m==5?0:themes[appearance.theme].background));
        glBindImageTexture(0,input,0,GL_FALSE,0,GL_READ_ONLY,GL_RGBA8);glBindImageTexture(1,output,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);glBindImageTexture(2,errors,0,GL_FALSE,0,GL_READ_WRITE,GL_R32F);
        glDispatchCompute(m<=2?1:(w*h+255)/256,1,1);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
        uploaded=frame;mode=m;theme=appearance.theme;scale=appearance.scale;bw=appearance.bw;glActiveTexture(GL_TEXTURE0);return true;
    }
};
}
