package pl.aridlin.kalwer;

import android.content.Context;
import android.widget.FrameLayout;

/** The background itself has halftone alpha; text and icons remain crisp. */
final class FieldBackground extends FrameLayout {
    private final float density;
    private int opacity = -1;

    FieldBackground(Context context) {
        super(context);
        density = getResources().getDisplayMetrics().density;
        setOpacity(72);
    }

    void setOpacity(int percent) {
        if (opacity == percent) return;
        opacity = percent;
        setBackground(new HalftoneDrawable((percent * 255 / 100 << 24) | 0x000f08, 0, density, 0).anchor(this));
    }
}
