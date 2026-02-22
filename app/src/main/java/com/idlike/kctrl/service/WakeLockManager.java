package com.idlike.kctrl.service;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.os.PowerManager;

public class WakeLockManager {
    private PowerManager.WakeLock wakeLock;

    public WakeLockManager(Context context) {
        PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MyApp:WakeLock");
        }
    }

    public void acquire() {
        if (wakeLock != null && !wakeLock.isHeld()) {
            wakeLock.acquire();
        }
    }

    public void release() {
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
    }
}

