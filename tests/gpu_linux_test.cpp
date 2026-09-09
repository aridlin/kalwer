#include "../gpu_dither.hpp"
#include <cassert>
#include <gtk/gtk.h>
#include <cstdio>
int main(int argc,char** argv){gtk_init(&argc,&argv);auto* window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(window),"Kalwer GPU check");auto* area=gtk_gl_area_new();gtk_gl_area_set_required_version(GTK_GL_AREA(area),4,3);gtk_container_add(GTK_CONTAINER(window),area);g_signal_connect(area,"render",G_CALLBACK((+[](GtkGLArea*,GdkGLContext*,gpointer)->gboolean {
    printf("GPU: %s\n",glGetString(GL_RENDERER));kalwer::GpuDither d;
    for(int size:{64,650}){
        auto frame=std::make_shared<kalwer::LiveBackdrop::Frame>();frame->width=size;frame->height=size;frame->pixels.resize(size*size);
        for(int y=0;y<size;y++)for(int x=0;x<size;x++){unsigned v=(x*17+y*31)%256;frame->pixels[y*size+x]=0xff000000|v<<16|v<<8|v;}
        for(int mode=1;mode<6;mode++){
            kalwer::appearance.mode=mode;kalwer::appearance.scale=1;GLuint query;glGenQueries(1,&query);glBeginQuery(GL_TIME_ELAPSED,query);d.update(frame);glEndQuery(GL_TIME_ELAPSED);GLuint64 ns;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);glDeleteQueries(1,&query);
            std::vector<unsigned> pixels(size*size);glBindTexture(GL_TEXTURE_2D,d.output);glGetTexImage(GL_TEXTURE_2D,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels.data());auto expected=kalwer::dither_image(frame->pixels,size,size,mode,0);int wrong=0;for(size_t i=0;i<pixels.size();i++)wrong+=pixels[i]!=expected[i];
            assert(glGetError()==GL_NO_ERROR);assert(wrong<(mode==2?size*size/25:1));
            printf("%dx%d mode %d GPU %.3f ms, mismatch %d/%zu, GL error %x\n",size,size,mode,ns/1e6,wrong,pixels.size(),glGetError());fflush(stdout);
        }
    }
    d.release();gtk_main_quit();return TRUE;
})),nullptr);gtk_widget_show_all(window);gtk_main();}
