#include <Arduino.h>
#include <MFRC522.h>
#include <SPI.h>

#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"

SET_LOOP_TASK_STACK_SIZE(16384);

// Global Instances
WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

// ==========================================
// 1. CẤU HÌNH CHÂN (HARDWARE)
// ==========================================
#define PIN_RFID_SS 5
#define PIN_RFID_RST 22
#define PIN_UART_RX2 16
#define PIN_UART_TX2 17
#define PIN_BUZZER 4

// 10 Chân IR (Đã né chân 12 lỗi Boot)
#define IR_T1_C1 21
#define IR_T1_C2 13
#define IR_T1_C3 14
#define IR_T2_C1 25
#define IR_T2_C2 26
#define IR_T2_C3 27
#define IR_T3_C1 32
#define IR_T3_C2 33
#define IR_T3_C3 2
#define IR_T3_C4 15

const uint8_t MANG_IR[10] = {IR_T1_C1, IR_T1_C2, IR_T1_C3, IR_T2_C1, IR_T2_C2,
                             IR_T2_C3, IR_T3_C1, IR_T3_C2, IR_T3_C3, IR_T3_C4};

// ==========================================
// 2. KHAI BÁO BIẾN & CẤU TRÚC
// ==========================================
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);

/**
 * @struct O_Do - Cấu trúc lưu thông tin một ô đỗ xe
 * @field ma_the_uid: UID thẻ RFID của chủ xe (định danh duy nhất)
 *                    Ví dụ: "4A3B2C1D" (quét từ thẻ RFID)
 *                    Rỗng "" = ô trống
 * @field co_xe: Trạng thái xe có/không (hiện không sử dụng, dùng ir_cu thay thế)
 * @field tang: Tầng đỗ (1, 2, hoặc 3)
 *              Ví dụ: tang=2 = Tầng 2
 * @field cot: Cột vị trí (1-4)
 *             Ví dụ: cot=3 = Cột 3
 *
 * Ví dụ đầy đủ: O_Do{ma_the_uid="4A3B2C1D", tang=2, cot=3}
 * Nghĩa: Xe của chủ thẻ "4A3B2C1D" đang đỗ ở Tầng 2, Cột 3
 */
struct O_Do {
    String ma_the_uid; // UID thẻ RFID - định danh xe
    bool co_xe;        // Trạng thái có xe (không dùng)
    int tang;          // Tầng (1, 2, 3)
    int cot;           // Cột vị trí (1, 2, 3, 4)
};

/**
 * @array ds_o[10] - Mảng 10 ô đỗ xe
 * Phân bổ vị trí:
 *   - ds_o[0..2]:  Tầng 1, Cột 1-3  (3 ô)
 *   - ds_o[3..5]:  Tầng 2, Cột 1-3  (3 ô)
 *   - ds_o[6..9]:  Tầng 3, Cột 1-4  (4 ô)
 *
 * Ví dụ trạng thái:
 *   ds_o[0] = {ma_the_uid="4A3B2C1D", tang=1, cot=1}  // Ô T1-C1 có xe
 *   ds_o[1] = {ma_the_uid="", tang=1, cot=2}         // Ô T1-C2 rỗng
 *   ds_o[5] = {ma_the_uid="7F8E9D0C", tang=2, cot=3}  // Ô T2-C3 có xe
 */
O_Do ds_o[10];

/**
 * @array ir_cu[10] - Lưu trạng thái cảm biến IR ở lần quét TRƯỚC
 * Dùng để phát hiện thay đổi (có xe vào/ra)
 *
 * Giá trị:
 *   true  = có xe (IR kích hoạt, LOW)
 *   false = không có xe (IR không kích hoạt, HIGH)
 *
 * Ví dụ:
 *   ir_cu[0] = true   // Lần trước IR0 phát hiện xe
 *   ir_cu[1] = false  // Lần trước IR1 không phát hiện xe
 *
 * Cách dùng: So sánh với digitalRead(MANG_IR[i]) để detect sự thay đổi
 * Nếu status != ir_cu[i] → In log "IR[i]: CO XE" hoặc "IR[i]: TRONG"
 */
bool ir_cu[10];

