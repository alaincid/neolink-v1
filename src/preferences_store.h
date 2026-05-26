#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — preferences_store.h
//  Wrapper de Preferences (NVS flash)
// ─────────────────────────────────────────────

void   prefs_init();

// WiFi
String prefs_wifi_ssid();
String prefs_wifi_pass();
void   prefs_save_wifi(const char *ssid, const char *pass);
void   prefs_clear_wifi();

// Device / backend
String prefs_device_name();
String prefs_server_host();
int    prefs_server_port();
void   prefs_save_device(const char *name, const char *host, int port);
