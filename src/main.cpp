#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"
#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>
#include <WiFi.h>
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

// #define IR_T1_C1 21
// #define IR_T1_C2 13
// #define IR_T1_C3 14
// #define IR_T2_C1 25
// #define IR_T2_C2 26
// #define IR_T2_C3 27
// #define IR_T3_C1 36
// #define IR_T3_C2 39
// #define IR_T3_C3 2
// #define IR_T3_C4 15

// const uint8_t MANG_IR[10] = {IR_T1_C1, IR_T1_C2, IR_T1_C3, IR_T2_C1, IR_T2_C2,
//                              IR_T2_C3, IR_T3_C1, IR_T3_C2, IR_T3_C3, IR_T3_C4};

// ==========================================
// 2. KHAI BAO BIEN & CAU TRUC
// ==========================================
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

/// Thông tin một ô pallet logic trong hệ thống.
struct O_Do {
    /// UID thẻ RFID gán cho ô này; rỗng nếu ô chưa có xe.
    String rfid;
    /// Tầng pallet: 1..3.
    int row;
    /// Cột pallet trong tầng: 1..4 (tầng 1-2 chỉ dùng 1..3).
    int col;
};

// Old field names are preserved through aliases so merged code can use old identifiers
#define ma_the_uid rfid
#define tang row
#define cot col

/// Danh sách 10 ô pallet logic trong hệ thống.
/// Chỉ số `i` là index nội bộ; không giống pallet ID trực tiếp.
O_Do ds_o[10];

/// Trạng thái cũ của cảm biến IR vật lý tại mỗi ô.
bool ir_cu[10];

/// Trạng thái công tắc/điểm ngang dùng để xác nhận di chuyển ngang.
/// sw[row][target] = true khi pallet đã đạt vị trí ngang mong muốn.
bool sw[4][5];

/// Trạng thái cảm biến vị trí dọc/thang nâng từ UART.
/// cam_bien_vi_tri[tang][cot] = true khi pallet ở đúng vị trí theo tầng và cột.
bool cam_bien_vi_tri[4][5];

bool cua_da_dong_hoan_toan = false;
bool cua_da_mo_hoan_toan = false;

// RFID consecutive read failure counter.
int rfidReadFailureCount = 0;

// ==========================================

// [VQ]
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

void sendCurrentParkingStatus();
void sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type,
                             bool is_done = false);

/*
 * // 0: false, 1: true
 * bool sw[4][5] = {
 *     {0, 0, 0, 0, 0}, // Hàng 0: Không dùng
 *     {0, 1, 1, 1, 0}, // Hàng 1 (Vật lý)
 *     {0, 1, 1, 1, 0}, // Hàng 2 (Vật lý)
 *     {0, 1, 1, 1, 1}  // Hàng 3 (Vật lý)
 * };
 *
 * 1 2 3 4 5 6 7 0 8 9 10 0
 *
 * {0, 0, 0, 0, 0},
 * {0, 1 (8), 1 (9), 1 (10), 0},
 * {0, 1 (5), 1 (6), 1 (7), 0},
 * {0, 1 (1), 1 (2), 1 (3), 1 (4)}
 */

/**
 * @brief Trạng thái gửi cho từng slot của hệ thống ParkingHandler.
 */
static ParkingStatus_Status SlotStatus[10] = {ParkingStatus_Status_UNKNOWN};

/**
 * @brief Bản đồ 1D của vị trí pallet logic trong lưới (0 = trống).
 */
static uint32_t Grid[12] = {1, 2, 3, 4, 5, 6, 7, 0, 8, 9, 10, 0};

// Forward declarations needed by recalcStatus()
int rowPallet2SlotID(int row, int indexInRow);
int rowPallet2SlotIndex(int row, int indexInRow);

/**
 * @brief Tính lại trạng thái OCCUPIED/EMPTY cho tất cả slot
 *        dựa trên ds_o[i].rfid (nguồn sự thật logic).
 */
void recalcStatus() {
    for (int i = 0; i < 10; i++) {
        int idx = rowPallet2SlotIndex(ds_o[i].row, ds_o[i].col);
        if (idx < 0)
            continue;
        SlotStatus[idx] =
            ds_o[i].rfid.isEmpty() ? ParkingStatus_Status_EMPTY : ParkingStatus_Status_OCCUPIED;
    }
}

/**
 * @brief Bảng chuyển đổi từ ID ODo sang chỉ số slot nội bộ.
 */

