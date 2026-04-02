#include "Arduino.h"
#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"

// Global Instances
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

void setup() {
    Serial.begin(115200);
    wifiManager.begin();
    webManager.begin();

    // Nối ParkingHandler ↔ WebManager bằng callbacks
    parkingHandler.setSendFn(
        [](const uint8_t *data, size_t len) { webManager.sendBinary(data, len); });
    parkingHandler.setClientCountFn([]() -> size_t { return webManager.clientCount(); });

    // WebManager → ParkingHandler: enqueue vào queue (thread-safe)
    webManager.setOnBinary(
        [](const uint8_t *data, size_t len) { parkingHandler.enqueueBinary(data, len); });
    webManager.setOnConnect([]() {
        // parkingHandler.enqueueClientConnected();
    });

    parkingHandler.begin();
}

void loop() {
    parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();
}
