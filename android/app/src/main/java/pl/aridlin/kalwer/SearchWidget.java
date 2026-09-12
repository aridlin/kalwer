package pl.aridlin.kalwer;

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.widget.RemoteViews;

public final class SearchWidget extends AppWidgetProvider {
    static final String SEARCH="pl.aridlin.kalwer.WIDGET_SEARCH";
    static Intent searchIntent(Context context){return new Intent(context,MainActivity.class).setAction(SEARCH).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK|Intent.FLAG_ACTIVITY_CLEAR_TOP);}
    static RemoteViews views(Context context){
        RemoteViews views=new RemoteViews(context.getPackageName(),R.layout.search_widget);
        views.setOnClickPendingIntent(R.id.widget_search,PendingIntent.getActivity(context,0,searchIntent(context),PendingIntent.FLAG_UPDATE_CURRENT|PendingIntent.FLAG_IMMUTABLE));
        return views;
    }
    @Override public void onUpdate(Context context,AppWidgetManager manager,int[] ids){for(int id:ids)manager.updateAppWidget(id,views(context));}
}
