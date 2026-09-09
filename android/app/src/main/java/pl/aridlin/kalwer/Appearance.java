package pl.aridlin.kalwer;

import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Color;
import org.json.JSONObject;

final class Appearance {
    static final String[] MODES={"Halftone","Atkinson","Floyd-Steinberg","Bayer 4x4","Bayer 8x8","Threshold"};
    static final String[] THEMES={"Forest","Amber","Glacier","Rose","Violet","Mono"};
    static final int[] DARK={0x081a18,0x20170b,0x0d1928,0x241420,0x19142a,0x191919};
    static final int[] ACCENT={0x8ce9b3,0xf4bb65,0x83d3f7,0xf1a0c5,0xbba1f6,0xd0d0d0};
    static final int[] TEXT={0xe0f5e8,0xffefcf,0xe0f1ff,0xffe4f0,0xeee7ff,0xf4f4f4};
    static int mode=0,popupMode=0,theme=0,scale=1;
    static boolean bw=false,keepHalftone=true,popupHalftone=true;
    static int clamp(int x,int lo,int hi){return Math.max(lo,Math.min(hi,x));}
    static void load(SharedPreferences p){bw=p.getBoolean("backdrop_bw",false);keepHalftone=p.getBoolean("keep_halftone",true);popupHalftone=p.getBoolean("popup_keep_halftone",true);popupMode=clamp(p.getInt("popup_dither",0),0,5);mode=clamp(p.getInt("dither",0),0,5);theme=clamp(p.getInt("theme",0),0,5);scale=clamp(p.getInt("dither_scale",1),1,8);}
    static int tint(int color) {
        if(theme==0)return color;
        int r=Color.red(color),g=Color.green(color),b=Color.blue(color);
        if(g<r*.92 || g<b*.9)return color;
        int max=Math.max(r,Math.max(g,b)); int target=max<65?DARK[theme]:max>221?TEXT[theme]:ACCENT[theme];
        return Color.argb(Color.alpha(color),Color.red(target),Color.green(target),Color.blue(target));
    }
    static String export(SharedPreferences p) throws Exception {
        JSONObject obj=new JSONObject();obj.put("version",1);
        obj.put("popup_dither",p.getInt("popup_dither",0));
        for(String key:new String[]{"backdrop_bw","keep_halftone","popup_keep_halftone"})obj.put(key,p.getBoolean(key,!key.equals("backdrop_bw")));
        for(String key:new String[]{"dither","theme","opacity","dither_scale","retention_ms"})obj.put(key,p.getInt(key,key.equals("opacity")?72:key.equals("dither_scale")?1:key.equals("retention_ms")?3000:0));
        return obj.toString(2)+"\n";
    }
    static void importConfig(SharedPreferences p,String text) throws Exception {
        JSONObject obj=new JSONObject(text);if(obj.getInt("version")!=1)throw new IllegalArgumentException("Unsupported config version");
        p.edit().putInt("popup_dither",clamp(obj.optInt("popup_dither",0),0,5)).putBoolean("backdrop_bw",obj.optBoolean("backdrop_bw",false)).putBoolean("keep_halftone",obj.optBoolean("keep_halftone",true)).putBoolean("popup_keep_halftone",obj.optBoolean("popup_keep_halftone",true)).putInt("dither",clamp(obj.getInt("dither"),0,5)).putInt("theme",clamp(obj.getInt("theme"),0,5))
            .putInt("opacity",clamp(obj.getInt("opacity"),30,95)).putInt("dither_scale",clamp(obj.getInt("dither_scale"),1,8))
            .putInt("retention_ms",clamp(obj.getInt("retention_ms"),0,30000)).apply();load(p);
    }
    static Bitmap dither(Bitmap input,int method,int palette) {
        int w=input.getWidth(),h=input.getHeight();int[] pixels=new int[w*h];input.getPixels(pixels,0,w,0,0,w,h);
        float[] a=new float[w*h];for(int i=0;i<a.length;i++)a[i]=(Color.red(pixels[i])*.2126f+Color.green(pixels[i])*.7152f+Color.blue(pixels[i])*.0722f)/255;
        int[] matrix={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
        for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
            int i=y*w+x;float threshold=.5f;
            if(method==3)threshold=(matrix[(y%4)*4+x%4]+.5f)/16;
            if(method==4)threshold=(4*matrix[(y%4)*4+x%4]+matrix[(y/4%2)*4+x/4%2]/4f+.5f)/64;
            boolean white=a[i]>=threshold;float e=a[i]-(white?1:0);pixels[i]=0xff000000|(method==5?(white?0xffffff:0):(white?TEXT[palette]:DARK[palette]));
            if(method==1){e/=8;add(a,w,h,x+1,y,e);add(a,w,h,x+2,y,e);add(a,w,h,x-1,y+1,e);add(a,w,h,x,y+1,e);add(a,w,h,x+1,y+1,e);add(a,w,h,x,y+2,e);}
            if(method==2){add(a,w,h,x+1,y,e*7/16);add(a,w,h,x-1,y+1,e*3/16);add(a,w,h,x,y+1,e*5/16);add(a,w,h,x+1,y+1,e/16);}
        }
        return Bitmap.createBitmap(pixels,w,h,Bitmap.Config.ARGB_8888);
    }
    private static void add(float[] a,int w,int h,int x,int y,float e){if(x>=0 && y>=0 && x<w && y<h)a[y*w+x]+=e;}
}
