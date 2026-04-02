#include "index.minify.h"
#include "websever.h"

const byte DNS_PORT = 53;

WebManager::WebManager()
    : server(80), ws("/ws"), serverStarted(false), onBinaryCallback(nullptr),
      onConnectCallback(nullptr) {}

void WebManager::begin() {
    Serial.println("[WebManager] Initializing Web Services...");
    Serial.print("[WebManager] Server IP address: ");
    Serial.println(WiFi.softAPIP());

    Serial.println("[WebManager] Starting DNS Server for Captive Portal...");
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", html, html_len);
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

void WebManager::setOnBinary(WsBinaryCallback callback) {
    onBinaryCallback = callback;
}
void WebManager::setOnConnect(WsConnectCallback callback) {
    onConnectCallback = callback;
}

void WebManager::sendBinary(const uint8_t *data, size_t len) {
    ws.binaryAll(data, len);
}

size_t WebManager::clientCount() const {
    return ws.count();
}

void WebManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
                           void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
        Serial.printf("[WebManager] Client #%u connected from %s\n", client->id(),
                      client->remoteIP().toString().c_str());
        if (onConnectCallback)
            onConnectCallback();
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("[WebManager] Client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA: {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->opcode == WS_BINARY && info->final && info->index == 0 && info->len == len) {
            if (onBinaryCallback)
                onBinaryCallback(data, len);
        }
        break;
    }

    case WS_EVT_ERROR:
        Serial.printf("[WebManager] Client #%u error\n", client->id());
        break;

    case WS_EVT_PONG:
        break;
    }
}
