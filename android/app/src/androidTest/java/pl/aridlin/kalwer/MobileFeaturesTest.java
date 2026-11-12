package pl.aridlin.kalwer;
import android.app.*;
import android.content.*;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.test.ActivityInstrumentationTestCase2;
import android.view.*;
import android.widget.*;
import java.util.concurrent.*;
@SuppressWarnings("deprecation")
public final class MobileFeaturesTest
    extends ActivityInstrumentationTestCase2<MainActivity> {
  public MobileFeaturesTest() { super(MainActivity.class); }
  private <T extends View> T find(View root, Class<T> type) {
    if (type.isInstance(root))
      return type.cast(root);
    if (root instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup)root).getChildCount(); i++) {
        T hit = find(((ViewGroup)root).getChildAt(i), type);
        if (hit != null)
          return hit;
      }
    return null;
  }
  private void waitFor(java.util.function.BooleanSupplier ready)
      throws Exception {
    long end = System.currentTimeMillis() + 10000;
    while (!ready.getAsBoolean() && System.currentTimeMillis() < end) {
      Thread.sleep(50);
      getInstrumentation().waitForIdleSync();
    }
    assertTrue(ready.getAsBoolean());
  }
  public void testIncomingShareChooserAndReview() throws Exception {
    MainActivity activity = getActivity();
    getInstrumentation().runOnMainSync(
        ()
            -> activity.onNewIntent(
                new Intent(Intent.ACTION_SEND)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_TEXT,
                              "Kalwer shared text fixture")));
    android.app.UiAutomation ui = getInstrumentation().getUiAutomation();
    waitFor(() -> {
      android.view.accessibility.AccessibilityNodeInfo root =
          ui.getRootInActiveWindow();
      return root != null &&
          !root.findAccessibilityNodeInfosByText("Binup").isEmpty();
    });
    android.view.accessibility.AccessibilityNodeInfo root =
        ui.getRootInActiveWindow();
    android.view.accessibility.AccessibilityNodeInfo choice =
        root.findAccessibilityNodeInfosByText("Binup").get(0);
    assertTrue(choice.performAction(
        android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK));
    waitFor(() -> {
      android.view.accessibility.AccessibilityNodeInfo active =
          ui.getRootInActiveWindow();
      return active != null &&
          !active.findAccessibilityNodeInfosByText("text.txt").isEmpty();
    });
    android.graphics.Bitmap shot = ui.takeScreenshot();
    try (java.io.OutputStream out = new java.io.FileOutputStream(
             new java.io.File(activity.getExternalFilesDir(null),
                              "mobile-share-review.png"))) {
      shot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
    }
    getInstrumentation().runOnMainSync(() -> activity.finish());
  }
  public void testServiceKeyAndSharedTextReview() throws Exception {
    MainActivity activity = getActivity();
    String key =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    ServiceApi.saveKey(activity, key);
    assertEquals(key, ServiceApi.key(activity));
    assertFalse(activity.getSharedPreferences("kalwer", 0)
                    .getString("memes_key", "")
                    .contains(key));
    ServicePopup[] popup = new ServicePopup[1];
    getInstrumentation().runOnMainSync(() -> {
      popup[0] =
          new ServicePopup(activity, activity.getSharedPreferences("kalwer", 0),
                           false, null, () -> {});
      popup[0].show();
      popup[0].incoming(new Intent(Intent.ACTION_SEND)
                            .setType("text/plain")
                            .putExtra(Intent.EXTRA_TEXT, "shared fixture"));
    });
    assertTrue(popup[0].preview);
    assertEquals(1, popup[0].pending.size());
    assertFalse(popup[0].busy);
    Uri uri = popup[0].pending.get(0).uri;
    assertEquals("content", uri.getScheme());
    try (java.io.InputStream in =
             activity.getContentResolver().openInputStream(uri)) {
      byte[] bytes = new byte[64];
      int n = in.read(bytes);
      assertEquals(
          "shared fixture",
          new String(bytes, 0, n, java.nio.charset.StandardCharsets.UTF_8));
    }
    try {
      activity.getContentResolver().openFileDescriptor(uri, "w");
      fail("Shared file must be read only");
    } catch (java.io.FileNotFoundException expected) {
    }
    getInstrumentation().runOnMainSync(() -> popup[0].dismiss());
    assertTrue(popup[0].disposed);
  }
  public void testServiceMediaBoundariesAndShareTarget() throws Exception {
    assertEquals("https://memes.aridlin.pl/storage/image/test.png",
                 ServiceApi.media("storage/image/test.png", true));
    for (String url : new String[] {"https://bin.aridlin.pl.evil/file/test",
                                    "http://bin.aridlin.pl/file/test",
                                    "https://bin.aridlin.pl:443/file/test",
                                    "https://evil.test/file/test"}) {
      try {
        ServiceApi.media(url, false);
        fail("Invalid origin accepted");
      } catch (java.io.IOException expected) {
      }
    }
    Intent share = new Intent(Intent.ACTION_SEND).setType("image/png");
    boolean found = false;
    for (android.content.pm.ResolveInfo info :
         getActivity().getPackageManager().queryIntentActivities(share, 0))
      if (info.activityInfo.packageName.equals(getActivity().getPackageName()))
        found = true;
    assertTrue("Kalwer must be a share target", found);
  }
  public void testUnicodeAndSharedEmoticons() throws Exception {
    CountDownLatch loaded = new CountDownLatch(1);
    TextTools.load(getActivity(), loaded::countDown);
    assertTrue(loaded.await(10, TimeUnit.SECONDS));
    assertEquals("\u200b", TextTools.search("+200b").get(0).value);
    assertTrue(TextTools.search("+d800").isEmpty());
    assertFalse(TextTools.search("+heart").isEmpty());
    assertFalse(TextTools.search("/wemote shrug").isEmpty());
  }
  public void testSystemPickerAndReadOnlySharing() {
    Intent file = FilePopup.picker(false), folder = FilePopup.picker(true);
    assertEquals(Intent.ACTION_OPEN_DOCUMENT, file.getAction());
    assertTrue(file.hasCategory(Intent.CATEGORY_OPENABLE));
    assertEquals(Intent.ACTION_OPEN_DOCUMENT_TREE, folder.getAction());
    assertTrue((file.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0);
    assertTrue(file.hasExtra(DocumentsContract.EXTRA_INITIAL_URI));
    Uri uri = Uri.parse(
        "content://pl.aridlin.kalwer.test.documents/document/new.txt");
    Intent share = FilePopup.shareIntent(getActivity(), uri);
    assertEquals(Intent.ACTION_SEND, share.getAction());
    assertEquals("text/plain", share.getType());
    assertEquals(uri, share.getParcelableExtra(Intent.EXTRA_STREAM));
    assertEquals(uri, share.getClipData().getItemAt(0).getUri());
    assertTrue((share.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0);
    assertEquals(0, share.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
  }
  public void testFolderBookmarkDateOrderPreviewAndCopy() throws Throwable {
    MainActivity activity = getActivity();
    var prefs = activity.getSharedPreferences("file-fixture-test", 0);
    prefs.edit().clear().commit();
    final FilePopup[] dialog = {null};
    Uri tree = DocumentsContract.buildTreeDocumentUri(
        "pl.aridlin.kalwer.test.documents", "root");
    runTestOnUiThread(() -> {
      dialog[0] = new FilePopup(activity, prefs, () -> {});
      dialog[0].show();
      dialog[0].result(FilePopup.PICK_FOLDER, Activity.RESULT_OK,
                       new Intent().setData(tree));
    });
    GridView grid = find(dialog[0].getWindow().getDecorView(), GridView.class);
    waitFor(() -> grid.getCount() == 3);
    assertEquals("new.txt",
                 ((FilePopup.Item)grid.getAdapter().getItem(0)).name);
    assertTrue(
        prefs.getString("file_bookmarks", "").contains("test.documents"));
    waitFor(() -> dialog[0].getWindow().getDecorView().hasWindowFocus());
    getInstrumentation().waitForIdleSync();
    android.graphics.Bitmap image =
        getInstrumentation().getUiAutomation().takeScreenshot();
    if (image != null) {
      try (java.io.FileOutputStream out =
               new java.io.FileOutputStream(new java.io.File(
                   activity.getExternalFilesDir(null), "mobile-files.png"))) {
        image.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
      }
      image.recycle();
    }
    Uri file = DocumentsContract.buildDocumentUriUsingTree(tree, "new.txt");
    runTestOnUiThread(() -> {
      dialog[0].showFile(file);
      dialog[0].copy();
    });
    getInstrumentation().waitForIdleSync();
    ClipboardManager clipboard =
        (ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
    assertEquals(file, clipboard.getPrimaryClip().getItemAt(0).getUri());
    runTestOnUiThread(() -> dialog[0].onBackPressed());
    assertNotNull(find(dialog[0].getWindow().getDecorView(), GridView.class));
    runTestOnUiThread(() -> dialog[0].dismiss());
    prefs.edit().clear().commit();
  }
  private boolean hasText(View root, String value) {
    if (root instanceof TextView &&
        ((TextView)root).getText().toString().contains(value))
      return true;
    if (root instanceof ViewGroup)
      for (int i = 0; i < ((ViewGroup)root).getChildCount(); i++)
        if (hasText(((ViewGroup)root).getChildAt(i), value))
          return true;
    return false;
  }
  public void testFileCommandUsesPickerAndPreview() throws Throwable {
    MainActivity activity = getActivity();
    Uri file = Uri.parse(
        "content://pl.aridlin.kalwer.test.documents/document/new.txt");
    android.app.Instrumentation.ActivityMonitor monitor =
        new android.app.Instrumentation.ActivityMonitor() {
          @Override
          public android.app.Instrumentation.ActivityResult onStartActivity(
              Intent intent) {
            if (Intent.ACTION_OPEN_DOCUMENT.equals(intent.getAction()))
              return new android.app.Instrumentation.ActivityResult(
                  Activity.RESULT_OK, new Intent().setData(file));
            return null;
          }
        };
    getInstrumentation().addMonitor(monitor);
    java.lang.reflect.Field field =
        MainActivity.class.getDeclaredField("filePopup");
    field.setAccessible(true);
    try {
      runTestOnUiThread(() -> {
        EditText query = activity.findViewById(R.id.search_query);
        query.setText("/file");
        query.onEditorAction(android.view.inputmethod.EditorInfo.IME_ACTION_GO);
      });
      waitFor(() -> {
        try {
          return field.get(activity) != null;
        } catch (Exception e) {
          return false;
        }
      });
      FilePopup popup = (FilePopup)field.get(activity);
      waitFor(()
                  -> popup.getWindow().getDecorView().hasWindowFocus() &&
                         hasText(popup.getWindow().getDecorView(),
                                 "mobile fixture"));
      if (android.os.Build.VERSION.SDK_INT >= 30)
        waitFor(() -> {
          android.view.WindowInsets insets =
              popup.getWindow().getDecorView().getRootWindowInsets();
          return insets != null &&
              !insets.isVisible(android.view.WindowInsets.Type.ime());
        });
      android.graphics.Bitmap image =
          getInstrumentation().getUiAutomation().takeScreenshot();
      if (image != null) {
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(
                 new java.io.File(activity.getExternalFilesDir(null),
                                  "mobile-file-preview.png"))) {
          image.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
        }
        image.recycle();
      }
      runTestOnUiThread(popup::dismiss);
    } finally {
      getInstrumentation().removeMonitor(monitor);
    }
  }
  public void testAppGroupRoutingAndFiltering() throws Throwable {
    MainActivity activity = getActivity();
    EditText query = activity.findViewById(R.id.search_query);
    ListView rows = activity.findViewById(R.id.search_results);
    runTestOnUiThread(() -> query.setText(""));
    waitFor(() -> rows.getCount() > 0);
    var prefs = activity.getSharedPreferences("kalwer", 0);
    String before = prefs.getString("app_groups", "{}");
    java.util.concurrent.atomic
        .AtomicReference<java.util.List<AppCatalog.App>> catalog =
        new java.util.concurrent.atomic.AtomicReference<>();
    CountDownLatch loaded = new CountDownLatch(1);
    AppCatalog.Callback callback = apps -> {
      catalog.set(apps);
      loaded.countDown();
    };
    runTestOnUiThread(() -> AppCatalog.get(activity).load(callback));
    assertTrue(loaded.await(10, TimeUnit.SECONDS));
    String component = catalog.get().get(0).id;
    org.json.JSONObject groups = new org.json.JSONObject();
    groups.put("work", new org.json.JSONArray().put(component));
    prefs.edit().putString("app_groups", groups.toString()).commit();
    try {
      runTestOnUiThread(() -> query.setText("&work"));
      getInstrumentation().waitForIdleSync();
      assertTrue(rows.getCount() >= 2);
      runTestOnUiThread(() -> query.setText("&work no-such-title"));
      getInstrumentation().waitForIdleSync();
      assertEquals(1, rows.getCount());
    } finally {
      prefs.edit().putString("app_groups", before).commit();
    }
  }
}
