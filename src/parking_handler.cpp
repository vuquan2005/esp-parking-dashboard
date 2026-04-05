#include "parking_handler.h"

ParkingHandler::ParkingHandler(WifiManager &wifiManager)
    : _wifiManager(wifiManager), _sendBinary(nullptr), _clientCount(nullptr), _cmdQueue(nullptr) {}

void ParkingHandler::begin() {
    _cmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(CmdData));
    if (!_cmdQueue) {
        Serial.println("[ParkingHandler] ERROR: Failed to create command queue!");
    }
}

void ParkingHandler::setSendFn(SendFn fn) {
    _sendBinary = fn;
}
void ParkingHandler::setClientCountFn(ClientCountFn fn) {
    _clientCount = fn;
}

// ===== Enqueue (gọi từ async thread — THREAD-SAFE) =====

void ParkingHandler::enqueueBinary(const uint8_t *data, size_t len) {
    if (!_cmdQueue || len > PARKING_PB_H_MAX_SIZE)
        return;

    CmdData cmd;
    cmd.type = CmdType::BINARY_DATA;
    cmd.len = len;
    memcpy(cmd.buffer, data, len);

    if (xQueueSend(_cmdQueue, &cmd, 0) != pdTRUE) {
        Serial.println("[ParkingHandler] WARNING: Command queue full, dropping message");
    }
}

void ParkingHandler::enqueueClientConnected() {
    if (!_cmdQueue)
        return;

    CmdData cmd;
    cmd.type = CmdType::CLIENT_CONNECTED;
    cmd.len = 0;

    xQueueSend(_cmdQueue, &cmd, 0);
}

// ===== Process Commands (gọi trong main loop) =====

void ParkingHandler::processCommands() {
    if (!_cmdQueue)
        return;

    CmdData cmd;
    while (xQueueReceive(_cmdQueue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case CmdType::BINARY_DATA:
            handleBinaryData(cmd.buffer, cmd.len);
            break;
        case CmdType::CLIENT_CONNECTED:
            sendDeviceStatus();
            break;
        }
    }
}

// ===== Gửi messages =====

bool ParkingHandler::sendParking(const Parking &msg) {
    uint8_t buffer[PARKING_PB_H_MAX_SIZE];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, Parking_fields, &msg)) {
        Serial.printf("[ParkingHandler] Encode failed: %s\n", PB_GET_ERROR(&stream));
        return false;
    }

    if (_sendBinary)
        _sendBinary(buffer, stream.bytes_written);
    return true;
}

void ParkingHandler::sendStatus() {
    if (!_clientCount || _clientCount() == 0) {
        // Không có client kết nối -> không gửi status lặp vô ích
        return;
    }

    sendDeviceStatus();
}

void ParkingHandler::sendDeviceStatus() {
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_wifi_status_tag;

    DeviceStatus &status = msg.payload.wifi_status;
    status.connected = WiFi.isConnected();
    status.wifi_mode = (DeviceStatus_WifiMode)WiFi.getMode();

    // STA info
    if (WiFi.isConnected()) {
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

    if (sendParking(msg)) {
        Serial.println("Status sent");
    }
}

void ParkingHandler::sendParkingStatus(const ParkingStatus &status) {
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_parking_status_tag;
    msg.payload.parking_status = status;

    if (sendParking(msg)) {
        Serial.printf("[ParkingHandler] ParkingStatus sent (%d slots)\n", status.slots_count);
    }
}

void ParkingHandler::sendParkingStatus(const uint32_t *pallet_grid, size_t pallet_grid_count,
                                       const ParkingStatus_Status *slots_array, size_t slots_count,
                                       const uint8_t (*rfid)[16], size_t rfid_count) {
    ParkingStatus status = ParkingStatus_init_zero;

    if (pallet_grid && pallet_grid_count > 0) {
        status.pallet_grid_count = (pallet_grid_count > 12) ? 12 : pallet_grid_count;
        for (size_t i = 0; i < status.pallet_grid_count; i++) {
            status.pallet_grid[i] = pallet_grid[i];
        }
    }

    if (slots_array && slots_count > 0) {
        status.slots_count = (slots_count > 10) ? 10 : slots_count;
        for (size_t i = 0; i < status.slots_count; i++) {
            status.slots[i] = slots_array[i];
        }
    }

    if (rfid && rfid_count > 0) {
        status.rfid_count = (rfid_count > 10) ? 10 : rfid_count;
        for (size_t i = 0; i < status.rfid_count; i++) {
            memcpy(status.rfid[i], rfid[i], sizeof(status.rfid[i]));
        }
    }

    sendParkingStatus(status);
}

void ParkingHandler::sendParkingEvent(const ParkingEvent &event) {
    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_parking_event_tag;
    msg.payload.parking_event = event;

    if (sendParking(msg)) {
        Serial.printf("[ParkingHandler] ParkingEvent sent (slot=%u, type=%d)\n", event.slot_id,
                      event.event_type);
    }
}

void ParkingHandler::sendParkingEvent(uint32_t event_id, uint32_t slot_id, uint64_t timestamp,
                                      ParkingEvent_EventType event_type, const uint8_t *rfid,
                                      bool is_done) {
    ParkingEvent event = ParkingEvent_init_zero;
    event.event_id = event_id;
    event.slot_id = slot_id;
    event.timestamp = timestamp;
    event.event_type = event_type;
    event.is_done = is_done;

    if (rfid) {
        memcpy(event.rfid, rfid, sizeof(event.rfid));
    }

    sendParkingEvent(event);
}

// ===== Nhận và decode messages =====

void ParkingHandler::handleBinaryData(const uint8_t *data, size_t len) {
    Parking msg = Parking_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);

    if (!pb_decode(&stream, Parking_fields, &msg)) {
        Serial.printf("[ParkingHandler] Decode failed: %s\n", PB_GET_ERROR(&stream));
        return;
    }

    Serial.printf("[ParkingHandler] Received payload type: %d\n", (int)msg.which_payload);

    switch (msg.which_payload) {
    case Parking_wifi_scanning_tag:
        handleWifiScanning();
        break;

    case Parking_wifi_config_tag:
        handleWifiConfig(msg.payload.wifi_config);
        break;

    case Parking_wifi_status_tag:
        sendDeviceStatus();
        break;

    default:
        Serial.printf("[ParkingHandler] Unhandled payload type: %d\n", (int)msg.which_payload);
        break;
    }
}

