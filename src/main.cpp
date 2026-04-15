#include "Arduino.h"
#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"
#include <sys/time.h>
#include <time.h>

SET_LOOP_TASK_STACK_SIZE(16384);

// Global Instances
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

void sendCurrentParkingStatus();
bool sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type,
                             bool is_done = false);
bool updateUnixTimeFromSerialMessage(const String &msg);

void sendCurrentParkingStatus() {
    static const size_t kSlotCount = 10;

    // Tối ưu: Dùng static const để mảng luôn nằm sẵn trong RAM/Flash, không khởi tạo lại mỗi lần
    static const uint32_t pallet_grid[kSlotCount] = {0};

    // Sửa lỗi: Khởi tạo mảng slots với giá trị mặc định để tránh giá trị rác
    ParkingStatus_Status slots[kSlotCount] = {ParkingStatus_Status_EMPTY};

    for (size_t i = 0; i < kSlotCount; ++i) {
        // Mở comment đoạn này nếu ds_o đã sẵn sàng
        // bool occupied = (ds_o[i].ma_the_uid.length() > 0);
        // slots[i] = occupied ? ParkingStatus_Status_OCCUPIED : ParkingStatus_Status_EMPTY;
    }

    parkingHandler.sendParkingStatus(pallet_grid, kSlotCount, slots, kSlotCount);
}

// increment event_id_counter when lay_xe() or gui_xe() is called
uint32_t event_id_counter = 1;

bool sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type, bool is_done) {
    struct timespec ts;
    uint64_t timestamp_ms = 0;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        timestamp_ms = ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
        parkingHandler.sendParkingEvent(event_id_counter++, slot_id, timestamp_ms, event_type,
                                        is_done);
        return true;
    } else {
        Serial.println("Failed to get current time");
        return false;
    }
}

bool updateUnixTimeFromSerialMessage(const String &msg) {
    // Kiểm tra an toàn độ dài chuỗi trước khi thao tác pointer
    if (msg.length() <= 5) {
        Serial.println("Invalid message length");
        return false;
    }
    const char *time_str_ptr = msg.c_str() + 5;
    unsigned long unix_time = strtoul(time_str_ptr, NULL, 10);

    if (unix_time > 1000000000UL) {
        struct timeval tv;
        tv.tv_sec = (time_t)unix_time;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        Serial.print("Time updated from serial: ");
        Serial.println(unix_time);
        return true;
    }

    Serial.print("Failed to parse Unix time: ");
    Serial.println(time_str_ptr);
    return false;
}

// Lấy xe/ Gửi xe
/*
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(idx + 1, ParkingEvent_EventType_OUT, true);
*/

void setup() {
    Serial.begin(115200);
    wifiManager.begin();
    webManager.begin();

    // Nối ParkingHandler ↔ WebManager bằng callbacks
    parkingHandler.setSendFn(
        [](const uint8_t *data, size_t len) { webManager.sendBinary(data, len); });
    parkingHandler.setClientCountFn([]() -> size_t { return webManager.clientCount(); });

    // WebManager → ParkingHandler: enqueue vào queue (thread-safe)
    webManager.setOnBinary(
        [](const uint8_t *data, size_t len) { parkingHandler.enqueueBinary(data, len); });
    webManager.setOnConnect([]() {
        // parkingHandler.enqueueClientConnected();
    });

    parkingHandler.begin();
}

void loop() {
    parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();
}
