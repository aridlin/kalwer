package pl.aridlin.kalwer;

import android.graphics.Bitmap;
import java.io.*;
import java.net.*;
import java.security.MessageDigest;

final class GameNative {
    static { System.loadLibrary("kalwer_games"); }
    static native void open(int kind,String directory,String libraries,long passive,int theme,int opacity,int dither,boolean halftone);
    static native void close();
    static native void focus(boolean focused);
    static native void key(int key,boolean down);
    static native void pointer(float x,float y,int button);
    static native String title();
    static native int kind();
    static native String info();
    static native String importWad(String path);
    static native boolean unlock();
    static native boolean surface(Bitmap atlas,float[] advances);
    static native void draw(int width,int height,double dt);
    // Called from the native preparation worker, never the Android UI thread.
    static boolean download(String url,String target) {
        HttpURLConnection connection=null;
        try {
            for(int redirect=0;redirect<6;redirect++) {
                URL address=new URL(url);if(!address.getProtocol().equals("https"))return false;
                connection=(HttpURLConnection)address.openConnection();connection.setConnectTimeout(15000);connection.setReadTimeout(30000);
                connection.setInstanceFollowRedirects(false);connection.setRequestProperty("User-Agent","Kalwer-Android");
                int code=connection.getResponseCode();
                if(code>=300 && code<400){String next=connection.getHeaderField("Location");if(next==null)return false;url=new URL(address,next).toString();connection.disconnect();continue;}
                if(code!=200 || connection.getContentLengthLong()>64L*1024*1024)return false;
                try(InputStream in=connection.getInputStream();OutputStream out=new FileOutputStream(target)) {
                    byte[] buffer=new byte[32768];long total=0;int n;
                    while((n=in.read(buffer))!=-1){total+=n;if(total>64L*1024*1024)return false;out.write(buffer,0,n);}
                }
                return true;
            }
        }catch(IOException ignored){}finally{if(connection!=null)connection.disconnect();}
        return false;
    }
    static String sha256(byte[] bytes) {
        try{byte[] digest=MessageDigest.getInstance("SHA-256").digest(bytes);StringBuilder out=new StringBuilder(64);for(byte b:digest)out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();}
        catch(Exception ignored){return "";}
    }
}
