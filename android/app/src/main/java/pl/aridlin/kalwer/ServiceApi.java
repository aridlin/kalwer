package pl.aridlin.kalwer;

import android.content.*;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.security.keystore.*;
import android.util.Base64;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;
import javax.crypto.*;
import javax.crypto.spec.GCMParameterSpec;
import org.json.*;

final class ServiceApi {
  static final String MEMES = "https://memes.aridlin.pl/",
                      BIN = "https://bin.aridlin.pl";
  final CookieManager cookies =
      new CookieManager(null, CookiePolicy.ACCEPT_ORIGINAL_SERVER);
  volatile boolean cancelled;
  volatile HttpURLConnection active;
  void cancel() {
    cancelled = true;
    HttpURLConnection c = active;
    if (c != null)
      c.disconnect();
  }
  void check() throws IOException {
    if (cancelled || Thread.currentThread().isInterrupted())
      throw new IOException("Cancelled.");
  }
  HttpURLConnection connect(String address, String method) throws Exception {
    check();
    URL url = new URL(address);
    HttpURLConnection c = (HttpURLConnection)url.openConnection();
    active = c;
    c.setInstanceFollowRedirects(false);
    c.setConnectTimeout(10000);
    c.setReadTimeout(30000);
    c.setRequestMethod(method);
    c.setRequestProperty("User-Agent", "Kalwer");
    for (Map.Entry<String, List<String>> e :
         cookies.get(url.toURI(), Collections.emptyMap()).entrySet())
      c.setRequestProperty(e.getKey(), String.join("; ", e.getValue()));
    return c;
  }
  byte[] response(HttpURLConnection c, int limit) throws Exception {
    try {
      int status = c.getResponseCode();
      cookies.put(c.getURL().toURI(), c.getHeaderFields());
      InputStream stream =
          status < 400 ? c.getInputStream() : c.getErrorStream();
      byte[] result = stream == null ? new byte[0] : read(stream, limit);
      if (status < 200 || status >= 300) {
        String message = "The server rejected this request (" + status + ").";
        try {
          message = new JSONObject(new String(result, StandardCharsets.UTF_8))
                        .optString("error", message);
        } catch (Exception ignored) {
        }
        throw new IOException(message);
      }
      return result;
    } finally {
      c.disconnect();
      if (active == c)
        active = null;
    }
  }
  byte[] read(InputStream stream, int limit) throws Exception {
    try (InputStream in = stream;
         ByteArrayOutputStream out = new ByteArrayOutputStream()) {
      byte[] bytes = new byte[32768];
      int n;
      while ((n = in.read(bytes)) != -1) {
        check();
        if (out.size() + n > limit)
          throw new IOException("File is too large.");
        out.write(bytes, 0, n);
      }
      return out.toByteArray();
    }
  }
  String request(String url, String method, byte[] body, String csrf)
      throws Exception {
    HttpURLConnection c = connect(url, method);
    if (csrf != null)
      c.setRequestProperty("X-CSRF-Token", csrf);
    if (body != null) {
      c.setDoOutput(true);
      c.setRequestProperty("Content-Type", method.equals("PUT")
                                               ? "application/octet-stream"
                                               : "application/json");
      c.setFixedLengthStreamingMode(body.length);
      try (OutputStream out = c.getOutputStream()) {
        out.write(body);
      }
    }
    return new String(response(c, 2 * 1024 * 1024), StandardCharsets.UTF_8);
  }
  static String hex(byte[] bytes) {
    StringBuilder s = new StringBuilder();
    for (byte b : bytes)
      s.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
    return s.toString();
  }
  static String enc(String text) { return Uri.encode(text); }
  static String media(String value, boolean meme) throws IOException {
    Uri u = Uri.parse(value);
    String base = meme ? MEMES : BIN;
    if (!u.isAbsolute())
      value =
          base + (value.startsWith("/") ? value : (meme ? value : "/" + value));
    u = Uri.parse(value);
    if (!"https".equals(u.getScheme()) ||
        !(meme ? "memes.aridlin.pl" : "bin.aridlin.pl").equals(u.getHost()) ||
        u.getUserInfo() != null || u.getPort() != -1)
      throw new IOException("Invalid media address.");
    if (!(meme ? (u.getPath().startsWith("/storage/") ||
                  u.getPath().equals("/kalwer.php"))
               : u.getPath().startsWith("/file/")))
      throw new IOException("Invalid media address.");
    return value;
  }
  JSONObject memes(String query, String category, int offset) throws Exception {
    return new JSONObject(
        request(MEMES + "kalwer.php?action=list&q=" + enc(query) +
                    "&cat=" + enc(category) + "&offset=" + offset,
                "GET", null, null));
  }
  JSONArray bin() throws Exception {
    return new JSONArray(request(BIN + "/items", "GET", null, null));
  }
  File download(Context context, String address, String name, long max)
      throws Exception {
    String safe = name.replaceAll("[^a-zA-Z0-9._-]", "_");
    if (safe.length() > 100)
      safe = safe.substring(safe.length() - 100);
    if (safe.isEmpty())
      safe = "file";
    File dir = new File(context.getCacheDir(), "service-media");
    dir.mkdirs();
    String hash = hex(MessageDigest.getInstance("SHA-256").digest(
        address.getBytes(StandardCharsets.UTF_8)));
    File target = new File(dir, hash + "-" + safe);
    if (target.isFile())
      return target;
    File temp = File.createTempFile("download-", ".part", dir);
    HttpURLConnection c = connect(address, "GET");
    try {
      if (c.getResponseCode() != 200)
        throw new IOException("Download failed.");
      if (c.getContentLengthLong() > max)
        throw new IOException("File is too large.");
      try (InputStream in = c.getInputStream();
           OutputStream out = new FileOutputStream(temp)) {
        byte[] data = new byte[32768];
        int n;
        long total = 0;
        while ((n = in.read(data)) != -1) {
          check();
          total += n;
          if (total > max)
            throw new IOException("File is too large.");
          out.write(data, 0, n);
        }
      }
      if (!temp.renameTo(target))
        throw new IOException("Could not save download.");
      return target;
    } finally {
      temp.delete();
      c.disconnect();
      active = null;
    }
  }
  static final class Local {
    final Uri uri;
    final String name, mime;
    Local(Uri u, String n, String m) {
      uri = u;
      name = n;
      mime = m;
    }
  }
  static Local local(Context c, Uri uri) {
    String name = uri.getLastPathSegment();
    try (Cursor cursor = c.getContentResolver().query(
             uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null,
             null)) {
      if (cursor != null && cursor.moveToFirst())
        name = cursor.getString(0);
    } catch (Exception ignored) {
    }
    if (name == null)
      name = "file";
    String mime = c.getContentResolver().getType(uri);
    return new Local(uri, name,
                     mime == null ? "application/octet-stream" : mime);
  }
  InputStream open(Context c, Local item) throws IOException {
    InputStream in = c.getContentResolver().openInputStream(item.uri);
    if (in == null)
      throw new IOException("Cannot read file.");
    return in;
  }
  static String key(Context c) throws Exception {
    String stored =
        c.getSharedPreferences("kalwer", 0).getString("memes_key", "");
    if (stored.isEmpty())
      return "";
    byte[] data = Base64.decode(stored, Base64.NO_WRAP);
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
    ks.load(null);
    cipher.init(Cipher.DECRYPT_MODE, ks.getKey("kalwer-memes", null),
                new GCMParameterSpec(128, Arrays.copyOfRange(data, 0, 12)));
    return new String(cipher.doFinal(Arrays.copyOfRange(data, 12, data.length)),
                      StandardCharsets.UTF_8);
  }
  static void saveKey(Context c, String value) throws Exception {
    value = value.trim();
    if (!value.matches("[a-f0-9]{64}"))
      throw new IOException("Enter the 64-character private upload key.");
    KeyStore ks = KeyStore.getInstance("AndroidKeyStore");
    ks.load(null);
    if (!ks.containsAlias("kalwer-memes")) {
      KeyGenerator gen = KeyGenerator.getInstance(
          KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
      gen.init(new KeyGenParameterSpec
                   .Builder("kalwer-memes", KeyProperties.PURPOSE_ENCRYPT |
                                                KeyProperties.PURPOSE_DECRYPT)
                   .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                   .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                   .build());
      gen.generateKey();
    }
    Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
    cipher.init(Cipher.ENCRYPT_MODE, ks.getKey("kalwer-memes", null));
    ByteArrayOutputStream out = new ByteArrayOutputStream();
    out.write(cipher.getIV());
    out.write(cipher.doFinal(value.getBytes(StandardCharsets.UTF_8)));
    c.getSharedPreferences("kalwer", 0)
        .edit()
        .putString("memes_key",
                   Base64.encodeToString(out.toByteArray(), Base64.NO_WRAP))
        .apply();
  }
  String uploadMeme(Context context, Local item, String category, String name)
      throws Exception {
    String key = key(context);
    if (key.isEmpty())
      throw new IOException("Save your private upload key first.");
    String boundary = "kalwer" + UUID.randomUUID().toString().replace("-", "");
    HttpURLConnection c = connect(MEMES + "kalwer.php?action=upload", "POST");
    c.setRequestProperty("X-Kalwer-Upload-Key", key);
    c.setRequestProperty("Content-Type",
                         "multipart/form-data; boundary=" + boundary);
    c.setDoOutput(true);
    c.setChunkedStreamingMode(32768);
    try (OutputStream out = c.getOutputStream()) {
      for (String[] field :
           new String[][] {{"category", category}, {"name", name}})
        out.write(("--" + boundary +
                   "\r\nContent-Disposition: form-data; name=\"" + field[0] +
                   "\"\r\n\r\n" + field[1] + "\r\n")
                      .getBytes(StandardCharsets.UTF_8));
      String filename = item.name.replaceAll("[\\\"\r\n]", "_");
      out.write(
          ("--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" +
           filename + "\"\r\nContent-Type: " +
           item.mime.replaceAll("[\r\n]", "") + "\r\n\r\n")
              .getBytes(StandardCharsets.UTF_8));
      try (InputStream in = open(context, item)) {
        byte[] data = new byte[32768];
        int n;
        long total = 0;
        while ((n = in.read(data)) != -1) {
          check();
          total += n;
          if (total > 200L * 1024 * 1024)
            throw new IOException("Memes allows up to 200 MiB per file.");
          out.write(data, 0, n);
        }
      }
      out.write(
          ("\r\n--" + boundary + "--\r\n").getBytes(StandardCharsets.UTF_8));
    } catch (Exception e) {
      c.disconnect();
      throw e;
    }
    return media(new JSONObject(new String(response(c, 2 * 1024 * 1024),
                                           StandardCharsets.UTF_8))
                     .getString("url"),
                 true);
  }
  String uploadBin(Context context, List<Local> items) throws Exception {
    if (items.isEmpty() || items.size() > 16)
      throw new IOException("Choose 1–16 files.");
    // Stage provider streams to disk: sizes must be known before starting the
    // transaction.
    ArrayList<File> staged = new ArrayList<>();
    String session = null, csrf = null;
    try {
      long total = 0;
      JSONArray rows = new JSONArray();
      for (Local item : items) {
        File f = File.createTempFile("binup-", ".part", context.getCacheDir());
        staged.add(f);
        try (InputStream in = open(context, item);
             OutputStream out = new FileOutputStream(f)) {
          byte[] data = new byte[32768];
          int n;
          long size = 0;
          while ((n = in.read(data)) != -1) {
            check();
            size += n;
            total += n;
            if (size > 1073741824L || total > 2147483648L)
              throw new IOException(
                  "Binup allows 1 GiB per file and 2 GiB per paste.");
            out.write(data, 0, n);
          }
        }
        rows.put(new JSONObject()
                     .put("name", item.name)
                     .put("mime", item.mime)
                     .put("size", f.length()));
      }
      String html = request(BIN + "/", "GET", null, null);
      csrf = value(html, "csrf");
      String revision = value(html, "revision");
      if (!csrf.matches("[a-f0-9]{64}"))
        throw new IOException("Cannot start upload session.");
      JSONObject body =
          new JSONObject()
              .put("action", revision.isEmpty() ? "replace" : "append")
              .put("revision", revision)
              .put("items", rows);
      String id =
          new JSONObject(
              request(BIN + "/uploads", "POST",
                      body.toString().getBytes(StandardCharsets.UTF_8), csrf))
              .getString("id");
      if (!id.matches("[a-f0-9]{48}"))
        throw new IOException("Invalid upload session.");
      session = BIN + "/uploads/" + id;
      byte[] chunk = new byte[4 * 1024 * 1024];
      for (int i = 0; i < staged.size(); i++)
        try (InputStream in = new FileInputStream(staged.get(i))) {
          long offset = 0;
          while (true) {
            int used = 0, n;
            while (used < chunk.length &&
                   (n = in.read(chunk, used, chunk.length - used)) != -1)
              used += n;
            if (used == 0)
              break;
            byte[] bytes = Arrays.copyOf(chunk, used);
            String answer = null;
            for (int attempt = 0;; attempt++)
              try {
                answer = request(session + "/" + i + "?offset=" + offset, "PUT",
                                 bytes, csrf);
                break;
              } catch (Exception e) {
                check();
                if (attempt >= 1)
                  throw e;
              }
            offset += used;
            if (new JSONObject(answer).getLong("offset") != offset)
              throw new IOException("Chunk was not accepted.");
          }
        }
      JSONObject committed = new JSONObject(
          request(session + "/commit", "POST", new byte[0], csrf));
      session = null;
      revision = committed.getString("revision");
      JSONArray uploaded = bin();
      StringBuilder links = new StringBuilder();
      for (int i = Math.max(0, uploaded.length() - items.size());
           i < uploaded.length(); i++) {
        String url = media(uploaded.getJSONObject(i).getString("url"), false);
        if (url.contains("revision=" + revision)) {
          if (links.length() > 0)
            links.append("\n");
          links.append(url);
        }
      }
      return links.length() > 0 ? links.toString() : BIN + "/";
    } finally {
      if (session != null) {
        boolean old = cancelled;
        cancelled = false;
        try {
          request(session, "DELETE", null, csrf);
        } catch (Exception ignored) {
        }
        cancelled = old;
      }
      for (File f : staged)
        f.delete();
    }
  }
  static String value(String html, String name) {
    java.util.regex.Matcher m =
        java.util.regex.Pattern
            .compile("name=\"" + name + "\" value=\"([^\"]*)\"")
            .matcher(html);
    return m.find() ? m.group(1) : "";
  }
}
