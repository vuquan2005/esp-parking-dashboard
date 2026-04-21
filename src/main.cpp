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
    String ma_the_uid;
    int tang;
    int cot;
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
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

bool sendCurrentParkingStatus(const ParkingStatus_Status *overrideSlots = nullptr);
bool sendCurrentParkingEvent(uint32_t slot_id, ParkingEvent_EventType event_type,
                             bool is_done = false);
bool updateUnixTimeFromSerialMessage(const String &msg);

/**
 * @brief Parse cấu hình grid 1D (logic) từ mảng trạng thái cảm biến SW (vật lý).
 *
 * @param sw Mảng 2 chiều lưu trạng thái công tắc (Tầng 1..3 tương ứng sw[1]..sw[3], Cột 1..4)
 * @param rows Số lượng hàng của grid logic (mặc định 3)
 * @param cols Số lượng cột của grid logic (mặc định 4)
 * @param grid Con trỏ mảng 1 chiều lưu trữ ID của pallet kích thước rows * cols (0 = khoảng trống)
 * @return true nếu map thành công, false nếu dữ liệu cảm biến không hợp lệ
 */
bool parseGridFromSW(bool sw[4][5], uint8_t rows, uint8_t cols, uint8_t *grid) {
    // Bảo vệ: Tránh truy xuất vượt quá kích thước mảng sw[4][5]
    if (rows > 3 || cols > 4) {
        return false;
    }

    // Tạo mảng tạm để không làm hỏng grid gốc nếu parse thất bại giữa chừng
    uint8_t temp_grid[rows * cols];
    for (int i = 0; i < rows * cols; i++) {
        temp_grid[i] = 0;
    }

    uint8_t next_id = 1;

    // Quét từng hàng logic của grid (từ trên xuống dưới: row 0 -> row 2)
    for (uint8_t row = 0; row < rows; row++) {
        uint8_t zero_count = 0;

        // CÔNG THỨC ĐẢO CHIỀU TỌA ĐỘ Y (Mapping Logic -> Vật lý)
        // Nếu rows = 3: row = 0 (Cao nhất) -> sw_row = 3
        //               row = 1 (Giữa)     -> sw_row = 2
        //               row = 2 (Trệt)     -> sw_row = 1
        uint8_t sw_row = rows - row;

        // Quét từng cột (từ trái qua phải)
        for (uint8_t col = 0; col < cols; col++) {
            // Đọc từ mảng sw (chú ý: cột của sw bắt đầu từ 1, nên phải + 1)
            bool has_pallet = sw[sw_row][col + 1];

            if (has_pallet) {
                temp_grid[row * cols + col] = next_id++;
            } else {
                temp_grid[row * cols + col] = 0;
                zero_count++;
            }
        }

        // KIỂM TRA ĐIỀU KIỆN HỢP LỆ (Giữ nguyên tinh thần của hệ thống cũ)
        if (row == 0) {
            // Hàng trên cùng (Cao nhất) phải luôn đầy pallet
            if (zero_count != 0) {
                return false;
            }
        } else {
            // Các hàng bên dưới phải có đúng 1 khoảng trống để di chuyển
            if (zero_count != 1) {
                return false;
            }
        }
    }

    // Nếu vượt qua toàn bộ bước kiểm tra, gán dữ liệu vào mảng grid gốc
    for (int i = 0; i < rows * cols; i++) {
        grid[i] = temp_grid[i];
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
bool sendCurrentParkingStatus(const ParkingStatus_Status *overrideSlots) {
    static const uint8_t kSlotCount = 10;
    static const uint8_t kGridRows = 3;
    static const uint8_t kGridCols = 4;
    static const size_t kPalletGridCount = size_t(kGridRows) * size_t(kGridCols);

    uint8_t grid_ids[kPalletGridCount] = {0};
    bool grid_ok = parseGridFromSW(sw, kGridRows, kGridCols, grid_ids);
    if (!grid_ok) {
        Serial.println("[Warning] Invalid SW grid, sending empty pallet_grid");
    }

    uint32_t pallet_grid[kPalletGridCount] = {0};
    for (size_t i = 0; i < kPalletGridCount; ++i) {
        pallet_grid[i] = grid_ids[i];
    }

    ParkingStatus_Status slots[kSlotCount] = {ParkingStatus_Status_UNKNOWN};

    for (size_t i = 0; i < kSlotCount; ++i) {
        if (overrideSlots && overrideSlots[i] != ParkingStatus_Status_UNKNOWN) {
            slots[i] = overrideSlots[i];
        } else {
            bool occupied = (ds_o[i].ma_the_uid.length() > 0);
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

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        timestamp_ms = ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
        parkingHandler.sendParkingEvent(event_id_counter++, slot_id, /* timestamp_ms, */
                                        event_type, is_done);
        return true;
    } else {
        Serial.println("Failed to get current time");
        return false;
    }
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
void day_den_sw(int t, int pallet, String huong, int sw_target) {
    cap_nhat_tin_hieu_ngoai_vi();
    if (sw[t][sw_target] == true) {
        return;
    }

    gui_lenh_motor(String(t) + String(pallet) + huong);
    unsigned long timeout = millis();
    while (sw[t][sw_target] == false) {
        cap_nhat_tin_hieu_ngoai_vi();
        if (millis() - timeout > 15000) {
            gui_lenh_motor("st");
            Serial.println("!!! LOI: MOTOR NGANG KET");
            return;
        }
        delay(10);
    }
    gui_lenh_motor(String(t) + String(pallet) + "ST");
    gui_lenh_motor("st");
    delay(400);
}

/**
 * @brief Dọn đường vét cạn cho pallet ngang trên tầng `t`.
 *
 * Hàm này di chuyển các pallet ngang trên tầng `t` để giải phóng
 * vị trí cột đích `cot_trong_yc` trước khi pallet chính được đưa lên hoặc hạ xuống.
 *
 * @param t Tầng hiện tại của pallet ngang cần dọn đường.
 * @param cot_trong_yc Cột đích cần giải phóng (1..4).
 *
 * @note
 * - `NP` là lệnh di chuyển sang phải (cột tăng).
 * - `NT` là lệnh di chuyển sang trái (cột giảm).
 */
void don_duong_vet_can(int t, int cot_trong_yc) {
    Serial.printf("\n--- DON DUONG T%d CHO COT %d ---\n", t, cot_trong_yc);
    // [VQ]
    if (cot_trong_yc == 1) {
        // Giải phóng cột 1: đẩy pallet ở cột 3 sang phải đến sw 4,
        // rồi pallet cột 2 sang phải đến sw 3, cuối cùng pallet cột 1 sang phải đến sw 2.
        day_den_sw(t, 3, "NP", 4);
        day_den_sw(t, 2, "NP", 3);
        day_den_sw(t, 1, "NP", 2);
    } else if (cot_trong_yc == 2) {
        // Giải phóng cột 2: kéo pallet cột 1 sang trái đến sw 1,
        // sau đó đẩy pallet cột 3 sang phải đến sw 4,
        // rồi đẩy pallet cột 2 sang phải đến sw 3.
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 3, "NP", 4);
        day_den_sw(t, 2, "NP", 3);
    } else if (cot_trong_yc == 3) {
        // Giải phóng cột 3: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // rồi đẩy pallet cột 3 sang phải đến sw 4.
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NP", 4);
    } else if (cot_trong_yc == 4) {
        // Giải phóng cột 4: kéo pallet cột 1 sang trái đến sw 1,
        // kéo pallet cột 2 sang trái đến sw 2,
        // kéo pallet cột 3 sang trái đến sw 3.
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NT", 3);
    }
    // [VQ END]
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
    int muc_tieu = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == "" && (digitalRead(MANG_IR[i]) == HIGH)) {
            muc_tieu = i;
            break;
        }
    }

    if (muc_tieu != -1) {
        int t = ds_o[muc_tieu].tang;
        int c = ds_o[muc_tieu].cot;
        ds_o[muc_tieu].ma_the_uid = uid;
        // [VQ]
        // [UI HOOK] selected slot identified; prepare parking_event.event_type =
        // ParkingEvent_EventType_IN and mark target slot_id = muc_tieu + 1 as PROCESSING/PENDING
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
            // --- KÉO LÊN TẦNG GỐC ---
            // [VQ]
            // [UI HOOK] animate selected slot moving up to target floor
            // [VQ END]
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
        sendCurrentParkingStatus();
        sendCurrentParkingEvent(muc_tieu + 1, ParkingEvent_EventType_IN, true);
        // [UI HOOK] complete send event; set parking_event.event_type = ParkingEvent_EventType_IN
        // and update slot state to OCCUPIED on UI [VQ END]
        beep(1);
    }
}

void lay_xe(int chi_so_o) {
    int t = ds_o[chi_so_o].tang;
    int c = ds_o[chi_so_o].cot;
    // [VQ]
    // [UI HOOK] pickup process started; prepare parking_event.event_type =
    // ParkingEvent_EventType_OUT and show slot_id = chi_so_o + 1 as PROCESSING
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
        // [VQ]
        // [UI HOOK] animate selected slot moving up to root floor
        // [VQ END]
        gui_lenh_motor(String(t) + String(c) + "KU");
        delay(300);

        // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
        while (cam_bien_vi_tri[t][c] == false) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    ds_o[chi_so_o].ma_the_uid = "";
    Serial.println(">> HOAN TAT LAY XE. O DA TRONG.");

    // [VQ]
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(chi_so_o + 1, ParkingEvent_EventType_OUT, true);
    // [UI HOOK] complete pickup event; set parking_event.event_type = ParkingEvent_EventType_OUT
    // and update slot state to EMPTY on UI [VQ END]
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
        // parkingHandler.enqueueClientConnected();
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
        ds_o[i].ma_the_uid = "";
    }

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
    // [VQ]
    if (Serial2.available() > 0) {
        String msg = Serial2.readStringUntil('\n');
        msg.trim();
        if (msg.startsWith("TIME:")) {
            updateUnixTimeFromSerialMessage(msg);
        }
    }
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