/**
 * @brief Chuyển vị trí pallet (hàng, cột logic) thành ID slot.
 *
 * @param row Tầng pallet (1 = tầng 1, 2 = tầng 2, 3 = tầng 3).
 * @param indexInRow Chỉ số pallet trong hàng (1..4, với hàng 1 và 2 chỉ dùng 1..3).
 * @return ID slot ứng với vị trí pallet, hoặc -1 nếu tham số không hợp lệ.
 */
int rowPallet2SlotID(int row, int indexInRow) {

    if (row < 1 || row > 3) {
        Serial.printf("[VQ] Invalid row: %d\n", row);
        return -1; // Invalid row
    }
    if (indexInRow < 1 || indexInRow > 4) {
        Serial.printf("[VQ] Invalid index: %d\n", indexInRow);
        return -1; // Invalid index
    }
    if (row < 3 && indexInRow > 3) {
        Serial.printf("[VQ] Invalid index for row %d: %d\n", row, indexInRow);
        return -1; // Invalid index for rows 1 and 2
    }
    // Mapping logic:

    // row 3: 1 2 3 4
    // row 2: 5 6 7
    // row 1: 8 9 10
    const int number =
        (row == 3) ? indexInRow : (row == 2 ? indexInRow + 4 : (row == 1 ? indexInRow + 7 : 0));
    if (number < 1 || number > 10) {
        Serial.printf("[VQ] Invalid slot number: %d\n", number);
        return -1; // Invalid row or index
    }
    return number;
}

/**
 * @brief Chuyển vị trí pallet (hàng, cột logic) thành chỉ số slot nội bộ.
 *
 * @param row Tầng pallet (1..3).
 * @param indexInRow Chỉ số pallet trong hàng.
 * @return Chỉ số slot nội bộ (0..9), hoặc -2 nếu vị trí không hợp lệ.
 */
int rowPallet2SlotIndex(int row, int indexInRow) {
    return rowPallet2SlotID(row, indexInRow) - 1;
}

/**
 * @brief Tìm chỉ số mảng Grid tương ứng với pallet ID.
 *
 * @param PalletId ID pallet cần tìm.
 * @return Chỉ số mảng Grid, hoặc -1 nếu không tìm thấy.
 */
int findGridIndex(int PalletId) {
    for (size_t i = 0; i < 12; ++i) {
        if (Grid[i] == PalletId) {
            return i;
        }
    }
    return -1; // Not found
}

/**
 * @brief Di chuyển pallet trong lưới logic.
 *
 * @param PalletId ID pallet cần di chuyển.
 * @param direction Hướng di chuyển (1 = phải, 2 = trái).
 * @return 0 nếu di chuyển thành công, -1 nếu lỗi chung, -2 nếu ô đích không trống.
 */
int movePalletInGrid(int PalletId, int direction) {
    int gridIndex = findGridIndex(PalletId);
    if (gridIndex == -1) {
        Serial.printf("[VQ] Pallet ID %d not found in grid\n", PalletId);
        return -1; // Pallet not found
    }
    int row = gridIndex / 4; // 0-based row index
    int col = gridIndex % 4; // 0-based column index

    if (row == 0) {
        Serial.printf("[VQ] Pallet ID %d is on the top row and cannot be moved\n", PalletId);
        return -1; // Cannot move pallets on the top row
    }

    if (direction == 1 && col < 3) { // Move right
        if (Grid[gridIndex + 1] != 0) {
            Serial.printf("[VQ] Cannot move Pallet ID %d to the right because the target position "
                          "is not empty\n",
                          PalletId);
            return -2; // Target position is not empty
        }

        std::swap(Grid[gridIndex], Grid[gridIndex + 1]);
    } else if (direction == 2 && col > 0) { // Move left
        if (Grid[gridIndex - 1] != 0) {
            Serial.printf("[VQ] Cannot move Pallet ID %d to the left because the target position "
                          "is not empty\n",
                          PalletId);
            return -2; // Target position is not empty
        }
        std::swap(Grid[gridIndex], Grid[gridIndex - 1]);
    } else {
        Serial.printf("[VQ] Invalid move for Pallet ID %d in direction %d (row: %d, col: %d)\n",
                      PalletId, direction, row, col);
        return -1;
    }
    return 0;
}

/**
 * @brief Gửi trạng thái sử dụng chỗ đậu xe hiện tại.
 *
 * Hàm này thu thập thông tin trạng thái chiếm chỗ từ mảng trạng thái cục bộ
 * `ds_o` và gửi thông điệp trạng thái bãi đậu xe qua `parkingHandler`.
 */
