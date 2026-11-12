package pl.aridlin.kalwer;
import android.content.Context;
import java.util.*;
import java.util.concurrent.*;
import org.json.*;
/** Search indexes load only when a text-copy command is first used. */
final class TextTools {
  static final class Item {
    final String value, name, key;
    Item(String v, String n, String tags) {
      value = v;
      name = n;
      key = (n + " " + tags).toLowerCase(Locale.ROOT);
    }
  }
  private static volatile List<Item> symbols, faces;
  private static boolean loading;
  private static final List<Runnable> listeners = new ArrayList<>();
  private static final ExecutorService io = Executors.newSingleThreadExecutor();
  static synchronized void load(Context context, Runnable ready) {
    if (symbols != null && faces != null) {
      ready.run();
      return;
    }
    listeners.add(ready);
    if (loading)
      return;
    loading = true;
    Context app = context.getApplicationContext();
    io.execute(() -> {
      List<Item> unicode = new ArrayList<>(), emoticons = new ArrayList<>();
      for (int cp = 32; cp <= 0x33ff; cp++) add(unicode, cp);
      for (int cp = 0x1f300; cp <= 0x1faff; cp++) add(unicode, cp);
      try (java.io.InputStream in = app.getAssets().open("emoticons.json")) {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        byte[] b = new byte[4096];
        int n;
        while ((n = in.read(b)) != -1) bytes.write(b, 0, n);
        JSONArray data = new JSONArray(bytes.toString("UTF-8"));
        for (int i = 0; i < data.length(); i++) {
          JSONArray row = data.getJSONArray(i);
          emoticons.add(new Item(
              row.getString(0), row.getString(1), row.getString(2) + " " + row.getString(3)));
        }
      } catch (Exception ignored) {
      }
      symbols = Collections.unmodifiableList(unicode);
      faces = Collections.unmodifiableList(emoticons);
      new android.os.Handler(android.os.Looper.getMainLooper()).post(() -> {
        List<Runnable> callbacks;
        synchronized (TextTools.class) {
          callbacks = new ArrayList<>(listeners);
          listeners.clear();
        }
        for (Runnable callback : callbacks) callback.run();
      });
    });
  }
  private static void add(List<Item> rows, int cp) {
    if (!Character.isDefined(cp) || Character.isISOControl(cp))
      return;
    String name = Character.getName(cp);
    if (name != null)
      rows.add(new Item(
          new String(Character.toChars(cp)), name, String.format(Locale.ROOT, "U+%04X", cp)));
  }
  static boolean ready() {
    return symbols != null && faces != null;
  }
  static List<Item> search(String input) {
    List<Item> found = new ArrayList<>();
    boolean face = input.startsWith("/wemote");
    String q = (face                ? input.substring(7)
            : input.startsWith("+") ? input.substring(1)
                                    : input)
                   .trim()
                   .toLowerCase(Locale.ROOT);
    if (!face && !q.isEmpty()) {
      String hex = q.replaceFirst("^(u\\+|0x)", "");
      if (hex.matches("[0-9a-f]{1,6}")) {
        int cp = Integer.parseInt(hex, 16);
        if (Character.isValidCodePoint(cp) && !(cp >= 0xd800 && cp <= 0xdfff)) {
          String name = Character.getName(cp);
          found.add(new Item(new String(Character.toChars(cp)),
              String.format(Locale.ROOT, "U+%04X", cp) + (name == null ? "" : " · " + name), ""));
          return found;
        }
      }
    }
    List<Item> source = face ? faces : symbols;
    if (source != null)
      for (Item item : source)
        if (item.key.contains(q)) {
          found.add(item);
          if (found.size() == 60)
            break;
        }
    return found;
  }
}
