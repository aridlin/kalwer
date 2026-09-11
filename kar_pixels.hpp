#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace kalwer::games::kar_pixels {
// The race has its own physical pixels. Host scale and UI font metrics must
// never change sprite edges, road coverage, or the in-race HUD layout.
struct Surface {
    static constexpr int width=240,height=320;
    std::vector<uint32_t> pixels=std::vector<uint32_t>(width*height);
    void pixel(int x,int y,unsigned color){if(x>=0 && x<width && y>=0 && y<height)pixels[y*width+x]=0xff000000u|color;}
    void rect(double x,double y,double w,double h,unsigned color){
        if(!std::isfinite(x+y+w+h) || w<=0 || h<=0)return;
        int left=int(std::clamp(std::floor(x),0.,double(width))),top=int(std::clamp(std::floor(y),0.,double(height)));
        int right=int(std::clamp(std::ceil(x+w),0.,double(width))),bottom=int(std::clamp(std::ceil(y+h),0.,double(height)));
        for(int row=top;row<bottom;++row)std::fill(pixels.begin()+row*width+left,pixels.begin()+row*width+right,0xff000000u|color);
    }
    void line(double x,double y,double a,double b,unsigned color,double thickness){
        if(!std::isfinite(x+y+a+b+thickness) || thickness<=0)return;
        // Clip before rasterizing; distant projected geometry can be enormous.
        double dx=a-x,dy=b-y,lo=0,hi=1;
        auto clip=[&](double p,double q){if(p==0)return q>=0;double t=q/p;if(p<0)lo=std::max(lo,t);else hi=std::min(hi,t);return lo<=hi;};
        double radius=std::clamp(thickness*.5,.5,320.);
        if(!clip(-dx,x+radius)||!clip(dx,width+radius-x)||!clip(-dy,y+radius)||!clip(dy,height+radius-y))return;
        a=x+dx*hi;b=y+dy*hi;x+=dx*lo;y+=dy*lo;
        int steps=std::max(1,int(std::ceil(std::max(std::abs(a-x),std::abs(b-y)))));
        for(int i=0;i<=steps;++i)rect(std::round(x+(a-x)*i/steps)-std::floor(radius),std::round(y+(b-y)*i/steps)-std::floor(radius),std::max(1.,std::round(thickness)),std::max(1.,std::round(thickness)),color);
    }
    void circle(double x,double y,double radius,unsigned color){
        if(!std::isfinite(x+y+radius) || radius<=0)return;
        for(int row=int(std::clamp(std::ceil(y-radius),0.,double(height)));row<int(std::clamp(std::ceil(y+radius),0.,double(height)));++row){
            double dy=row+.5-y,half=std::sqrt(std::max(0.,radius*radius-dy*dy));rect(std::ceil(x-half),row,std::floor(x+half)-std::ceil(x-half)+1,1,color);
        }
    }
    void triangle(double ax,double ay,double bx,double by,double cx,double cy,unsigned color){
        if(!std::isfinite(ax+ay+bx+by+cx+cy))return;
        int top=int(std::clamp(std::ceil(std::min({ay,by,cy})-.5),0.,double(height)));
        int bottom=int(std::clamp(std::ceil(std::max({ay,by,cy})-.5),0.,double(height)));
        for(int y=top;y<bottom;++y){
            double scan=y+.5,left=1e30,right=-1e30;
            auto edge=[&](double x1,double y1,double x2,double y2){if((y1<=scan && y2>scan)||(y2<=scan && y1>scan)){double x=x1+(x2-x1)*(scan-y1)/(y2-y1);left=std::min(left,x);right=std::max(right,x);}};
            edge(ax,ay,bx,by);edge(bx,by,cx,cy);edge(cx,cy,ax,ay);
            if(right>left)rect(std::ceil(left-.5),y,std::ceil(right-.5)-std::ceil(left-.5),1,color);
        }
    }
    void textured_triangle(const std::array<double,6>& xy,const std::array<double,6>& uv,const uint32_t* source,int sw,int sh){
        double ax=xy[0],ay=xy[1],bx=xy[2],by=xy[3],cx=xy[4],cy=xy[5];
        if(!source || sw<=0 || sh<=0 || !std::isfinite(ax+ay+bx+by+cx+cy))return;
        double area=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);if(std::abs(area)<.001)return;
        int left=int(std::clamp(std::ceil(std::min({ax,bx,cx})-.5),0.,double(width))),right=int(std::clamp(std::ceil(std::max({ax,bx,cx})-.5),0.,double(width)));
        int top=int(std::clamp(std::ceil(std::min({ay,by,cy})-.5),0.,double(height))),bottom=int(std::clamp(std::ceil(std::max({ay,by,cy})-.5),0.,double(height)));
        double da=(by-cy)/area,db=(cy-ay)/area;
        for(int y=top;y<bottom;++y){double a=((by-cy)*(left+.5-cx)+(cx-bx)*(y+.5-cy))/area,b=((cy-ay)*(left+.5-cx)+(ax-cx)*(y+.5-cy))/area;
            for(int x=left;x<right;++x,a+=da,b+=db){double c=1-a-b;if(a<-.00001 || b<-.00001 || c<-.00001)continue;
                int u=int(std::clamp((a*uv[0]+b*uv[2]+c*uv[4])*sw,0.,double(sw-1))),v=int(std::clamp((a*uv[1]+b*uv[3]+c*uv[5])*sh,0.,double(sh-1)));
                auto color=source[v*sw+u],alpha=color>>24;if(!alpha)continue;
                auto& target=pixels[y*width+x];if(alpha==255){target=color;continue;}
                uint32_t mixed=0xff000000;for(int shift:{0,8,16})mixed|=((((color>>shift)&255)*alpha+((target>>shift)&255)*(255-alpha)+127)/255)<<shift;target=mixed;
            }
        }
    }
    static std::array<uint8_t,7> glyph(char ch){
        // Hand-authored five-column HUD alphabet; no desktop font rasterizer.
        constexpr std::string_view alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr std::array<std::array<uint8_t,7>,36> shapes={{
            {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
            {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
            {31,16,16,30,16,16,16},{15,16,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
            {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},
            {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
            {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
        }};
        if(ch>='a' && ch<='z')ch-=32;
        auto at=alphabet.find(ch);if(at!=std::string_view::npos)return shapes[at];
        switch(ch){case '/':return {1,1,2,4,8,16,16};case ':':return {0,4,4,0,4,4,0};case '.':return {0,0,0,0,0,4,4};
        case '-':return {0,0,0,31,0,0,0};case '+':return {0,4,4,31,4,4,0};case '!':return {4,4,4,4,4,0,4};
        case '$':return {4,15,20,14,5,30,4};case '%':return {25,25,2,4,8,19,19};default:return {};}
    }
    void text(double x,double baseline,double size,const std::string_view text,unsigned color){
        int scale=std::max(1,int(std::round(size/9.))),left=int(std::round(x)),top=int(std::round(baseline))-7*scale;
        for(char ch:text){auto shape=glyph(ch);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(shape[row]&(1<<(4-col)))rect(left+col*scale,top+row*scale,scale,scale,color);left+=6*scale;}
    }
    // Straight-alpha ARGB source, integer nearest-neighbor sampling and exact
    // clipping. Palette sprites and full-color backgrounds share this path.
    void sprite(const uint32_t* source,int sw,int sh,int x,int y,int w,int h,bool flip=false){
        if(!source || sw<=0 || sh<=0 || w<=0 || h<=0)return;
        int left=std::max(0,x),right=std::min(width,x+w),top=std::max(0,y),bottom=std::min(height,y+h);
        for(int row=top;row<bottom;++row){int sy=int(int64_t(row-y)*sh/h);for(int col=left;col<right;++col){
            int sx=int(int64_t(col-x)*sw/w);if(flip)sx=sw-1-sx;uint32_t sample=source[sy*sw+sx],alpha=sample>>24;
            if(!alpha)continue;
            auto& target=pixels[row*width+col];if(alpha==255){target=sample;continue;}
            uint32_t mixed=0xff000000;for(int shift:{0,8,16})mixed|=((((sample>>shift)&255)*alpha+((target>>shift)&255)*(255-alpha)+127)/255)<<shift;target=mixed;
        }}
    }
};
}
