#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include <functional>
#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "parking.pb.h"
#include "wifimanager.h"

#ifndef PARKING_PB_H_MAX_SIZE
#define PARKING_PB_H_MAX_SIZE 1024
#endif

/// Loại command được đẩy từ async callback vào main loop
enum class CmdType : uint8_t {
    BINARY_DATA,      // Raw protobuf binary nhận từ WebSocket
    CLIENT_CONNECTED, // Client mới kết nối → gửi DeviceStatus
};

/// Payload cho command queue
/// Chứa raw protobuf binary — decode sẽ xảy ra trên main thread
struct CmdData {
    CmdType type;
    uint8_t buffer[PARKING_PB_H_MAX_SIZE]; // Copy of raw protobuf data
    size_t len;                            // Actual data length
};

/**
 * ParkingHandler - Xử lý encode/decode protobuf
 *
 * KHÔNG phụ thuộc vào ESPAsyncWebServer — chỉ nhận/gửi raw binary
 * thông qua std::function callbacks. Thread safety đảm bảo bởi
 * FreeRTOS queue: async thread enqueue, main loop dequeue + process.
 */
class ParkingHandler {
  public:
    using SendFn = std::function<void(const uint8_t *, size_t)>;
    using ClientCountFn = std::function<size_t()>;

    ParkingHandler(WifiManager &wifiManager);

    /// Khởi tạo command queue
    void begin();

    /// Thiết lập hàm gửi binary (gọi trước begin)
    void setSendFn(SendFn fn);

    /// Thiết lập hàm đếm client (gọi trước begin)
    void setClientCountFn(ClientCountFn fn);

    /// Enqueue raw binary data từ async thread (THREAD-SAFE)
    void enqueueBinary(const uint8_t *data, size_t len);

    /// Enqueue client connected event từ async thread (THREAD-SAFE)
    void enqueueClientConnected();

    /// Xử lý command queue — GỌI TRONG MAIN LOOP
    void processCommands();

    /// Gửi DeviceStatus cho tất cả client
    void sendDeviceStatus();

    /// Gửi status tổng quát cho client
    void sendStatus();

    /// Gửi ParkingStatus cho tất cả client
    void sendParkingStatus(const ParkingStatus &status);

    /// Tạo và gửi ParkingStatus từ mảng ParkingStatus_Status
    void sendParkingStatus(const uint32_t *pallet_grid = nullptr, size_t pallet_grid_count = 0,
                           const ParkingStatus_Status *slots_array = nullptr,
                           size_t slots_count = 0, const uint8_t (*rfid)[10] = nullptr,
                           size_t rfid_count = 0);

    /// Gửi ParkingEvent cho tất cả client
    void sendParkingEvent(const ParkingEvent &event);

    /// Tạo và gửi ParkingEvent với các tham số rời rạc
    void sendParkingEvent(uint32_t event_id, uint32_t slot_id, uint64_t timestamp,
                          ParkingEvent_EventType event_type, const uint8_t *rfid = nullptr,
                          bool is_done = false);

    /// Vòng lặp để kiểm tra scan async
    void loop();

  private:
    WifiManager &_wifiManager;
    SendFn _sendBinary;
    ClientCountFn _clientCount;

    QueueHandle_t _cmdQueue;
    static constexpr size_t CMD_QUEUE_SIZE = 2;

    bool _scanInProgress = false;
    unsigned long _lastStatusMillis = 0;
    static constexpr unsigned long _statusIntervalMs = 10UL * 1000UL; // 10 giây

    /// Encode và gửi message Parking qua callback
    bool sendParking(const Parking &msg);

    /// Xử lý kết quả scan WiFi async
    void sendWifiScanResults(int count);

    /// Xử lý binary data (chạy trên main thread)
    void handleBinaryData(const uint8_t *data, size_t len);

    /// Xử lý từng loại payload
    void handleWifiScanning();
    void handleWifiConfig(const WifiConfig &config);
};
