/**
 * @file parking_handler.h
 * @brief Protobuf command handling for parking status and events.
 *
 * This header defines the parking handler class responsible for receiving
 * raw protobuf binary payloads from asynchronous callbacks, decoding them on
 * the main thread, and sending outbound protobuf messages to connected clients.
 */
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

/**
 * @brief Loại command được đẩy từ async callback vào main loop.
 */
// enum class CmdType : uint8_t {
//     BINARY_DATA,      ///< Raw protobuf binary nhận từ WebSocket
//     CLIENT_CONNECTED, ///< Client mới kết nối → gửi DeviceStatus
// };
using CmdType = uint8_t;
static constexpr CmdType CMD_TYPE_BINARY_DATA = 0;
static constexpr CmdType CMD_TYPE_CLIENT_CONNECTED = 1;

/**
 * @brief Payload cho command queue.
 *
 * Chứa raw protobuf binary — decode sẽ xảy ra trên main thread.
 */
struct CmdData {
    CmdType type;
    uint8_t buffer[PARKING_PB_H_MAX_SIZE]; ///< Copy of raw protobuf data
    size_t len;                            ///< Actual data length
};

/**
 * @brief ParkingHandler - Xử lý encode/decode protobuf.
 *
 * KHÔNG phụ thuộc vào ESPAsyncWebServer — chỉ nhận/gửi raw binary
 * thông qua std::function callbacks.
 * Thread safety đảm bảo bởi FreeRTOS queue: async thread enqueue,
 * main loop dequeue + process.
 */
class ParkingHandler {
  public:
    using SendFn = std::function<void(const uint8_t *, size_t)>;
    using ClientCountFn = std::function<size_t()>;

    /**
     * @brief Construct a new ParkingHandler object.
     *
     * @param wifiManager Reference to WifiManager.
     */
    ParkingHandler(WifiManager &wifiManager);

    /**
     * @brief Khởi tạo command queue.
     */
    void begin();

    /**
     * @brief Thiết lập hàm gửi binary (gọi trước begin).
     *
     * @param fn Callback để gửi raw binary.
     */
    void setSendFn(SendFn fn);

    /**
     * @brief Thiết lập hàm đếm client (gọi trước begin).
     *
     * @param fn Callback để trả về số lượng client đang kết nối.
     */
    void setClientCountFn(ClientCountFn fn);

    /**
     * @brief Enqueue raw binary data từ async thread.
     *
     * THREAD-SAFE.
     *
     * @param data Raw protobuf data.
     * @param len Độ dài dữ liệu.
     */
    void enqueueBinary(const uint8_t *data, size_t len);

    /**
     * @brief Enqueue client connected event từ async thread.
     *
     * THREAD-SAFE.
     */
    void enqueueClientConnected();

    /**
     * @brief Xử lý command queue.
     *
     * GỌI TRONG MAIN LOOP.
     */
    void processCommands();

    /**
     * @brief Gửi DeviceStatus cho tất cả client.
     */
    void sendDeviceStatus();

    /**
     * @brief Gửi status tổng quát cho client.
     */
    void sendStatus();

    /**
     * @brief Gửi ParkingStatus cho tất cả client.
     *
     * @param status Parking status message.
     */
    void sendParkingStatus(const ParkingStatus &status);

    /**
     * @brief Tạo và gửi ParkingStatus từ mảng ParkingStatus_Status.
     *
     * @param pallet_grid Mảng pallet grid.
     * @param pallet_grid_count Số phần tử pallet_grid.
     * @param slots_array Mảng ParkingStatus_Status.
     * @param slots_count Số phần tử slots_array.
     */
    void sendParkingStatus(const uint32_t *pallet_grid = nullptr, size_t pallet_grid_count = 0,
                           const ParkingStatus_Status *slots_array = nullptr,
                           size_t slots_count = 0);

    /**
     * @brief Gửi ParkingEvent cho tất cả client.
     *
     * @param event Parking event message.
     */
    void sendParkingEvent(const ParkingEvent &event);

    /**
     * @brief Tạo và gửi ParkingEvent với các tham số rời rạc.
     *
     * @param event_id ID của event.
     * @param pallet_id ID của pallet.
     * @param event_type Loại event.
     * @param is_done Flag ghi nhận sự kiện hoàn thành.
     */
    void sendParkingEvent(uint32_t event_id, uint32_t pallet_id,
                          /* uint64_t timestamp, */
                          ParkingEvent_EventType event_type, bool is_done = false);

    /**
     * @brief Vòng lặp để kiểm tra scan async.
     */
    void loop();

  private:
    WifiManager &_wifiManager;
    SendFn _sendBinary;
    ClientCountFn _clientCount;

    QueueHandle_t _cmdQueue;
    static constexpr size_t CMD_QUEUE_SIZE = 2;

    bool _scanInProgress = false;
    unsigned long _lastStatusMillis = 0;
    static constexpr unsigned long _statusIntervalMs = 100UL * 1000UL; // 10 giây

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
