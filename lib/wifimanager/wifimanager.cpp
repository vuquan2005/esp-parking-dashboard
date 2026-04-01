#include "wifimanager.h"

WifiManager::WifiManager() {}

void WifiManager::begin()
{
    Serial.println("\n[WifiManager] Initializing Access Point (AP)...");

    // Load config from Preferences (NVS)
    WifiPrefs prefs = loadPrefs();

    // Set WiFi mode
    WiFi.mode(WIFI_AP_STA);

    const char *pass = prefs.ap_password.length() >= 8 ? prefs.ap_password.c_str() : nullptr;

    int channel = WiFi.channel();
    if (channel == 0)
        channel = 1; // Default to 1 if no active WiFi interface

    // Start AP with config from Preferences
    if (WiFi.softAP(prefs.ap_ssid.c_str(), pass, channel, 0, 4))
    {
        Serial.printf("[WifiManager] AP Network '%s' Started (CH=%d)\n", prefs.ap_ssid.c_str(), channel);
        Serial.printf("[WifiManager] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
    else
    {
        Serial.println("[WifiManager] ERROR: Failed to start AP network.");
    }

    // Auto-connect STA nếu có config đã lưu
    if (prefs.sta_ssid.length() > 0)
    {
        connectSta(prefs.sta_ssid, prefs.sta_password);
    }
}

WifiPrefs WifiManager::loadPrefs()
{
    WifiPrefs prefs;
    preferences.begin("wifi", false);
    prefs.ap_ssid = preferences.getString("ap_ssid", "ESP32-Dashboard");
    prefs.ap_password = preferences.getString("ap_pass", "");
    prefs.sta_ssid = preferences.getString("sta_ssid", "");
    prefs.sta_password = preferences.getString("sta_pass", "");
    preferences.end();

    Serial.printf("[WifiManager] Loaded prefs - AP: %s, STA: %s\n",
                  prefs.ap_ssid.c_str(), prefs.sta_ssid.c_str());
    return prefs;
}

void WifiManager::savePrefs(const WifiPrefs &prefs)
{
    preferences.begin("wifi", false);
    preferences.putString("ap_ssid", prefs.ap_ssid);
    preferences.putString("ap_pass", prefs.ap_password);
    preferences.putString("sta_ssid", prefs.sta_ssid);
    preferences.putString("sta_pass", prefs.sta_password);
    preferences.end();

    Serial.printf("[WifiManager] Saved prefs - AP: %s, STA: %s\n",
                  prefs.ap_ssid.c_str(), prefs.sta_ssid.c_str());
}

void WifiManager::applyApConfig(const WifiPrefs &prefs)
{
    Serial.println("[WifiManager] Applying new AP config...");

    WiFi.softAPdisconnect(true);
    delay(100);

    const char *pass = prefs.ap_password.length() >= 8 ? prefs.ap_password.c_str() : nullptr;

    if (WiFi.softAP(prefs.ap_ssid.c_str(), pass))
    {
        Serial.printf("[WifiManager] AP restarted: SSID='%s'\n", prefs.ap_ssid.c_str());
    }
    else
    {
        Serial.println("[WifiManager] ERROR: Failed to restart AP!");
    }
}

void WifiManager::connectSta(const String &ssid, const String &password)
{
    Serial.printf("[WifiManager] Connecting STA to '%s'...\n", ssid.c_str());
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), password.c_str());
}
