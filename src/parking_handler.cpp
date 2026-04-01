#include "parking_handler.h"

ParkingHandler::ParkingHandler(WebManager &webManager, WifiManager &wifiManager)
    : _webManager(webManager), _wifiManager(wifiManager) {}

void ParkingHandler::begin()
{
    _webManager.setWsEventCallback(
        [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg,
               uint8_t *data, size_t len)
        { this->onWsEvent(server, client, type, arg, data, len); });
}

// ===== Gửi messages =====

bool ParkingHandler::sendParking(const Parking &msg)
{
    uint8_t buffer[Parking_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, Parking_fields, &msg))
    {
        Serial.printf("[ParkingHandler] Encode failed: %s\n", PB_GET_ERROR(&stream));
        return false;
    }

    _webManager.sendBinary(buffer, stream.bytes_written);
    return true;
}

void ParkingHandler::sendDeviceStatus()
{
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_wifi_status_tag;

    DeviceStatus &status = msg.payload.wifi_status;
    status.connected = WiFi.isConnected();
    status.wifi_mode = (DeviceStatus_WifiMode)WiFi.getMode();

    // STA info
    if (WiFi.isConnected())
    {
        strncpy(status.sta_ssid, WiFi.SSID().c_str(), sizeof(status.sta_ssid) - 1);
        strncpy(status.sta_ip, WiFi.localIP().toString().c_str(), sizeof(status.sta_ip) - 1);
        status.rssi = WiFi.RSSI();
        status.channel = WiFi.channel();
    }

    // AP info
    strncpy(status.ap_ssid, WiFi.softAPSSID().c_str(), sizeof(status.ap_ssid) - 1);
    strncpy(status.ap_ip, WiFi.softAPIP().toString().c_str(), sizeof(status.ap_ip) - 1);

    // System info
    status.free_heap = ESP.getFreeHeap();
    status.min_free_heap = ESP.getMinFreeHeap();
    status.max_free_block_size = ESP.getMaxAllocHeap();
    status.uptime_seconds = millis() / 1000;

    if (sendParking(msg))
    {
        Serial.println("[ParkingHandler] DeviceStatus sent");
    }
}

void ParkingHandler::sendParkingStatus(const ParkingStatus &status)
{
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_parking_status_tag;
    msg.payload.parking_status = status;

    if (sendParking(msg))
    {
        Serial.printf("[ParkingHandler] ParkingStatus sent (%d slots)\n", status.slots_count);
    }
}

void ParkingHandler::sendParkingEvent(const ParkingEvent &event)
{
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_parking_event_tag;
    msg.payload.parking_event = event;

    if (sendParking(msg))
    {
        Serial.printf("[ParkingHandler] ParkingEvent sent (slot=%u, type=%d)\n", event.slot_id,
                      event.event_type);
    }
}

// ===== Nhận và decode messages =====

void ParkingHandler::handleBinaryData(AsyncWebSocketClient *client, uint8_t *data, size_t len)
{
    Parking msg = Parking_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    if (!pb_decode(&stream, Parking_fields, &msg))
    {
        Serial.printf("[ParkingHandler] Decode failed: %s\n", PB_GET_ERROR(&stream));
        return;
    }

    Serial.printf("[ParkingHandler] Received payload type: %d\n", (int)msg.which_payload);

    switch (msg.which_payload)
    {
    case Parking_wifi_scanning_tag:
        handleWifiScanning(client);
        break;

    case Parking_wifi_config_tag:
        handleWifiConfig(client, msg.payload.wifi_config);
        break;

    case Parking_wifi_status_tag:
        sendDeviceStatus();
        break;

    default:
        Serial.printf("[ParkingHandler] Unhandled payload type: %d\n", (int)msg.which_payload);
        break;
    }
}

void ParkingHandler::handleWifiScanning(AsyncWebSocketClient *client)
{
    Serial.println("[ParkingHandler] WiFi scan requested");

    int n = WiFi.scanNetworks();
    if (n < 0)
    {
        Serial.println("[ParkingHandler] WiFi scan failed");
        return;
    }

    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_scan_results_tag;
    ScanResults &results = msg.payload.scan_results;

    int count = min(n, 10);
    results.access_points_count = count;

    for (int i = 0; i < count; i++)
    {
        ScanResults_AP &ap = results.access_points[i];
        strncpy(ap.ssid, WiFi.SSID(i).c_str(), sizeof(ap.ssid) - 1);

        // Copy BSSID (6 bytes MAC)
        uint8_t *bssid = WiFi.BSSID(i);
        if (bssid)
        {
            memcpy(ap.bssid, bssid, 6);
        }

        ap.rssi = WiFi.RSSI(i);
        ap.channel = WiFi.channel(i);
        ap.encryption = (ScanResults_WifiAuthMode)WiFi.encryptionType(i);
    }

    WiFi.scanDelete();

    if (sendParking(msg))
    {
        Serial.printf("[ParkingHandler] ScanResults sent (%d APs)\n", count);
    }
}

void ParkingHandler::handleWifiConfig(AsyncWebSocketClient *client,
                                      const WifiConfig &config)
{
    Serial.printf("[ParkingHandler] WiFi config received - AP: '%s', STA: '%s'\n", config.ap_ssid,
                  config.sta_ssid);

    // Load current prefs as base
    WifiPrefs prefs = _wifiManager.loadPrefs();
    bool apChanged = false;

    // Update AP config if provided
    if (strlen(config.ap_ssid) > 0)
    {
        if (prefs.ap_ssid != config.ap_ssid || prefs.ap_password != config.ap_password)
        {
            apChanged = true;
        }
        prefs.ap_ssid = config.ap_ssid;
        prefs.ap_password = config.ap_password;
    }

    // Update STA config if provided
    if (strlen(config.sta_ssid) > 0)
    {
        prefs.sta_ssid = config.sta_ssid;
        prefs.sta_password = config.sta_password;
    }

    // Save to NVS
    _wifiManager.savePrefs(prefs);

    // Apply AP changes if needed
    if (apChanged)
    {
        Serial.println("[ParkingHandler] AP config changed, restarting AP...");
        _wifiManager.applyApConfig(prefs);
    }

    // Connect STA if STA SSID provided
    if (strlen(config.sta_ssid) > 0)
    {
        _wifiManager.connectSta(config.sta_ssid, config.sta_password);
    }

    sendDeviceStatus();
}

// ===== WebSocket Event Handler =====

void ParkingHandler::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                               AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    switch (type)
    {
    case WS_EVT_CONNECT:
        Serial.printf("[ParkingHandler] Client #%u connected from %s\n", client->id(),
                      client->remoteIP().toString().c_str());
        // Gửi device status cho client mới kết nối
        sendDeviceStatus();
        break;

    case WS_EVT_DISCONNECT:
        Serial.printf("[ParkingHandler] Client #%u disconnected\n", client->id());
        break;

    case WS_EVT_DATA:
    {
        AwsFrameInfo *info = (AwsFrameInfo *)arg;
        if (info->opcode == WS_BINARY && info->final && info->index == 0 && info->len == len)
        {
            // Complete binary frame
            handleBinaryData(client, data, len);
        }
        break;
    }

    case WS_EVT_ERROR:
        Serial.printf("[ParkingHandler] Client #%u error\n", client->id());
        break;

    case WS_EVT_PONG:
        break;
    }
}
