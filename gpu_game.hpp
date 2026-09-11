#pragma once
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#else
#include <epoxy/gl.h>
#include <cairo.h>
#endif
#include <vector>
#include <array>
#include <cmath>
#include "appearance.hpp"
namespace kalwer::games {
// Batched native geometry and a reusable glyph atlas. No per-frame Cairo rasterization.
struct GpuPainter {
    struct Vertex {float x,y,u,v,r,g,b,a,kind;};
    std::vector<Vertex> vertices;
    GLuint program=0,vao=0,vbo=0,atlas=0,backdrop=0,frame=0;
    std::array<float,96> advances{};
    int frame_width=0,frame_height=0;
    float logical_width=420,logical_height=452,origin_y=38;
    static GLuint shader(GLenum type,const char* source) {GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);return s;}
    bool init(const void* glyph_pixels=nullptr,const float* glyph_advances=nullptr) {
#ifdef __ANDROID__
        constexpr const char* version="#version 300 es\nprecision highp float;\nprecision highp int;\n";
#else
        constexpr const char* version="#version 330 core\n";
        (void)glyph_pixels;(void)glyph_advances;
#endif
        const std::string vs=std::string(version)+R"(layout(location=0) in vec2 pos;layout(location=1) in vec2 tex;layout(location=2) in vec4 tint;layout(location=3) in float shape;
out vec2 uv;out vec4 rgba;flat out int kind;
uniform vec2 logicalSize;uniform float originY;
void main(){gl_Position=vec4(pos.x*2./logicalSize.x-1.,1.-(pos.y-originY)*2./logicalSize.y,0,1);uv=tex;rgba=tint;kind=int(shape);})";
        std::string fs=std::string(version)+R"(in vec2 uv;in vec4 rgba;flat in int kind;out vec4 color;
uniform sampler2D glyphs;uniform sampler2D frame;uniform sampler2D backdrop;uniform vec2 backdropSize;uniform vec3 dark;
void main(){float a=rgba.a;vec3 rgb=rgba.rgb;
if(kind==1){float d=length(uv);a*=1.-smoothstep(1.-fwidth(d),1.,d);}
if(kind==2)a*=texture(glyphs,uv).a;
if(kind==3){vec2 p=uv/7.;p.x-=mod(floor(p.y),2.)*.5;float d=length(fract(p)-.5);a*=.75+.25*(1.-smoothstep(.35,.43,d));}
if(kind==4){vec2 p=uv*backdropSize;vec3 value=texture(backdrop,(floor(p)+.5)/backdropSize).rgb;float d=length(fract(p)-.5);float dot=1.-smoothstep(.36,.49,d);rgb=mix(dark,mix(dark,value,.5),dot);}
if(kind==5){color=vec4(texture(frame,uv).rgb,1);return;}
color=vec4(rgb*a,a);})";
#ifdef __ANDROID__
        const auto swap=fs.find("texture(frame,uv).rgb");
        if(swap!=std::string::npos)fs.replace(swap,std::string("texture(frame,uv).rgb").size(),"texture(frame,uv).bgr");
#endif
        GLuint v=shader(GL_VERTEX_SHADER,vs.c_str()),f=shader(GL_FRAGMENT_SHADER,fs.c_str());program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);glDeleteShader(v);glDeleteShader(f);
        GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok)return false;
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        for(int i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==2?4:i==3?1:2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(size_t(i==0?0:i==1?2:i==2?4:8)*sizeof(float)));}
        glGenTextures(1,&atlas);glBindTexture(GL_TEXTURE_2D,atlas);
#ifdef __ANDROID__
        if(!glyph_pixels || !glyph_advances)return false;
        std::copy(glyph_advances,glyph_advances+96,advances.begin());
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,512,256,0,GL_RGBA,GL_UNSIGNED_BYTE,glyph_pixels);
#else
        auto* surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,512,256);auto* cr=cairo_create(surface);
        cairo_select_font_face(cr,"sans",CAIRO_FONT_SLANT_NORMAL,CAIRO_FONT_WEIGHT_NORMAL);cairo_set_font_size(cr,24);cairo_set_source_rgba(cr,1,1,1,1);
        for(int i=0;i<96;i++){char text[2]={char(i+32),0};cairo_text_extents_t ext;cairo_text_extents(cr,text,&ext);advances[i]=ext.x_advance;cairo_move_to(cr,(i%16)*32+2,(i/16)*40+29);cairo_show_text(cr,text);}
        cairo_destroy(cr);cairo_surface_flush(surface);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,512,256,0,GL_BGRA,GL_UNSIGNED_BYTE,cairo_image_surface_get_data(surface));cairo_surface_destroy(surface);