void sendCurrentParkingStatus() {
    // serial debug Grid & SlotStatus
    // Serial.println("[VQ] sendCurrentParkingStatus called");
    // Serial.println("[VQ] Grid:");
    // for (size_t i = 0; i < 12; ++i) {
    //     Serial.print(Grid[i]);
    //     Serial.print(" ");
    //     // newline every 4 entries
    //     if ((i + 1) % 4 == 0) {
    //         Serial.println();
    //     }
    // }
    // Serial.println();
    // Serial.print("[VQ] SlotStatus: ");
    // for (size_t i = 0; i < 10; ++i) {
    //     Serial.print(SlotStatus[i]);
    //     Serial.print(" ");
    // }
    // Serial.println();

    parkingHandler.sendParkingStatus(Grid, 12, SlotStatus, 10);
}

/**
 * @brief Bộ đếm sự kiện dùng để gán event_id cho các thông điệp gửi đi.
 */
uint32_t event_id_counter = 1;

/**
 * @brief Gửi một sự kiện đỗ/nhận xe đến hệ thống phía sau.
 *
 * @param pallet_id ID pallet/ODo (1..10).
 * @param event_type Loại sự kiện đỗ/nhận xe.
 * @param is_done True nếu quá trình đã hoàn tất thành công.
 */
void sendCurrentParkingEvent(uint32_t pallet_id, ParkingEvent_EventType event_type, bool is_done) {
    if (pallet_id < 1 || pallet_id > 10) {
        Serial.printf("[VQ] Invalid pallet_id=%u passed to sendCurrentParkingEvent\n", pallet_id);
        return;
    }

    // Debug log
    // Serial.printf(
    //     "[VQ] sendCurrentParkingEvent called with pallet_id=%d, event_type=%d, is_done=%d\n",
    //     pallet_id, event_type, is_done);

    uint32_t event_id = event_id_counter;
    if (is_done) {
        event_id_counter++;
    }

    parkingHandler.sendParkingEvent(event_id, pallet_id, /* timestamp_ms, */
                                    event_type, is_done);
}
// [VQ END]

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
    // cua_da_mo_hoan_toan = false;

    dieu_khien_goc_servo(90);

    // unsigned long timeout = millis();
    // while (cua_da_mo_hoan_toan == false) {
    //     cap_nhat_tin_hieu_ngoai_vi();
    //     if (millis() - timeout > 20000) {
    //         dung_motor_cong();
    //         Serial.println("!!! LOI: CUA KET KHI MO (TIMEOUT)");
    //         return;
    //     }
    //     delay(10);
    // }
    delay(1000);
    dung_motor_cong();
    Serial.println(">> CUA DA MO HOAN TOAN.");
}

void dong_cua_chinh() {
    Serial.println(">> DANG DONG CUA...");
    // cua_da_dong_hoan_toan = false;

    dieu_khien_goc_servo(0);

    // unsigned long timeout = millis();
    // while (cua_da_dong_hoan_toan == false) {
    //     cap_nhat_tin_hieu_ngoai_vi();
    //     if (millis() - timeout > 20000) {
    //         dung_motor_cong();
    //         Serial.println("!!! LOI: CUA KET KHI DONG (TIMEOUT)");
    //         return;
    //     }
    //     delay(10);
    // }
    delay(1000);
    dung_motor_cong();
    Serial.println(">> CUA DA DONG AN TOAN.");
}

