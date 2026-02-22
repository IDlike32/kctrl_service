package com.idlike.kctrl.service;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class WakeLockReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if ("com.example.GET_WAKELOCK".equals(intent.getAction())) {
            WakeLockManager manager = new WakeLockManager(context);
            manager.acquire();
        }
    }
}