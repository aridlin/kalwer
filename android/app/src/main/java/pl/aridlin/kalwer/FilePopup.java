package pl.aridlin.kalwer;

import android.app.*;
import android.content.*;
import android.database.Cursor;
import android.graphics.*;
import android.net.Uri;
import android.os.*;
import android.provider.*;
import android.util.LruCache;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.lang.ref.WeakReference;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

/** Documents-provider access only; no broad storage or filesystem-path permission. */
final class FilePopup extends Dialog {
  static final int PICK_FILE = 910, PICK_FOLDER = 911;
  private final Activity activity;
  private final SharedPreferences prefs;
  private final LinearLayout body, bar, bookmarks;
  private final TextView title, message;
  private final GridView grid;
  private final EditText search;
  private final ArrayList<Item> items = new ArrayList<>(), visible = new ArrayList<>();
  private final HashMap<Uri, String> labels = new HashMap<>();
  private final ArrayDeque<Uri> parents = new ArrayDeque<>();
  private final ThreadPoolExecutor io = new ThreadPoolExecutor(2, 2, 0, TimeUnit.MILLISECONDS,
      new ArrayBlockingQueue<>(24), new ThreadPoolExecutor.DiscardOldestPolicy());
  private final LruCache<String, Bitmap> thumbs = new LruCache<>(8 * 1024 * 1024) {
    protected int sizeOf(String k, Bitmap b) {
      return b.getAllocationByteCount();
    }
  };
  private final Handler main = new Handler(Looper.getMainLooper());
  private Uri folder, selected;
  private boolean preview, disposed;
  private volatile int generation;
  private final BaseAdapter adapter = new BaseAdapter() {
    public int getCount() {
      return visible.size();
    }
    public Item getItem(int p) {
      return visible.get(p);
    }
    public long getItemId(int p) {
      return p;
    }
    public View getView(int position, View recycled, android.view.ViewGroup parent) {
      LinearLayout row =
          recycled instanceof LinearLayout ? (LinearLayout) recycled : new LinearLayout(activity);
      if (row.getChildCount() == 0) {
        row.setOrientation(LinearLayout.VERTICAL);
        row.setGravity(Gravity.CENTER);
        row.setPadding(dp(6), dp(6), dp(6), dp(6));
        ImageView image = new ImageView(activity);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        row.addView(image, new LinearLayout.LayoutParams(-1, dp(84)));
        TextView name = text("", 11);
        name.setMaxLines(2);
        name.setEllipsize(android.text.TextUtils.TruncateAt.END);
        row.addView(name, new LinearLayout.LayoutParams(-1, dp(36)));
      }
      Item item = getItem(position);
      ImageView image = (ImageView) row.getChildAt(0);
      ((TextView) row.getChildAt(1)).setText(item.name);
      image.setImageResource(
          item.directory ? android.R.drawable.ic_menu_agenda : android.R.drawable.ic_menu_save);
      image.setTag(item.uri.toString());
      if (!item.directory)
        thumbnail(item, image);
      row.setContentDescription(item.name);
      return row;
    }
  };
  static final class Item {
    final Uri uri;
    final String name, mime;
    final long modified;
    final boolean directory;
    Item(Uri u, String n, String m, long t) {
      uri = u;
      name = n;
      mime = m == null ? "application/octet-stream" : m;
      modified = t;
      directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
    }
  }
  FilePopup(Activity activity, SharedPreferences prefs, Runnable dismissed) {
    this(activity, prefs, null, dismissed);
  }
  FilePopup(Activity activity, SharedPreferences prefs, Bitmap snapshot, Runnable dismissed) {
    super(activity);
    this.activity = activity;
    this.prefs = prefs;
    getWindow().setBackgroundDrawableResource(android.R.color.transparent);
    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
    getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN
        | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    LinearLayout root = new LinearLayout(activity);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setPadding(dp(10), dp(8), dp(10), dp(8));
    android.graphics.drawable.Drawable fill =
        new HalftoneDrawable((prefs.getInt("opacity", 72) * 255 / 100 << 24) | 0x00130b,
            0xff000000 | Appearance.ACCENT[Appearance.theme],
            activity.getResources().getDisplayMetrics().density, 6, true)
            .anchor(root);
    root.setBackground(new android.graphics.drawable.Drawable() {
      final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
      final int[] at = new int[2];
      public void draw(Canvas c) {
        if (Appearance.popupMode > 0 && snapshot != null && !snapshot.isRecycled()) {
          root.getLocationOnScreen(at);
          c.save();
          c.translate(-at[0], -at[1]);
          paint.setAlpha(150);
          c.drawBitmap(snapshot, null,
              new Rect(0, 0, activity.getResources().getDisplayMetrics().widthPixels,
                  activity.getResources().getDisplayMetrics().heightPixels),
              paint);
          c.restore();
        }
        fill.setBounds(getBounds());
        fill.draw(c);
      }
      public void setAlpha(int value) {
        fill.setAlpha(value);
      }
      public void setColorFilter(ColorFilter value) {
        fill.setColorFilter(value);
      }
      public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
      }
    });
    root.setFocusableInTouchMode(true);
    root.requestFocus();
    bar = new LinearLayout(activity);
    bar.setGravity(Gravity.CENTER_VERTICAL);
    title = text("Files", 14);
    bar.addView(title, new LinearLayout.LayoutParams(0, dp(48), 1));
    bar.addView(button("↑", () -> back()), new LinearLayout.LayoutParams(dp(48), dp(48)));
    bar.addView(button("×", () -> {
      if (preview)
        showGrid();
      else
        dismiss();
    }), new LinearLayout.LayoutParams(dp(48), dp(48)));
    root.addView(bar);
    LinearLayout actions = new LinearLayout(activity);
    actions.addView(
        button("Browse", () -> pick(false)), new LinearLayout.LayoutParams(0, dp(48), 1));
    actions.addView(
        button("Bookmark folder", () -> pick(true)), new LinearLayout.LayoutParams(0, dp(48), 1));
    root.addView(actions);
    HorizontalScrollView scroll = new HorizontalScrollView(activity);
    bookmarks = new LinearLayout(activity);
    scroll.addView(bookmarks);
    root.addView(scroll);
    search = new EditText(activity);
    search.setSingleLine(true);
    search.setTextColor(color());
    search.setHintTextColor(0xff92b89f);
    search.setHint("Find a file…");
    root.addView(search, new LinearLayout.LayoutParams(-1, dp(48)));
    search.addTextChangedListener(new android.text.TextWatcher() {
      public void beforeTextChanged(CharSequence s, int a, int c, int f) {}
      public void onTextChanged(CharSequence s, int a, int b, int c) {
        filter(s.toString());
      }
      public void afterTextChanged(android.text.Editable e) {}
    });
    body = new LinearLayout(activity);
    body.setOrientation(LinearLayout.VERTICAL);
    root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
    grid = new GridView(activity);
    grid.setNumColumns(GridView.AUTO_FIT);
    grid.setColumnWidth(dp(110));
    grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);
    grid.setAdapter(adapter);
    body.addView(grid, new LinearLayout.LayoutParams(-1, -1));
    grid.setOnItemClickListener((p, v, i, id) -> {
      Item item = visible.get(i);
      if (item.directory) {
        if (folder != null)
          parents.push(folder);
        load(item.uri);
      } else
        showFile(item.uri);
    });
    grid.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
      public void onNothingSelected(android.widget.AdapterView<?> p) {}
      public void onItemSelected(android.widget.AdapterView<?> p, View v, int i, long id) {
        if (i >= 0 && i < visible.size())
          selected = visible.get(i).uri;
      }
    });
    grid.setOnItemLongClickListener((p, v, i, id) -> {
      selected = visible.get(i).uri;
      new AlertDialog.Builder(activity)
          .setItems(new String[] {"Preview", "Copy", "Share"},
              (d, which) -> {
                if (which == 0)
                  showFile(selected);
                else if (which == 1)
                  copy();
                else
                  share();
              })
          .show();
      return true;
    });
    message = text("", 12);
    root.addView(message);
    setContentView(root);
    setOnDismissListener(d -> dismissed.run());
    refreshBookmarks();
  }
  @Override
  public void dismiss() {
    if (disposed)
      return;
    disposed = true;
    generation++;
    io.shutdownNow();
    thumbs.evictAll();
    super.dismiss();
  }
  @Override
  public void show() {
    super.show();
    Window w = getWindow();
    w.setGravity(Gravity.TOP | Gravity.END);
    w.setLayout(Math.min(dp(420), activity.getResources().getDisplayMetrics().widthPixels - dp(20)),
        Math.min(dp(600), activity.getResources().getDisplayMetrics().heightPixels - dp(48)));
  }
  void open() {
    show();
    String last = prefs.getString("file_default_tree", "");
    if (!last.isEmpty())
      load(Uri.parse(last));
    else
      pick(false);
  }
  static Intent picker(boolean folder) {
    Intent i = new Intent(folder ? Intent.ACTION_OPEN_DOCUMENT_TREE : Intent.ACTION_OPEN_DOCUMENT)
                   .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                       | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
    if (!folder)
      i.setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
    i.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
        DocumentsContract.buildRootUri("com.android.providers.downloads.documents", "downloads"));
    return i;
  }
  private void pick(boolean tree) {
    try {
      activity.startActivityForResult(picker(tree), tree ? PICK_FOLDER : PICK_FILE);
    } catch (ActivityNotFoundException e) {
      message.setText("No document picker is installed.");
    }
  }
  boolean result(int request, int result, Intent data) {
    if (request != PICK_FILE && request != PICK_FOLDER)
      return false;
    if (result != Activity.RESULT_OK || data == null || data.getData() == null)
      return true;
    Uri uri = data.getData();
    try {
      if ((data.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION) != 0)
        activity.getContentResolver().takePersistableUriPermission(
            uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
    } catch (SecurityException ignored) {
    }
    if (request == PICK_FOLDER) {
      JSONArray saved = readBookmarks();
      boolean found = false;
      for (int n = 0; n < saved.length(); n++)
        if (uri.toString().equals(saved.optJSONObject(n).optString("uri")))
          found = true;
      if (!found) {
        JSONObject entry = new JSONObject();
        try {
          entry.put("uri", uri.toString());
          entry.put("name", name(uri));
          saved.put(entry);
        } catch (JSONException ignored) {
        }
      }
      prefs.edit()
          .putString("file_bookmarks", saved.toString())
          .putString("file_default_tree", uri.toString())
          .apply();
      refreshBookmarks();
      parents.clear();
      load(uri);
    } else
      showFile(uri);
    return true;
  }
  private JSONArray readBookmarks() {
    try {
      return new JSONArray(prefs.getString("file_bookmarks", "[]"));
    } catch (JSONException e) {
      return new JSONArray();
    }
  }
  private void refreshBookmarks() {
    bookmarks.removeAllViews();
    JSONArray saved = readBookmarks();
    for (int i = 0; i < saved.length(); i++) {
      JSONObject item = saved.optJSONObject(i);
      if (item == null)
        continue;
      String value = item.optString("uri");
      View b = button(item.optString("name", "Folder"), () -> {
        parents.clear();
        load(Uri.parse(value));
      });
      b.setOnLongClickListener(v -> {
        JSONArray next = new JSONArray();
        for (int k = 0; k < saved.length(); k++)
          if (!value.equals(saved.optJSONObject(k).optString("uri")))
            next.put(saved.optJSONObject(k));
        SharedPreferences.Editor edit = prefs.edit().putString("file_bookmarks", next.toString());
        if (value.equals(prefs.getString("file_default_tree", "")))
          edit.remove("file_default_tree");
        edit.apply();
        refreshBookmarks();
        return true;
      });
      bookmarks.addView(b, new LinearLayout.LayoutParams(-2, dp(44)));
    }
  }
  private void load(Uri uri) {
    folder = uri;
    selected = null;
    preview = false;
    showGrid();
    title.setText(name(uri));
    message.setText("Loading…");
    int version = ++generation;
    items.clear();
    filter("");
    search.setText("");
    io.getQueue().clear();
    io.execute(() -> {
      ArrayList<Item> found = new ArrayList<>();
      String error = null, display = null;
      try {
        String id = DocumentsContract.isTreeUri(uri) ? DocumentsContract.getTreeDocumentId(uri)
                                                     : DocumentsContract.getDocumentId(uri);
        if (DocumentsContract.isDocumentUri(activity, uri))
          id = DocumentsContract.getDocumentId(uri);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(uri, id);
        try (Cursor c = activity.getContentResolver().query(children,
                 new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                     DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                     DocumentsContract.Document.COLUMN_MIME_TYPE,
                     DocumentsContract.Document.COLUMN_LAST_MODIFIED},
                 null, null, null)) {
          if (c != null)
            while (c.moveToNext()) {
              if (version != generation)
                return;
              found.add(new Item(DocumentsContract.buildDocumentUriUsingTree(uri, c.getString(0)),
                  c.getString(1), c.getString(2), c.getLong(3)));
            }
        }
        try (Cursor label = activity.getContentResolver().query(
                 DocumentsContract.buildDocumentUriUsingTree(uri, id),
                 new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
          if (label != null && label.moveToFirst())
            display = label.getString(0);
        } catch (Exception ignored) {
        }
        found.sort(Comparator.comparingLong((Item i) -> i.modified)
                .reversed()
                .thenComparing(i -> i.name, String.CASE_INSENSITIVE_ORDER));
      } catch (Exception e) {
        error = "Cannot read this folder. Select it again with Bookmark folder.";
      }
      String failure = error, folderName = display;
      main.post(() -> {
        if (!isShowing() || generation != version)
          return;
        if (folderName != null) {
          labels.put(uri, folderName);
          if (!preview)
            title.setText(folderName);
          JSONArray saved = readBookmarks();
          for (int i = 0; i < saved.length(); i++) {
            JSONObject entry = saved.optJSONObject(i);
            if (entry != null && uri.toString().equals(entry.optString("uri")))
              try {
                entry.put("name", folderName);
              } catch (JSONException ignored) {
              }
          }
          prefs.edit().putString("file_bookmarks", saved.toString()).apply();
          refreshBookmarks();
        }
        items.addAll(found);
        for (Item item : found) labels.put(item.uri, item.name);
        filter(search.getText().toString());
        message.setText(failure != null ? failure : found.isEmpty() ? "Folder is empty." : "");
      });
    });
  }
  private void filter(String value) {
    String needle = value.toLowerCase(Locale.ROOT);
    visible.clear();
    for (Item i : items)
      if (i.name.toLowerCase(Locale.ROOT).contains(needle))
        visible.add(i);
    adapter.notifyDataSetChanged();
  }
  private void thumbnail(Item item, ImageView view) {
    String key = item.uri + "@" + item.modified;
    Bitmap cached = thumbs.get(key);
    if (cached != null) {
      view.setImageBitmap(cached);
      return;
    }
    WeakReference<ImageView> ref = new WeakReference<>(view);
    int version = generation;
    io.execute(() -> {
      if (version != generation)
        return;
      Bitmap b = null;
      try {
        b = DocumentsContract.getDocumentThumbnail(
            activity.getContentResolver(), item.uri, new Point(dp(96), dp(96)), null);
      } catch (Exception ignored) {
      }
      if (b == null && item.mime.startsWith("image/"))
        try {
          b = decode(item.uri, 256);
        } catch (Exception ignored) {
        }
      Bitmap image = b;
      main.post(() -> {
        if (image == null || !isShowing() || version != generation)
          return;
        thumbs.put(key, image);
        ImageView target = ref.get();
        if (target != null && item.uri.toString().equals(target.getTag()))
          target.setImageBitmap(image);
      });
    });
  }
  private Bitmap decode(Uri uri, int limit) throws IOException {
    BitmapFactory.Options options = new BitmapFactory.Options();
    options.inJustDecodeBounds = true;
    try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
      BitmapFactory.decodeStream(in, null, options);
    }
    options.inSampleSize = 1;
    while (Math.max(options.outWidth, options.outHeight) / options.inSampleSize > limit)
      options.inSampleSize *= 2;
    options.inJustDecodeBounds = false;
    try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
      return BitmapFactory.decodeStream(in, null, options);
    }
  }
  void showFile(Uri uri) {
    search.clearFocus();
    body.setFocusableInTouchMode(true);
    body.requestFocus();
    android.view.inputmethod.InputMethodManager keyboard =
        (android.view.inputmethod.InputMethodManager) activity.getSystemService(
            Context.INPUT_METHOD_SERVICE);
    keyboard.hideSoftInputFromWindow(activity.getWindow().getDecorView().getWindowToken(), 0);
    keyboard.hideSoftInputFromWindow(getWindow().getDecorView().getWindowToken(), 0);
    ((android.view.inputmethod.InputMethodManager) activity.getSystemService(
         Context.INPUT_METHOD_SERVICE))
        .hideSoftInputFromWindow(search.getWindowToken(), 0);
    selected = uri;
    preview = true;
    search.setVisibility(View.GONE);
    body.removeAllViews();
    title.setText(name(uri));
    message.setText("");
    io.execute(() -> {
      String display = null;
      try (Cursor c = activity.getContentResolver().query(
               uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
        if (c != null && c.moveToFirst())
          display = c.getString(0);
      } catch (Exception ignored) {
      }
      String value = display;
      if (value != null)
        main.post(() -> {
          if (isShowing() && uri.equals(selected)) {
            labels.put(uri, value);
            title.setText(value);
          }
        });
    });
    LinearLayout actions = new LinearLayout(activity);
    actions.addView(button("Copy", this::copy), new LinearLayout.LayoutParams(0, dp(48), 1));
    actions.addView(button("Share", this::share), new LinearLayout.LayoutParams(0, dp(48), 1));
    actions.addView(
        button("Open", () -> openExternal(uri)), new LinearLayout.LayoutParams(0, dp(48), 1));
    body.addView(actions);
    String type;
    try {
      type = activity.getContentResolver().getType(uri);
    } catch (SecurityException e) {
      message.setText("Access expired. Select this file again with Browse.");
      return;
    }
    if (type != null && type.startsWith("image/")) {
      ImageView image = new ImageView(activity);
      image.setScaleType(ImageView.ScaleType.FIT_CENTER);
      body.addView(image, new LinearLayout.LayoutParams(-1, 0, 1));
      int version = generation;
      io.execute(() -> {
        Bitmap b = null;
        try {
          b = decode(uri, 1280);
        } catch (Exception ignored) {
        }
        Bitmap decoded = b;
        main.post(() -> {
          if (isShowing() && version == generation && uri.equals(selected)) {
            if (decoded != null)
              image.setImageBitmap(decoded);
            else
              message.setText("Preview unavailable; use Open or Share.");
          }
        });
      });
      image.setOnLongClickListener(v -> {
        v.startDragAndDrop(ClipData.newUri(activity.getContentResolver(), name(uri), uri),
            new View.DragShadowBuilder(v), null,
            View.DRAG_FLAG_GLOBAL | View.DRAG_FLAG_GLOBAL_URI_READ);
        return true;
      });
    } else if (type != null && (type.startsWith("text/") || type.equals("application/json"))) {
      ScrollView scroll = new ScrollView(activity);
      TextView text = text("", 13);
      text.setTextIsSelectable(true);
      scroll.addView(text);
      body.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
      int version = generation;
      io.execute(() -> {
        String value;
        try (InputStream in = activity.getContentResolver().openInputStream(uri)) {
          ByteArrayOutputStream out = new ByteArrayOutputStream();
          byte[] bytes = new byte[4096];
          while (out.size() < 65536) {
            int n = in.read(bytes, 0, Math.min(bytes.length, 65536 - out.size()));
            if (n < 0)
              break;
            out.write(bytes, 0, n);
          }
          value = out.toString("UTF-8");
        } catch (Exception e) {
          value = "Preview unavailable; use Open or Share.";
        }
        String content = value;
        main.post(() -> {
          if (isShowing() && version == generation && uri.equals(selected))
            text.setText(content);
        });
      });
    } else {
      ImageView icon = new ImageView(activity);
      icon.setImageResource(android.R.drawable.ic_menu_save);
      body.addView(icon, new LinearLayout.LayoutParams(-1, 0, 1));
    }
  }
  private void showGrid() {
    preview = false;
    search.setVisibility(View.VISIBLE);
    body.removeAllViews();
    body.addView(grid, new LinearLayout.LayoutParams(-1, -1));
    title.setText(folder == null ? "Files" : name(folder));
  }
  private void back() {
    if (preview)
      showGrid();
    else if (!parents.isEmpty())
      load(parents.pop());
    else
      dismiss();
  }
  private String name(Uri uri) {
    String known = labels.get(uri);
    if (known != null)
      return known;
    String leaf = uri.getLastPathSegment();
    if (leaf == null)
      return "File";
    int index = Math.max(leaf.lastIndexOf('/'), leaf.lastIndexOf(':'));
    return leaf.substring(index + 1);
  }
  void copy() {
    if (selected == null)
      return;
    try {
      ((ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE))
          .setPrimaryClip(ClipData.newUri(activity.getContentResolver(), name(selected), selected));
    } catch (SecurityException e) {
      message.setText("Access expired. Select this file again with Browse.");
    }
  }
  static Intent shareIntent(Context c, Uri uri) {
    String type = c.getContentResolver().getType(uri);
    Intent intent = new Intent(Intent.ACTION_SEND)
                        .setType(type == null ? "application/octet-stream" : type)
                        .putExtra(Intent.EXTRA_STREAM, uri)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    intent.setClipData(ClipData.newUri(c.getContentResolver(), "File", uri));
    return intent;
  }
  private void share() {
    if (selected == null)
      return;
    try {
      activity.startActivity(Intent.createChooser(shareIntent(activity, selected), "Share file"));
    } catch (ActivityNotFoundException | SecurityException e) {
      message.setText("No app can share this file.");
    }
  }
  private void openExternal(Uri uri) {
    try {
      String type = activity.getContentResolver().getType(uri);
      activity.startActivity(new Intent(Intent.ACTION_VIEW)
              .setDataAndType(uri, type == null ? "*/*" : type)
              .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION));
    } catch (ActivityNotFoundException | SecurityException e) {
      message.setText("No app can open this file.");
    }
  }
  @Override
  public void onBackPressed() {
    back();
  }
  @Override
  public boolean onKeyDown(int key, KeyEvent event) {
    if (key == KeyEvent.KEYCODE_ESCAPE || key == KeyEvent.KEYCODE_BACK) {
      back();
      return true;
    }
    if (key == KeyEvent.KEYCODE_C && event.isCtrlPressed()) {
      copy();
      return true;
    }
    return super.onKeyDown(key, event);
  }
  private int dp(float n) {
    return Math.round(n * activity.getResources().getDisplayMetrics().density);
  }
  private int color() {
    return 0xff000000 | Appearance.TEXT[Appearance.theme];
  }
  private TextView text(String value, int size) {
    TextView v = new TextView(activity);
    v.setText(value);
    v.setTextColor(color());
    v.setTextSize(size);
    v.setTypeface(Typeface.MONOSPACE);
    v.setGravity(Gravity.CENTER_VERTICAL);
    return v;
  }
  private View button(String value, Runnable action) {
    TextView v = text(value, 12);
    v.setGravity(Gravity.CENTER);
    v.setPadding(dp(8), 0, dp(8), 0);
    v.setBackground(
        new HalftoneDrawable(0xb0002e18, 0xff000000 | Appearance.ACCENT[Appearance.theme],
            activity.getResources().getDisplayMetrics().density, 4, true)
            .anchor(v));
    v.setOnClickListener(w -> {
      v.animate().alpha(.65f).setDuration(60).withEndAction(
          () -> v.animate().alpha(1).setDuration(120));
      action.run();
    });
    return v;
  }
}
