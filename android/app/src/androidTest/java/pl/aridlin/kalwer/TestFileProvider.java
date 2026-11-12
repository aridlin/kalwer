package pl.aridlin.kalwer;
import android.content.*;
import android.database.*;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.*;
import java.io.*;
/** Isolated generated fixtures, installed only in the instrumentation APK. */
public final class TestFileProvider extends ContentProvider {
  public boolean onCreate() {
    try {
      java.nio.file.Files.write(new File(getContext().getFilesDir(), "new.txt").toPath(),
          "mobile fixture\n".getBytes("UTF-8"));
      android.graphics.Bitmap b =
          android.graphics.Bitmap.createBitmap(8, 8, android.graphics.Bitmap.Config.ARGB_8888);
      b.eraseColor(0xff79d394);
      try (FileOutputStream out =
               new FileOutputStream(new File(getContext().getFilesDir(), "Picture.png"))) {
        b.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, out);
      }
      b.recycle();
      return true;
    } catch (Exception e) {
      return false;
    }
  }
  public String getType(Uri uri) {
    return uri.getLastPathSegment().equals("Picture.png") ? "image/png"
        : uri.getLastPathSegment().equals("new.txt")      ? "text/plain"
                                                     : DocumentsContract.Document.MIME_TYPE_DIR;
  }
  public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
    String[] columns = projection == null
        ? new String[] {DocumentsContract.Document.COLUMN_DOCUMENT_ID, OpenableColumns.DISPLAY_NAME,
              DocumentsContract.Document.COLUMN_MIME_TYPE,
              DocumentsContract.Document.COLUMN_LAST_MODIFIED}
        : projection;
    MatrixCursor cursor = new MatrixCursor(columns);
    if ("children".equals(uri.getLastPathSegment())) {
      row(cursor, columns, "new.txt", "new.txt", "text/plain", 4000);
      row(cursor, columns, "Picture.png", "Picture.png", "image/png", 3000);
      row(cursor, columns, "folder", "Folder", DocumentsContract.Document.MIME_TYPE_DIR, 1000);
    } else {
      String id = uri.getLastPathSegment();
      row(cursor, columns, id, id.equals("root") ? "Test folder" : id, getType(uri), 4000);
    }
    return cursor;
  }
  private void row(
      MatrixCursor cursor, String[] columns, String id, String name, String type, long modified) {
    Object[] data = new Object[columns.length];
    for (int i = 0; i < columns.length; i++) {
      String c = columns[i];
      if (c.equals(DocumentsContract.Document.COLUMN_DOCUMENT_ID))
        data[i] = id;
      else if (c.equals(OpenableColumns.DISPLAY_NAME))
        data[i] = name;
      else if (c.equals(DocumentsContract.Document.COLUMN_MIME_TYPE))
        data[i] = type;
      else if (c.equals(DocumentsContract.Document.COLUMN_LAST_MODIFIED))
        data[i] = modified;
      else if (c.equals(OpenableColumns.SIZE))
        data[i] = 14L;
    }
    cursor.addRow(data);
  }
  public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
    if (!"r".equals(mode))
      throw new FileNotFoundException("Read only");
    String id = uri.getLastPathSegment();
    if (!id.equals("new.txt") && !id.equals("Picture.png"))
      throw new FileNotFoundException();
    return ParcelFileDescriptor.open(
        new File(getContext().getFilesDir(), id), ParcelFileDescriptor.MODE_READ_ONLY);
  }
  public Uri insert(Uri uri, ContentValues v) {
    throw new UnsupportedOperationException();
  }
  public int update(Uri u, ContentValues v, String s, String[] a) {
    throw new UnsupportedOperationException();
  }
  public int delete(Uri u, String s, String[] a) {
    throw new UnsupportedOperationException();
  }
}
