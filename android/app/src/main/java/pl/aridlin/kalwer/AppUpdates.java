package pl.aridlin.kalwer;

import android.app.AlertDialog;
import android.content.*;
import android.content.pm.*;
import android.net.Uri;
import android.provider.Settings;
import android.widget.Toast;
import org.json.*;
import java.io.*;
import java.net.*;
import java.nio.file.Files;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;

final class AppUpdates {
    private static final AtomicBoolean busy=new AtomicBoolean();
    private final MainActivity activity;
    private boolean permissionPending,offered;
    AppUpdates(MainActivity activity){this.activity=activity;}
    static File file(Context c){return new File(c.getFilesDir(),"updates/update.apk");}
    static int compare(String a,String b){String[] x=a.split("\\."),y=b.split("\\.");for(int i=0;i<Math.max(x.length,y.length);i++){int d=Integer.compare(i<x.length?Integer.parseInt(x[i]):0,i<y.length?Integer.parseInt(y[i]):0);if(d!=0)return d;}return 0;}
    static final class Release {String version,url,checksum;}
    static Release select(JSONArray releases,String current,boolean manual)throws JSONException{
        Release best=null;
        for(int i=0;i<releases.length();i++){
            JSONObject release=releases.getJSONObject(i);if(release.optBoolean("draft") || (!manual && release.optBoolean("prerelease")))continue;
            JSONArray assets=release.optJSONArray("assets");if(assets==null)continue;
            for(int j=0;j<assets.length();j++){
                JSONObject asset=assets.getJSONObject(j);String name=asset.optString("name");
                if(!name.matches("kalwer-android-[0-9]{1,6}\\.[0-9]{1,6}\\.[0-9]{1,6}\\.apk"))continue;
                String version=name.substring(15,name.length()-4);if(compare(version,current)<=0 || (best!=null && compare(version,best.version)<=0))continue;
                for(int k=0;k<assets.length();k++){JSONObject sum=assets.getJSONObject(k);if(sum.optString("name").equals(name+".sha256")){
                    String url=asset.optString("browser_download_url"),checksum=sum.optString("browser_download_url");
                    if(!url.startsWith("https://github.com/aridlin/kalwer/releases/download/") || !checksum.startsWith("https://github.com/aridlin/kalwer/releases/download/"))continue;
                    best=new Release();best.version=version;best.url=url;best.checksum=checksum;break;
                }}
            }
        }return best;
    }
    static void download(String url,OutputStream out,int limit)throws IOException{
        for(int redirects=0;redirects<6;redirects++){
            URL target=new URL(url);if(!"https".equals(target.getProtocol()))throw new IOException("HTTPS required");
            HttpURLConnection c=(HttpURLConnection)target.openConnection();c.setConnectTimeout(15000);c.setReadTimeout(30000);c.setInstanceFollowRedirects(false);c.setRequestProperty("User-Agent","Kalwer-Android");
            try{int status=c.getResponseCode();if(status>=300 && status<400){String location=c.getHeaderField("Location");if(location==null)throw new IOException("Missing redirect");url=new URL(target,location).toString();continue;}if(status!=200)throw new IOException("HTTP "+status);
                try(InputStream in=c.getInputStream()){byte[] buffer=new byte[32768];int n,total=0;while((n=in.read(buffer))!=-1){total+=n;if(total>limit)throw new IOException("Download too large");out.write(buffer,0,n);}}return;
            }finally{c.disconnect();}
        }throw new IOException("Too many redirects");
    }
    private static String fetch(String url,int limit)throws IOException{ByteArrayOutputStream out=new ByteArrayOutputStream();download(url,out,limit);return out.toString("UTF-8");}
    static String hash(File file)throws Exception{MessageDigest digest=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] b=new byte[32768];int n;while((n=in.read(b))!=-1)digest.update(b,0,n);}StringBuilder out=new StringBuilder();for(byte b:digest.digest())out.append(String.format(java.util.Locale.ROOT,"%02x",b&255));return out.toString();}
    @SuppressWarnings("deprecation")
    static boolean valid(Context c,File apk){try{PackageManager pm=c.getPackageManager();PackageInfo installed=pm.getPackageInfo(c.getPackageName(),PackageManager.GET_SIGNATURES),candidate=pm.getPackageArchiveInfo(apk.getPath(),PackageManager.GET_SIGNATURES);return candidate!=null && c.getPackageName().equals(candidate.packageName) && candidate.versionCode>installed.versionCode && candidate.signatures!=null && candidate.signatures.length>0 && Arrays.equals(candidate.signatures,installed.signatures);}catch(Exception e){return false;}}
    void resume(){
        if(permissionPending){permissionPending=false;if(activity.getPackageManager().canRequestPackageInstalls())install();return;}
        activity.getWindow().getDecorView().postDelayed(()->{if(activity.updatesVisible())check(false);},1800);
    }
    void check(boolean manual){
        Context app=activity.getApplicationContext();SharedPreferences prefs=app.getSharedPreferences("kalwer",0);
        long age=System.currentTimeMillis()-prefs.getLong("update_check",0);
        if(!manual && offered)return;
        if(!manual && !file(app).exists() && age>=0 && age<6*60*60*1000L)return;
        if(!busy.compareAndSet(false,true)){if(manual)Toast.makeText(activity,"Update check already running",Toast.LENGTH_SHORT).show();return;}
        prefs.edit().putLong("update_check",System.currentTimeMillis()).apply();
        if(manual)Toast.makeText(activity,"Checking for updates…",Toast.LENGTH_SHORT).show();
        new Thread(()->{
            String message=null;boolean ready=false;
            try{
                File target=file(app);
                if(valid(app,target))ready=true;
                else{
                    target.delete();String current=app.getPackageManager().getPackageInfo(app.getPackageName(),0).versionName;
                    Release release=select(new JSONArray(fetch("https://api.github.com/repos/aridlin/kalwer/releases?per_page=100",2*1024*1024)),current,manual);
                    if(release!=null){
                        String expected=fetch(release.checksum,4096).trim().split("\\s+")[0];if(!expected.matches("[a-fA-F0-9]{64}"))throw new IOException("Invalid checksum");
                        target.getParentFile().mkdirs();File part=new File(target.getParentFile(),"update.part");
                        try{try(OutputStream out=new FileOutputStream(part)){download(release.url,out,64*1024*1024);}if(!expected.equalsIgnoreCase(hash(part)) || !valid(app,part))throw new IOException("Update verification failed");Files.move(part.toPath(),target.toPath(),java.nio.file.StandardCopyOption.REPLACE_EXISTING);ready=true;}finally{part.delete();}
                    }else message="Kalwer is up to date.";
                    prefs.edit().putLong("update_check",System.currentTimeMillis()).apply();
                }
            }catch(Exception e){message="Could not update: "+e.getMessage();}
            finally{busy.set(false);}
            final boolean downloaded=ready;final String status=message;
            activity.runOnUiThread(()->{if(!activity.updatesVisible())return;if(downloaded){offered=true;new AlertDialog.Builder(activity).setTitle("Kalwer update ready").setMessage("The verified update is downloaded. Install it now?").setPositiveButton("Install",(d,w)->install()).setNegativeButton("Later",null).show();}else if(manual)Toast.makeText(activity,status,Toast.LENGTH_LONG).show();});
        },"kalwer-updates").start();
    }
    private void install(){
        if(!valid(activity,file(activity))){Toast.makeText(activity,"Update is no longer available; run /updates",Toast.LENGTH_LONG).show();return;}
        try{
            if(!activity.getPackageManager().canRequestPackageInstalls()){permissionPending=true;activity.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+activity.getPackageName())));return;}
            Uri uri=UpdateProvider.uri(activity);Intent intent=new Intent(Intent.ACTION_VIEW).setDataAndType(uri,"application/vnd.android.package-archive").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);intent.setClipData(ClipData.newRawUri("Kalwer update",uri));activity.startActivity(intent);
        }catch(RuntimeException e){permissionPending=false;Toast.makeText(activity,"Could not open Android's installer",Toast.LENGTH_LONG).show();}
    }
}
