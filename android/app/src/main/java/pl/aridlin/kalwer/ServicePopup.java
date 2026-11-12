package pl.aridlin.kalwer;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.net.Uri;
import android.os.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;

final class ServicePopup extends Dialog {
  static final int PICK = 912, KEY = 913;
  final Activity activity;
  final boolean memes;
  final android.content.SharedPreferences prefs;
  final LinearLayout root, body, actions;
  final TextView status;
  final EditText search;
  final GridView grid;
  final Spinner category;
  final ArrayList<JSONObject> items = new ArrayList<>();
  final ArrayList<ServiceApi.Local> pending = new ArrayList<>();
  final ExecutorService io = Executors.newSingleThreadExecutor();
  final ThreadPoolExecutor images = new ThreadPoolExecutor(
      2, 2, 0, TimeUnit.SECONDS, new ArrayBlockingQueue<>(24),
      new ThreadPoolExecutor.DiscardOldestPolicy());
  final android.util.LruCache<String, Bitmap> cache =
      new android.util.LruCache<>(8 * 1024 * 1024) {
        protected int sizeOf(String k, Bitmap b) {
          return b.getAllocationByteCount();
        }
      };
  final Handler main = new Handler(Looper.getMainLooper());
  final ServiceApi api = new ServiceApi();
  final BaseAdapter adapter = new BaseAdapter() {
    public int getCount() { return items.size(); }
    public Object getItem(int p) { return items.get(p); }
    public long getItemId(int p) { return p; }
    public View getView(int p, View old, ViewGroup parent) {
      LinearLayout row = old instanceof LinearLayout
                             ? (LinearLayout)old
                             : new LinearLayout(activity);
      if (row.getChildCount() == 0) {
        row.setOrientation(LinearLayout.VERTICAL);
        ImageView image = new ImageView(activity);
        image.setScaleType(ImageView.ScaleType.FIT_CENTER);
        row.addView(image, new LinearLayout.LayoutParams(-1, dp(88)));
        TextView label = text("", 11);
        label.setMaxLines(2);
        row.addView(label, new LinearLayout.LayoutParams(-1, dp(38)));
      }
      JSONObject item = items.get(p);
      ImageView image = (ImageView)row.getChildAt(0);
      ((TextView)row.getChildAt(1)).setText(item.optString("name"));
      String thumb = item.optString("thumbnail");
      if (!memes && item.optString("mime").startsWith("image/") &&
          item.optLong("size") < 8 * 1024 * 1024)
        thumb = item.optString("url");
      image.setTag(thumb);
      image.setImageResource(android.R.drawable.ic_menu_gallery);
      if (!thumb.isEmpty()) {
        Bitmap bitmap = cache.get(thumb);
        if (bitmap != null)
          image.setImageBitmap(bitmap);
        else
          thumbnail(thumb, image);
      }
      return row;
    }
  };
  boolean disposed, busy, preview, settingCategories;
  int generation, next = 0;
  VideoView video;
  android.graphics.drawable.AnimatedImageDrawable animation;
  LinearLayout form;
  final Runnable debounced = () -> load(false);
  ServicePopup(Activity a, android.content.SharedPreferences p, boolean meme,
               Bitmap snapshot, Runnable closed) {
    super(a);
    activity = a;
    prefs = p;
    memes = meme;
    getWindow().setBackgroundDrawableResource(android.R.color.transparent);
    getWindow().clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
    getWindow().setSoftInputMode(
        WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN |
        WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    root = new LinearLayout(a);
    root.setOrientation(LinearLayout.VERTICAL);
    root.setPadding(dp(10), dp(8), dp(10), dp(8));
    android.graphics.drawable.Drawable fill =
        new HalftoneDrawable(
            (prefs.getInt("opacity", 72) * 255 / 100 << 24) | 0x00130b,
            0xff000000 | Appearance.ACCENT[Appearance.theme],
            activity.getResources().getDisplayMetrics().density, 6, true)
            .anchor(root);
    root.setBackground(new android.graphics.drawable.Drawable() {
      final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
      final int[] at = new int[2];
      public void draw(Canvas c) {
        if (Appearance.popupMode > 0 && snapshot != null &&
            !snapshot.isRecycled()) {
          root.getLocationOnScreen(at);
          c.save();
          c.translate(-at[0], -at[1]);
          paint.setAlpha(150);
          c.drawBitmap(
              snapshot, null,
              new Rect(
                  0, 0, activity.getResources().getDisplayMetrics().widthPixels,
                  activity.getResources().getDisplayMetrics().heightPixels),
              paint);
          c.restore();
        }
        fill.setBounds(getBounds());
        fill.draw(c);
      }
      public void setAlpha(int value) { fill.setAlpha(value); }
      public void setColorFilter(ColorFilter value) {
        fill.setColorFilter(value);
      }
      public int getOpacity() { return PixelFormat.TRANSLUCENT; }
    });
    LinearLayout bar = new LinearLayout(a);
    TextView title = text(memes ? "Memes" : "Binup", 16);
    bar.addView(title, new LinearLayout.LayoutParams(0, dp(48), 1));
    bar.addView(button("↻", () -> load(false)));
    bar.addView(button("×", () -> {
      if (preview)
        gallery();
      else
        dismiss();
    }));
    root.addView(bar);
    actions = new LinearLayout(a);
    actions.addView(button("Upload", this::pick));
    actions.addView(button("Text", this::textForm));
    if (memes)
      actions.addView(button("Key", this::keyForm));
    root.addView(actions);
    search = new EditText(a);
    search.setSingleLine(true);
    search.setTextColor(color());
    search.setHintTextColor(0xff92b89f);
    search.setHint("Search…");
    root.addView(search, new LinearLayout.LayoutParams(-1, dp(48)));
    search.addTextChangedListener(new android.text.TextWatcher() {
      public void beforeTextChanged(CharSequence s, int x, int y, int z) {}
      public void onTextChanged(CharSequence s, int x, int y, int z) {
        main.removeCallbacks(debounced);
        main.postDelayed(debounced, 300);
      }
      public void afterTextChanged(android.text.Editable e) {}
    });
    category = new Spinner(a);
    category.setAdapter(new ArrayAdapter<String>(
        a, android.R.layout.simple_spinner_dropdown_item,
        new String[] {"all"}));
    if (memes)
      root.addView(category);
    category.setOnItemSelectedListener(
        new AdapterView.OnItemSelectedListener() {
          public void onNothingSelected(AdapterView<?> p) {}
          public void onItemSelected(AdapterView<?> p, View v, int i, long id) {
            if (!settingCategories)
              load(false);
          }
        });
    body = new LinearLayout(a);
    body.setOrientation(LinearLayout.VERTICAL);
    root.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
    grid = new GridView(a);
    grid.setNumColumns(GridView.AUTO_FIT);
    grid.setColumnWidth(dp(110));
    grid.setStretchMode(GridView.STRETCH_COLUMN_WIDTH);
    grid.setAdapter(adapter);
    grid.setOnItemClickListener((p0, v, i, id) -> open(items.get(i)));
    grid.setOnScrollListener(new AbsListView.OnScrollListener() {
      public void onScrollStateChanged(AbsListView v, int state) {}
      public void onScroll(AbsListView v, int first, int count, int total) {
        if (total > 0 && first + count >= total - 3 && next >= 0 && !busy &&
            memes)
          load(true);
      }
    });
    body.addView(grid, new LinearLayout.LayoutParams(-1, -1));
    status = text("", 12);
    root.addView(status);
    setContentView(root);
    setOnDismissListener(d -> closed.run());
    setOnKeyListener((d, key, event) -> {
      if (key == KeyEvent.KEYCODE_BACK && preview &&
          event.getAction() == KeyEvent.ACTION_UP) {
        gallery();
        return true;
      }
      return false;
    });
  }
  int dp(int n) {
    return Math.round(n * activity.getResources().getDisplayMetrics().density);
  }
  int color() { return 0xff000000 | Appearance.ACCENT[Appearance.theme]; }
  TextView text(String s, int size) {
    TextView v = new TextView(activity);
    v.setText(s);
    v.setTextSize(size);
    v.setTextColor(color());
    v.setTypeface(Typeface.MONOSPACE);
    return v;
  }
  Button button(String s, Runnable run) {
    Button b = new Button(activity);
    b.setText(s);
    b.setTextColor(color());
    b.setAllCaps(false);
    android.graphics.drawable.GradientDrawable fill =
        new android.graphics.drawable.GradientDrawable();
    fill.setColor(0xcc081a18);
    fill.setCornerRadius(dp(6));
    fill.setStroke(dp(1), 0x668ce9b3);
    b.setBackground(new android.graphics.drawable.RippleDrawable(
        android.content.res.ColorStateList.valueOf(0x448ce9b3), fill, null));
    b.setPadding(dp(12), dp(8), dp(12), dp(8));
    b.setMinimumHeight(dp(44));

    b.setMinWidth(0);
    b.setMinimumWidth(0);
    b.setOnClickListener(v -> {
      b.animate().alpha(.65f).setDuration(70).withEndAction(
          () -> b.animate().alpha(1).setDuration(120));
      run.run();
    });
    return b;
  }
  @Override
  public void show() {
    super.show();
    getWindow().setGravity(Gravity.TOP | Gravity.END);
    getWindow().setLayout(
        Math.min(dp(440),
                 activity.getResources().getDisplayMetrics().widthPixels -
                     dp(20)),
        Math.min(dp(620),
                 activity.getResources().getDisplayMetrics().heightPixels -
                     dp(48)));
    root.setFocusableInTouchMode(true);
    root.requestFocus();
  }
  void start() {
    show();
    load(false);
  }
  interface Job {
    Object run() throws Exception;
  }
  interface Done {
    void run(Object value) throws Exception;
  }
  void task(Job job, Done done) {
    if (busy || disposed)
      return;
    busy = true;
    status.setText("Loading…");
    io.execute(() -> {
      Object result = null;
      Exception error = null;
      try {
        result = job.run();
      } catch (Exception e) {
        error = e;
      }
      Object value = result;
      Exception failure = error;
      main.post(() -> {
        if (disposed)
          return;
        busy = false;
        if (failure != null) {
          status.setText(failure.getMessage() == null
                             ? "Request failed. Try again."
                             : failure.getMessage());
          return;
        }
        try {
          status.setText("");
          done.run(value);
        } catch (Exception e) {
          status.setText("Could not display this item.");
        }
      });
    });
  }
  void load(boolean more) {
    if (busy || disposed || preview)
      return;
    String q = search.getText().toString(),
           cat = String.valueOf(category.getSelectedItem());
    int offset = more ? next : 0;
    if (!more) {
      generation++;
      items.clear();
      adapter.notifyDataSetChanged();
      next = -1;
    }
    task(() -> memes ? api.memes(q, cat, offset) : api.bin(), value -> {
      JSONArray rows;
      if (memes) {
        JSONObject page = (JSONObject)value;
        rows = page.getJSONArray("items");
        next = page.isNull("next") ? -1 : page.getInt("next");
        if (category.getAdapter().getCount() == 1) {
          settingCategories = true;
          ArrayList<String> cats = new ArrayList<>();
          cats.add("all");
          JSONArray cs = page.getJSONArray("categories");
          for (int i = 0; i < cs.length(); i++)
            cats.add(cs.getString(i));
          category.setAdapter(new ArrayAdapter<>(
              activity, android.R.layout.simple_spinner_dropdown_item, cats));
          category.post(() -> settingCategories = false);
        }
      } else {
        rows = (JSONArray)value;
        next = -1;
      }
      for (int i = 0; i < rows.length(); i++) {
        JSONObject row = rows.getJSONObject(i);
        if (memes || row.optString("name")
                         .toLowerCase(Locale.ROOT)
                         .contains(q.toLowerCase(Locale.ROOT)))
          items.add(row);
      }
      adapter.notifyDataSetChanged();
      status.setText(items.isEmpty() ? "No files." : "");
      if (!q.equals(search.getText().toString()) ||
          !cat.equals(String.valueOf(category.getSelectedItem())))
        main.post(() -> load(false));
    });
  }
  void thumbnail(String value, ImageView image) {
    int token = generation;
    images.execute(() -> {
      try {
        ServiceApi client = new ServiceApi();
        File file = client.download(activity, ServiceApi.media(value, memes),
                                    "thumbnail.jpg", 8 * 1024 * 1024);
        BitmapFactory.Options size = new BitmapFactory.Options();
        size.inJustDecodeBounds = true;
        BitmapFactory.decodeFile(file.toString(), size);
        size.inSampleSize = 1;
        while (size.outWidth / size.inSampleSize > 320 ||
               size.outHeight / size.inSampleSize > 320)
          size.inSampleSize *= 2;
        size.inJustDecodeBounds = false;
        Bitmap bitmap = BitmapFactory.decodeFile(file.toString(), size);
        if (bitmap == null)
          return;
        cache.put(value, bitmap);
        main.post(() -> {
          if (!disposed && token == generation && value.equals(image.getTag()))
            image.setImageBitmap(bitmap);
        });
      } catch (Exception ignored) {
      }
    });
  }
  void stopVideo() {
    if (android.os.Build.VERSION.SDK_INT >= 28 && animation != null) {
      animation.stop();
      animation = null;
    }
    if (video != null) {
      video.stopPlayback();
      video = null;
    }
  }
  void gallery() {
    stopVideo();
    preview = false;
    form = null;
    body.removeAllViews();
    body.addView(grid, new LinearLayout.LayoutParams(-1, -1));
    actions.setVisibility(View.VISIBLE);
    search.setVisibility(View.VISIBLE);
    category.setVisibility(memes ? View.VISIBLE : View.GONE);
    root.animate().alpha(1).setDuration(140);
  }
  void open(JSONObject item) {
    if (busy)
      return;
    task(()
             -> api.download(activity,
                             ServiceApi.media(item.getString("url"), memes),
                             item.getString("name"), 200L * 1024 * 1024),
         value -> preview((File)value, item.optString("name")));
  }
  void preview(File file, String name) throws Exception {
    stopVideo();
    preview = true;
    body.removeAllViews();
    actions.setVisibility(View.GONE);
    search.setVisibility(View.GONE);
    category.setVisibility(View.GONE);
    Uri uri = SharedMediaProvider.uri(activity, file);
    String mime = activity.getContentResolver().getType(uri);
    TextView title = text(name, 13);
    body.addView(title);
    if (mime.startsWith("video/") ||
        name.toLowerCase(Locale.ROOT).endsWith(".gif")) {
      if (mime.startsWith("video/")) {
        video = new VideoView(activity);
        video.setVideoURI(uri);
        MediaController controls = new MediaController(activity);
        controls.setAnchorView(video);
        video.setMediaController(controls);
        video.setOnPreparedListener(player -> {
          player.setLooping(true);
          video.seekTo(1);
        });
        body.addView(video, new LinearLayout.LayoutParams(-1, 0, 1));
      } else
        image(file);
    } else if (mime.startsWith("image/"))
      image(file);
    else {
      TextView content = text("", 13);
      ScrollView scroll = new ScrollView(activity);
      scroll.addView(content);
      body.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
      if (mime.startsWith("text/") || name.endsWith(".txt")) {
        task(() -> {
          try (InputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[65536];
            int used = 0, n;
            while (used < bytes.length &&
                   (n = in.read(bytes, used, bytes.length - used)) != -1)
              used += n;
            return new String(bytes, 0, used,
                              java.nio.charset.StandardCharsets.UTF_8);
          }
        }, v -> content.setText((String)v));
      } else
        content.setText(mime);
    }
    LinearLayout buttons = new LinearLayout(activity);
    buttons.addView(button("Back", this::gallery));
    buttons.addView(button("Copy", () -> {
      ((ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE))
          .setPrimaryClip(
              ClipData.newUri(activity.getContentResolver(), name, uri));
      status.setText("Copied.");
    }));
    buttons.addView(button("Share", () -> {
      Intent send = new Intent(Intent.ACTION_SEND)
                        .setType(mime)
                        .putExtra(Intent.EXTRA_STREAM, uri)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
      send.setClipData(
          ClipData.newUri(activity.getContentResolver(), name, uri));
      activity.startActivity(Intent.createChooser(send, "Share"));
    }));
    body.addView(buttons);
    hideKeyboard();
  }
  void image(File file) {
    ImageView image = new ImageView(activity);
    image.setScaleType(ImageView.ScaleType.FIT_CENTER);
    body.addView(image, new LinearLayout.LayoutParams(-1, 0, 1));
    if (android.os.Build.VERSION.SDK_INT >= 28 &&
        file.getName().toLowerCase(Locale.ROOT).endsWith(".gif")) {
      task(()
               -> android.graphics.ImageDecoder.decodeDrawable(
                   android.graphics.ImageDecoder.createSource(file),
                   (decoder, info, source) -> {
                     int w = info.getSize().getWidth(),
                         h = info.getSize().getHeight();
                     double scale = Math.min(1., 1280. / Math.max(w, h));
                     decoder.setTargetSize(Math.max(1, (int)(w * scale)),
                                           Math.max(1, (int)(h * scale)));
                   }),
           value -> {
             android.graphics.drawable.Drawable drawable =
                 (android.graphics.drawable.Drawable)value;
             image.setImageDrawable(drawable);
             if (drawable instanceof
                 android.graphics.drawable.AnimatedImageDrawable) {
               animation =
                   (android.graphics.drawable.AnimatedImageDrawable)drawable;
               animation.start();
             }
           });
      return;
    }
    task(() -> {
      BitmapFactory.Options options = new BitmapFactory.Options();
      options.inJustDecodeBounds = true;
      BitmapFactory.decodeFile(file.toString(), options);
      options.inSampleSize = 1;
      while (options.outWidth / options.inSampleSize > 1280 ||
             options.outHeight / options.inSampleSize > 1280)
        options.inSampleSize *= 2;
      options.inJustDecodeBounds = false;
      return BitmapFactory.decodeFile(file.toString(), options);
    }, value -> image.setImageBitmap((Bitmap)value));
  }
  void hideKeyboard() {
    ((android.view.inputmethod.InputMethodManager)activity.getSystemService(
         Context.INPUT_METHOD_SERVICE))
        .hideSoftInputFromWindow(root.getWindowToken(), 0);
    root.requestFocus();
  }
  void pick() {
    if (busy)
      return;
    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT)
                        .setType("*/*")
                        .addCategory(Intent.CATEGORY_OPENABLE)
                        .putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    activity.startActivityForResult(intent, PICK);
  }
  void result(int request, int result, Intent data) {
    if (result != Activity.RESULT_OK || data == null)
      return;
    if (request == KEY) {
      Uri uri = data.getData();
      task(
          ()
              -> {
            try (InputStream in =
                     activity.getContentResolver().openInputStream(uri)) {
              return new String(api.read(in, 4096),
                                java.nio.charset.StandardCharsets.UTF_8)
                  .trim();
            }
          },
          v -> {
            ServiceApi.saveKey(activity, (String)v);
            status.setText("Key saved.");
          });
      return;
    }
    pending.clear();
    if (data.getClipData() != null) {
      for (int i = 0; i < Math.min(16, data.getClipData().getItemCount()); i++)
        pending.add(ServiceApi.local(activity,
                                     data.getClipData().getItemAt(i).getUri()));
    } else if (data.getData() != null)
      pending.add(ServiceApi.local(activity, data.getData()));
    review();
  }
  void incoming(Intent intent) {
    pending.clear();
    ArrayList<Uri> uris =
        Intent.ACTION_SEND_MULTIPLE.equals(intent.getAction())
            ? intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM)
            : null;
    if (uris != null) {
      for (Uri uri : uris)
        if (uri != null && "content".equals(uri.getScheme()))
          pending.add(ServiceApi.local(activity, uri));
    } else {
      Uri uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
      if (uri != null && "content".equals(uri.getScheme()))
        pending.add(ServiceApi.local(activity, uri));
    }
    CharSequence text = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
    if (pending.isEmpty() && text != null) {
      try {
        File dir = new File(activity.getCacheDir(), "service-media");
        dir.mkdirs();
        File file = File.createTempFile("shared-", ".txt", dir);
        try (OutputStream out = new FileOutputStream(file)) {
          out.write(text.toString().getBytes(
              java.nio.charset.StandardCharsets.UTF_8));
        }
        pending.add(new ServiceApi.Local(
            SharedMediaProvider.uri(activity, file), "text.txt", "text/plain"));
      } catch (Exception e) {
        status.setText("Cannot read shared text.");
      }
    }
    review();
  }
  void review() {
    if (pending.isEmpty())
      return;
    if (pending.size() > 16) {
      status.setText("Choose up to 16 files.");
      return;
    }
    preview = true;
    stopVideo();
    body.removeAllViews();
    actions.setVisibility(View.GONE);
    search.setVisibility(View.GONE);
    category.setVisibility(View.GONE);
    form = new LinearLayout(activity);
    form.setOrientation(LinearLayout.VERTICAL);
    ScrollView scroll = new ScrollView(activity);
    scroll.addView(form);
    body.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
    for (ServiceApi.Local item : pending)
      form.addView(text(item.name, 13));
    EditText name = new EditText(activity);
    name.setText(pending.get(0).name);
    name.setTextColor(color());
    if (memes)
      form.addView(name);
    Spinner destination = new Spinner(activity);
    ArrayList<String> cats = new ArrayList<>();
    for (int i = 1; i < category.getAdapter().getCount(); i++)
      cats.add(String.valueOf(category.getAdapter().getItem(i)));
    if (cats.isEmpty())
      cats.addAll(Arrays.asList("image", "gify", "video", "clipy"));
    destination.setAdapter(new ArrayAdapter<>(
        activity, android.R.layout.simple_spinner_dropdown_item, cats));
    if (memes) {
      form.addView(destination);
      form.addView(button("Key", this::keyForm));
    }
    form.addView(button("Upload", () -> {
      if (busy)
        return;
      ArrayList<ServiceApi.Local> selected = new ArrayList<>(pending);
      String cat = String.valueOf(destination.getSelectedItem()),
             title = name.getText().toString();
      task(
          ()
              -> {
            if (memes) {
              String link = "";
              for (ServiceApi.Local item : selected)
                link = api.uploadMeme(activity, item, cat,
                                      selected.size() == 1 ? title : item.name);
              return link;
            }
            return api.uploadBin(activity, selected);
          },
          value -> {
            ((ClipboardManager)activity.getSystemService(
                 Context.CLIPBOARD_SERVICE))
                .setPrimaryClip(ClipData.newPlainText("Link", (String)value));
            pending.clear();
            gallery();
            load(false);
            status.setText("Uploaded. Link copied.");
          });
    }));
    form.addView(button("Cancel", () -> {
      if (busy)
        return;
      pending.clear();
      gallery();
    }));
    hideKeyboard();
  }
  void textForm() {
    if (busy)
      return;
    preview = true;
    body.removeAllViews();
    EditText text = new EditText(activity);
    text.setTextColor(color());
    text.setGravity(Gravity.TOP);
    body.addView(text, new LinearLayout.LayoutParams(-1, 0, 1));
    body.addView(button("Continue", () -> {
      if (text.length() == 0)
        return;
      Intent intent =
          new Intent(Intent.ACTION_SEND)
              .putExtra(Intent.EXTRA_TEXT, text.getText().toString());
      incoming(intent);
    }));
    body.addView(button("Back", this::gallery));
  }
  void keyForm() {
    EditText input = new EditText(activity);
    input.setSingleLine();
    input.setInputType(android.text.InputType.TYPE_CLASS_TEXT |
                       android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD);
    new AlertDialog.Builder(activity)
        .setTitle("Private upload key")
        .setView(input)
        .setPositiveButton("Save",
                           (d, w) -> {
                             try {
                               ServiceApi.saveKey(activity,
                                                  input.getText().toString());
                               status.setText("Key saved.");
                             } catch (Exception e) {
                               status.setText(e.getMessage());
                             }
                           })
        .setNeutralButton(
            "Import file",
            (d, w)
                -> activity.startActivityForResult(
                    new Intent(Intent.ACTION_OPEN_DOCUMENT)
                        .setType("text/*")
                        .addCategory(Intent.CATEGORY_OPENABLE)
                        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),
                    KEY))
        .setNegativeButton("Cancel", null)
        .show();
  }
  @Override
  public void dismiss() {
    if (disposed)
      return;
    disposed = true;
    main.removeCallbacksAndMessages(null);
    stopVideo();
    api.cancel();
    io.shutdownNow();
    images.shutdownNow();
    cache.evictAll();
    super.dismiss();
  }
}
