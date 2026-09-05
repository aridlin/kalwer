package pl.aridlin.kalwer;

import android.content.*;
import android.content.pm.ResolveInfo;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.os.*;
import android.util.LruCache;
import android.widget.ImageView;
import java.lang.ref.WeakReference;
import java.util.*;
import java.util.concurrent.*;

/** Process cache owns only application context. No Activity survives closing the launcher. */
final class AppCatalog {
    static final class App {
        final ComponentName component;
        final String title, id, nameKey, packageKey;
        final ResolveInfo info;
        App(ResolveInfo info, Context context) {
            this.info = info;
            component = new ComponentName(info.activityInfo.packageName, info.activityInfo.name);
            title = info.loadLabel(context.getPackageManager()).toString();
            id = component.flattenToString();
            nameKey = SearchLogic.normalize(title);
            packageKey = SearchLogic.normalize(component.getPackageName());
        }
        String id() { return id; }
    }
    interface Callback { void loaded(List<App> apps); }
    private static AppCatalog instance;
    static synchronized AppCatalog get(Context context) {
        if (instance == null) instance = new AppCatalog((android.app.Application) context.getApplicationContext());
        return instance;
    }
    private final android.app.Application context;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final LruCache<String, Bitmap> icons = new LruCache<>(2 * 1024 * 1024) {
        @Override protected int sizeOf(String key, Bitmap value) { return value.getAllocationByteCount(); }
    };
    private final Map<String, List<WeakReference<ImageView>>> iconWaiters = new HashMap<>();
    private final List<WeakReference<Callback>> waiters = new ArrayList<>();
    private List<App> cache;
    private boolean loading, dirty = true;
    private long loadedAt;
    private int generation;

    private AppCatalog(android.app.Application context) {
        this.context = context;
        BroadcastReceiver changed = new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent intent) { dirty = true; generation++; icons.evictAll(); }
        };
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_PACKAGE_ADDED); filter.addAction(Intent.ACTION_PACKAGE_REMOVED);
        filter.addAction(Intent.ACTION_PACKAGE_CHANGED); filter.addAction(Intent.ACTION_PACKAGE_REPLACED);
        filter.addDataScheme("package");
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(changed, filter, Context.RECEIVER_EXPORTED);
        else context.registerReceiver(changed, filter);
        IntentFilter locale = new IntentFilter(Intent.ACTION_LOCALE_CHANGED);
        if (Build.VERSION.SDK_INT >= 33) context.registerReceiver(changed, locale, Context.RECEIVER_EXPORTED);
        else context.registerReceiver(changed, locale);
    }
    void load(Callback callback) {
        if (cache != null) callback.loaded(cache);
        if (!dirty && SystemClock.elapsedRealtime() - loadedAt < 60000) return;
        waiters.add(new WeakReference<>(callback));
        if (loading) return;
        loading = true;
        int version = generation;
        io.execute(() -> {
            List<App> found = new ArrayList<>();
            try {
                Set<String> seen = new HashSet<>();
                Intent intent = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
                for (ResolveInfo info : context.getPackageManager().queryIntentActivities(intent, 0)) {
                    if (info.activityInfo.packageName.equals(context.getPackageName())) continue;
                    try {
                        App app = new App(info, context);
                        if (seen.add(app.id)) found.add(app);
                    } catch (RuntimeException ignored) { /* Package may be removed during discovery. */ }
                }
                found.sort(Comparator.comparing(a -> a.nameKey));
            } catch (RuntimeException error) { found = null; }
            List<App> ready = found == null ? null : Collections.unmodifiableList(found);
            main.post(() -> {
                loading = false;
                if (ready != null) { cache = ready; dirty = generation != version; loadedAt = SystemClock.elapsedRealtime(); }
                List<WeakReference<Callback>> callbacks = new ArrayList<>(waiters);
                waiters.clear();
                for (WeakReference<Callback> ref : callbacks) { Callback c = ref.get(); if (c != null) c.loaded(ready); }
            });
        });
    }
    void icon(App app, ImageView view) {
        view.setTag(app.id);
        Bitmap bitmap = icons.get(app.id);
        view.setImageBitmap(bitmap);
        if (bitmap != null) return;
        List<WeakReference<ImageView>> targets = iconWaiters.get(app.id);
        if (targets != null) { targets.add(new WeakReference<>(view)); return; }
        targets = new ArrayList<>(); targets.add(new WeakReference<>(view)); iconWaiters.put(app.id, targets);
        int size = Math.round(36 * context.getResources().getDisplayMetrics().density);
        int version = generation;
        io.execute(() -> {
            Bitmap loaded = null;
            try {
                Drawable drawable = app.info.loadIcon(context.getPackageManager());
                loaded = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
                drawable.setBounds(0, 0, size, size); drawable.draw(new Canvas(loaded));
            } catch (RuntimeException ignored) { }
            Bitmap result = loaded;
            main.post(() -> {
                List<WeakReference<ImageView>> views = iconWaiters.remove(app.id);
                if (result == null || version != generation) return;
                icons.put(app.id, result);
                if (views != null) for (WeakReference<ImageView> ref : views) {
                    ImageView target = ref.get();
                    if (target != null && app.id.equals(target.getTag())) target.setImageBitmap(result);
                }
            });
        });
    }
}
