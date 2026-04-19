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

#define PIN_CONG_IN1 32
#define PIN_CONG_IN2 33

#define PIN_UART_RX2 16
#define PIN_UART_TX2 17
#define PIN_UART_RX1 35
#define PIN_UART_TX1 -1

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
        bool occupied = (ds_o[i].ma_the_uid.length() > 0);
        slots[i] = occupied ? ParkingStatus_Status_OCCUPIED : ParkingStatus_Status_EMPTY;
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

void dung_motor_cong() {
    digitalWrite(PIN_CONG_IN1, LOW);
    digitalWrite(PIN_CONG_IN2, LOW);
}

void cap_nhat_tin_hieu_ngoai_vi();

void mo_cong() {
    Serial.println(">> DANG MO CONG...");
    cua_da_mo_hoan_toan = false;

    digitalWrite(PIN_CONG_IN1, HIGH);
    digitalWrite(PIN_CONG_IN2, LOW);

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

    digitalWrite(PIN_CONG_IN1, LOW);
    digitalWrite(PIN_CONG_IN2, HIGH);

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

void don_duong_vet_can(int t, int cot_trong_yc) {
    Serial.printf("\n--- DON DUONG T%d CHO COT %d ---\n", t, cot_trong_yc);
    if (cot_trong_yc == 1) {
        day_den_sw(t, 3, "NP", 4);
        day_den_sw(t, 2, "NP", 3);
        day_den_sw(t, 1, "NP", 2);
    } else if (cot_trong_yc == 2) {
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 3, "NP", 4);
        day_den_sw(t, 2, "NP", 3);
    } else if (cot_trong_yc == 3) {
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NP", 4);
    } else if (cot_trong_yc == 4) {
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NT", 3);
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
        Serial.printf("\n>>> GUI XE VAO T%d-C%d\n", t, c);

        if (t > 1) {
            for (int i = 1; i < t; i++) {
                don_duong_vet_can(i, c);
            }

            // --- HẠ XUỐNG TẦNG 1 ---

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
            gui_lenh_motor(String(t) + String(c) + "KU");
            delay(300);

            // Đợi tín hiệu cảm biến vị trí Tầng đích báo 1
            while (cam_bien_vi_tri[t][c] == false) {
                cap_nhat_tin_hieu_ngoai_vi();
                delay(10);
            }
            gui_lenh_motor("st");
        }
        beep(1);
    }
}

void lay_xe(int chi_so_o) {
    int t = ds_o[chi_so_o].tang;
    int c = ds_o[chi_so_o].cot;
    Serial.printf("\n>>> LAY XE T%d-C%d\n", t, c);

    if (t > 1) {
        for (int i = 1; i < t; i++) {
            don_duong_vet_can(i, c);
        }

        // --- HẠ PALLET XUỐNG TẦNG 1 ---
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

    ds_o[chi_so_o].ma_the_uid = "";
    Serial.println(">> HOAN TAT LAY XE. O DA TRONG.");
    beep(2);
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
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
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX2, PIN_UART_TX2);
    Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX1, -1);

    pinMode(PIN_CONG_IN1, OUTPUT);
    pinMode(PIN_CONG_IN2, OUTPUT);
    dung_motor_cong();

    SPI.begin();
    rfid.PCD_Init();
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_NUT_XAC_NHAN, INPUT);

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
    beep(1);
}

void loop() {

    parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();

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