package pl.aridlin.kalwer;

import android.app.Instrumentation;
import android.content.Intent;
import android.content.SharedPreferences;
import android.test.ActivityInstrumentationTestCase2;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

/** Real activity tests using Android's built-in test runner; no external dependencies. */
@SuppressWarnings("deprecation")
public final class LauncherTest extends ActivityInstrumentationTestCase2<MainActivity> {
    public LauncherTest() { super(MainActivity.class); }

    @Override protected void setUp() throws Exception {
        super.setUp();
        // startActivitySync must run on the instrumentation thread, before UI callbacks.
        getActivity();
    }

    private EditText search() { return getActivity().findViewById(R.id.search_query); }
    private ListView results() { return getActivity().findViewById(R.id.search_results); }
    private void type(String value) throws Throwable {
        runTestOnUiThread(() -> search().setText(value));
        getInstrumentation().waitForIdleSync();
        long deadline = System.currentTimeMillis() + 10000;
        while (results().getCount() == 0 && System.currentTimeMillis() < deadline) {
            Thread.sleep(50);
            getInstrumentation().waitForIdleSync();
        }
    }

    private String title(int position) {
        LinearLayout row = (LinearLayout) results().getAdapter().getView(position, null, results());
        return ((TextView) ((LinearLayout) row.getChildAt(1)).getChildAt(0)).getText().toString();
    }

    public void testInstalledAppsAndFavourites() throws Throwable {
        type("");
        assertTrue("Discover launcher apps", results().getCount() > 0);
        SharedPreferences prefs = getActivity().getSharedPreferences("kalwer", 0);
        int before = prefs.getStringSet("favourites", java.util.Collections.emptySet()).size();
        runTestOnUiThread(() -> {
            LinearLayout row = (LinearLayout) results().getAdapter().getView(0, null, results());
            row.getChildAt(2).performClick();
        });
        assertEquals(1, Math.abs(before - prefs.getStringSet("favourites", java.util.Collections.emptySet()).size()));
    }

    public void testNoMatchEnterUsesEncodedGoogleQuery() throws Throwable {
        final Intent[] opened = {null};
        Instrumentation.ActivityMonitor monitor = new Instrumentation.ActivityMonitor() {
            @Override public Instrumentation.ActivityResult onStartActivity(Intent intent) {
                opened[0] = intent;
                return new Instrumentation.ActivityResult(0, null);
            }
        };
        getInstrumentation().addMonitor(monitor);
        try {
            type("nonexistent app cats & dogs + Łódź");
            assertEquals("Search Google", title(0));
            runTestOnUiThread(() -> search().onEditorAction(android.view.inputmethod.EditorInfo.IME_ACTION_GO));
            assertNotNull(opened[0]);
            assertEquals("www.google.com", opened[0].getData().getHost());
            assertEquals("nonexistent app cats & dogs + Łódź", opened[0].getData().getQueryParameter("q"));
        } finally { getInstrumentation().removeMonitor(monitor); }
    }

    public void testExplicitGoogleOverridesMatchingApps() throws Throwable {
        type("? settings");
        assertEquals(1, results().getCount());
        assertEquals("Search Google", title(0));
    }

    public void testCalculatorAndActionModes() throws Throwable {
        type("2+3*4");
        assertEquals("14", title(0));
        type("> timer 1h30m");
        assertEquals("Set timer", title(0));
        type("> timer 25h");
        assertEquals("Use minutes or h/m/s", title(0));
        type("> share hello world");
        assertEquals("Share text", title(0));
    }

    public void testTranslucentFullscreenTheme() throws Throwable {
        type("");
        android.content.res.TypedArray attrs = getActivity().obtainStyledAttributes(new int[]{
                android.R.attr.windowIsTranslucent, android.R.attr.windowIsFloating});
        try { assertTrue(attrs.getBoolean(0, false)); assertFalse(attrs.getBoolean(1, true)); }
        finally { attrs.recycle(); }
    }

