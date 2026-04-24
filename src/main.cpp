#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"
#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>
#include <sys/time.h>
#include <time.h>

SET_LOOP_TASK_STACK_SIZE(16384);

// ==========================================
// 1. CAU HINH CHAN (PIN MAPPING)
// ==========================================
#define PIN_RFID_SS 5
#define PIN_RFID_RST 22
#define PIN_BUZZER 4
#define PIN_NUT_XAC_NHAN 34

#define PIN_SERVO_CONG 32

#define PIN_UART_RX2 16
#define PIN_UART_TX2 17
#define PIN_UART_RX1 35
#define PIN_UART_TX1 -1

const int KENH_PWM = 0;
const int TAN_SO_PWM = 50;
const int DO_PHAN_GIAI = 16;

#define IR_T1_C1 21
#define IR_T1_C2 13
#define IR_T1_C3 14
#define IR_T2_C1 25
#define IR_T2_C2 26
#define IR_T2_C3 27
#define IR_T3_C1 36
#define IR_T3_C2 39
#define IR_T3_C3 2
#define IR_T3_C4 15

const uint8_t MANG_IR[10] = {IR_T1_C1, IR_T1_C2, IR_T1_C3, IR_T2_C1, IR_T2_C2,
                             IR_T2_C3, IR_T3_C1, IR_T3_C2, IR_T3_C3, IR_T3_C4};

// ==========================================
// 2. KHAI BAO BIEN & CAU TRUC
// ==========================================
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

struct O_Do {
    String rfid;
    int row;
    int col;
};

O_Do ds_o[10];
bool ir_cu[10];
bool sw[4][5];

// BỔ SUNG: Mảng lưu trạng thái cảm biến vị trí từ UART
bool cam_bien_vi_tri[4][5];

bool cua_da_dong_hoan_toan = false;
bool cua_da_mo_hoan_toan = false;

// ==========================================

// [VQ]
bool mock_sw[4][5] = {{0, 0, 0, 0, 0}, {0, 1, 1, 1, 0}, {0, 1, 1, 1, 0}, {0, 1, 1, 1, 1}};

WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

bool sendCurrentParkingStatus();
bool sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type,
                             bool is_done = false);
// bool updateUnixTimeFromSerialMessage(const String &msg);

static ParkingStatus_Status Satus[10] = {ParkingStatus_Status_UNKNOWN};

void resetStatus() {
    for (size_t i = 0; i < 10; ++i) {
        Satus[i] = ParkingStatus_Status_UNKNOWN;
    }
}

int rowPallet2SlotId(int row, int pallet) {
    /*
    Protobuf grid
    [    1, 2, 3, 4    5, 6, 7, 0    8, 9, 10, 0    ]
    row 3 (thượng) -> pallet 1,2,3,4
    row 2 (giữa) -> pallet 1, 2, 3 ,0
    row 1 (dưới) -> pallet 1, 2, 3, 0
    */
    // 4 là offset của row0 dữ liệu gửi đi tức row 1 của hệ thống
    // (3 - row - 1) * 3

    return 4 + (3 - row - 1) * 3 + pallet;
}

/**
 * @brief Parse cấu hình grid 1D (logic) từ mảng trạng thái cảm biến SW (vật lý).
 *
 * @param sw Mảng 2 chiều lưu trạng thái công tắc
 * @param rows Số lượng hàng của grid logic (mặc định 3)
 * @param cols Số lượng cột của grid logic (mặc định 4)
 * @param grid Con trỏ mảng 1 chiều lưu trữ ID của pallet kích thước rows * cols (0 = khoảng trống)
 * @return true nếu map thành công, false nếu dữ liệu cảm biến không hợp lệ
 *
 * @example
 * // 0: false, 1: true
 * bool sw[4][5] = {
 *     {0, 0, 0, 0, 0}, // Hàng 0: Không dùng
 *     {0, 1, 1, 1, 0}, // Hàng 1 (Vật lý)
 *     {0, 1, 1, 1, 0}, // Hàng 2 (Vật lý)
 *     {0, 1, 1, 1, 1}  // Hàng 3 (Vật lý)
 * };
 *
 * // Resulting grid (row-major logic order):
 * // 1 2 3 4 5 6 7 0 8 9 10 0
 */
