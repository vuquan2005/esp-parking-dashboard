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
    parkingHandler.loop();
    webManager.loop();
}
