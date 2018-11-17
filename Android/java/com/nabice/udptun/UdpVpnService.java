package com.nabice.udptun;

import android.app.PendingIntent;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.util.Pair;

import java.io.IOException;
import java.util.concurrent.atomic.AtomicReference;

public class UdpVpnService extends VpnService {
    public static final String ACTION_CONNECT = "com.nabice.udptun.START";
    public static final String ACTION_DISCONNECT = "com.nabice.udptun.STOP";
    private PendingIntent mConfigureIntent;
    private final AtomicReference<Thread> mConnectingThread = new AtomicReference<>();
    private static class Connection extends Pair<Thread, ParcelFileDescriptor> {
        public Connection(Thread thread, ParcelFileDescriptor pfd) {
            super(thread, pfd);
        }
    }
    private final AtomicReference<Connection> mConnection = new AtomicReference<>();

    @Override
    public void onCreate() {
        // Create the intent to "configure" the connection (just start ToyVpnClient).
        mConfigureIntent = PendingIntent.getActivity(this, 0, new Intent(this, UdpVpnClient.class),
                PendingIntent.FLAG_UPDATE_CURRENT);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_DISCONNECT.equals(intent.getAction())) {
            disconnect();
            return START_NOT_STICKY;
        } else {
            connect();
            return START_STICKY;
        }
    }

    @Override
    public void onDestroy() {
        disconnect();
    }

    private void connect() {
        // Extract information from the shared preferences.
        final SharedPreferences prefs = getSharedPreferences("connection", MODE_PRIVATE);
        final String server = prefs.getString("server", "");
        final String client = prefs.getString("client", "");
        final String dns = prefs.getString("dns", "");
        startConnection(new UdpVpnConnection(this, server, client, dns));
    }

    private void disconnect() {
        setConnectingThread(null);
        setConnection(null);
        stopForeground(true);
        stopService(new Intent(this, UdpVpnService.class));
    }

    private void startConnection(final UdpVpnConnection connection) {
        // Replace any existing connecting thread with the  new one.
        final Thread thread = new Thread(connection, "UdpVpnThread");
        setConnectingThread(thread);

        // Handler to mark as connected once onEstablish is called.
        connection.setConfigureIntent(mConfigureIntent);
        connection.setOnEstablishListener(tunInterface -> {
            mConnectingThread.compareAndSet(thread, null);
            setConnection(new Connection(thread, tunInterface));
        });
        thread.start();
    }

    private void setConnectingThread(final Thread thread) {
        final Thread oldThread = mConnectingThread.getAndSet(thread);
        if (oldThread != null) {
            oldThread.interrupt();
        }
    }

    private void setConnection(final Connection connection) {
        final Connection oldConnection = mConnection.getAndSet(connection);
        if (oldConnection != null) {
            try {
                oldConnection.first.interrupt();
                oldConnection.second.close();
            } catch (IOException e) {
                Log.e("UdpVpnService", "Closing VPN interface", e);
            }
        }
    }
}