/**
 * @array sw[4][5] - Ma trận công tắc hành trình (Limit Switch)
 * sw[t][c] = trạng thái công tắc tầng t, cột c
 *
 * Index:
 *   t = 0..3 (index tầng, không dùng index 0)
 *   c = 0..4 (index cột, không dùng index 0)
 *
 * Giá trị:
 *   true  = công tắc được kích hoạt (pallet chạm SW)
 *   false = công tắc chưa kích hoạt (pallet chưa chạm)
 *
 * Ví dụ trạng thái:
 *   sw[1][1] = true   // Tầng 1, Cột 1: Pallet 1 đã chạm SW
 *   sw[2][4] = false  // Tầng 2, Cột 4: Pallet chưa chạm SW
 *   sw[3][2] = true   // Tầng 3, Cột 2: Pallet đã chạm SW
 *
 * Cập nhật từ: Lệnh UART "SW[t][c][state]" từ bộ điều khiển motor
 * Ví dụ UART nhận: "SW141" → sw[1][4] = true (T1 C4 công tắc kích hoạt)
 * Ví dụ UART nhận: "SW220" → sw[2][2] = false (T2 C2 công tắc chưa kích hoạt)
 */
bool sw[4][5]; // Ma trận công tắc hành trình [Tầng][Cột]

// ==========================================
// 3. HÀM TIỆN ÍCH
// ==========================================
void beep(int n) {
    for (int i = 0; i < n; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
        delay(100);
    }
}

void gui_lenh(String cmd) {
    Serial2.println(cmd);
    Serial.println("[TX -> ACTION]: " + cmd);
}

/**
 * @function doc_sensor_uart - Đọc dữ liệu cảm biến từ UART
 * Nhận lệnh từ bộ điều khiển motor về trạng thái công tắc hành trình
 *
 * Định dạng lệnh nhận:
 *   "SW[t][c][state]" = "SW" + Tầng + Cột + Trạng thái
 *   Ví dụ: "SW141" → Tầng 1, Cột 4, Trạng thái=1 (kích hoạt, true)
 *   Ví dụ: "SW230" → Tầng 2, Cột 3, Trạng thái=0 (chưa kích hoạt, false)
 *   Ví dụ: "SW321" → Tầng 3, Cột 2, Trạng thái=1 (kích hoạt, true)
 *
 * Cập nhật: sw[t][c] = state (true hoặc false)
 *
 * Ví dụ thực tế:
 *   Nhận "SW141" → sw[1][4] = true  (Pallet tầng 1 chạm công tắc 4)
 *   Nhận "SW320" → sw[3][2] = false (Pallet tầng 3 chưa chạm công tắc 2)
 */
void doc_sensor_uart() {
    while (Serial2.available() > 0) {
        String msg = Serial2.readStringUntil('\n');
        msg.trim();
        if (msg.startsWith("SW") && msg.length() >= 5) {
            // Giải mã: msg = "SW141" → t=1, c=4, state=1
            int t = msg[2] - '0';         // Tầng từ ký tự index 2
            int c = msg[3] - '0';         // Cột từ ký tự index 3
            bool state = (msg[4] == '1'); // Trạng thái từ ký tự index 4

            // Kiểm tra hợp lệ: tầng 1-3, cột 1-4
            if (t >= 1 && t <= 3 && c >= 1 && c <= 4)
                sw[t][c] = state; // Cập nhật trạng thái công tắc
        }
    }
}

// ==========================================
// 4. HÀM ĐẨY PALLET VÉT CẠN (TRÁI TIM DEBUG)
// ==========================================

/**
 * @function day_den_sw - Đẩy 1 pallet đến khi chạm công tắc mục tiêu
 * @param t: Tầng (1, 2, 3)
 * @param pallet: Số pallet (1, 2, 3)
 * @param huong: Hướng di chuyển ("NP"=Ngang Phải, "NT"=Ngang Trái)
 * @param sw_target: Công tắc mục tiêu để dừng (1, 2, 3, 4)
 *
 * Quy trình:
 *   1. Kiểm tra: Nếu pallet đã ở vị trí mục tiêu (sw[t][sw_target]==true) → bỏ qua
 *   2. Gửi lệnh chuyển động UART: "[t][pallet][huong]"
 *   3. Chờ đến khi chạm công tắc mục tiêu sw[t][sw_target] = true
 *   4. Nếu quá 10 giây không chạm → lỗi (gửi "st" dừng khẩn cấp)
 *   5. Gửi lệnh dừng: "[t][pallet]ST"
 *   6. Chờ ổn định cơ học 400ms
 *
 * Ví dụ gọi 1:
 *   day_den_sw(1, 3, "NP", 4);
 *   → Gửi "13NP" (T1 P3 Ngang Phải)
 *   → Chờ sw[1][4] = true
 *   → Gửi "13ST" (dừng)
 *   → Pallet 3 tầng 1 dừng ở công tắc 4
 *
 * Ví dụ gọi 2:
 *   day_den_sw(2, 2, "NT", 1);
 *   → Gửi "22NT" (T2 P2 Ngang Trái)
 *   → Chờ sw[2][1] = true
 *   → Gửi "22ST" (dừng)
 *   → Pallet 2 tầng 2 dừng ở công tắc 1
 */
