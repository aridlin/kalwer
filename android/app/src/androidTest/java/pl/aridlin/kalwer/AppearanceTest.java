package pl.aridlin.kalwer;
import android.test.AndroidTestCase;
import android.graphics.Bitmap;
public final class AppearanceTest extends AndroidTestCase {
    public void testConfigDoesNotExportOrResetKoins() throws Exception {
        var prefs=getContext().getSharedPreferences("appearance-test",0);
        prefs.edit().clear().putLong("koins",123).putInt("dither",1).putInt("theme",2).putInt("opacity",66).commit();
        String config=Appearance.export(prefs);assertFalse(config.contains("koins"));
        Appearance.importConfig(prefs,config);assertEquals(123,prefs.getLong("koins",0));assertEquals(1,Appearance.mode);assertEquals(2,Appearance.theme);
        prefs.edit().clear().commit();Appearance.mode=Appearance.theme=0;
    }
    public void testGpuSnapshotDithers() {
        int[] colors=new int[32*32];for(int i=0;i<colors.length;i++){int v=i%32*8;colors[i]=0xff000000|v<<16|v<<8|v;}
        Bitmap input=Bitmap.createBitmap(colors,32,32,Bitmap.Config.ARGB_8888);
        for(int mode=1;mode<6;mode++){
            Bitmap output=GpuDither.process(getContext(),input,mode,0);assertNotNull("GPU mode "+mode,output);
            output.getPixels(colors,0,32,0,0,32,32);int bright=0;for(int c:colors)if(android.graphics.Color.red(c)>128)bright++;
            assertTrue(bright>200 && bright<800);output.recycle();
        }
        input.recycle();
    }
    public void testDithersUseTwoColorsAndDiffer() {
        int[] colors=new int[64*64];for(int y=0;y<64;y++)for(int x=0;x<64;x++){int v=x*4;colors[y*64+x]=0xff000000|v<<16|v<<8|v;}
        Bitmap input=Bitmap.createBitmap(colors,64,64,Bitmap.Config.ARGB_8888);java.util.HashSet<Integer> hashes=new java.util.HashSet<>();
        for(int mode=1;mode<6;mode++) {
            Bitmap output=Appearance.dither(input,mode,0);output.getPixels(colors,0,64,0,0,64,64);int bright=0;
            for(int c:colors){int light=mode==5?0xffffffff:0xff000000|Appearance.TEXT[0],dark=mode==5?0xff000000:0xff000000|Appearance.DARK[0];assertTrue(c==light || c==dark);if(c==light)bright++;}
            assertTrue(bright>1000 && bright<3000);assertTrue(hashes.add(java.util.Arrays.hashCode(colors)));output.recycle();
        }
        input.recycle();
    }
}
