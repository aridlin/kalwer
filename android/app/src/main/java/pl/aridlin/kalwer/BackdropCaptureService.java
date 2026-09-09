package pl.aridlin.kalwer;

import android.app.*;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.*;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.*;
import java.nio.ByteBuffer;
import java.lang.ref.WeakReference;

/** One consented, in-memory backdrop snapshot. No files, audio, or ongoing capture. */
public final class BackdropCaptureService extends Service {
    static final class Snapshot {Bitmap launcher,popup;Snapshot(Bitmap a,Bitmap b){launcher=a;popup=b;}void recycle(){if(launcher!=null)launcher.recycle();if(popup!=null)popup.recycle();}}
    interface Receiver { void captured(Snapshot image); }
    static WeakReference<Receiver> receiver=new WeakReference<>(null);
    private MediaProjection projection;
    private VirtualDisplay display;
    private ImageReader reader;
    private HandlerThread thread;
    private final Handler main=new Handler(Looper.getMainLooper());
    private boolean finished;
    private int[] latestPixels;
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public int onStartCommand(Intent intent,int flags,int id) {
        NotificationManager manager=getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel("backdrop","Backdrop snapshot",NotificationManager.IMPORTANCE_LOW));
        Notification notification=new Notification.Builder(this,"backdrop").setSmallIcon(R.drawable.ic_kalwer).setContentTitle("Kalwer backdrop snapshot").setContentText("Capturing one frame; stops immediately afterward").build();
        if(Build.VERSION.SDK_INT>=29)startForeground(21,notification,ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION);else startForeground(21,notification);
        try {
            if(intent==null)throw new IllegalArgumentException();
            Appearance.load(getSharedPreferences("kalwer",MODE_PRIVATE));
            final int mode=Appearance.mode,popupMode=Appearance.popupMode,theme=Appearance.theme;
            int w=intent.getIntExtra("width",360),h=intent.getIntExtra("height",640);
            thread=new HandlerThread("kalwer-snapshot");thread.start();Handler worker=new Handler(thread.getLooper());
            projection=getSystemService(MediaProjectionManager.class).getMediaProjection(Activity.RESULT_OK,intent.getParcelableExtra("consent"));
            if(projection==null)throw new IllegalStateException();
            projection.registerCallback(new MediaProjection.Callback(){@Override public void onStop(){finishCapture(null);}},main);
            reader=ImageReader.newInstance(w,h,PixelFormat.RGBA_8888,2);
            reader.setOnImageAvailableListener(source->{
                try(Image image=source.acquireLatestImage()) {
                    if(image==null)return;
                    Image.Plane plane=image.getPlanes()[0];ByteBuffer bytes=plane.getBuffer();int stride=plane.getRowStride(),pixelStride=plane.getPixelStride();int[] pixels=new int[w*h];
                    for(int y=0;y<h;y++)for(int x=0;x<w;x++){int offset=y*stride+x*pixelStride;pixels[y*w+x]=Color.rgb(bytes.get(offset)&255,bytes.get(offset+1)&255,bytes.get(offset+2)&255);}
                    latestPixels=pixels;
                } catch(RuntimeException ignored) { }
            },worker);
            // The first virtual-display frame may still contain the consent transition.
            // Drain those frames and keep only the settled backdrop; no frame is written to disk.
            worker.postDelayed(new Runnable(){public void run(){
                if(latestPixels==null){worker.postDelayed(this,100);return;}
                reader.setOnImageAvailableListener(null,null);Snapshot result=null;
                if(latestPixels!=null){Bitmap raw=Bitmap.createBitmap(latestPixels,w,h,Bitmap.Config.ARGB_8888);Bitmap launcher=mode>0?GpuDither.process(BackdropCaptureService.this,raw,mode,theme):null;Bitmap popup=popupMode>0?GpuDither.process(BackdropCaptureService.this,raw,popupMode,theme):null;if(launcher!=null || popup!=null)result=new Snapshot(launcher,popup);raw.recycle();latestPixels=null;}
                Snapshot output=result;main.post(()->finishCapture(output));
            }},500);
            display=projection.createVirtualDisplay("Kalwer backdrop",w,h,getResources().getDisplayMetrics().densityDpi,DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,reader.getSurface(),null,worker);
            main.postDelayed(()->finishCapture(null),4000);
        } catch(RuntimeException e) {finishCapture(null);}
        return START_NOT_STICKY;
    }
    private void finishCapture(Snapshot image) {
        if(finished){if(image!=null)image.recycle();return;}finished=true;
        Receiver listener=receiver.get();receiver.clear();if(listener!=null)listener.captured(image);else if(image!=null)image.recycle();
        stopSelf();
    }
    @Override public void onDestroy() {
        if(display!=null)display.release();if(reader!=null)reader.close();if(projection!=null)projection.stop();if(thread!=null)thread.quit();main.removeCallbacksAndMessages(null);stopForeground(STOP_FOREGROUND_REMOVE);super.onDestroy();
    }
}
