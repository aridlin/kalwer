package pl.aridlin.kalwer;
import android.content.Context;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.widget.FrameLayout;
final class FieldBackground extends FrameLayout {
    private int opacity=72;
    private Bitmap snapshot;
    FieldBackground(Context context){super(context);refresh();}
    void setOpacity(int percent){opacity=percent;refresh();}
    void setSnapshot(Bitmap bitmap){if(snapshot!=null && snapshot!=bitmap)snapshot.recycle();snapshot=bitmap;refresh();}
    void refresh(){
        final Drawable fill=new HalftoneDrawable((opacity*255/100<<24)|0x000f08,0,getResources().getDisplayMetrics().density,0).anchor(this);
        setBackground(new Drawable(){
            final Paint paint=new Paint();
            public void draw(Canvas canvas){
                if(Appearance.mode>0 && snapshot!=null){paint.setAlpha(180);paint.setFilterBitmap(true);canvas.drawBitmap(snapshot,0,0,paint);}
                fill.setBounds(getBounds());fill.draw(canvas);
            }
            public void setAlpha(int alpha){fill.setAlpha(alpha);}
            public void setColorFilter(ColorFilter filter){fill.setColorFilter(filter);}
            public int getOpacity(){return PixelFormat.TRANSLUCENT;}
        });
    }
}