void day_den_sw(int t, int pallet, String huong, int sw_target) {
    doc_sensor_uart();
    if (sw[t][sw_target] == true) // true là 1 false là 0, nếu đã chạm SW mục tiêu rồi thì thôi
    {
        Serial.printf("P%d%d da o SW%d. Bo qua.\n", t, pallet, sw_target);
        return;
    }

    gui_lenh(String(t) + String(pallet) + huong);
    unsigned long timeout = millis();

    while (sw[t][sw_target] == false) // Chờ đến khi chạm SW mục tiêu
    {
        doc_sensor_uart();
        if (millis() - timeout > 10000) { // Quá 10s dừng khẩn cấp
            gui_lenh("st");
            Serial.println("!!! LOI: MOTOR KET");
            return;
        }
        delay(10);
    }
    gui_lenh(String(t) + String(pallet) + "ST");
    delay(400); // Nghỉ ổn định cơ khí
}

/**
 * @function don_duong_vet_can - Dọn đường (vét cạn) cho một tầng
 * @param t: Tầng cần dọn (1, 2, 3)
 * @param cot_trong_yc: Cột mục tiêu cần trống (1, 2, 3, 4)
 *
 * Chiến lược: Di chuyển tất cả pallet để trống cột mục tiêu
 * Nguyên tắc: Luôn đẩy pallet xa nhất trước để tránh va chạm
 *
 * Ví dụ 1 - Cần trống Cột 1 (cot_trong_yc=1):
 *   Trước: [P1][P2][P3][ ]
 *   Sau:   [ ][P1][P2][P3]
 *   Lệnh gửi:
 *     day_den_sw(t, 3, "NP", 4)  // P3 sang phải chạm SW4
 *     day_den_sw(t, 2, "NP", 3)  // P2 sang phải chạm SW3
 *     day_den_sw(t, 1, "NP", 2)  // P1 sang phải chạm SW2
 *
 * Ví dụ 2 - Cần trống Cột 2 (cot_trong_yc=2):
 *   Trước: [ ][P1][P2][P3]
 *   Sau:   [P1][ ][P2][P3]
 *   Lệnh gửi:
 *     day_den_sw(t, 1, "NT", 1)  // P1 sang trái chạm SW1
 *     day_den_sw(t, 3, "NP", 4)  // P3 sang phải chạm SW4
 *     day_den_sw(t, 2, "NP", 3)  // P2 sang phải chạm SW3
 *
 * Ví dụ 3 - Cần trống Cột 3 (cot_trong_yc=3):
 *   Trước: [P1][P2][ ][P3]
 *   Sau:   [P1][P2][P3][ ]
 *   Lệnh gửi:
 *     day_den_sw(t, 1, "NT", 1)  // P1 sang trái chạm SW1
 *     day_den_sw(t, 2, "NT", 2)  // P2 sang trái chạm SW2
 *     day_den_sw(t, 3, "NP", 4)  // P3 sang phải chạm SW4
 *
 * Ví dụ 4 - Cần trống Cột 4 (cot_trong_yc=4):
 *   Trước: [P1][P2][P3][ ]
 *   Sau:   [ ][P1][P2][P3]
 *   Lệnh gửi:
 *     day_den_sw(t, 1, "NT", 1)  // P1 sang trái chạm SW1
 *     day_den_sw(t, 2, "NT", 2)  // P2 sang trái chạm SW2
 *     day_den_sw(t, 3, "NT", 3)  // P3 sang trái chạm SW3
 */
