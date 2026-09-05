package pl.aridlin.kalwer;

import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.util.LruCache;
import android.view.View;

/** Repeated alpha tile: one GPU fill, no screen-sized bitmap, layer, or frame loop. */
final class HalftoneDrawable extends Drawable {
    private static final LruCache<String, Bitmap> TILES = new LruCache<>(24);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint edge = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF bounds = new RectF();
    private final float radius, stroke;
    private final int tileSize;
    private final int[] location = new int[2];
    private final Matrix phase = new Matrix();
    private View anchor;
    private int phaseX = Integer.MIN_VALUE, phaseY = Integer.MIN_VALUE;

    HalftoneDrawable anchor(View view) { anchor = view; return this; }

    HalftoneDrawable(int color, int outline, float density, float radiusDp) {
        radius = radiusDp * density;
        stroke = outline == Color.TRANSPARENT ? 0 : density;
        int size = Math.max(4, Math.round(8 * density));
        tileSize = size;
        String key = color + ":" + size;
        Bitmap tile = TILES.get(key);
        if (tile == null) {
            tile = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(tile);
            int alpha = Color.alpha(color), delta = Math.min(alpha, 255 - alpha);
            int rgb = color & 0x00ffffff;
            canvas.drawColor(((alpha - delta) << 24) | rgb);
            Paint dot = new Paint(Paint.ANTI_ALIAS_FLAG);
            dot.setColor(((alpha + delta) << 24) | rgb);
            // SRC preserves the requested alpha instead of compositing it onto the low-alpha base.
            dot.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC));
            canvas.drawCircle(size * .5f, size * .5f, size * .399f, dot);
            TILES.put(key, tile);
        }
        fill.setShader(new BitmapShader(tile, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT));
        edge.setColor(outline); edge.setStyle(Paint.Style.STROKE); edge.setStrokeWidth(stroke);
    }
    @Override protected void onBoundsChange(Rect b) {
        bounds.set(b.left + stroke / 2, b.top + stroke / 2, b.right - stroke / 2, b.bottom - stroke / 2);
    }
    @Override public void draw(Canvas canvas) {
        if (anchor != null) {
            anchor.getLocationInWindow(location);
            int x = Math.floorMod(location[0], tileSize), y = Math.floorMod(location[1], tileSize);
            if (x != phaseX || y != phaseY) {
                phaseX = x; phaseY = y;
                phase.setTranslate(-x, -y); fill.getShader().setLocalMatrix(phase);
            }
        }
        canvas.drawRoundRect(bounds, radius, radius, fill);
        if (stroke > 0) canvas.drawRoundRect(bounds, radius, radius, edge);
    }
    @Override public void setAlpha(int alpha) { fill.setAlpha(alpha); edge.setAlpha(alpha); invalidateSelf(); }
    @Override public void setColorFilter(ColorFilter filter) { fill.setColorFilter(filter); invalidateSelf(); }
    @Override public int getOpacity() { return PixelFormat.TRANSLUCENT; }
}
