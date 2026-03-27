#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

class WebManager {
  public:
    WebManager();
    void begin();
    void loop();

  private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    DNSServer dnsServer;
    bool serverStarted;

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                   void *arg, uint8_t *data, size_t len);
};
