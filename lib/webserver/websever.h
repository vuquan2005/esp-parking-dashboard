#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <functional>

using WsEventCallback = std::function<void(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType,
                                           void *, uint8_t *, size_t)>;

class WebManager {
  public:
    WebManager();
    void begin();
    void loop();

    /// Đăng ký callback xử lý WebSocket event từ bên ngoài (src/)
    void setWsEventCallback(WsEventCallback callback);

    /// Gửi binary data cho tất cả WebSocket clients
    void sendBinary(const uint8_t *data, size_t len);


  private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    DNSServer dnsServer;
    bool serverStarted;
    WsEventCallback wsCallback;

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                   void *arg, uint8_t *data, size_t len);
};
