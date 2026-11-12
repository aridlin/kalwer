package pl.aridlin.kalwer;
import android.app.*;
import android.content.*;
import android.widget.*;
import java.util.*;
import org.json.*;
/** Explicit per-device app groups; exported configs never contain installed app IDs. */
final class AppGroups {
  static JSONObject read(SharedPreferences p) {
    try {
      return new JSONObject(p.getString("app_groups", "{}"));
    } catch (JSONException e) {
      return new JSONObject();
    }
  }
  static List<String> names(SharedPreferences p) {
    ArrayList<String> names = new ArrayList<>();
    read(p).keys().forEachRemaining(names::add);
    Collections.sort(names);
    return names;
  }
  static Set<String> members(SharedPreferences p, String name) {
    Set<String> set = new HashSet<>();
    JSONArray rows = read(p).optJSONArray(name);
    if (rows != null)
      for (int i = 0; i < rows.length(); i++) set.add(rows.optString(i));
    return set;
  }
  static void manage(Activity a, SharedPreferences p, List<AppCatalog.App> apps, Runnable changed) {
    ArrayList<String> rows = new ArrayList<>(names(p));
    rows.add("New group…");
    new AlertDialog.Builder(a)
        .setTitle("App groups")
        .setItems(rows.toArray(new String[0]),
            (d, index) -> {
              if (index == rows.size() - 1) {
                EditText input = new EditText(a);
                input.setSingleLine(true);
                new AlertDialog.Builder(a)
                    .setTitle("Group name")
                    .setView(input)
                    .setPositiveButton("Next",
                        (dialog, which) -> {
                          String name = input.getText().toString().trim().toLowerCase(Locale.ROOT);
                          if (!name.matches("[\\p{L}\\p{N}_-]{1,32}")
                              || name.equals("favourites")) {
                            Toast
                                .makeText(a, "Use 1–32 letters, numbers, _ or -", Toast.LENGTH_LONG)
                                .show();
                            return;
                          }
                          edit(a, p, apps, name, changed);
                        })
                    .setNegativeButton("Cancel", null)
                    .show();
              } else
                edit(a, p, apps, rows.get(index), changed);
            })
        .setNegativeButton("Close", null)
        .show();
  }
  private static void edit(
      Activity a, SharedPreferences p, List<AppCatalog.App> apps, String name, Runnable changed) {
    ArrayList<AppCatalog.App> sorted = new ArrayList<>(apps);
    sorted.sort(Comparator.comparing(x -> x.nameKey));
    String[] labels = new String[sorted.size()];
    boolean[] checked = new boolean[sorted.size()];
    Set<String> selected = members(p, name);
    for (int i = 0; i < labels.length; i++) {
      labels[i] = sorted.get(i).title;
      checked[i] = selected.contains(sorted.get(i).id);
    }
    new AlertDialog.Builder(a)
        .setTitle("&" + name)
        .setMultiChoiceItems(labels, checked,
            (d, index, on) -> {
              if (on)
                selected.add(sorted.get(index).id);
              else
                selected.remove(sorted.get(index).id);
            })
        .setPositiveButton("Save",
            (d, w) -> {
              JSONObject all = read(p);
              try {
                all.put(name, new JSONArray(selected));
              } catch (JSONException ignored) {
              }
              p.edit().putString("app_groups", all.toString()).apply();
              changed.run();
            })
        .setNeutralButton("Delete",
            (d, w) -> {
              JSONObject all = read(p);
              all.remove(name);
              p.edit().putString("app_groups", all.toString()).apply();
              changed.run();
            })
        .setNegativeButton("Cancel", null)
        .show();
  }
}
