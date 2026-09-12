package pl.aridlin.kalwer;

import android.test.ActivityInstrumentationTestCase2;
import android.widget.EditText;
import android.view.View;
import android.net.Uri;
import org.json.*;
import java.io.*;

@SuppressWarnings("deprecation")
public final class WidgetUpdateTest extends ActivityInstrumentationTestCase2<MainActivity> {
    public WidgetUpdateTest(){super(MainActivity.class);}
    @Override protected void setUp()throws Exception{super.setUp();getInstrumentation().getTargetContext().getSharedPreferences("kalwer",0).edit().putInt("dither",0).putInt("popup_dither",0).putLong("update_check",System.currentTimeMillis()).commit();}
    public void testWidgetOpensFreshSearch()throws Throwable{
        MainActivity activity=getActivity();EditText query=activity.findViewById(R.id.search_query);
        runTestOnUiThread(()->{query.setText("old query");View widget=SearchWidget.views(activity).apply(activity,null);assertNotNull(widget.findViewById(R.id.widget_search));widget.findViewById(R.id.widget_search).performClick();});
        long deadline=System.currentTimeMillis()+10000;while(query.length()!=0 && System.currentTimeMillis()<deadline)Thread.sleep(50);
        assertEquals("",query.getText().toString());assertTrue(query.hasFocus());
    }
    private JSONObject release(String version,boolean pre,boolean checksum)throws Exception{
        String name="kalwer-android-"+version+".apk",base="https://github.com/aridlin/kalwer/releases/download/test/";
        JSONArray assets=new JSONArray().put(new JSONObject().put("name",name).put("browser_download_url",base+name));
        if(checksum)assets.put(new JSONObject().put("name",name+".sha256").put("browser_download_url",base+name+".sha256"));
        return new JSONObject().put("prerelease",pre).put("assets",assets);
    }
    public void testReleaseSelectionRequiresNewerVerifiedAsset()throws Exception{
        JSONArray releases=new JSONArray().put(release("0.4.1",false,true)).put(release("0.5.0",true,true)).put(release("9.0.0",false,false));
        assertEquals("0.4.1",AppUpdates.select(releases,"0.4.0",false).version);
        assertEquals("0.5.0",AppUpdates.select(releases,"0.4.0",true).version);
        assertNull(AppUpdates.select(releases,"0.5.0",true));
        JSONObject bad=release("10.0.0",false,true);bad.getJSONArray("assets").getJSONObject(0).put("browser_download_url","https://example.com/wrong.apk");assertNull(AppUpdates.select(new JSONArray().put(bad),"0.4.0",false));
        assertTrue(AppUpdates.compare("0.10.0","0.9.9")>0);
    }
    public void testUpdateProviderRejectsOtherFilesAndWrites()throws Exception{
        android.content.Context c=getInstrumentation().getTargetContext();
        try{c.getContentResolver().openFileDescriptor(UpdateProvider.uri(c),"rw");fail("Update APK must be read only");}catch(FileNotFoundException expected){}
        try{c.getContentResolver().openFileDescriptor(Uri.parse("content://"+c.getPackageName()+".updates/../koins-v1"),"r");fail("Only the update APK may be shared");}catch(IllegalArgumentException|FileNotFoundException expected){}
        assertFalse(AppUpdates.valid(c,new File(c.getApplicationInfo().sourceDir)));
        File f=File.createTempFile("update-test",".bin",c.getCacheDir());try{try(FileOutputStream out=new FileOutputStream(f)){out.write("abc".getBytes(java.nio.charset.StandardCharsets.UTF_8));}assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",AppUpdates.hash(f));assertFalse(AppUpdates.valid(c,f));}finally{f.delete();}
    }
}
