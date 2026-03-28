#include "Arduino.h"
#include "websever.h" // WebManager for Captive Portal & Dashboard
#include <WiFi.h>

// ===== AP Configuration =====
const char *ap_ssid = "ESP32-Dashboard";
const char *ap_password = "";
const int ap_channel = 1;
const int ap_max_conn = 4;

// IP Configuration for AP
IPAddress local_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// Global Instances
WebManager webManager;

void setupAP() {
    Serial.println("\n[System] Initializing Access Point (AP)...");

    // Set WiFi mode to Access Point
    WiFi.mode(WIFI_AP);

    // Configure IP Addresses
    WiFi.softAPConfig(local_ip, gateway, subnet);

    // Start AP with custom configuration
    if (WiFi.softAP(ap_ssid, ap_password, ap_channel, 0, ap_max_conn)) {
        Serial.print("[System] AP Network '");
        Serial.print(ap_ssid);
        Serial.println("' Started Successfully.");
        Serial.print("[System] AP IP address: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("[System] ERROR: Failed to start AP network.");
        
    }
}

void setup() {
    Serial.begin(115200);
    setupAP();
    webManager.begin();
}

void loop() {
    webManager.loop();
}