#endif
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        return true;
    }
    void release(){glDeleteTextures(1,&frame);glDeleteTextures(1,&atlas);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);program=frame=0;frame_width=frame_height=0;}
    void quad(double x,double y,double w,double h,unsigned c,float a,int kind,float u=0,float v=0,float uw=1,float vh=1) {
        c=appearance.tint(c);float r=((c>>16)&255)/255.f,g=((c>>8)&255)/255.f,b=(c&255)/255.f;
        Vertex q[4]={{float(x),float(y),u,v,r,g,b,a,float(kind)},{float(x+w),float(y),u+uw,v,r,g,b,a,float(kind)},{float(x+w),float(y+h),u+uw,v+vh,r,g,b,a,float(kind)},{float(x),float(y+h),u,v+vh,r,g,b,a,float(kind)}};
        for(int i:{0,1,2,0,2,3})vertices.push_back(q[i]);
    }
    void rect(double x,double y,double w,double h,unsigned c){
        bool surface=c==0x081a18 || c==0x112e29 || c==0x2b5145 || c==0x183b32;
        float alpha=surface?appearance.opacity/100.f:1.f;
        // A quiet translucent board, with circular halftone only on UI tiles.
        int shape=surface && c!=0x081a18 && (appearance.popup_mode==0 || appearance.popup_keep_halftone)?3:0;
        if(c==0x081a18)alpha*=.48f;
        if(c==0x112e29)alpha*=.9f;
        quad(x,y,w,h,c,alpha,shape,x,y,w,h);
    }
    void circle(double x,double y,double r,unsigned c){quad(x-r,y-r,r*2,r*2,c,1,1,-1,-1,2,2);}
    void line(double x,double y,double a,double b,unsigned c,double width){
        double length=std::hypot(a-x,b-y);if(length<.001)return;
        double nx=-(b-y)/length*width*.5,ny=(a-x)/length*width*.5;
        size_t start=vertices.size();quad(0,0,1,1,c,1,0);
        double px[4]={x+nx,a+nx,a-nx,x-nx},py[4]={y+ny,b+ny,b-ny,y-ny};int n=0;
        for(int i:{0,1,2,0,2,3}){vertices[start+n].x=px[i];vertices[start+n++].y=py[i];}
    }
    void text(double x,double y,double size,const std::string& text,unsigned c){
        double s=size/24.;for(unsigned char ch:text){if(ch<32 || ch>127)continue;int i=ch-32;quad(x-2*s,y-29*s,32*s,40*s,c,1,2,(i%16)*32/512.f,(i/16)*40/256.f,32/512.f,40/256.f);x+=advances[i]*s;}
    }
    void image(double x,double y,double w,double h,const uint32_t* pixels,int width,int height){
#ifdef __ANDROID__
        constexpr GLenum pixel_format=GL_RGBA;
#else
        constexpr GLenum pixel_format=GL_BGRA;
#endif
        if(!frame)glGenTextures(1,&frame);
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,frame);
        if(frame_width!=width || frame_height!=height){
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,pixel_format,GL_UNSIGNED_BYTE,pixels);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            frame_width=width;frame_height=height;
        }else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,pixel_format,GL_UNSIGNED_BYTE,pixels);
        quad(x,y,w,h,0xffffff,1,5);
    }
    void flush(){
        glUseProgram(program);glUniform2f(glGetUniformLocation(program,"logicalSize"),logical_width,logical_height);glUniform1f(glGetUniformLocation(program,"originY"),origin_y);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,atlas);glUniform1i(glGetUniformLocation(program,"glyphs"),0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,backdrop);glUniform1i(glGetUniformLocation(program,"backdrop"),1);
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,frame);glUniform1i(glGetUniformLocation(program,"frame"),2);
        glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLES,0,vertices.size());glDisable(GL_BLEND);vertices.clear();glActiveTexture(GL_TEXTURE0);
    }
};
}
