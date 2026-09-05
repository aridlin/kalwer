package pl.aridlin.kalwer;

import android.content.*;
import android.content.pm.ResolveInfo;
import android.graphics.*;
import android.graphics.drawable.Drawable;
import android.os.*;
import android.util.LruCache;
import android.util.AtomicFile;
import java.io.*;
import android.widget.ImageView;
import java.lang.ref.WeakReference;
import java.util.*;
import java.util.concurrent.*;

/** Persistent metadata cache plus a process cache. Only application context is retained. */
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
        App(ComponentName component, String title) {
            this.info = null;
            this.component = component;
            this.title = title;
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
    private final ExecutorService iconIo = Executors.newSingleThreadExecutor();
    private final AtomicFile diskCache;
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
        diskCache = new AtomicFile(new File(context.getCacheDir(), "installed-apps-v1.bin"));
        BroadcastReceiver changed = new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent intent) {
                dirty = true; generation++; icons.evictAll();
                if (!waiters.isEmpty()) refresh();
            }
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
    private void deliver(List<App> apps) {
        waiters.removeIf(ref -> ref.get() == null);
        for (WeakReference<Callback> ref : new ArrayList<>(waiters)) {
            Callback callback = ref.get();
            if (callback != null) callback.loaded(apps);
        }
    }
    void load(Callback callback) {
        waiters.removeIf(ref -> ref.get() == null || ref.get() == callback);
        waiters.add(new WeakReference<>(callback));
        if (cache != null) callback.loaded(cache);
        if (!dirty && SystemClock.elapsedRealtime() - loadedAt < 300000) return;
        refresh();
    }
    // All disk access stays on the metadata executor. Icons have their own queue,
    // so a PackageManager refresh cannot block icons for an already visible list.
    private void refresh() {
        if (loading) return;
        loading = true;
        final int version = generation;
        final boolean cold = cache == null;
        final String locale = context.getResources().getConfiguration().getLocales().toLanguageTags();
        io.execute(() -> {
            if (cold) {
                long start = SystemClock.elapsedRealtime();
                List<App> saved = readCache(diskCache, locale);
                if (saved != null) main.post(() -> {
                    if (cache == null && version == generation) {
                        cache = saved;
                        deliver(cache);
                        android.util.Log.d("KalwerCatalog", "Disk cache visible in " + (SystemClock.elapsedRealtime() - start) + " ms (" + saved.size() + " apps)");
                    }
                });
            }
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
            if (ready != null) writeCache(diskCache, locale, ready);
            main.post(() -> {
                loading = false;
                if (ready != null && generation == version) {
                    cache = ready; dirty = false; loadedAt = SystemClock.elapsedRealtime();
                    deliver(cache);
                } else if (generation != version) refresh();
                else if (cache == null) deliver(null);
            });
        });
    }
    static List<App> readCache(AtomicFile file, String locale) {
        try (DataInputStream input = new DataInputStream(new BufferedInputStream(file.openRead()))) {
            if (input.readInt() != 0x4b415031 || !input.readUTF().equals(locale)) return null;
            int count = input.readInt();
            if (count < 0 || count > 100000) return null;
            List<App> apps = new ArrayList<>(count);
            Set<String> seen = new HashSet<>();
            for (int i = 0; i < count; i++) {
                ComponentName component = ComponentName.unflattenFromString(input.readUTF());
                String title = input.readUTF();
                if (component == null) return null;
                App app = new App(component, title);
                if (seen.add(app.id)) apps.add(app);
            }
            if (input.read() != -1) return null;
            return Collections.unmodifiableList(apps);
        } catch (IOException | RuntimeException ignored) { return null; }
    }
    static void writeCache(AtomicFile file, String locale, List<App> apps) {
        FileOutputStream stream = null;
        try {
            stream = file.startWrite();
            DataOutputStream output = new DataOutputStream(new BufferedOutputStream(stream));
            output.writeInt(0x4b415031); output.writeUTF(locale); output.writeInt(apps.size());
            for (App app : apps) { output.writeUTF(app.id); output.writeUTF(app.title); }
            output.flush();
            file.finishWrite(stream);
        } catch (IOException | RuntimeException ignored) { file.failWrite(stream); }
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
        iconIo.execute(() -> {
            Bitmap loaded = null;
            try {
                Drawable drawable = app.info != null ? app.info.loadIcon(context.getPackageManager()) : context.getPackageManager().getActivityIcon(app.component);
                loaded = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
                drawable.setBounds(0, 0, size, size); drawable.draw(new Canvas(loaded));
            } catch (android.content.pm.PackageManager.NameNotFoundException | RuntimeException ignored) { }
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