    public void testHalftoneDotsAndGapsRemainTranslucent() {
        android.graphics.Bitmap image = android.graphics.Bitmap.createBitmap(32, 32, android.graphics.Bitmap.Config.ARGB_8888);
        HalftoneDrawable drawable = new HalftoneDrawable(0xb8000f08, 0, 1, 0);
        drawable.setBounds(0, 0, 32, 32);
        drawable.draw(new android.graphics.Canvas(image));
        int dot = android.graphics.Color.alpha(image.getPixel(4, 4));
        int gap = android.graphics.Color.alpha(image.getPixel(0, 0));
        assertTrue("Halftone must alter alpha, not just colour", dot - gap > 100);
        assertTrue("Gaps reveal the previous app", gap < 150);
        assertEquals("Dot opacity follows the requested opacity", 184, dot);
        assertTrue("Dots must also reveal the previous app", dot < 255);
        image.eraseColor(0);
        HalftoneDrawable opaque = new HalftoneDrawable(0xffccffee, 0, 1, 0);
        opaque.setBounds(0, 0, 32, 32); opaque.draw(new android.graphics.Canvas(image));
        assertTrue(android.graphics.Color.alpha(image.getPixel(0, 0)) < 242);
        assertEquals(242, android.graphics.Color.alpha(image.getPixel(4, 4)));
        image.recycle();
    }

    public void testResultRowsAreReused() throws Throwable {
        type("");
        assertTrue(results().getCount() > 1);
        runTestOnUiThread(() -> {
            android.view.View first = results().getAdapter().getView(0, null, results());
            assertSame(first, results().getAdapter().getView(1, first, results()));
        });
    }

    public void testPersistentCatalogRoundTripAndLocaleInvalidation() {
        android.util.AtomicFile file = new android.util.AtomicFile(new java.io.File(getActivity().getCacheDir(), "catalog-test.bin"));
        try {
            java.util.List<AppCatalog.App> apps = java.util.List.of(new AppCatalog.App(
                new android.content.ComponentName("example.package", "example.package.Main"), "Łódź 角色"));
            AppCatalog.writeCache(file, "pl-PL", apps);
            java.util.List<AppCatalog.App> restored = AppCatalog.readCache(file, "pl-PL");
            assertNotNull(restored);
            assertEquals("Łódź 角色", restored.get(0).title);
            assertEquals(apps.get(0).component, restored.get(0).component);
            assertNull("Cache restoration must not require PackageManager metadata", restored.get(0).info);
            assertNull(AppCatalog.readCache(file, "en-US"));
        } finally { file.delete(); }
    }

    public void testFailedCatalogWritePreservesPreviousSnapshot() {
        android.util.AtomicFile file = new android.util.AtomicFile(new java.io.File(getActivity().getCacheDir(), "catalog-test.bin"));
        try {
            android.content.ComponentName component = new android.content.ComponentName("example.package", "example.package.Main");
            AppCatalog.writeCache(file, "en-US", java.util.List.of(new AppCatalog.App(component, "Saved")));
            AppCatalog.writeCache(file, "en-US", java.util.List.of(new AppCatalog.App(component, new String(new char[70000]).replace('\0', 'x'))));
            assertEquals("Saved", AppCatalog.readCache(file, "en-US").get(0).title);
        } finally { file.delete(); }
    }

    public void testTruncatedCatalogFallsBackToDiscovery() throws Exception {
        android.util.AtomicFile file = new android.util.AtomicFile(new java.io.File(getActivity().getCacheDir(), "catalog-test.bin"));
        try {
            java.io.FileOutputStream output = file.startWrite();
            output.write(new byte[]{0x4b, 0x41, 0x50, 0x31, 0});
            file.finishWrite(output);
            assertNull(AppCatalog.readCache(file, "en-US"));
        } finally { file.delete(); }
    }
}