void don_duong_vet_can(int t, int cot_trong_yc) {
    Serial.printf("\n--- DON DUONG T%d CHO COT %d ---\n", t, cot_trong_yc);

    if (cot_trong_yc == 1) {
        // Cần trống C1 -> 3 pallet dạt hết sang Phải
        day_den_sw(t, 3, "NP", 4); // Đẩy thằng xa nhất trước
        day_den_sw(t, 2, "NP", 3); // Đẩy thằng giữa sau
        day_den_sw(t, 1, "NP", 2); // Đẩy thằng gần nhất sau cùng
    } else if (cot_trong_yc == 2) {
        // Cần trống C2 -> P1 sang trái, P2-3 sang phải
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 3, "NP", 4);
        day_den_sw(t, 2, "NP", 3);
    } else if (cot_trong_yc == 3) {
        // Cần trống C3 -> P1-2 sang trái, P3 sang phải
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NP", 4);
    } else if (cot_trong_yc == 4) {
        // Cần trống C4 -> 3 pallet dạt hết sang Trái
        day_den_sw(t, 1, "NT", 1);
        day_den_sw(t, 2, "NT", 2);
        day_den_sw(t, 3, "NT", 3);
    }
}

// ==========================================
// 5. QUY TRÌNH GỬI / LẤY XE
// ==========================================

/**
 * @function lay_xe - Lấy xe ra khỏi bãi đỗ
 * @param idx: Index ô đỗ trong mảng ds_o[] (0-9)
 *
 * Quy trình:
 *   1. Lấy tầng & cột từ ds_o[idx]
 *   2. In log: ">>> LAY XE T[t]-C[c]"
 *   3. Nếu tầng > 1: Dọn đường các tầng DƯỚI (từ tầng 1 đến tầng-1) cho cột này
 *   4. Gửi lệnh kéo dọc "KD" để hạ xe xuống
 *   5. Xóa UID khỏi ô: ds_o[idx].ma_the_uid = ""
 *   6. Phát âm báo: 2 tiếng beep
 *
 * Ví dụ 1:
 *   ds_o[5] = {ma_the_uid="4A3B2C1D", tang=2, cot=3}
 *   lay_xe(5):
 *     → In ">>> LAY XE T2-C3"
 *     → Dọn đường T1 cho C3 (don_duong_vet_can(1, 3))
 *     → Gửi "23KD" (hạ xe từ T2-C3)\n *     → ds_o[5].ma_the_uid = "" (ô trống)\n *     → Phát 2
 * tiếng beep
 *
 * Ví dụ 2:
 *   ds_o[0] = {ma_the_uid="7F8E9D0C", tang=1, cot=1}
 *   lay_xe(0):
 *     → In ">>> LAY XE T1-C1"
 *     → Không cần dọn đường (tang == 1)\n *     → ds_o[0].ma_the_uid = "" (ô trống)\n *     → Phát
 * 2 tiếng beep
 */
void lay_xe(int idx) {
    int t = ds_o[idx].tang;
    int c = ds_o[idx].cot;
    Serial.printf("\n>>> LAY XE T%d-C%d\n", t, c);

    if (t > 1) {
        for (int i = 1; i < t; i++) {
            // Dọn đường tầng dưới theo cách vét cạn
            don_duong_vet_can(i, c);
        }
        // Hạ mâm KD (Kéo Dọc)
        gui_lenh(String(t) + String(c) + "KD");
    }
    ds_o[idx].ma_the_uid = "";
    beep(2);
}

