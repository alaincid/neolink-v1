#include "preferences_store.h"
#include "config.h"
#include <Preferences.h>

static Preferences s_p;

void prefs_init() {
    s_p.begin("neolink", false);
}

String prefs_wifi_ssid() { return s_p.getString("wifi_ssid", ""); }
String prefs_wifi_pass() { return s_p.getString("wifi_pass", ""); }

void prefs_save_wifi(const char *ssid, const char *pass) {
    s_p.putString("wifi_ssid", ssid);
    s_p.putString("wifi_pass", pass);
}

void prefs_clear_wifi() {
    s_p.remove("wifi_ssid");
    s_p.remove("wifi_pass");
}

String prefs_device_name() { return s_p.getString("dev_name", DEVICE_ID); }
String prefs_server_host() { return s_p.getString("srv_host", BACKEND_HOST); }
int    prefs_server_port() { return s_p.getInt("srv_port", BACKEND_PORT); }

void prefs_save_device(const char *name, const char *host, int port) {
    s_p.putString("dev_name", name);
    s_p.putString("srv_host", host);
    s_p.putInt("srv_port",  port);
}
