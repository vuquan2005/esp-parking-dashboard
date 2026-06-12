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
        if (request->hasHeader("If-None-Match") &&
            request->header("If-None-Match") == ETAG_STRING) {
            request->send(304);
            return;
        }

        AsyncWebServerResponse *response = request->beginChunkedResponse(
            "text/html", [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                if (index >= html_len) {
                    return 0;
                }

                size_t chunkSize = maxLen;
                if (index + chunkSize > html_len) {
                    chunkSize = html_len - index;
                }

                memcpy_P(buffer, html + index, chunkSize);
                return chunkSize;
            });

        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache");
        response->addHeader("ETag", ETAG_STRING);

        request->send(response);
    });

    // ── Handle captive portal and other special URLs
    server.onNotFound([](AsyncWebServerRequest *request) {
        String url = request->url();
        Serial.printf("[WebManager] Received request for URL: %s\n", url.c_str());
        String targetUrl = String("http://") + WiFi.softAPIP().toString() + "/";

        if (url == "/generate_204" || url == "/gen_204") {
            request->send(204);
            return;
        }

        if (url == "/hotspot-detect.html" || url == "/library/test/success.html" || url == "/success.html") {
            const char *APPLE_SUCCESS = "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
                                        "<BODY>Success</BODY></HTML>";
            request->send(200, "text/html", APPLE_SUCCESS);
            // request->redirect(targetUrl);
            return;
        }

        if (url == "/connecttest.txt" || url == "/ncsi.txt") {
            request->redirect(targetUrl);
            return;
        }

        if (url == "/redirect") {
            AsyncWebServerResponse *r = request->beginResponse(200, "text/plain", "");
            r->addHeader("Cache-Control", "no-cache");
            request->send(r);
            return;
        }

        if (url == "/check_network_status.txt" || url == "/nm") {
            request->send(200, "text/plain", "NetworkManager is online\n");
            return;
        }

        request->redirect(targetUrl);
    });

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
