#include "../kar_pixels.hpp"
#include "../kar_art.hpp"
#include <cassert>
#include <iostream>
using kalwer::games::kar_pixels::Surface;
int main(){
 Surface s;s.rect(0,0,240,320,0x102030);s.rect(-2,-2,3,3,0xffffff);
 assert(s.pixels[0]==0xffffffff && s.pixels[1]==0xff102030);
 const uint32_t source[]={0xffff0000,0,0x800000ff,0xff00ff00};
 s.sprite(source,2,2,10,10,4,4);
 assert(s.pixels[10*240+10]==0xffff0000 && s.pixels[11*240+11]==0xffff0000);
 assert(s.pixels[10*240+12]==0xff102030);
 assert(s.pixels[12*240+10]==0xff081098 && s.pixels[13*240+13]==0xff00ff00);
 s.sprite(source,2,2,-1,-1,2,2,true);assert(s.pixels[0]==0xff7f7fff);
 s.sprite(source,2,2,239,319,10,10);assert(s.pixels.back()==0xffff0000);
 s.line(-1e12,160,1e12,160,0xffffff,1);assert(s.pixels[160*240+120]==0xffffffff);
 s.line(-1e12,-1e12,-1e11,-1e11,0,2);
 s.circle(-100,-100,1,0);s.rect(0,0,240,320,0);s.text(0,7,9,"A",0xffffff);
 assert(s.pixels[1]==0xffffffff && s.pixels[0]==0xff000000 && s.pixels[3*240]==0xffffffff);
 s.rect(0,0,240,320,0);s.triangle(-20,-20,20,-20,0,20,0xff0000);assert(s.pixels[0]==0xffff0000);
 s.rect(0,0,240,320,0);s.textured_triangle({0,0,8,0,0,8},{0,0,1,0,0,1},source,2,2);
 assert(s.pixels[0]==0xffff0000 && s.pixels[6]==0xff000000 && s.pixels[6*240]==0xff000080);
 auto scene_file=std::filesystem::temp_directory_path()/"kalwer-kar-scene-test.kars";
 {std::ofstream out(scene_file,std::ios::binary);out.write("KARSC001",8);auto word=[&](uint32_t v){for(int i=0;i<4;++i)out.put(char(v>>(8*i)));};word(1);word(0);word(0xffffff);word(0);word(0);for(int i=0;i<9;i++)word(i*100);}
 auto scene=kalwer::games::kar_pixels::Art::load_scene(scene_file);assert(scene.size()==1 && scene[0].vertices[8]==800);
 {std::ofstream out(scene_file,std::ios::binary|std::ios::app);out.put('x');}assert(kalwer::games::kar_pixels::Art::load_scene(scene_file).empty());std::filesystem::remove(scene_file);
 auto file=std::filesystem::temp_directory_path()/"kalwer-kar-art-test.karp";
 auto pack=[&](uint32_t width,bool truncated){std::ofstream out(file,std::ios::binary);out.write("KARPX001",8);auto word=[&](uint32_t v){for(int i=0;i<4;++i)out.put(char(v>>(i*8)));};word(1);word(65536);word(uint32_t(-1));word(uint32_t(-1));word(width);word(2);if(!truncated)for(auto color:source)word(color);};
 pack(2,false);auto art=kalwer::games::kar_pixels::Art::load(file);assert(art && art->get(1,0) && art->get(1,0)->x==-1);
 s.rect(0,0,240,320,0);assert(art->draw(s,1,0,11,11));assert(s.pixels[10*240+10]==0xffff0000);
 pack(2,true);assert(!kalwer::games::kar_pixels::Art::load(file));
 pack(0xffffffff,false);assert(!kalwer::games::kar_pixels::Art::load(file));
 pack(2,false);{std::ofstream out(file,std::ios::binary|std::ios::app);out.put('x');}assert(!kalwer::games::kar_pixels::Art::load(file));
 std::filesystem::remove(file);
 std::cout<<"Kar raster clipping, sprite transparency, mirroring, scaling and bitmap glyphs passed.\n";
}