bool parseGridFromSW(const bool sw[4][5], uint8_t rows, uint8_t cols, uint8_t *grid) {
    // Bảo vệ: Tránh truy xuất vượt quá kích thước hoặc con trỏ rỗng
    if (rows == 0 || cols == 0 || rows > 3 || cols > 4 || grid == nullptr) {
        return false;
        Serial.println("[Error] Invalid parameters for parseGridFromSW");
    }

    uint8_t next_id = 1;

    // --- PHASE 1: Hàng logic 0 (Cao nhất) ---
    // Hàng trên cùng không có limit switch vật lý, luôn giả định là có đầy pallet.
    for (uint8_t col = 0; col < cols; col++) {
        grid[col] = next_id++;
    }

    // --- PHASE 2: Các hàng logic còn lại ---
    for (uint8_t row = 1; row < rows; row++) {
        uint8_t zero_count = 0;

        // CÔNG THỨC ĐẢO CHIỀU TỌA ĐỘ Y (Mapping Logic -> Vật lý)
        // VD rows = 3: row = 1 (Giữa) -> sw_row = 2 | row = 2 (Trệt) -> sw_row = 1
        uint8_t sw_row = rows - row;

        for (uint8_t col = 0; col < cols; col++) {
            uint8_t grid_idx = row * cols + col;

            // Đọc từ mảng sw (cột của sw bắt đầu từ 1, nên phải + 1)
            if (sw[sw_row][col + 1]) {
                grid[grid_idx] = next_id++;
            } else {
                grid[grid_idx] = 0;
                zero_count++;
            }
        }

        // KIỂM TRA ĐIỀU KIỆN HỢP LỆ
        // Các hàng bên dưới phải có đúng 1 khoảng trống để mâm có thể di chuyển
        if (zero_count != 1) {
            Serial.println("[Error] Invalid SW grid, expected exactly one empty slot in row");
            return false;
        }
    }

    return true;
}

/**
 * @brief Gửi trạng thái sử dụng chỗ đậu xe hiện tại.
 *
 * Hàm này thu thập thông tin trạng thái chiếm chỗ từ mảng trạng thái cục bộ
 * `ds_o` và gửi thông điệp trạng thái bãi đậu xe qua `parkingHandler`.
 *
 * @param overrideSlots Optional array of slot status values to override the
 *        locally-derived occupancy state. Nếu `nullptr`, trạng thái sẽ được
 *        tính toán từ `ds_o`.
 * @return true nếu dữ liệu SW hợp lệ và trạng thái được gửi; false nếu grid
 *         SW không hợp lệ.
 */
bool sendCurrentParkingStatus() {
    static const uint8_t kSlotCount = 10;
    static const uint8_t kGridRows = 3;
    static const uint8_t kGridCols = 4;
    static const size_t kPalletGridCount = size_t(kGridRows) * size_t(kGridCols);

    uint8_t grid_ids[kPalletGridCount] = {0};
    bool grid_ok = parseGridFromSW(mock_sw, kGridRows, kGridCols, grid_ids);
    if (!grid_ok) {
        Serial.println("[Warning] Invalid SW grid, sending empty pallet_grid");
    }

    uint32_t pallet_grid[kPalletGridCount] = {0};
    for (size_t i = 0; i < kPalletGridCount; ++i) {
        pallet_grid[i] = grid_ids[i];
    }

    ParkingStatus_Status slots[kSlotCount] = {ParkingStatus_Status_UNKNOWN};

    for (size_t i = 0; i < kSlotCount; ++i) {
        if (Satus[i] != ParkingStatus_Status_UNKNOWN) {
            slots[i] = Satus[i];
        } else {
            bool occupied = (ds_o[i].rfid.length() > 0);
            slots[i] = occupied ? ParkingStatus_Status_OCCUPIED : ParkingStatus_Status_EMPTY;
        }
    }

    parkingHandler.sendParkingStatus(pallet_grid, kPalletGridCount, slots, kSlotCount);
    return grid_ok;
}

// increment event_id_counter when lay_xe() hoặc gui_xe() is called
uint32_t event_id_counter = 1;

/**
 * @brief Gửi một sự kiện đậu xe đến hệ thống phía sau.
 *
 * @param slot_id ID của chỗ đậu xe.
 * @param event_type Loại sự kiện đậu xe.
 * @param is_done True nếu sự kiện đã hoàn thành thành công.
 * @return true khi sự kiện được gửi thành công, false nếu lấy thời gian thất bại.
 */