void cap_nhat_tin_hieu_ngoai_vi() {
    // Đọc từ ESP Sensor (UART1)
    while (Serial1.available() > 0) {
        String tin_nhan = Serial1.readStringUntil('\n');
        tin_nhan.trim();

        if (tin_nhan.length() > 0) {
            Serial.print(">>> [UART1 - ESP SENSOR]: ");
            Serial.println(tin_nhan);
        }

        if (tin_nhan == "DOORCLOSE") {
            cua_da_dong_hoan_toan = true;
        } else if (tin_nhan == "DOOROPEN") {
            cua_da_mo_hoan_toan = true;
        } else if (tin_nhan.startsWith("SW") && tin_nhan.length() >= 5) {
            int t = tin_nhan[2] - '0';
            int c = tin_nhan[3] - '0';
            bool trang_thai_sw = (tin_nhan[4] == '1');
            if (t >= 0 && t <= 3 && c >= 1 && c <= 4) {
                sw[t][c] = trang_thai_sw;
            }
        }
        // BỔ SUNG: Bắt tín hiệu cảm biến vị trí thang tời (Ví dụ: IR111, IR211...)
        else if ((tin_nhan.startsWith("IR") || tin_nhan.startsWith("ir")) &&
                 tin_nhan.length() >= 5) {
            int tang_hien_tai = tin_nhan[2] - '0';
            int cot_hien_tai = tin_nhan[3] - '0';
            bool trang_thai_vi_tri = (tin_nhan[4] == '1');

            if (tang_hien_tai >= 1 && tang_hien_tai <= 3 && cot_hien_tai >= 1 &&
                cot_hien_tai <= 4) {
                cam_bien_vi_tri[tang_hien_tai][cot_hien_tai] = trang_thai_vi_tri;
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
    const int target = rowPallet2SlotID(row, pallet);
    if (huong == "NP") { // Phải
        movePalletInGrid(target, 1);
    } else if (huong == "NT") { // Trái
        movePalletInGrid(target, 2);
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
        // SlotStatus[] tương ứng với pallet_id
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NP", 3);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NP", 2);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 2) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 2: kéo pallet cột 1 sang trái đến sw 1,
        // sau đó đẩy pallet cột 3 sang phải đến sw 4,
        // rồi đẩy pallet cột 2 sang phải đến sw 3.
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NP", 3);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 3) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 3: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // rồi đẩy pallet cột 3 sang phải đến sw 4.
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NT", 2);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NP", 4);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
        // [VQ END]

    } else if (cot_trong_yc == 4) {
        // [VQ]
        // [UI HOOK]
        // Giải phóng cột 4: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // kéo pallet cột 3 sang trái đến sw 3.
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 1, "NT", 1);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 2, "NT", 2);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]

        day_den_sw(row, 3, "NT", 3);

        // [VQ]
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
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
        if (digitalRead(PIN_NUT_XAC_NHAN) == HIGH) {
            delay(100); // Debounce delay
        }
    }

    Serial.println(">> DA NHAN NUT XAC NHAN!");
    beep(2);
    delay(500);
}

void gui_xe(String uid) {
    int target = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == "" /* && (digitalRead(MANG_IR[i]) == HIGH)*/) {
            target = i;
            break;
        }
    }

    if (target != -1) {
        int t = ds_o[target].row;
        int c = ds_o[target].col;

        ds_o[target].ma_the_uid = uid;
        // [VQ]
        // [UI HOOK] selected slot identified
        int pallet_id = rowPallet2SlotID(t, c);
        int slotIndex = rowPallet2SlotIndex(t, c);

        if (pallet_id < 1 || slotIndex < 0) {
            Serial.printf("[VQ] Invalid slot mapping for target=%d (t=%d,c=%d)\n", target, t, c);
            return;
        }
        SlotStatus[slotIndex] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();
        sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_IN, false);
        // [VQ END]
        Serial.printf("\n>>> GUI XE VAO T%d-C%d\n", t, c);

        if (t > 1) {
            if (t == 2) {
                don_duong_vet_can(2, 4);
            }
            for (int i = 1; i < t; i++) {
                don_duong_vet_can(i, c);
            }

            // --- HẠ XUỐNG TẦNG 1 ---

            // [VQ]
            // [UI HOOK] animate selected slot moving down to floor 1
            SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
            sendCurrentParkingStatus();
            // [VQ END]

            gui_lenh_motor(String(t) + String(c) + "KD");
            delay(300);

            // Đợi tín hiệu cảm biến vị trí Tầng 1 báo 1
            while (cam_bien_vi_tri[1][c] == false) {
                cap_nhat_tin_hieu_ngoai_vi();
                delay(10);
            }
            gui_lenh_motor("st");
        }

        mo_cong();
        cho_nguoi_dung_xac_nhan();
        dong_cua_chinh();

        if (t > 1) {
            gui_lenh_motor(String(t) + String(c) + "KU");
            delay(300);

            // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
            while (cam_bien_vi_tri[t][c] == false) {
                cap_nhat_tin_hieu_ngoai_vi();
                delay(10);
            }
            gui_lenh_motor("st");
        }

        // [VQ]
        // [UI HOOK] complete send event
        recalcStatus();
        sendCurrentParkingStatus();
        sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_IN, true);
        // [VQ END]
        beep(1);
    }
}

