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

    /// Gửi status tổng quát cho client
    void sendStatus();

    /// Gửi ParkingStatus cho tất cả client
    void sendParkingStatus(const ParkingStatus &status);

    /// Gửi ParkingEvent cho tất cả client
    void sendParkingEvent(const ParkingEvent &event);

    /// Vòng lặp để kiểm tra scan async
    void loop();

private:
    WebManager &_webManager;
    WifiManager &_wifiManager;

    bool _scanInProgress = false;
    unsigned long _lastStatusMillis = 0;
    static constexpr unsigned long _statusIntervalMs = 10UL * 1000UL; // 10 giây

    /// Encode và gửi message Parking qua WebSocket
    bool sendParking(const Parking &msg);

    /// Xử lý kết quả scan WiFi async
    void sendWifiScanResults(int count);

    /// Xử lý binary data nhận từ WebSocket
    void handleBinaryData(AsyncWebSocketClient *client, uint8_t *data, size_t len);

    /// Xử lý từng loại payload
    void handleWifiScanning(AsyncWebSocketClient *client);
    void handleWifiConfig(AsyncWebSocketClient *client, const WifiConfig &config);

    /// WebSocket event callback
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                   void *arg, uint8_t *data, size_t len);
};
