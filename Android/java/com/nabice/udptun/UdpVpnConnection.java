package com.nabice.udptun;

import android.app.PendingIntent;
import android.content.Intent;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.net.SocketAddress;
import java.net.SocketException;
import java.nio.ByteBuffer;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.DatagramChannel;
import java.util.concurrent.TimeUnit;

class UdpVpnConnection implements Runnable {
    private final VpnService mService;
    private final String mServer;
    private final String mClient;
    private final String mDNS;
    private PendingIntent mConfigureIntent;
    private OnEstablishListener mOnEstablishListener;
    private static final int MAX_PACKET_SIZE = Short.MAX_VALUE;
    public Thread timer;
    public Thread send;
    public Thread recv;

    UdpVpnConnection(UdpVpnService service, String server, String client, String dns) {
        mService = service;
        mServer = server;
        mClient= client;
        mDNS = dns;
    }

    public interface OnEstablishListener {
        void onEstablish(ParcelFileDescriptor tunInterface);
    }

    public void setConfigureIntent(PendingIntent intent) {
        mConfigureIntent = intent;
    }

    public void setOnEstablishListener(OnEstablishListener listener) {
        mOnEstablishListener = listener;
    }

    @Override
    public void run() {
        try {
            Log.i(getTag(), "Starting");
            final SocketAddress serverAddress = new InetSocketAddress(mServer, 51122);
            run(serverAddress);
        } catch (IOException | IllegalArgumentException e) {
            panic("Connection failed, exiting");
        }
    }

    private void panic(String msg) {
        if(msg != null) {
            Log.e(getTag(), msg);
        }
        ((UdpVpnService)mService).disconnect();
    }
    private FileInputStream in;
    private FileOutputStream out;
    private DatagramChannel tunnel;
    private boolean ip_done = false;
    private int idle = 1;
    private void run(SocketAddress server)
            throws IOException, IllegalArgumentException {
        try {
            tunnel = DatagramChannel.open();
            if (!mService.protect(tunnel.socket())) {
                throw new IllegalStateException("Cannot protect the tunnel");
            }

            tunnel.connect(server);

            final byte[] bytes = {'N', 'U', 'D', 'P', 'N', 'C', 0, 0, 0, 0};
            ByteBuffer packet_cmd = ByteBuffer.wrap(bytes);
            tunnel.write(packet_cmd);
            packet_cmd.clear();
            timer = new Thread(() -> {
                try {
                    TimeUnit.SECONDS.sleep(10);
                } catch (InterruptedException e) {
                    return;
                }
                if(!ip_done){
                    panic("Can not connect to the server");
                }
                while (true) {
                    if(Thread.interrupted()) {
                        return;
                    }
                    try {
                        TimeUnit.MINUTES.sleep(10);
                    } catch (InterruptedException e) {
                        return;
                    }
                    if (idle > 4) {
                        panic("Server timeout");
                    }
                    idle++;
                    bytes[5] = 'L';
                    ByteBuffer packet_c = ByteBuffer.wrap(bytes);
                    try {
                        tunnel.write(packet_c);
                    } catch (IOException e) {
                        panic("Keepalive failed");
                    }
                    packet_c.clear();
                }
            });
            timer.start();

            recv = new Thread(() -> {
                ByteBuffer packet_out = ByteBuffer.allocate(MAX_PACKET_SIZE);
                while (true) {
                    if(Thread.interrupted()) {
                        break;
                    }
                    try {
                        packet_out.position(0);
                        int length = tunnel.read(packet_out);
                        if (length <= 10 && packet_out.get(0) == 'N' && packet_out.get(1) == 'U' && packet_out.get(2) == 'D' && packet_out.get(3) == 'P' && packet_out.get(4) == 'N') {
                            if (packet_out.get(5) == 'S') {
                                VpnService.Builder builder = mService.new Builder();
                                ParcelFileDescriptor iface;
                                builder.setBlocking(true);
                                builder.addAddress(mClient + String.valueOf(packet_out.get(6) + 2), 24);
                                if(mDNS.length() != 0) {
                                    builder.addDnsServer(mDNS);
                                }
                                builder.addRoute("0.0.0.0", 0);
                                builder.setMtu(1440);
                                synchronized (mService) {
                                    iface = builder
                                            .setSession(mServer)
                                            .setConfigureIntent(mConfigureIntent)
                                            .establish();
                                    if (mOnEstablishListener != null) {
                                        mOnEstablishListener.onEstablish(iface);
                                    }
                                }
                                in = new FileInputStream(iface.getFileDescriptor());
                                out = new FileOutputStream(iface.getFileDescriptor());
                                send = new Thread(() -> {
                                    byte[] from_tun = new byte[MAX_PACKET_SIZE];
                                    while (true) {
                                        if(Thread.interrupted()) {
                                            return;
                                        }
                                        try {
                                            int length_in = in.read(from_tun);;
                                            if (length_in > 0) {
                                                encrypt(from_tun, length_in);
                                                ByteBuffer packet_in = ByteBuffer.wrap(from_tun);
                                                packet_in.limit(length_in);
                                                tunnel.write(packet_in);
                                                packet_in.clear();
                                            }
                                        } catch (IOException e) {
                                            panic("Unable to write to server");
                                        }
                                    }
                                });
                                send.start();
                                ip_done = true;
                            } else if (packet_out.get(5) == 'L') {
                                Log.i(getTag(), "Keep alive");
                                //server received our keepalive packet, then sent us one
                            } else if (packet_out.get(5) == 'F') {
                                panic("Too many clients");
                            } else if (packet_out.get(5) == 'K') {
                                bytes[5] = 'C';
                                ByteBuffer packet_c = ByteBuffer.wrap(bytes);
                                tunnel.write(packet_c);
                                packet_c.clear();
                            }
                        } else if (length > 0) {
                            byte[] from_net = packet_out.array();
                            decrypt(from_net, length);
                            out.write(from_net, 0, length);
                            packet_out.clear();
                        }
                        idle = 1;
                    } catch (ClosedChannelException cce) {
                        break;
                    } catch (IOException e) {
                        panic("Unable to write to tun");
                    }
                }
            });
            recv.start();
            while (true) {
                if(Thread.interrupted()) {
                    send.interrupt();
                    timer.interrupt();
                    recv.interrupt();
                    break;
                }
                TimeUnit.SECONDS.sleep(10);
            }
        } catch (SocketException e) {
            panic("Cannot use socket");
        } catch (InterruptedException e) {
            send.interrupt();
            timer.interrupt();
            recv.interrupt();
        }

    }

    private void encrypt(byte[] in, int len) {
    }

    private void decrypt(byte[] in, int len) {
    }

    private String getTag() {
        return "UdpVpnConnection";
    }
}
