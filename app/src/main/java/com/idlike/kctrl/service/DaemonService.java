package com.idlike.kctrl.service;

import android.os.RemoteException;
import android.util.Log;

import com.idlike.kctrl.service.IUserService;

public class DaemonService extends IUserService.Stub {
    private static final String TAG = "DaemonService";

    @Override
    public void startBinary(String binaryPath, String[] args) throws RemoteException {
        Log.i(TAG, "startBinary called: " + binaryPath);

        new Thread(() -> {
            try {
                String[] cmd = new String[args.length + 1];
                cmd[0] = binaryPath;
                System.arraycopy(args, 0, cmd, 1, args.length);

                ProcessBuilder pb = new ProcessBuilder(cmd);
                pb.inheritIO();

                Process process = pb.start();
                Log.i(TAG, "Binary started: " + binaryPath);

                int exitCode = process.waitFor();
                Log.i(TAG, "Binary exited with code: " + exitCode);

            } catch (Exception e) {
                Log.e(TAG, "Error starting binary", e);
            }
        }, "BinaryExecutor").start();
    }

    @Override
    public void destroy() {
        Log.i(TAG, "destroy");
        System.exit(0);
    }

    @Override
    public void exit() {
        destroy();
    }
}