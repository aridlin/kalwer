package pl.aridlin.kalwer;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.opengl.GLSurfaceView;
import android.os.*;
import android.view.*;
import android.view.animation.DecelerateInterpolator;
import android.widget.*;
import java.io.File;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

final class GamePopup extends Dialog implements GLSurfaceView.Renderer {
    private final Activity owner;
    private final Runnable importWad,onClosed;
    private final Handler handler=new Handler(Looper.getMainLooper());
    private final GLSurfaceView surface;
    private final LinearLayout controls;
    private final TextView title;
    private final Reveal body;
    private boolean active,closing,opened,resumed=true;
    private volatile boolean disposed;
    private volatile long lastFrame;
    private long titleTime;
    private int width,height,controlKind=-1;
    private final Choreographer.FrameCallback frame=new Choreographer.FrameCallback(){public void doFrame(long now){
        if(!active || closing)return;
        surface.requestRender();
        if(now-titleTime>500000000L){titleTime=now;title.setText(GameNative.title());if(controlKind!=GameNative.kind())buildControls();}
        Choreographer.getInstance().postFrameCallback(this);
    }};
    private int dp(float n){return Math.round(n*owner.getResources().getDisplayMetrics().density);}
    GamePopup(Activity activity,int kind,Bitmap snapshot,Runnable importAction,Runnable closed,boolean unlock){
        super(activity);owner=activity;importWad=importAction;onClosed=closed;
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        android.content.SharedPreferences prefs=activity.getSharedPreferences("kalwer",Context.MODE_PRIVATE);
        File data=new File(activity.getFilesDir(),"games");data.mkdirs();
        GameNative.open(kind,data.getAbsolutePath(),activity.getApplicationInfo().nativeLibraryDir,prefs.getLong("passive_koins",0),Appearance.theme,prefs.getInt("opacity",72),Appearance.popupMode,Appearance.popupHalftone);
        if(unlock && !GameNative.unlock())Toast.makeText(owner,"Could not save unlocks",Toast.LENGTH_LONG).show();
        FrameLayout stage=new FrameLayout(activity);
        body=new Reveal(activity);body.setOrientation(LinearLayout.VERTICAL);
        Drawable fill=new HalftoneDrawable(0xe6081a18,0xff8ce9b3,activity.getResources().getDisplayMetrics().density,10,true).anchor(body);
        if(Appearance.popupMode>0 && snapshot!=null && !snapshot.isRecycled()){
            Drawable backdrop=new Drawable(){final Paint paint=new Paint();final int[] origin=new int[2];public void draw(Canvas canvas){if(snapshot.isRecycled())return;body.getLocationOnScreen(origin);paint.setAlpha(180);canvas.drawBitmap(snapshot,-origin[0],-origin[1],paint);}public void setAlpha(int a){paint.setAlpha(a);}public void setColorFilter(ColorFilter filter){paint.setColorFilter(filter);}public int getOpacity(){return PixelFormat.TRANSLUCENT;}};
            fill=new android.graphics.drawable.LayerDrawable(new Drawable[]{backdrop,fill});
        }
        android.graphics.drawable.GradientDrawable shade=new android.graphics.drawable.GradientDrawable();shade.setColor(0x90000000|Appearance.DARK[Appearance.theme]);shade.setCornerRadius(dp(10));
        body.setBackground(new android.graphics.drawable.LayerDrawable(new Drawable[]{shade,fill}));body.setPadding(dp(2),dp(2),dp(2),dp(2));
        LinearLayout bar=new LinearLayout(activity);bar.setGravity(Gravity.CENTER_VERTICAL);
        title=new TextView(activity);title.setTextColor(0xff000000|Appearance.TEXT[Appearance.theme]);title.setTextSize(13);title.setPadding(dp(9),0,0,0);title.setSingleLine(true);title.setGravity(Gravity.CENTER_VERTICAL);title.setText(GameNative.title());
        bar.addView(title,new LinearLayout.LayoutParams(0,dp(42),1));
        Button close=button("×");close.setContentDescription("Close game");close.setOnClickListener(v->closeAnimated());bar.addView(close,new LinearLayout.LayoutParams(dp(44),dp(42)));body.addView(bar);
        surface=new GLSurfaceView(activity);surface.setFocusableInTouchMode(true);surface.setEGLContextClientVersion(3);surface.setEGLConfigChooser(8,8,8,8,16,0);surface.getHolder().setFormat(PixelFormat.TRANSLUCENT);surface.setZOrderOnTop(true);surface.setPreserveEGLContextOnPause(true);surface.setRenderer(this);surface.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);surface.setVisibility(View.INVISIBLE);
        body.addView(surface,new LinearLayout.LayoutParams(-1,0,1));
        surface.setOnTouchListener(new View.OnTouchListener(){float x,y,downX,downY;boolean longPress;int pressedKind;final Runnable flag=()->{longPress=true;GameNative.pointer(x,y,3);surface.requestRender();};
            public boolean onTouch(View v,android.view.MotionEvent e){x=e.getX()*420/v.getWidth();y=38+e.getY()*452/v.getHeight();int action=e.getActionMasked(),kind=GameNative.kind();
                if(action==MotionEvent.ACTION_DOWN){pressedKind=kind;downX=x;downY=y;longPress=false;if(kind==1)handler.postDelayed(flag,400);else GameNative.pointer(x,y,kind==2?0:1);}
                else if(action==MotionEvent.ACTION_MOVE){if(Math.hypot(x-downX,y-downY)>12)handler.removeCallbacks(flag);if(kind==2 || kind==7)GameNative.pointer(x,y,0);}
                else if(action==MotionEvent.ACTION_UP){handler.removeCallbacks(flag);if(pressedKind==1 && kind==1 && !longPress)GameNative.pointer(x,y,1);if(pressedKind==2 && kind==2)GameNative.pointer(x,y,1);v.performClick();}
                else if(action==MotionEvent.ACTION_CANCEL)handler.removeCallbacks(flag);
                surface.requestRender();return true;
            }});
        controls=new LinearLayout(activity);controls.setOrientation(LinearLayout.VERTICAL);body.addView(controls);buildControls();
        int available=activity.getResources().getDisplayMetrics().heightPixels-dp(24);
        int popupWidth=Math.min(dp(380),activity.getResources().getDisplayMetrics().widthPixels-dp(16));
        int popupHeight=Math.min(available,Math.round(popupWidth*452f/420)+dp(148));
        FrameLayout.LayoutParams placement=new FrameLayout.LayoutParams(popupWidth,popupHeight,Gravity.TOP|Gravity.RIGHT);placement.topMargin=dp(12);placement.rightMargin=dp(8);stage.addView(body,placement);setContentView(stage);
        setCanceledOnTouchOutside(false);setOnDismissListener(d->dispose());
    }
    @Override public void show(){super.show();Window window=getWindow();window.setBackgroundDrawableResource(android.R.color.transparent);window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);window.setLayout(-1,-1);window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);body.post(()->animate(false));}
    private void animate(boolean reverse){
        android.animation.ValueAnimator animation=android.animation.ValueAnimator.ofFloat(reverse?1:0,reverse?0:1);animation.setDuration(360);animation.setInterpolator(new DecelerateInterpolator());animation.addUpdateListener(a->{body.progress=(float)a.getAnimatedValue();body.invalidate();});
        animation.addListener(new android.animation.AnimatorListenerAdapter(){@Override public void onAnimationEnd(android.animation.Animator a){if(reverse){dismiss();onClosed.run();}else{opened=true;surface.setVisibility(View.VISIBLE);surface.requestFocus();setActive(resumed && getWindow().getDecorView().hasWindowFocus());}}});animation.start();
    }
    void closeAnimated(){if(closing)return;closing=true;setActive(false);surface.setVisibility(View.INVISIBLE);animate(true);}
    void setResumed(boolean value){resumed=value;setActive(value && opened && getWindow()!=null && getWindow().getDecorView().hasWindowFocus());}
    @Override public void onWindowFocusChanged(boolean focused){super.onWindowFocusChanged(focused);setActive(focused && resumed && opened);}
    private void setActive(boolean value){value=value && !closing && !disposed;if(value==active)return;active=value;GameNative.focus(value);Choreographer.getInstance().removeFrameCallback(frame);if(value){lastFrame=0;surface.onResume();Choreographer.getInstance().postFrameCallback(frame);}else{handler.removeCallbacksAndMessages(null);surface.onPause();}}
    void dispose(){if(disposed)return;setActive(false);disposed=true;handler.removeCallbacksAndMessages(null);surface.onPause();GameNative.close();}
    @Override public void onBackPressed(){closeAnimated();}
    @Override public boolean dispatchKeyEvent(KeyEvent e){if(e.getKeyCode()==KeyEvent.KEYCODE_BACK)return super.dispatchKeyEvent(e);int code=map(e);if(code<0)return super.dispatchKeyEvent(e);GameNative.key(code,e.getAction()==KeyEvent.ACTION_DOWN);surface.requestRender();return true;}
    private static int map(KeyEvent e){switch(e.getKeyCode()){case KeyEvent.KEYCODE_DPAD_LEFT:return 1;case KeyEvent.KEYCODE_DPAD_RIGHT:return 2;case KeyEvent.KEYCODE_DPAD_UP:return 3;case KeyEvent.KEYCODE_DPAD_DOWN:return 4;case KeyEvent.KEYCODE_ENTER:return 13;case KeyEvent.KEYCODE_SPACE:return 32;case KeyEvent.KEYCODE_TAB:return 9;case KeyEvent.KEYCODE_ESCAPE:return 'q';default:int c=e.getUnicodeChar();return c>0 && c<128?Character.toLowerCase(c):-1;}}
    private void buildControls(){handler.removeCallbacksAndMessages(null);controls.removeAllViews();controlKind=GameNative.kind();
        LinearLayout first=row();add(first,"←",1);add(first,"↑",3);add(first,"↓",4);add(first,"→",2);add(first,"Play",13);
        LinearLayout second=row();
        switch(controlKind){
            case 1:add(second,"Flag",'f');break;
            case 2:add(second,"Fire",32);break;
            case 3:for(int i=1;i<=5;i++)add(second,""+i,'0'+i);add(second,"Sun",32);add(second,"Dig",'x');break;
            case 4:add(second,"2P",'h');add(second,"Q",'q');add(second,"B",'b');add(second,"N",'n');break;
            case 6:add(second,"Drop",32);add(second,"Hold",'c');add(second,"↶",'z');break;
            case 7:add(second,"Serve",32);break;
            case 8:add(second,"Nitro",32);add(second,"Car",'c');break;
            case 9:add(second,"Fire",32);add(second,"Use",'e');add(second,"Menu",'q');add(second,"Tab",9);Button wad=button("WAD");wad.setOnClickListener(v->importWad.run());second.addView(wad,new LinearLayout.LayoutParams(0,dp(42),1));
                LinearLayout third=row();add(third,"Strafe L",'a');add(third,"Strafe R",'d');for(int i=1;i<=7;i++)add(third,""+i,'0'+i);break;
            default:break;
        }
        add(second,controlKind==9?"WAD list":"Restart",'r');
    }
    private LinearLayout row(){LinearLayout r=new LinearLayout(owner);controls.addView(r);return r;}
    private Button button(String label){Button b=new Button(owner);b.setText(label);b.setTextSize(11);b.setMinWidth(0);b.setMinimumWidth(0);b.setPadding(0,0,0,0);b.setAllCaps(false);b.setTextColor(0xff000000|Appearance.TEXT[Appearance.theme]);b.setBackground(new HalftoneDrawable(0xd0112e29,0xff2b5145,owner.getResources().getDisplayMetrics().density,4,true));return b;}
    private void add(LinearLayout row,String label,int key){Button b=button(controlKind==9 && key=='a'?"◁":controlKind==9 && key=='d'?"▷":label);b.setContentDescription(label);b.setTooltipText(label);final boolean[] touchClick={false};b.setOnClickListener(v->{if(!touchClick[0]){GameNative.key(key,true);GameNative.key(key,false);surface.requestRender();}});b.setOnTouchListener(new View.OnTouchListener(){boolean held;final Runnable repeat=new Runnable(){public void run(){if(held && active){if(controlKind!=8 && controlKind!=9)GameNative.key(key,true);handler.postDelayed(this,95);}}};public boolean onTouch(View v,android.view.MotionEvent e){if(e.getActionMasked()==MotionEvent.ACTION_DOWN){held=true;GameNative.key(key,true);if(key>=1 && key<=4)handler.postDelayed(repeat,220);v.setPressed(true);return true;}if(e.getActionMasked()==MotionEvent.ACTION_UP || e.getActionMasked()==MotionEvent.ACTION_CANCEL){held=false;handler.removeCallbacks(repeat);GameNative.key(key,false);v.setPressed(false);if(e.getActionMasked()==MotionEvent.ACTION_UP){touchClick[0]=true;v.performClick();touchClick[0]=false;}return true;}return true;}});row.addView(b,new LinearLayout.LayoutParams(0,dp(42),1));}
    @Override public void onSurfaceCreated(GL10 gl,EGLConfig config){Bitmap atlas=Bitmap.createBitmap(512,256,Bitmap.Config.ARGB_8888);Canvas canvas=new Canvas(atlas);Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);paint.setTextSize(24);paint.setColor(Color.WHITE);float[] advances=new float[96];for(int i=0;i<96;i++){String letter=String.valueOf((char)(i+32));advances[i]=paint.measureText(letter);canvas.drawText(letter,(i%16)*32+2,(i/16)*40+29,paint);}if(!GameNative.surface(atlas,advances))handler.post(()->Toast.makeText(owner,"OpenGL game surface unavailable",Toast.LENGTH_LONG).show());atlas.recycle();}
    @Override public void onSurfaceChanged(GL10 gl,int w,int h){width=w;height=h;}
    @Override public void onDrawFrame(GL10 gl){long now=System.nanoTime();double dt=lastFrame==0?0:(now-lastFrame)/1e9;lastFrame=now;if(!disposed)GameNative.draw(width,height,dt);}
    private static final class Reveal extends LinearLayout {float progress;Reveal(Context c){super(c);setWillNotDraw(false);}@Override public void draw(Canvas c){int save=c.save();float across=Math.min(1,progress/.4f),down=Math.max(0,(progress-.4f)/.6f);c.clipRect(0,0,getWidth()*across,Math.max(2,getHeight()*down));super.draw(c);c.restoreToCount(save);}}
}