void ParkingHandler::sendWifiScanResults(int n) {
    if (n < 0) {
        Serial.println("[ParkingHandler] Wifi scan failed, returning empty results");
    } else if (n == 0) {
        Serial.println("[ParkingHandler] Wifi scan returned no APs");
    }

    Parking msg = Parking_init_zero;
    msg.which_payload = Parking_scan_results_tag;
    ScanResults &results = msg.payload.scan_results;

    int count = (n > 0) ? min(n, 10) : 0;
    results.access_points_count = count;

    for (int i = 0; i < count; i++) {
        ScanResults_AP &ap = results.access_points[i];
        strncpy(ap.ssid, WiFi.SSID(i).c_str(), sizeof(ap.ssid) - 1);

        uint8_t *bssid = WiFi.BSSID(i);
        if (bssid) {
            memcpy(ap.bssid, bssid, 6);
        }

        ap.rssi = WiFi.RSSI(i);
        ap.channel = WiFi.channel(i);
        ap.encryption = (ScanResults_WifiAuthMode)WiFi.encryptionType(i);
    }

    WiFi.scanDelete();

    if (sendParking(msg)) {
        Serial.printf("[ParkingHandler] ScanResults sent (%d APs)\n", count);
    }
}

void ParkingHandler::handleWifiScanning() {
    if (_scanInProgress) {
        Serial.println("[ParkingHandler] WiFi scan already in progress");
        return;
    }

    Serial.println("[ParkingHandler] WiFi scan requested (async)");

    int n = WiFi.scanNetworks(true, true); // async, show_hidden

    if (n == WIFI_SCAN_RUNNING) {
        _scanInProgress = true;
        Serial.println("[ParkingHandler] WiFi scan started");
        return;
    }

    if (n == WIFI_SCAN_FAILED) {
        Serial.println("[ParkingHandler] WiFi scan failed to start");
        sendWifiScanResults(WIFI_SCAN_FAILED);
        return;
    }
}

void ParkingHandler::loop() {
    unsigned long now = millis();
    if (now - _lastStatusMillis >= _statusIntervalMs) {
        _lastStatusMillis = now;
        sendStatus();
    }

    if (_scanInProgress) {
        int n = WiFi.scanComplete();
        if (n != WIFI_SCAN_RUNNING) {
            _scanInProgress = false;

            if (n == WIFI_SCAN_FAILED) {
                Serial.println("[ParkingHandler] WiFi scan failed (async complete)");
                WiFi.scanDelete();
                sendWifiScanResults(WIFI_SCAN_FAILED);
            } else {
                sendWifiScanResults(n);
            }
        }
    }
}

void ParkingHandler::handleWifiConfig(const WifiConfig &config) {
    Serial.printf("[ParkingHandler] WiFi config received - AP: '%s', STA: '%s'\n", config.ap_ssid,
                  config.sta_ssid);

    // Load current prefs as base
    WifiPrefs prefs = _wifiManager.loadPrefs();
    bool apChanged = false;

    // Update AP config if provided
    if (strlen(config.ap_ssid) > 0) {
        if (prefs.ap_ssid != config.ap_ssid || prefs.ap_password != config.ap_password) {
            apChanged = true;
        }
        prefs.ap_ssid = config.ap_ssid;
        prefs.ap_password = config.ap_password;
    }

    // Update STA config if provided
    if (strlen(config.sta_ssid) > 0) {
        prefs.sta_ssid = config.sta_ssid;
        prefs.sta_password = config.sta_password;
    }

    // Save to NVS
    _wifiManager.savePrefs(prefs);

    // Apply AP changes if needed
    if (apChanged) {
        Serial.println("[ParkingHandler] AP config changed, restarting AP...");
        _wifiManager.applyApConfig(prefs);
    }

    // Connect STA if STA SSID provided
    if (strlen(config.sta_ssid) > 0) {
        _wifiManager.connectSta(config.sta_ssid, config.sta_password);
    }

    sendDeviceStatus();
}