/**
 * @function gui_xe - Đỗ xe vào bãi
 * @param uid: UID thẻ RFID của chủ xe (Ví dụ: "4A3B2C1D")
 *
 * Quy trình:
 *   1. Tìm ô trống với điều kiện:
 *      - ds_o[i].ma_the_uid == "" (ô trống trong dữ liệu)
 *      - digitalRead(MANG_IR[i]) == HIGH (IR sensor không detect xe - rỗng thực tế)
 *      - Ưu tiên tầng thấp (duyệt từ index 0 → 9)
 *
 *   2. Nếu tìm thấy (target != -1):
 *      → Lấy tầng & cột: t = ds_o[target].tang, c = ds_o[target].cot
 *      → In log: ">>> GUI XE VAO T[t]-C[c]"
 *      → Lưu uid: ds_o[target].ma_the_uid = uid
 *      → Nếu tầng > 1: Dọn đường các tầng dưới (don_duong_vet_can)\n *      → Gửi lệnh "[t][c]KD"
 * kéo dọc (xe vào ô)\n *      → Phát 1 tiếng beep (thành công)
 *
 *   3. Nếu không tìm thấy (target == -1):\n *      → In "BAI DAY!" (bãi đã đầy)\n *      → Phát 3
 * tiếng beep (thất bại)
 *
 * Ví dụ 1 (thành công):
 *   gui_xe("4A3B2C1D"):
 *     → Tầng 1 tìm được: ds_o[1] (T1-C2) trống\n *     → ds_o[1].ma_the_uid = "4A3B2C1D"\n *     →
 * Không cần dọn đường (tang == 1)\n *     → Gửi "12KD" → xe vào T1-C2\n *     → Phát 1 beep
 *
 * Ví dụ 2 (thành công - tầng cao):
 *   gui_xe("7F8E9D0C"):
 *     → Tầng 1 toàn bộ đầy, tìm được: ds_o[5] (T2-C3) trống\n *     → Dọn đường T1 cho C3
 * (don_duong_vet_can(1, 3))\n *     → Gửi "23KD" → xe vào T2-C3\n *     → Phát 1 beep
 *
 * Ví dụ 3 (thất bại):
 *   gui_xe("ABCDEF12"):
 *     → Tất cả ô đều đầy hoặc có xe\n *     → In "BAI DAY!"\n *     → Phát 3 beep
 */
void gui_xe(String uid) {
    // Tìm ô trống tầng thấp trước (ưu tiên index nhỏ = tầng thấp)
    int target = -1;
    for (int i = 0; i < 10; i++) {
        // Điều kiện: ô trống trong mảng ds_o[] VÀ IR sensor rỗng (HIGH = no car)
        if (ds_o[i].ma_the_uid == "" && (digitalRead(MANG_IR[i]) == HIGH)) {
            target = i; // Lấy ô trống đầu tiên
            break;
        }
    }

    if (target != -1) {
        int t = ds_o[target].tang;
        int c = ds_o[target].cot;
        ds_o[target].ma_the_uid = uid;
        Serial.printf("\n>>> GUI XE VAO T%d-C%d\n", t, c);

        if (t > 1) {
            for (int i = 1; i < t; i++)
                don_duong_vet_can(i, c);
            gui_lenh(String(t) + String(c) + "KD");
        }
        beep(1);
    } else {
        Serial.println("BAI DAY!");
        beep(3);
    }
}

// ==========================================
// 6. SETUP & LOOP
// ==========================================
void setup() {
    // Web settup
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

    webManager.setOnConnect([]() {});

    // Kết thúc begin setup của WebManager, bắt đầu setup của ParkingHandler

    parkingHandler.begin();

    Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX2, PIN_UART_TX2);
    Serial2.setTimeout(20);

    SPI.begin();
    rfid.PCD_Init();
    pinMode(PIN_BUZZER, OUTPUT);

    for (int i = 0; i < 10; i++) {
        pinMode(MANG_IR[i], INPUT_PULLUP);
        ir_cu[i] = (digitalRead(MANG_IR[i]) == LOW);
        ds_o[i].ma_the_uid = "";
    }

    // Khởi tạo bản đồ (Tọa độ mâm sắt)
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

    Serial.println("\n--- MASTER READY (VET CAN MODE) ---");
    beep(1);
}

void loop() {
    // Xử lý lệnh từ WebManager và ParkingHandler
    parkingHandler.processCommands();
    parkingHandler.loop();
    webManager.loop();
    // Kết thúc xử lý web

    doc_sensor_uart();

    // Quét IR báo trạng thái xe
    for (int i = 0; i < 10; i++) {
        bool status = (digitalRead(MANG_IR[i]) == LOW);
        if (status != ir_cu[i]) {
            ir_cu[i] = status;
            Serial.printf("IR%d: %s\n", i, status ? "CO XE" : "TRONG");
        }
    }

    // Quét thẻ RFID
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial())
        return;

    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
        uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();

    int found = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == uid) {
            found = i;
            break;
        }
    }

    if (found != -1)
        lay_xe(found);
    else
        gui_xe(uid);

    rfid.PICC_HaltA();
    delay(500);
}