package pl.aridlin.kalwer;
import android.content.Context;
import android.graphics.Bitmap;
import android.opengl.*;
import java.nio.*;
/** One native-resolution compute dispatch per consented snapshot. Never runs in the UI loop. */
final class GpuDither {
    static Bitmap process(Context context,Bitmap input,int method,int theme) {
        EGLDisplay display=EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
        EGLContext eglContext=EGL14.EGL_NO_CONTEXT;EGLSurface surface=EGL14.EGL_NO_SURFACE;
        int[] textures=new int[3];int program=0,shader=0,fbo=0;
        try {
            if(!EGL14.eglInitialize(display,new int[2],0,new int[2],0))throw new IllegalStateException("EGL init");
            EGLConfig[] configs=new EGLConfig[1];int[] count=new int[1];
            int[] options={EGL14.EGL_RENDERABLE_TYPE,0x40,EGL14.EGL_SURFACE_TYPE,EGL14.EGL_PBUFFER_BIT,EGL14.EGL_RED_SIZE,8,EGL14.EGL_GREEN_SIZE,8,EGL14.EGL_BLUE_SIZE,8,EGL14.EGL_NONE};
            if(!EGL14.eglChooseConfig(display,options,0,configs,0,1,count,0)||count[0]==0)throw new IllegalStateException("GLES3 config");
            eglContext=EGL14.eglCreateContext(display,configs[0],EGL14.EGL_NO_CONTEXT,new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION,3,0x30fb,1,EGL14.EGL_NONE},0);
            surface=EGL14.eglCreatePbufferSurface(display,configs[0],new int[]{EGL14.EGL_WIDTH,1,EGL14.EGL_HEIGHT,1,EGL14.EGL_NONE},0);
            if(!EGL14.eglMakeCurrent(display,surface,surface,eglContext))throw new IllegalStateException("GLES context");
            String source;try(var in=context.getAssets().open("dither.comp")){java.io.ByteArrayOutputStream out=new java.io.ByteArrayOutputStream();byte[] chunk=new byte[4096];int n;while((n=in.read(chunk))!=-1)out.write(chunk,0,n);source=new String(out.toByteArray(),java.nio.charset.StandardCharsets.UTF_8);}
            shader=GLES31.glCreateShader(GLES31.GL_COMPUTE_SHADER);GLES31.glShaderSource(shader,source);GLES31.glCompileShader(shader);
            int[] okay=new int[1];GLES31.glGetShaderiv(shader,GLES31.GL_COMPILE_STATUS,okay,0);if(okay[0]==0)throw new IllegalStateException(GLES31.glGetShaderInfoLog(shader));
            program=GLES31.glCreateProgram();GLES31.glAttachShader(program,shader);GLES31.glLinkProgram(program);GLES31.glGetProgramiv(program,GLES31.GL_LINK_STATUS,okay,0);if(okay[0]==0)throw new IllegalStateException("GPU link: "+GLES31.glGetProgramInfoLog(program)+" GL="+GLES31.glGetError()+" "+GLES31.glGetString(GLES31.GL_VERSION));
            int w=input.getWidth(),h=input.getHeight();GLES31.glGenTextures(3,textures,0);
            GLES31.glBindTexture(GLES31.GL_TEXTURE_2D,textures[0]);GLUtils.texImage2D(GLES31.GL_TEXTURE_2D,0,input,0);
            GLES31.glBindTexture(GLES31.GL_TEXTURE_2D,textures[1]);GLES31.glTexStorage2D(GLES31.GL_TEXTURE_2D,1,GLES31.GL_RGBA8,w,h);
            GLES31.glBindTexture(GLES31.GL_TEXTURE_2D,textures[2]);GLES31.glTexStorage2D(GLES31.GL_TEXTURE_2D,1,GLES31.GL_R32F,w,h);
            GLES31.glUseProgram(program);GLES31.glUniform2i(GLES31.glGetUniformLocation(program,"size"),w,h);GLES31.glUniform1i(GLES31.glGetUniformLocation(program,"method"),method);GLES31.glUniform1i(GLES31.glGetUniformLocation(program,"spacing"),1);
            color(program,"lightColor",(Appearance.bw || method==5?0xffffff:Appearance.TEXT[theme]));color(program,"darkColor",(Appearance.bw || method==5?0:Appearance.DARK[theme]));
            GLES31.glBindImageTexture(0,textures[0],0,false,0,GLES31.GL_READ_ONLY,GLES31.GL_RGBA8);GLES31.glBindImageTexture(1,textures[1],0,false,0,GLES31.GL_WRITE_ONLY,GLES31.GL_RGBA8);GLES31.glBindImageTexture(2,textures[2],0,false,0,GLES31.GL_READ_WRITE,GLES31.GL_R32F);
            GLES31.glDispatchCompute(method<=2?1:(w*h+127)/128,1,1);GLES31.glMemoryBarrier(GLES31.GL_FRAMEBUFFER_BARRIER_BIT|GLES31.GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            int[] fb=new int[1];GLES31.glGenFramebuffers(1,fb,0);fbo=fb[0];GLES31.glBindFramebuffer(GLES31.GL_FRAMEBUFFER,fbo);GLES31.glFramebufferTexture2D(GLES31.GL_FRAMEBUFFER,GLES31.GL_COLOR_ATTACHMENT0,GLES31.GL_TEXTURE_2D,textures[1],0);
            ByteBuffer bytes=ByteBuffer.allocateDirect(w*h*4).order(ByteOrder.nativeOrder());GLES31.glReadPixels(0,0,w,h,GLES31.GL_RGBA,GLES31.GL_UNSIGNED_BYTE,bytes);
            if(GLES31.glGetError()!=GLES31.GL_NO_ERROR)throw new IllegalStateException("GPU snapshot readback");
            Bitmap result=Bitmap.createBitmap(w,h,Bitmap.Config.ARGB_8888);bytes.rewind();result.copyPixelsFromBuffer(bytes);return result;
        } catch(Exception e){android.util.Log.w("KalwerBackdrop","GPU snapshot unavailable",e);return null;}
        finally {
            if(eglContext!=EGL14.EGL_NO_CONTEXT){GLES31.glDeleteTextures(3,textures,0);if(fbo!=0)GLES31.glDeleteFramebuffers(1,new int[]{fbo},0);if(program!=0)GLES31.glDeleteProgram(program);if(shader!=0)GLES31.glDeleteShader(shader);}
            EGL14.eglMakeCurrent(display,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_SURFACE,EGL14.EGL_NO_CONTEXT);
            if(surface!=EGL14.EGL_NO_SURFACE)EGL14.eglDestroySurface(display,surface);if(eglContext!=EGL14.EGL_NO_CONTEXT)EGL14.eglDestroyContext(display,eglContext);EGL14.eglTerminate(display);EGL14.eglReleaseThread();
        }
    }
    private static void color(int program,String name,int c){GLES31.glUniform3f(GLES31.glGetUniformLocation(program,name),((c>>16)&255)/255f,((c>>8)&255)/255f,(c&255)/255f);}
}
