#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

/// Cấu hình WiFi lưu trong NVS
struct WifiPrefs
{
    String ap_ssid;
    String ap_password;
    String sta_ssid;
    String sta_password;
};

class WifiManager
{
public:
    WifiManager();

    /// Khởi tạo và thiết lập WiFi (AP, STA nếu có)
    void begin();

    /// Load WiFi config từ Preferences (NVS)
    WifiPrefs loadPrefs();

    /// Save WiFi config vào Preferences (NVS)
    void savePrefs(const WifiPrefs &prefs);

    /// Áp dụng config WiFi AP (restart AP với config mới)
    void applyApConfig(const WifiPrefs &prefs);

    /// Kết nối tới STA mới
    void connectSta(const String &ssid, const String &password);

private:
    Preferences preferences;
};