bool sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type, bool is_done) {
    struct timespec ts;
    uint64_t timestamp_ms = 0;

    slot_id = 11 - slot_id;

    uint32_t event_id = event_id_counter;
    if (is_done) {
        event_id_counter++;
    }

    parkingHandler.sendParkingEvent(event_id, slot_id, /* timestamp_ms, */
                                    event_type, is_done);

    // if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
    //     timestamp_ms = ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
    //     parkingHandler.sendParkingEvent(event_id, slot_id, /* timestamp_ms, */
    //                                     event_type, is_done);
    //     return true;
    // } else {
    //     Serial.println("Failed to get current time");
    //     return false;
    // }
    return true;
}

/**
 * @brief Cập nhật đồng hồ thiết bị từ thông điệp thời gian qua serial.
 *
 * Thông điệp đầu vào được kỳ vọng bắt đầu bằng "TIME:" theo sau là giá trị
 * Unix epoch tính theo giây.
 *
 * @param msg Thông điệp serial chứa giá trị thời gian Unix.
 * @return true khi đồng hồ được cập nhật thành công, false nếu không.
 */
// bool updateUnixTimeFromSerialMessage(const String &msg) {
//     // Kiểm tra an toàn độ dài chuỗi trước khi thao tác pointer
//     if (msg.length() <= 5) {
//         Serial.println("Invalid message length");
//         return false;
//     }
//     const char *time_str_ptr = msg.c_str() + 5;
//     unsigned long unix_time = strtoul(time_str_ptr, NULL, 10);

//     if (unix_time > 1000000000UL) {
//         struct timeval tv;
//         tv.tv_sec = (time_t)unix_time;
//         tv.tv_usec = 0;
//         settimeofday(&tv, NULL);

//         Serial.print("Time updated from serial: ");
//         Serial.println(unix_time);
//         return true;
//     }

//     Serial.print("Failed to parse Unix time: ");
//     Serial.println(time_str_ptr);
//     return false;
// }
// [VQ]

// ==========================================
// 3. HAM TIEN ICH & CONG
// ==========================================
void beep(int n) {
    for (int i = 0; i < n; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
        delay(100);
    }
}

void gui_lenh_motor(String lenh) {
    Serial2.println(lenh);
    Serial.println("[MASTER -> ACTION]: " + lenh);
}

void dieu_khien_goc_servo(int goc) {
    int duty = map(goc, 0, 180, 1638, 8192);
    ledcWrite(KENH_PWM, duty);
}

void dung_motor_cong() {
    ledcWrite(KENH_PWM, 0);
}

void cap_nhat_tin_hieu_ngoai_vi();

void mo_cong() {
    Serial.println(">> DANG MO CONG...");
    cua_da_mo_hoan_toan = false;

    dieu_khien_goc_servo(90);

    unsigned long timeout = millis();
    while (cua_da_mo_hoan_toan == false) {
        cap_nhat_tin_hieu_ngoai_vi();
        if (millis() - timeout > 20000) {
            dung_motor_cong();
            Serial.println("!!! LOI: CUA KET KHI MO (TIMEOUT)");
            return;
        }
        delay(10);
    }

    dung_motor_cong();
    Serial.println(">> CUA DA MO HOAN TOAN.");
}

void dong_cua_chinh() {
    Serial.println(">> DANG DONG CUA...");
    cua_da_dong_hoan_toan = false;

    dieu_khien_goc_servo(0);

    unsigned long timeout = millis();
    while (cua_da_dong_hoan_toan == false) {
        cap_nhat_tin_hieu_ngoai_vi();
        if (millis() - timeout > 20000) {
            dung_motor_cong();
            Serial.println("!!! LOI: CUA KET KHI DONG (TIMEOUT)");
            return;
        }
        delay(10);
    }

    dung_motor_cong();
    Serial.println(">> CUA DA DONG AN TOAN.");
}

