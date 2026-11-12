package pl.aridlin.kalwer;

import android.content.*;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.*;

/**
 * Exposes only cached service media, read-only, with temporary URI grants.
 */
public final class SharedMediaProvider extends ContentProvider {
  static Uri uri(Context c, File file) {
    return new Uri.Builder()
        .scheme("content")
        .authority(c.getPackageName() + ".media")
        .appendPath(file.getName())
        .build();
  }
  private File file(Uri uri) {
    try {
      File root = new File(getContext().getCacheDir(), "service-media")
                      .getCanonicalFile();
      File f = new File(root, uri.getLastPathSegment()).getCanonicalFile();
      if (!uri.getAuthority().equals(getContext().getPackageName() +
                                     ".media") ||
          !root.equals(f.getParentFile()) || !f.isFile())
        throw new IllegalArgumentException("Unknown file");
      return f;
    } catch (IOException e) {
      throw new IllegalArgumentException(e);
    }
  }
  @Override
  public boolean onCreate() {
    return true;
  }
  @Override
  public String getType(Uri uri) {
    File f = file(uri);
    String ext =
        android.webkit.MimeTypeMap.getFileExtensionFromUrl(f.getName());
    String mime =
        android.webkit.MimeTypeMap.getSingleton().getMimeTypeFromExtension(
            ext.toLowerCase(java.util.Locale.ROOT));
    return mime == null ? "application/octet-stream" : mime;
  }
  @Override
  public ParcelFileDescriptor openFile(Uri uri, String mode)
      throws FileNotFoundException {
    if (!"r".equals(mode))
      throw new FileNotFoundException("Read only");
    return ParcelFileDescriptor.open(file(uri),
                                     ParcelFileDescriptor.MODE_READ_ONLY);
  }
  @Override
  public Cursor query(Uri uri, String[] projection, String selection,
                      String[] args, String sort) {
    File f = file(uri);
    String[] columns =
        projection != null
            ? projection
            : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
    MatrixCursor cursor = new MatrixCursor(columns);
    Object[] row = new Object[columns.length];
    for (int i = 0; i < columns.length; i++)
      row[i] = OpenableColumns.DISPLAY_NAME.equals(columns[i])
                   ? f.getName().replaceFirst("^[a-f0-9]{64}-", "")
               : OpenableColumns.SIZE.equals(columns[i]) ? f.length()
                                                         : null;
    cursor.addRow(row);
    return cursor;
  }
  @Override
  public Uri insert(Uri uri, ContentValues values) {
    throw new UnsupportedOperationException();
  }
  @Override
  public int delete(Uri uri, String where, String[] args) {
    throw new UnsupportedOperationException();
  }
  @Override
  public int update(Uri uri, ContentValues values, String where,
                    String[] args) {
    throw new UnsupportedOperationException();
  }
}
