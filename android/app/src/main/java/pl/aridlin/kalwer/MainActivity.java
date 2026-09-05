package pl.aridlin.kalwer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ResolveInfo;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.AlarmClock;
import android.provider.MediaStore;
import android.provider.Settings;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.BaseAdapter;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import pl.aridlin.kalwer.AppCatalog.App;

public final class MainActivity extends Activity {
    private static final int TEXT = 0xffd0e8d6, MUTED = 0xff92b89f, GREEN = 0xff79d394;
    private AppCatalog catalog;
    private final AppCatalog.Callback catalogCallback = this::catalogLoaded;
    private final List<App> apps = new ArrayList<>();
    private final List<Result> results = new ArrayList<>();
    private Set<String> favourites;
    private SharedPreferences prefs;
    private FieldBackground root;
    private EditText query;
    private TextView status;
    private ListView list;
    private ResultAdapter adapter;
    private boolean catalogReady, loading;
    private String pendingSubmit;
    private int selected;
    private final Runnable showIme = () -> {
        if (!isFinishing() && query.hasWindowFocus()) {
            ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE)).showSoftInput(query, InputMethodManager.SHOW_IMPLICIT);
        }
    };

    private static final class RankedApp {
        final App app;
        final int score;
        final boolean pinned;
        RankedApp(App app, int score, boolean pinned) { this.app = app; this.score = score; this.pinned = pinned; }
    }

    private static final class Result {
        final String title, detail, symbol;
        final Runnable action;
        final App app;
        Result(String t, String d, String s, Runnable a, App app) {
            title = t; detail = d; symbol = s; action = a; this.app = app;
        }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences("kalwer", MODE_PRIVATE);
        catalog = AppCatalog.get(this);
        favourites = new HashSet<>(prefs.getStringSet("favourites", new HashSet<>()));
        getWindow().setBackgroundDrawableResource(android.R.color.transparent);
        if (Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                    WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        buildUi();
        if (Build.VERSION.SDK_INT >= 33) {
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(0, this::finish);
        }
        String restored = state != null ? state.getString("query", "") : retainedQuery();
        query.setText(restored);
        query.setSelection(query.length());
        if (state != null) selected = state.getInt("selected", 0);
        refreshResults();
    }

    private String retainedQuery() {
        long age = System.currentTimeMillis() - prefs.getLong("last_closed", 0);
        return age >= 0 && age < prefs.getInt("retention_ms", 3000) ? prefs.getString("query", "") : "";
    }

    @Override protected void onResume() {
        super.onResume();
        loadApps();
        showKeyboard();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        query.setText(retainedQuery());
        query.setSelection(query.length());
        showKeyboard();
    }

    @Override protected void onPause() {
        pendingSubmit = null;
        prefs.edit().putString("query", query.getText().toString())
                .putLong("last_closed", System.currentTimeMillis()).apply();
        super.onPause();
    }

    @Override protected void onSaveInstanceState(Bundle out) {
        out.putString("query", query.getText().toString());
        out.putInt("selected", selected);
        super.onSaveInstanceState(out);
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) { immersive(); showKeyboard(); }
    }

    private void immersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private void showKeyboard() {
        query.requestFocus();
        query.removeCallbacks(showIme);
        query.post(showIme);
    }

    private void buildUi() {
        root = new FieldBackground(this);
        root.setOpacity(prefs.getInt("opacity", 72));
        setContentView(root);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(16), dp(10), dp(16), dp(10));
        FrameLayout.LayoutParams body = new FrameLayout.LayoutParams(
                Math.min(getResources().getDisplayMetrics().widthPixels, dp(640)), -1, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        root.addView(content, body);
        root.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            int width = Math.min(right - left - root.getPaddingLeft() - root.getPaddingRight(), dp(640));
            if (width > 0 && content.getLayoutParams().width != width) {
                content.getLayoutParams().width = width;
                content.requestLayout();
            }
        });
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                Insets safe = insets.getInsets(WindowInsets.Type.displayCutout() | WindowInsets.Type.systemBars());
                Insets ime = insets.getInsets(WindowInsets.Type.ime());
                root.setPadding(safe.left, safe.top, safe.right, Math.max(safe.bottom, ime.bottom));
            } else {
                root.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        TextView brand = text("KALWER", 14, GREEN);
        brand.setLetterSpacing(.16f);
        header.addView(brand, new LinearLayout.LayoutParams(0, dp(48), 1));
        header.addView(button("⚙", "Kalwer settings", this::settings), new LinearLayout.LayoutParams(dp(48), dp(48)));
        header.addView(button("×", "Close Kalwer", this::finish), new LinearLayout.LayoutParams(dp(48), dp(48)));
        content.addView(header);

        LinearLayout search = new LinearLayout(this);
        search.setGravity(Gravity.CENTER_VERTICAL);
        search.setBackground(panel(0xd9001b0f, GREEN).anchor(search));
        query = new EditText(this);
        query.setId(R.id.search_query);
        query.setSingleLine(true);
        query.setTextColor(TEXT);
        query.setHintTextColor(MUTED);
        query.setTextSize(18);
        query.setTypeface(Typeface.MONOSPACE);
        query.setHint("SEARCH THE VAULT");
        query.setContentDescription("Search apps, calculate, or search Google");
        query.setBackgroundColor(Color.TRANSPARENT);
        query.setPadding(dp(14), dp(12), dp(4), dp(12));
        query.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        query.setFilters(new InputFilter[]{new InputFilter.LengthFilter(2048)});
        query.setImeOptions(EditorInfo.IME_ACTION_GO | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        search.addView(query, new LinearLayout.LayoutParams(0, -2, 1));
        search.addView(button("×", "Clear search", () -> query.setText("")), new LinearLayout.LayoutParams(dp(48), dp(56)));
        content.addView(search, new LinearLayout.LayoutParams(-1, -2));

        LinearLayout modes = new LinearLayout(this);
        modes.addView(button("> ACTIONS", "Phone quick actions", () -> setQuery("> ")), new LinearLayout.LayoutParams(0, dp(48), 1));
        modes.addView(button("? GOOGLE", "Google search mode", () -> setQuery("? " + SearchLogic.googleQuery(query.getText().toString()))), new LinearLayout.LayoutParams(0, dp(48), 1));
        modes.addView(button("↵ GO", "Open selected result", this::submit), new LinearLayout.LayoutParams(dp(72), dp(48)));
        content.addView(modes);
        status = text("Loading installed apps…", 12, MUTED);
        status.setPadding(dp(4), 0, 0, dp(8));
        content.addView(status);
        list = new ListView(this);
        list.setId(R.id.search_results);
        list.setDividerHeight(dp(6));
        list.setDivider(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        list.setCacheColorHint(Color.TRANSPARENT);
        list.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        list.setClipToPadding(false);
        list.setPadding(0, dp(2), 0, dp(12));
        adapter = new ResultAdapter();
        list.setAdapter(adapter);
        list.setOnItemClickListener((parent, view, position, id) -> activate(position));
        list.setOnItemLongClickListener((parent, view, position, id) -> { toggleFavourite(position); return true; });
        content.addView(list, new LinearLayout.LayoutParams(-1, 0, 1));
        query.addTextChangedListener(new TextWatcher() {
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                selected = 0;
                pendingSubmit = null;
                refreshResults();
                list.setSelection(0);
            }
            public void afterTextChanged(Editable s) {}
        });
        query.setOnEditorActionListener((v, action, event) -> {
            if (action == EditorInfo.IME_ACTION_GO || action == EditorInfo.IME_ACTION_SEARCH
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                if (event == null || event.getAction() == KeyEvent.ACTION_DOWN) {
                    if (event != null && event.isShiftPressed()) toggleFavourite(selected); else submit();
                }
                return true;
            }
            return false;
        });
        query.setOnKeyListener((v, key, event) -> {
            if (event.getAction() != KeyEvent.ACTION_DOWN) return false;
            if (key == KeyEvent.KEYCODE_ESCAPE) { finish(); return true; }
            if (key == KeyEvent.KEYCODE_DPAD_DOWN || key == KeyEvent.KEYCODE_DPAD_UP) {
                if (!results.isEmpty()) {
                    selected = Math.floorMod(selected + (key == KeyEvent.KEYCODE_DPAD_DOWN ? 1 : -1), results.size());
                    adapter.notifyDataSetChanged();
                    list.smoothScrollToPosition(selected);
                }
                return true;
            }
            return false;
        });
        immersive();
    }

    private void setQuery(String value) { query.setText(value); query.setSelection(query.length()); showKeyboard(); }

    private void loadApps() {
        if (loading) return;
        loading = true;
        catalog.load(catalogCallback);
    }

    private void catalogLoaded(List<App> found) {
        if (isDestroyed() || isFinishing()) return;
        loading = false;
        if (found == null) { pendingSubmit = null; status.setText(R.string.apps_unavailable); return; }
        apps.clear(); apps.addAll(found); catalogReady = true;
        refreshResults();
        if (pendingSubmit != null && pendingSubmit.equals(query.getText().toString())) {
            pendingSubmit = null; submit();
        }
    }

    private void refreshResults() {
        if (query == null || adapter == null) return;
        results.clear();
        String q = query.getText().toString().trim();
        if (q.equals("/settings")) {
            results.add(new Result("Kalwer settings", "Transparency, query memory & controls", "⚙", this::settings, null));
        } else if (q.startsWith("?")) {
            if (!SearchLogic.googleQuery(q).isEmpty()) addGoogle(q);
        } else if (q.startsWith(">")) {
            actions(q.substring(1).trim());
        } else {
            String answer = SearchLogic.calculate(q);
            if (answer != null) results.add(new Result(answer, "Calculator · tap to copy", "=", () -> copy(answer), null));
            if (catalogReady) {
                SearchLogic.Query prepared = new SearchLogic.Query(q);
                List<RankedApp> matches = new ArrayList<>();
                for (App app : apps) {
                    int score = SearchLogic.scoreNormalized(app.nameKey, app.packageKey, prepared);
                    if (score >= 0) matches.add(new RankedApp(app, score, favourites.contains(app.id)));
                }
                matches.sort(Comparator.<RankedApp, Boolean>comparing(a -> !a.pinned)
                        .thenComparingInt(a -> a.score).thenComparing(a -> a.app.nameKey));
                for (RankedApp match : matches) {
                    App app = match.app;
                    results.add(new Result(app.title, app.component.getPackageName(), "", () -> launch(
                        new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setComponent(app.component)
                                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED)), app));
                }
                if (results.isEmpty() && !q.isEmpty()) addGoogle(q);
            }
        }
        selected = Math.max(0, Math.min(selected, results.size() - 1));
        String hint;
        if (q.startsWith(">")) hint = "PHONE ACTIONS · Enter to open";
        else if (q.startsWith("?")) hint = "GOOGLE · type a query, then Enter";
        else if (!catalogReady) hint = "Loading installed apps…";
        else if (results.size() == 1 && results.get(0).symbol.equals("?")) hint = "No local matches · Enter searches Google";
        else hint = results.size() + " RESULTS · hold an app to pin";
        status.setText(hint);
        adapter.notifyDataSetChanged();
    }

    private void addGoogle(String q) {
        String term = SearchLogic.googleQuery(q);
        results.add(new Result("Search Google", term, "?", () -> launch(new Intent(Intent.ACTION_VIEW,
                Uri.parse("https://www.google.com/search").buildUpon().appendQueryParameter("q", term).build())), null));
    }

    private void submit() {
        String q = query.getText().toString().trim();
        if (!catalogReady && !q.startsWith("?") && !q.startsWith(">") && !q.equals("/settings")
                && SearchLogic.calculate(q) == null) {
            pendingSubmit = query.getText().toString();
            status.setText(R.string.apps_waiting);
            loadApps();
            return;
        }
        if (!results.isEmpty()) activate(selected);
    }

    private void activate(int position) {
        if (position < 0 || position >= results.size()) return;
        selected = position;
        results.get(position).action.run();
    }

    private void launch(Intent intent) {
        try { startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); finish(); }
        catch (ActivityNotFoundException | SecurityException e) {
            Toast.makeText(this, "No available app can open this action", Toast.LENGTH_LONG).show();
        }
    }

    private void copy(String text) {
        ((ClipboardManager) getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(ClipData.newPlainText("Kalwer", text));
        if (Build.VERSION.SDK_INT < 33) Toast.makeText(this, "Copied", Toast.LENGTH_SHORT).show();
    }

    private void toggleFavourite(int position) {
        if (position < 0 || position >= results.size()) return;
        App app = results.get(position).app;
        if (app == null) return;
        if (!favourites.remove(app.id())) favourites.add(app.id());
        prefs.edit().putStringSet("favourites", new HashSet<>(favourites)).apply();
        refreshResults();
        for (int i = 0; i < results.size(); i++) if (results.get(i).app == app) { selected = i; break; }
        adapter.notifyDataSetChanged();
        list.smoothScrollToPosition(selected);
    }

    private void actions(String q) {
        String[] parts = q.split("\\s+", 2);
        String command = SearchLogic.normalize(parts[0]);
        String argument = parts.length > 1 ? parts[1].trim() : "";
        if (!argument.isEmpty()) {
            switch (command) {
                case "share":
                    results.add(new Result("Share text", argument, "↗", () -> launch(Intent.createChooser(
                            new Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, argument), "Share with")), null)); return;
                case "copy":
                    results.add(new Result("Copy text", argument, "=", () -> copy(argument), null)); return;
                case "maps":
                    results.add(new Result("Find on map", argument, "↗", () -> launch(new Intent(Intent.ACTION_VIEW,
                            Uri.parse("geo:0,0?q=" + Uri.encode(argument)))), null)); return;
                case "dial":
                    if (argument.matches("[+0-9 ()#*.-]+")) {
                        results.add(new Result("Open dialer", argument + " · review before calling", "↗", () -> launch(
                                new Intent(Intent.ACTION_DIAL, Uri.fromParts("tel", argument, null))), null));
                    } else actionHelp("Enter a phone number", "> dial +48 123 456 789", "> dial ");
                    return;
                case "timer":
                    int seconds = SearchLogic.timerSeconds(argument);
                    if (seconds > 0) results.add(new Result("Set timer", argument + " · open Clock to confirm", "◷", () -> launch(
                            new Intent(AlarmClock.ACTION_SET_TIMER).putExtra(AlarmClock.EXTRA_LENGTH, seconds)
                                    .putExtra(AlarmClock.EXTRA_SKIP_UI, false).putExtra(AlarmClock.EXTRA_MESSAGE, "Kalwer")), null));
                    else actionHelp("Use minutes or h/m/s", "> timer 5m · > timer 1h30m (up to 24h)", "> timer ");
                    return;
            }
        }
        staticAction(q, "Settings", "Phone settings", "settings", Settings.ACTION_SETTINGS);
        staticAction(q, "Wi-Fi", "Open Wi-Fi settings", "wifi wireless internet", Settings.ACTION_WIFI_SETTINGS);
        staticAction(q, "Bluetooth", "Open Bluetooth settings", "bluetooth devices", Settings.ACTION_BLUETOOTH_SETTINGS);
        staticAction(q, "Sound", "Volume and sound settings", "sound volume", Settings.ACTION_SOUND_SETTINGS);
        staticAction(q, "Display", "Brightness and screen settings", "display brightness", Settings.ACTION_DISPLAY_SETTINGS);
        staticAction(q, "Battery", "Battery saver settings", "battery power", Settings.ACTION_BATTERY_SAVER_SETTINGS);
        staticAction(q, "Camera", "Open camera", "camera photo", MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA);
        staticAction(q, "Alarm", "Create an alarm in Clock", "alarm clock", AlarmClock.ACTION_SET_ALARM);
        if (SearchLogic.score("Timer", "timer clock", q) >= 0) actionHelp("Timer", "Try > timer 5m", "> timer ");
        if (SearchLogic.score("Dial", "dial call phone", q) >= 0) results.add(new Result("Dial", "Open dialer · or > dial NUMBER", "↗", () -> launch(new Intent(Intent.ACTION_DIAL)), null));
        if (SearchLogic.score("Maps", "maps navigation", q) >= 0) actionHelp("Maps", "Try > maps coffee near me", "> maps ");
        if (SearchLogic.score("Share", "share send text", q) >= 0) actionHelp("Share text", "Try > share hello", "> share ");
        if (SearchLogic.score("Copy", "copy clipboard", q) >= 0) actionHelp("Copy text", "Try > copy hello", "> copy ");
        if (results.isEmpty()) addGoogle("> " + q);
    }

    private void staticAction(String q, String name, String detail, String keywords, String action) {
        if (SearchLogic.score(name, keywords, q) >= 0) results.add(new Result(name, detail, "↗", () -> launch(new Intent(action)), null));
    }

    private void actionHelp(String title, String detail, String prefix) {
        results.add(new Result(title, detail, ">", () -> setQuery(prefix), null));
    }

    private void settings() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(24), dp(12), dp(24), dp(12));
        TextView opacity = text("", 14, TEXT);
        box.addView(opacity);
        SeekBar alpha = new SeekBar(this);
        alpha.setMax(65);
        alpha.setProgress(prefs.getInt("opacity", 72) - 30);
        opacity.setText(getString(R.string.opacity_label, alpha.getProgress() + 30));
        alpha.setContentDescription("Background opacity");
        alpha.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                int value = progress + 30;
                opacity.setText(getString(R.string.opacity_label, value));
                root.setOpacity(value);
                prefs.edit().putInt("opacity", value).apply();
            }
            public void onStartTrackingTouch(SeekBar bar) {}
            public void onStopTrackingTouch(SeekBar bar) {}
        });
        box.addView(alpha, new LinearLayout.LayoutParams(-1, dp(48)));
        TextView retention = text("", 14, TEXT);
        box.addView(retention);
        SeekBar memory = new SeekBar(this);
        memory.setMax(30);
        memory.setProgress(prefs.getInt("retention_ms", 3000) / 1000);
        retention.setText(getString(R.string.retention_label, memory.getProgress()));
        memory.setContentDescription("Query retention in seconds");
        memory.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            public void onProgressChanged(SeekBar bar, int value, boolean fromUser) {
                retention.setText(getString(R.string.retention_label, value));
                prefs.edit().putInt("retention_ms", value * 1000).apply();
            }
            public void onStartTrackingTouch(SeekBar bar) {}
            public void onStopTrackingTouch(SeekBar bar) {}
        });
        box.addView(memory, new LinearLayout.LayoutParams(-1, dp(48)));
        TextView help = text("Side button: choose Kalwer in your phone’s Open app binding.\n\n"
                + "Type to find apps. Hold an app or tap its star to pin it.\n"
                + "? forces Google; no matches also search Google on Enter.\n"
                + "> opens phone actions. Arithmetic works locally.\n"
                + "Back or × closes Kalwer.", 13, MUTED);
        help.setPadding(0, dp(16), 0, dp(12));
        box.addView(help);
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.addView(box);
        new AlertDialog.Builder(this).setTitle("Kalwer settings").setView(scroll).setPositiveButton("Done", null).show();
    }

    private final class ResultAdapter extends BaseAdapter {
        public int getCount() { return results.size(); }
        public Object getItem(int position) { return results.get(position); }
        public long getItemId(int position) { return position; }
        public View getView(int position, View convertView, ViewGroup parent) {
            Result result = results.get(position);
            Row holder;
            if (convertView == null) holder = new Row();
            else holder = (Row) convertView.getTag();
            holder.position = position;
            holder.title.setText(result.title);
            holder.detail.setText(result.detail);
            boolean app = result.app != null;
            holder.icon.setVisibility(app ? View.VISIBLE : View.GONE);
            holder.symbol.setVisibility(app ? View.GONE : View.VISIBLE);
            holder.star.setVisibility(app ? View.VISIBLE : View.GONE);
            if (app) {
                catalog.icon(result.app, holder.icon);
                boolean pinned = favourites.contains(result.app.id);
                holder.star.setText(pinned ? "★" : "☆");
                holder.star.setContentDescription((pinned ? "Unpin " : "Pin ") + result.title);
            } else {
                holder.icon.setTag(null); holder.icon.setImageDrawable(null);
                holder.symbol.setText(result.symbol);
            }
            boolean active = position == selected;
            if (holder.active == null || holder.active != active) {
                holder.row.setBackground(active ? holder.selectedBackground : holder.normalBackground);
                holder.active = active;
            }
            return holder.row;
        }
    }

    private final class Row {
        final LinearLayout row = new LinearLayout(MainActivity.this);
        final ImageView icon = new ImageView(MainActivity.this);
        final TextView symbol = text("", 26, GREEN), title = text("", 17, TEXT), detail = text("", 11, MUTED);
        final TextView star;
        final Drawable selectedBackground = panel(0xe008321e, GREEN).anchor(row), normalBackground = panel(0xb8001b0f, 0x554ead78).anchor(row);
        Boolean active;
        int position;
        Row() {
            row.setTag(this);
            row.setGravity(Gravity.CENTER_VERTICAL);
            row.setPadding(dp(12), dp(10), dp(4), dp(10));
            row.setMinimumHeight(dp(76));
            FrameLayout leading = new FrameLayout(MainActivity.this);
            icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            leading.addView(icon, new FrameLayout.LayoutParams(-1, -1));
            symbol.setGravity(Gravity.CENTER);
            leading.addView(symbol, new FrameLayout.LayoutParams(-1, -1));
            row.addView(leading, new LinearLayout.LayoutParams(dp(36), dp(36)));
            LinearLayout labels = new LinearLayout(MainActivity.this);
            labels.setOrientation(LinearLayout.VERTICAL);
            labels.setPadding(dp(12), 0, dp(4), 0);
            title.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
            title.setMaxLines(2);
            title.setEllipsize(android.text.TextUtils.TruncateAt.END);
            labels.addView(title);
            detail.setMaxLines(2);
            detail.setEllipsize(android.text.TextUtils.TruncateAt.END);
            detail.setPadding(0, dp(4), 0, 0);
            labels.addView(detail);
            row.addView(labels, new LinearLayout.LayoutParams(0, -2, 1));
            star = button("☆", "Pin app", () -> toggleFavourite(position));
            star.setFocusable(false);
            row.addView(star, new LinearLayout.LayoutParams(dp(48), dp(48)));
        }
    }

    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private TextView text(String value, float size, int color) {
        TextView view = new TextView(this);
        view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setTypeface(Typeface.MONOSPACE); view.setGravity(Gravity.CENTER_VERTICAL);
        return view;
    }
    private TextView button(String label, String description, Runnable action) {
        TextView view = text(label, label.length() > 2 ? 12 : 23, GREEN);
        view.setGravity(Gravity.CENTER); view.setContentDescription(description);
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x4479d394), null, panel(Color.WHITE, Color.TRANSPARENT)));
        view.setOnClickListener(v -> action.run()); view.setFocusable(true);
        return view;
    }
    private HalftoneDrawable panel(int fill, int stroke) {
        return new HalftoneDrawable(fill, stroke, getResources().getDisplayMetrics().density, 14);
    }
}