void cap_nhat_tin_hieu_ngoai_vi() {
    // Đọc từ ESP Sensor (UART1)
    while (Serial1.available() > 0) {
        String mesage = Serial1.readStringUntil('\n');
        mesage.trim();

        if (mesage.length() > 0) {
            Serial.print(">>> [UART1 - ESP SENSOR]: ");
            Serial.println(mesage);
        }

        if (mesage == "DOORCLOSE") {
            cua_da_dong_hoan_toan = true;
        } else if (mesage == "DOOROPEN") {
            cua_da_mo_hoan_toan = true;
        } else if (mesage.startsWith("SW") && mesage.length() >= 5) {
            int row = mesage[2] - '0';
            int col = mesage[3] - '0';
            bool trang_thai_sw = (mesage[4] == '1');
            if (row >= 0 && row <= 3 && col >= 1 && col <= 4) {
                sw[row][col] = trang_thai_sw;
            }
        }
        // BỔ SUNG: Bắt tín hiệu cảm biến vị trí thang tời (Ví dụ: IR111, IR211...)
        else if ((mesage.startsWith("IR") || mesage.startsWith("ir")) && mesage.length() >= 5) {
            int current_row = mesage[2] - '0';
            int current_column = mesage[3] - '0';
            bool position_status = (mesage[4] == '1');

            if (current_row >= 1 && current_row <= 3 && current_column >= 1 &&
                current_column <= 4) {
                cam_bien_vi_tri[current_row][current_column] = position_status;
            }
        }
    }

    // Đọc từ PC (Debug)
    if (Serial.available() > 0) {
        String pc = Serial.readStringUntil('\n');
        pc.trim();
        if (pc == "DOORCLOSE") {
            cua_da_dong_hoan_toan = true;
        }
        if (pc == "DOOROPEN") {
            cua_da_mo_hoan_toan = true;
        }
        if (pc.startsWith("SW")) {
            sw[pc[2] - '0'][pc[3] - '0'] = (pc[4] == '1');
        }

        // Thêm debug cho PC giả lập tín hiệu IR vị trí
        if ((pc.startsWith("IR") || pc.startsWith("ir")) && pc.length() >= 5) {
            cam_bien_vi_tri[pc[2] - '0'][pc[3] - '0'] = (pc[4] == '1');
        }

        if (pc == "1") {
            mo_cong();
        }
        if (pc == "2") {
            dong_cua_chinh();
        }
        if (pc == "0") {
            dung_motor_cong();
        }
    }
}

// ==========================================
// 4. THUAT TOAN VET CAN (TRUOT NGANG)
// ==========================================
void day_den_sw(int row, int pallet, String huong, int sw_target) {
    cap_nhat_tin_hieu_ngoai_vi();
    if (sw[row][sw_target] == true) {
        return;
    }

    gui_lenh_motor(String(row) + String(pallet) + huong);
    unsigned long timeout = millis();
    while (sw[row][sw_target] == false) {
        cap_nhat_tin_hieu_ngoai_vi();
        if (millis() - timeout > 15000) {
            gui_lenh_motor("st");
            Serial.println("!!! LOI: MOTOR NGANG KET");
            return;
        }
        delay(10);
    }
    gui_lenh_motor(String(row) + String(pallet) + "ST");
    gui_lenh_motor("st");
    delay(400);
    // [VQ]
    mock_sw[row][sw_target] = true;
    if (huong == "NP") {
        mock_sw[row][pallet] = false;
    } else if (huong == "NT") {
        mock_sw[row][pallet + 1] = false;
    }
    // [VQ END]
}

/**
 * @brief Dọn đường vét cạn cho pallet ngang trên tầng `row`.
 *
 * Hàm này di chuyển các pallet ngang trên tầng `row` để giải phóng
 * vị trí cột đích `cot_trong_yc` trước khi pallet chính được đưa lên hoặc hạ xuống.
 *
 * @param row Tầng hiện tại của pallet ngang cần dọn đường.
 * @param cot_trong_yc Cột đích cần giải phóng (1..4).
 *
 * @note
 * - `NP` là lệnh di chuyển sang phải (cột tăng).
 * - `NT` là lệnh di chuyển sang trái (cột giảm).
 */
