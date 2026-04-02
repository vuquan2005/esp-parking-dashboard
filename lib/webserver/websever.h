#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <functional>

/// Callback khi nhận binary data từ WebSocket (data + len)
using WsBinaryCallback = std::function<void(const uint8_t *, size_t)>;

/// Callback khi client connect
using WsConnectCallback = std::function<void()>;

class WebManager
{
public:
    WebManager();
    void begin();
    void loop();

    /// Đăng ký callback khi nhận binary data
    void setOnBinary(WsBinaryCallback callback);

    /// Đăng ký callback khi client kết nối
    void setOnConnect(WsConnectCallback callback);

    /// Gửi binary data cho tất cả WebSocket clients
    void sendBinary(const uint8_t *data, size_t len);

    /// Trả về số lượng client đang kết nối
    size_t clientCount() const;

private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    DNSServer dnsServer;
    bool serverStarted;
    WsBinaryCallback onBinaryCallback;
    WsConnectCallback onConnectCallback;

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                   void *arg, uint8_t *data, size_t len);
};
