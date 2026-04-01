#pragma once

#include <Arduino.h>
#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "parking.pb.h"
#include "websever.h"
#include "wifimanager.h"

/**
 * ParkingHandler - Xử lý encode/decode protobuf qua WebSocket
 *
 * File này nằm trong src/ (không phải lib/) vì parking.pb.h được
 * nanopb generate trong quá trình build, và lib có thể biên dịch
 * trước khi file này tồn tại.
 */
class ParkingHandler
{
public:
    ParkingHandler(WebManager &webManager, WifiManager &wifiManager);

    /// Đăng ký WebSocket event handler vào WebManager
    void begin();

    /// Gửi DeviceStatus cho tất cả client
    void sendDeviceStatus();

    /// Gửi ParkingStatus cho tất cả client
    void sendParkingStatus(const ParkingStatus &status);

    /// Gửi ParkingEvent cho tất cả client
    void sendParkingEvent(const ParkingEvent &event);

private:
    WebManager &_webManager;
    WifiManager &_wifiManager;

    /// Encode và gửi message Parking qua WebSocket
    bool sendParking(const Parking &msg);

    /// Xử lý binary data nhận từ WebSocket
    void handleBinaryData(AsyncWebSocketClient *client, uint8_t *data, size_t len);

    /// Xử lý từng loại payload
    void handleWifiScanning(AsyncWebSocketClient *client);
    void handleWifiConfig(AsyncWebSocketClient *client, const WifiConfig &config);

    /// WebSocket event callback
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                   void *arg, uint8_t *data, size_t len);
};
