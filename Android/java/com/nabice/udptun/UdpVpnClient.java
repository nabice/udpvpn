package com.nabice.udptun;

import android.content.Intent;
import android.content.SharedPreferences;
import android.net.VpnService;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.widget.TextView;

public class UdpVpnClient extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        final TextView server = findViewById(R.id.server);
        final TextView client = findViewById(R.id.client);
        final TextView dns = findViewById(R.id.dns);

        final SharedPreferences prefs = getSharedPreferences("connection", MODE_PRIVATE);
        findViewById(R.id.connect).setOnClickListener(v -> {
            prefs.edit()
                    .putString("server", server.getText().toString())
                    .putString("client", client.getText().toString())
                    .putString("dns", dns.getText().toString())
                    .commit();

            Intent intent = VpnService.prepare(UdpVpnClient.this);
            if (intent != null) {
                startActivityForResult(intent, 0);
            } else {
                onActivityResult(0, RESULT_OK, null);
            }
        });
        findViewById(R.id.disconnect).setOnClickListener(v -> startService(getServiceIntent().setAction(UdpVpnService.ACTION_DISCONNECT)));
        findViewById(R.id.myhome).setOnClickListener(v -> {
            server.setText("server1.com");
            client.setText("172.25.1.");
            dns.setText("8.8.8.8");

        });
        findViewById(R.id.goout).setOnClickListener(v -> {
            server.setText("server2.com");
            client.setText("172.25.2.");
            dns.setText("8.8.8.8");

        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (result == RESULT_OK) {
            startService(getServiceIntent().setAction(UdpVpnService.ACTION_CONNECT));
        }
    }

    private Intent getServiceIntent() {
        return new Intent(this, UdpVpnService.class);
    }
}
