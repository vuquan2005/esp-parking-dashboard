#include "index.minify.h"
#include "websever.h"

const byte DNS_PORT = 53;

WebManager::WebManager() : server(80), ws("/ws"), serverStarted(false), wsCallback(nullptr) {}

void WebManager::begin() {
    Serial.println("[WebManager] Initializing Web Services...");
    Serial.print("[WebManager] Server IP address: ");
    Serial.println(WiFi.softAPIP());

    Serial.println("[WebManager] Starting DNS Server for Captive Portal...");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response =
            request->beginResponse(200, "text/html", html, html_len);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/#/config"); });

    ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                      void *arg, uint8_t *data,
                      size_t len) { this->onWsEvent(server, client, type, arg, data, len); });
    server.addHandler(&ws);

    if (!serverStarted) {
        Serial.println("[WebManager] Starting web server...");
        server.begin();
        serverStarted = true;
    }
}

void WebManager::loop() {
    dnsServer.processNextRequest();
    ws.cleanupClients();
}

void WebManager::setWsEventCallback(WsEventCallback callback) { wsCallback = callback; }

void WebManager::sendBinary(const uint8_t *data, size_t len) { ws.binaryAll(data, len); }

void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                           void *arg, uint8_t *data, size_t len) {
    // Chuyển tiếp event cho callback đã đăng ký (ParkingHandler trong src/)
    if (wsCallback) {
        wsCallback(server, client, type, arg, data, len);
    }
}