void lay_xe(int target) {
    int t = ds_o[target].row;
    int c = ds_o[target].col;
    int pallet_id = rowPallet2SlotID(t, c);
    int slotIndex = rowPallet2SlotIndex(t, c);

    if (pallet_id < 1 || slotIndex < 0) {
        Serial.printf("[VQ] Invalid slot mapping for target=%d (t=%d,c=%d)\n", target, t, c);
        return;
    }

    // [VQ]
    // [UI HOOK] pickup process started
    SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_OUT, false);
    // [VQ END]
    Serial.printf("\n>>> LAY XE T%d-C%d\n", t, c);

    if (t > 1) {
        if (t == 2) {
            don_duong_vet_can(2, 4);
        }
        for (int i = 1; i < t; i++) {
            don_duong_vet_can(i, c);
        }

        // --- HẠ PALLET XUỐNG TẦNG 1 ---
        // [VQ]
        // [UI HOOK] animate selected slot moving down to floor 1
        SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        // [VQ END]
        gui_lenh_motor(String(t) + String(c) + "KD");
        delay(300);

        // Đợi tín hiệu cảm biến vị trí Tầng 1 báo 1
        while (cam_bien_vi_tri[1][c] == false) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    mo_cong();
    cho_nguoi_dung_xac_nhan();
    dong_cua_chinh();

    if (t > 1) {
        // --- KÉO PALLET VỀ TẦNG GỐC ---
        gui_lenh_motor(String(t) + String(c) + "KU");
        delay(300);

        // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
        while (cam_bien_vi_tri[t][c] == false) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    ds_o[target].ma_the_uid = "";
    Serial.println(">> HOAN TAT LAY XE. O DA TRONG.");

    // [VQ]
    // [UI HOOK] complete pickup event
    recalcStatus();
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_OUT, true);
    // [VQ END]
    beep(2);

    // WiFi.mode(WIFI_OFF);
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX2, PIN_UART_TX2);
    Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX1, -1);

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

    ledcSetup(KENH_PWM, TAN_SO_PWM, DO_PHAN_GIAI);
    ledcAttachPin(PIN_SERVO_CONG, KENH_PWM);
    dung_motor_cong();

    pinMode(PIN_RFID_RST, OUTPUT);
    digitalWrite(PIN_RFID_RST, LOW);
    delay(100);
    digitalWrite(PIN_RFID_RST, HIGH);
    delay(100);

    SPI.begin(18, 19, 23, PIN_RFID_SS);
    SPI.setFrequency(1000000);
    rfid.PCD_Init();
    Serial.println("--- Kiem tra ket noi RC522 ---");
    rfid.PCD_DumpVersionToSerial();
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_NUT_XAC_NHAN, INPUT_PULLUP);

    memset(sw, 0, sizeof(sw));
    memset(cam_bien_vi_tri, 0, sizeof(cam_bien_vi_tri));

    // for (int i = 0; i < 10; i++) {
    //     if (MANG_IR[i] == 36 || MANG_IR[i] == 39) {
    //         pinMode(MANG_IR[i], INPUT);
    //     } else {
    //         pinMode(MANG_IR[i], INPUT_PULLUP);
    //     }

    //     ir_cu[i] = (digitalRead(MANG_IR[i]) == LOW);
    //     ds_o[i].ma_the_uid = "";
    // }

    for (int i = 0; i < 3; i++) {
        ds_o[i].tang = 1;
        ds_o[i].cot = i + 1;
    }
    for (int i = 3; i < 6; i++) {
        ds_o[i].tang = 2;
        ds_o[i].cot = i - 2;
    }
    for (int i = 6; i < 10; i++) {
        ds_o[i].tang = 3;
        ds_o[i].cot = i - 5;
    }

    Serial.println("\n--- HE THONG MASTER FULL READY ---");
    don_duong_vet_can(1, 4);
    don_duong_vet_can(2, 4);

    beep(1);
}

void loop() {
    // parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();

    vTaskDelay(pdMS_TO_TICKS(5));
    // [VQ END]

    cap_nhat_tin_hieu_ngoai_vi();

    // SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        // SPI.endTransaction();
        rfidReadFailureCount++;
        if (rfidReadFailureCount >= 100) {
            Serial.print("[VQ] RFID read failed 100 times... ");
            rfid.PCD_DumpVersionToSerial();
            rfidReadFailureCount = 0;
            digitalWrite(PIN_RFID_RST, LOW);
            delay(50);
            digitalWrite(PIN_RFID_RST, HIGH);
            delay(50);
        }
        return;
    }
    // SPI.endTransaction();

    rfidReadFailureCount = 0;
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    Serial.println("\n--- THE RFID MOI DUOC QUET: " + uid + " ---");

    int vi_tri_tim_thay = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == uid) {
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