package pl.aridlin.kalwer;

import android.content.*;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.*;

/** Exposes only the verified APK, read-only, with temporary installer URI grants. */
public final class UpdateProvider extends ContentProvider {
    static Uri uri(Context c){return Uri.parse("content://"+c.getPackageName()+".updates/update.apk");}
    private File file(Uri uri){if(!uri(getContext()).equals(uri))throw new IllegalArgumentException("Unknown update URI");return AppUpdates.file(getContext());}
    @Override public boolean onCreate(){return true;}
    @Override public String getType(Uri uri){file(uri);return "application/vnd.android.package-archive";}
    @Override public ParcelFileDescriptor openFile(Uri uri,String mode)throws FileNotFoundException{if(!"r".equals(mode))throw new FileNotFoundException("Read only");return ParcelFileDescriptor.open(file(uri),ParcelFileDescriptor.MODE_READ_ONLY);}
    @Override public Cursor query(Uri uri,String[] projection,String selection,String[] args,String sort){File f=file(uri);String[] columns=projection!=null?projection:new String[]{OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE};MatrixCursor cursor=new MatrixCursor(columns);Object[] row=new Object[columns.length];for(int i=0;i<columns.length;i++)row[i]=OpenableColumns.DISPLAY_NAME.equals(columns[i])?"kalwer-update.apk":OpenableColumns.SIZE.equals(columns[i])?f.length():null;cursor.addRow(row);return cursor;}
    @Override public Uri insert(Uri uri,ContentValues values){throw new UnsupportedOperationException();}
    @Override public int delete(Uri uri,String where,String[] args){throw new UnsupportedOperationException();}
    @Override public int update(Uri uri,ContentValues values,String where,String[] args){throw new UnsupportedOperationException();}
}
