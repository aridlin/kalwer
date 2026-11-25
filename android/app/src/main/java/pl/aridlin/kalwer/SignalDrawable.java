package pl.aridlin.kalwer;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;

final class SignalDrawable extends Drawable {
    enum Surface { MASTHEAD, SEARCH, ROW, SELECTED_ROW, BUTTON }
    static final int INK = 0xff140f12;
    static final int STAGE = 0xff111111;
    static final int COAL = 0xff1e1419;
    static final int RULE = 0xff66424a;
    static final int RED = 0xffef5148;
    static final int RED_FILL = 0xff3a1c23;
    static final int MUTED = 0xffc5b7bb;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private final Surface surface;
    private int alpha = 255;

    SignalDrawable(Surface surface, float density) {
        this.surface = surface;
        this.density = density;
    }

    private void fill(Canvas canvas, float left, float top, float right, float bottom, int color) {
        if (right <= left || bottom <= top) return;
        paint.setColor(color);
        paint.setAlpha(alpha * Color.alpha(color) / 255);
        canvas.drawRect(left, top, right, bottom, paint);
    }

    @Override public void draw(Canvas canvas) {
        Rect bounds = getBounds();
        if (bounds.isEmpty()) return;
        canvas.save();
        canvas.clipRect(bounds);
        canvas.translate(bounds.left, bounds.top);
        canvas.scale(density, density);
        float w = bounds.width() / density;
        float h = bounds.height() / density;

        switch (surface) {
            case MASTHEAD:
                fill(canvas, 0, h - 1, w, h, RULE);
                float tabTop = (h - 28) * .5f;
                float tabWidth = Math.min(w, 110);
                fill(canvas, 0, tabTop, tabWidth - 12, tabTop + 28, RED);
                for (int line = 0; line < 28; line++)
                    fill(canvas, tabWidth - 12, tabTop + line, tabWidth - line * 12f / 28,
                            tabTop + line + 1, RED);
                break;
            case SEARCH:
                fill(canvas, 0, 0, w, h, STAGE);
                fill(canvas, 0, 0, 39, h - 9, COAL);
                fill(canvas, 39, 0, 40, h - 9, RULE);
                fill(canvas, 42, 0, w, 1, RULE);

                float cx = 19.5f;
                float cy = (h - 9) * .5f;
                fill(canvas, cx - 15, cy - .5f, cx - 8, cy + .5f, RULE);
                fill(canvas, cx + 8, cy - .5f, cx + 15, cy + .5f, RED);
                fill(canvas, cx - .5f, cy - 15, cx + .5f, cy - 8, RULE);
                fill(canvas, cx - .5f, cy + 8, cx + .5f, cy + 15, RULE);
                paint.setColor(RED);
                paint.setAlpha(alpha);
                paint.setStrokeWidth(1.5f);
                paint.setStyle(Paint.Style.STROKE);
                canvas.drawArc(cx - 8, cy - 8, cx + 8, cy + 8, 24, 312, false, paint);
                paint.setStyle(Paint.Style.FILL);
                fill(canvas, cx - 2, cy - 2, cx + 2, cy + 2, Appearance.SIGNAL_CREAM);
                float accentEnd = Math.min(w, 72);
                fill(canvas, 0, h - 2, accentEnd, h, RED);
                fill(canvas, accentEnd, h - 1, w, h, RULE);
                break;
            case ROW:
            case SELECTED_ROW:
                boolean active = surface == Surface.SELECTED_ROW;
                fill(canvas, 0, 0, w, h, active ? RED_FILL : COAL);
                fill(canvas, 0, 1, 52, h - 2, STAGE);
                fill(canvas, 52, 8, 53, h - 8, RULE);
                fill(canvas, 0, h - (active ? 2 : 1), w, h, active ? RED : RULE);
                if (active) {
                    fill(canvas, 0, 0, 3, h, RED);
                    fill(canvas, w - 10, h - 3, w - 4, h - 1, Appearance.SIGNAL_CREAM);
                }
                break;
            case BUTTON:
                fill(canvas, 2, 4, w - 2, h - 4, COAL);
                fill(canvas, 2, h - 5, w - 2, h - 4, RULE);
                break;
        }
        canvas.restore();
    }

    @Override public void setAlpha(int alpha) { this.alpha = alpha; invalidateSelf(); }
    @Override public void setColorFilter(ColorFilter filter) { paint.setColorFilter(filter); invalidateSelf(); }
    @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
}
