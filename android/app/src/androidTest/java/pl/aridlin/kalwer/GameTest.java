package pl.aridlin.kalwer;

import android.test.ActivityInstrumentationTestCase2;
import android.widget.EditText;
import android.view.inputmethod.EditorInfo;
import org.json.JSONObject;

@SuppressWarnings("deprecation")
public final class GameTest extends ActivityInstrumentationTestCase2<MainActivity> {
    public GameTest(){super(MainActivity.class);}
    @Override protected void setUp()throws Exception{super.setUp();getInstrumentation().getTargetContext().getSharedPreferences("kalwer",0).edit().putInt("dither",0).putInt("popup_dither",0).commit();}
    private void waitFocused()throws Exception{long until=System.currentTimeMillis()+15000;while(!new JSONObject(GameNative.info()).getBoolean("focused") && System.currentTimeMillis()<until)Thread.sleep(50);assertTrue("Popup must finish opening and gain focus: "+GameNative.info(),new JSONObject(GameNative.info()).getBoolean("focused"));}
    private void waitElapsed(double previous)throws Exception{long until=System.currentTimeMillis()+10000;while(new JSONObject(GameNative.info()).getDouble("elapsed")<=previous && System.currentTimeMillis()<until)Thread.sleep(50);assertTrue("Gameplay must advance: "+GameNative.info(),new JSONObject(GameNative.info()).getDouble("elapsed")>previous);}
    private GamePopup popup()throws Exception{java.lang.reflect.Field f=MainActivity.class.getDeclaredField("gamePopup");f.setAccessible(true);return (GamePopup)f.get(getActivity());}
    public void testGamePopupFocusAndPermanentUnlock()throws Throwable {
        MainActivity activity=getActivity();
        runTestOnUiThread(()->{EditText search=activity.findViewById(R.id.search_query);search.setText("/unlockall");search.onEditorAction(EditorInfo.IME_ACTION_GO);});
        long deadline=System.currentTimeMillis()+10000;
        while(popup()==null && System.currentTimeMillis()<deadline)Thread.sleep(100);
        assertNotNull(popup());waitFocused();
        JSONObject before=new JSONObject(GameNative.info());assertEquals(10,before.getInt("kind"));assertEquals(15<<5,before.getLong("owned")&(15<<5));
        // Select Tetris from the native catalog through the popup's touch surface.
        java.lang.reflect.Field field=GamePopup.class.getDeclaredField("surface");field.setAccessible(true);android.view.View surface=(android.view.View)field.get(popup());int[] position=new int[2];
        runTestOnUiThread(()->surface.getLocationOnScreen(position));
        float x=position[0]+surface.getWidth()*.25f,y=position[1]+(55+5*42+20-38)*surface.getHeight()/452f;
        long now=android.os.SystemClock.uptimeMillis();getInstrumentation().sendPointerSync(android.view.MotionEvent.obtain(now,now,0,x,y,0));getInstrumentation().sendPointerSync(android.view.MotionEvent.obtain(now,now+40,1,x,y,0));
        Thread.sleep(600);assertEquals(6,GameNative.kind());
        getInstrumentation().sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_ENTER);waitElapsed(0);
        double running=new JSONObject(GameNative.info()).getDouble("elapsed");assertTrue(running>0);
        runTestOnUiThread(()->{try{popup().setResumed(false);}catch(Exception e){throw new RuntimeException(e);}});
        double paused=new JSONObject(GameNative.info()).getDouble("elapsed");Thread.sleep(350);assertEquals(paused,new JSONObject(GameNative.info()).getDouble("elapsed"),.000001);
        runTestOnUiThread(()->{try{popup().setResumed(true);}catch(Exception e){throw new RuntimeException(e);}});waitElapsed(paused);
        for(int kind:new int[]{0,1,2,3,4,5,7,8}){
            GameNative.open(kind,new java.io.File(activity.getFilesDir(),"games").getAbsolutePath(),activity.getApplicationInfo().nativeLibraryDir,0,0,72,0,true);GameNative.focus(true);GameNative.key(13,true);GameNative.key(13,false);Thread.sleep(200);assertEquals(kind,GameNative.kind());
        }
        runTestOnUiThread(()->{try{popup().closeAnimated();}catch(Exception e){throw new RuntimeException(e);}});Thread.sleep(500);assertTrue(activity.isFinishing());
    }
    public void testKoomNativeRuntimeAndAudioPause()throws Throwable {
        MainActivity activity=getActivity();
        runTestOnUiThread(()->{EditText search=activity.findViewById(R.id.search_query);search.setText("/koom");search.onEditorAction(EditorInfo.IME_ACTION_GO);});
        long deadline=System.currentTimeMillis()+10000;while(popup()==null && System.currentTimeMillis()<deadline)Thread.sleep(100);assertNotNull(popup());waitFocused();
        getInstrumentation().sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_ENTER);
        deadline=System.currentTimeMillis()+90000;while(new JSONObject(GameNative.info()).getLong("frames")<3 && System.currentTimeMillis()<deadline)Thread.sleep(100);
        assertTrue("Actual APK Koom helper must produce frames: "+GameNative.info(),new JSONObject(GameNative.info()).getLong("frames")>=3);
        runTestOnUiThread(()->{try{popup().setResumed(false);}catch(Exception e){throw new RuntimeException(e);}});Thread.sleep(200);
        long frames=new JSONObject(GameNative.info()).getLong("frames");Thread.sleep(350);assertEquals(frames,new JSONObject(GameNative.info()).getLong("frames"));
        runTestOnUiThread(()->{try{popup().closeAnimated();}catch(Exception e){throw new RuntimeException(e);}});Thread.sleep(500);assertTrue(activity.isFinishing());
    }
}
