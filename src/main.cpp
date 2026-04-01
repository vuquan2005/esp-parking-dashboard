#include "Arduino.h"
#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"
#include <WiFi.h>

// Global Instances
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(webManager, wifiManager);

void setup()
{
    Serial.begin(115200);
    wifiManager.begin();
    webManager.begin();
    parkingHandler.begin();
}

void loop()
{
    static unsigned long lastStatusMillis = 0;
    const unsigned long statusIntervalMs = 10UL * 1000UL; // 10 giây

    unsigned long now = millis();
    if (now - lastStatusMillis >= statusIntervalMs)
    {
        lastStatusMillis = now;
        parkingHandler.sendStatus();
    }

    webManager.loop();
}