void don_duong_vet_can(int row, int cot_trong_yc) {
    Serial.printf("\n--- DON DUONG T%d CHO COT %d ---\n", row, cot_trong_yc);
    if (cot_trong_yc == 1) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 1: đẩy pallet ở cột 3 sang phải đến sw 4,
        // rồi pallet cột 2 sang phải đến sw 3, cuối cùng pallet cột 1 sang phải đến sw 2.
        // 4 + (3 - row - 1)*3 + x là công thức convert từ tọa độ (row, pallet) sang index của
        // Satus[] tương ứng với pallet_id
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PENDING;
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        Satus[rowPallet2SlotId(row, 4)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NP", 3);

        // [VQ]
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NP", 2);

        // [VQ]
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 2) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 2: kéo pallet cột 1 sang trái đến sw 1,
        // sau đó đẩy pallet cột 3 sang phải đến sw 4,
        // rồi đẩy pallet cột 2 sang phải đến sw 3.
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PENDING;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        Satus[rowPallet2SlotId(row, 4)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NP", 3);

        // [VQ]
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 3) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 3: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // rồi đẩy pallet cột 3 sang phải đến sw 4.
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NT", 2);

        // [VQ]
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 4) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 4: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // kéo pallet cột 3 sang trái đến sw 3.
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        Satus[rowPallet2SlotId(row, 1)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_PROCESSING;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NT", 2);

        // [VQ]
        Satus[rowPallet2SlotId(row, 2)] = ParkingStatus_Status_UNKNOWN;
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NT", 3);

        // [VQ]
        Satus[rowPallet2SlotId(row, 3)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]
    }
}

// ==========================================
// 5. QUY TRINH GUI / LAY XE
// ==========================================
void cho_nguoi_dung_xac_nhan() {
    Serial.println(">> DANG CHO BAM NUT XAC NHAN...");
    while (digitalRead(PIN_NUT_XAC_NHAN) == HIGH) {
        delay(50);
    }
    Serial.println(">> DA NHAN NUT XAC NHAN!");
    beep(2);
    delay(500);
}

void gui_xe(String uid) {
    int target = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].rfid == "" && (digitalRead(MANG_IR[i]) == HIGH)) {
            target = i;
            break;
        }
    }

    if (target != -1) {
        int targetRow = ds_o[target].row;
        int targetColumn = ds_o[target].col;
        ds_o[target].rfid = uid;
        // [VQ]
        // [UI HOOK] selected slot identified
        Satus[target] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        sendCurrentParkingEvent(target + 1, ParkingEvent_EventType_IN, false);
        // [VQ END]
        Serial.printf("\n>>> GUI XE VAO T%d-C%d\n", targetRow, targetColumn);

        if (targetRow > 1) {
            if (targetRow == 2) {
                don_duong_vet_can(2, 4);
            }
            for (int i = 1; i < targetRow; i++) {
                don_duong_vet_can(i, targetColumn);
            }

            // --- HẠ XUỐNG TẦNG 1 ---

            // [VQ]
            // [UI HOOK] animate selected slot moving down to floor 1
            Satus[target] = ParkingStatus_Status_PROCESSING;
            sendCurrentParkingStatus();
            // [VQ END]

            gui_lenh_motor(String(targetRow) + String(targetColumn) + "KD");
            delay(300);

            // Đợi tín hiệu cảm biến vị trí Tầng 1 báo 1
            while (cam_bien_vi_tri[1][targetColumn] == false) {
                cap_nhat_tin_hieu_ngoai_vi();
                delay(10);
            }
            gui_lenh_motor("st");
        }

        mo_cong();
        cho_nguoi_dung_xac_nhan();
        dong_cua_chinh();

        if (targetRow > 1) {
            gui_lenh_motor(String(targetRow) + String(targetColumn) + "KU");
            delay(300);

            // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
            while (cam_bien_vi_tri[targetRow][targetColumn] == false) {
                cap_nhat_tin_hieu_ngoai_vi();
                delay(10);
            }
            gui_lenh_motor("st");
        }

        // [VQ]
        // [UI HOOK] complete send event
        resetStatus();
        sendCurrentParkingStatus();
        sendCurrentParkingEvent(target + 1, ParkingEvent_EventType_IN, true);
        // [VQ END]
        beep(1);
    }
}

void lay_xe(int target) {
    int targetRow = ds_o[target].row;
    int targetColumn = ds_o[target].col;
    // [VQ]
    // [UI HOOK] pickup process started
    Satus[target] = ParkingStatus_Status_PROCESSING;
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(target + 1, ParkingEvent_EventType_OUT, false);
    // [VQ END]
    Serial.printf("\n>>> LAY XE T%d-C%d\n", targetRow, targetColumn);

    if (targetRow > 1) {
        if (targetRow == 2) {
            don_duong_vet_can(2, 4);
        }
        for (int i = 1; i < targetRow; i++) {
            don_duong_vet_can(i, targetColumn);
        }

        // --- HẠ PALLET XUỐNG TẦNG 1 ---
        // [VQ]
        // [UI HOOK] animate selected slot moving down to floor 1
        Satus[target] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]
        gui_lenh_motor(String(targetRow) + String(targetColumn) + "KD");
        delay(300);

        // Đợi tín hiệu cảm biến vị trí Tầng 1 báo 1
        while (cam_bien_vi_tri[1][targetColumn] == false) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    mo_cong();
    cho_nguoi_dung_xac_nhan();
    dong_cua_chinh();

    if (targetRow > 1) {
        // --- KÉO PALLET VỀ TẦNG GỐC ---
        gui_lenh_motor(String(targetRow) + String(targetColumn) + "KU");
        delay(300);

        // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
        while (cam_bien_vi_tri[targetRow][targetColumn] == false) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    ds_o[target].rfid = "";
    Serial.println(">> HOAN TAT LAY XE. O DA TRONG.");

    // [VQ]
    // [UI HOOK] complete pickup event
    resetStatus();
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(target + 1, ParkingEvent_EventType_OUT, true);
    // [VQ END]
    beep(2);
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    // [VQ]
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
        sendCurrentParkingStatus();
        parkingHandler.sendStatus();
    });

    parkingHandler.begin();
    // [VQ END]

    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX2, PIN_UART_TX2);
    Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX1, -1);

    ledcSetup(KENH_PWM, TAN_SO_PWM, DO_PHAN_GIAI);
    ledcAttachPin(PIN_SERVO_CONG, KENH_PWM);
    dung_motor_cong();

    SPI.begin();
    rfid.PCD_Init();
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_NUT_XAC_NHAN, INPUT);

    memset(sw, 0, sizeof(sw));
    memset(cam_bien_vi_tri, 0, sizeof(cam_bien_vi_tri));

    for (int i = 0; i < 10; i++) {
        if (MANG_IR[i] == 36 || MANG_IR[i] == 39) {
            pinMode(MANG_IR[i], INPUT);
        } else {
            pinMode(MANG_IR[i], INPUT_PULLUP);
        }

        ir_cu[i] = (digitalRead(MANG_IR[i]) == LOW);
        ds_o[i].rfid = "";
    }

    for (int i = 0; i < 3; i++) {
        ds_o[i].row = 1;
        ds_o[i].col = i + 1;
    }
    for (int i = 3; i < 6; i++) {
        ds_o[i].row = 2;
        ds_o[i].col = i - 2;
    }
    for (int i = 6; i < 10; i++) {
        ds_o[i].row = 3;
        ds_o[i].col = i - 5;
    }

    Serial.println("\n--- HE THONG MASTER FULL READY ---");
    don_duong_vet_can(1, 4);
    don_duong_vet_can(2, 4);
    beep(1);
}

void loop() {
    // [VQ]
    // if (Serial2.available() > 0) {
    //     String msg = Serial2.readStringUntil('\n');
    //     msg.trim();
    //     if (msg.startsWith("TIME:")) {
    //         updateUnixTimeFromSerialMessage(msg);
    //     }
    // }
    parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();
    // [VQ END]

    cap_nhat_tin_hieu_ngoai_vi();

    for (int i = 0; i < 10; i++) {
        bool trang_thai = (digitalRead(MANG_IR[i]) == LOW);

        if (trang_thai != ir_cu[i]) {
            ir_cu[i] = trang_thai;

            int tang, cot;
            if (i < 3) {
                tang = 1;
                cot = i + 1;
            } else if (i < 6) {
                tang = 2;
                cot = i - 2;
            } else {
                tang = 3;
                cot = i - 5;
            }

            Serial.print(">>> [IR STATUS]: IR_T");
            Serial.print(tang);
            Serial.print("_C");
            Serial.print(cot);
            Serial.print(" -> ");
            Serial.println(trang_thai ? "CHẠM (CÓ XE)" : "KO (TRỐNG)");
        }
    }

    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        return;
    }

    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    int vi_tri_tim_thay = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].rfid == uid) {
            vi_tri_tim_thay = i;
            break;
        }
    }

    if (vi_tri_tim_thay != -1) {
        lay_xe(vi_tri_tim_thay);
    } else {
        gui_xe(uid);
    }

    rfid.PICC_HaltA();
    delay(500);
}