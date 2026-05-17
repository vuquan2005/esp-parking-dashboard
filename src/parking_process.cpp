#include "parking_process.h"
#include "hardware.h"
#include <Arduino.h>

static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static const unsigned long BUTTON_PRESS_TIMEOUT_MS = 20000;

static bool waitForButtonPress() {
    unsigned long pressedAt = 0;
    unsigned long startTime = millis();

    while (millis() - startTime < BUTTON_PRESS_TIMEOUT_MS) {
        bool pressed = digitalRead(PIN_NUT_XAC_NHAN) == LOW;
        if (pressed) {
            if (pressedAt == 0) {
                pressedAt = millis();
            } else if (millis() - pressedAt >= BUTTON_DEBOUNCE_MS) {
                return true;
            }
        } else {
            pressedAt = 0;
        }
        delay(5);
    }
    return false;
}

void initParkingPositions() {
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
}

void beep(int n) {
    for (int i = 0; i < n; i++) {
        digitalWrite(PIN_BUZZER, HIGH);
        delay(100);
        digitalWrite(PIN_BUZZER, LOW);
        delay(100);
    }
}

void gui_lenh_motor(const String &lenh) {
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

void mo_cong() {
    Serial.println(">> DANG MO CONG...");
    dieu_khien_goc_servo(90);
    delay(1000);
    dung_motor_cong();
    Serial.println(">> CUA DA MO HOAN TOAN.");
}

void dong_cua_chinh() {
    Serial.println(">> DANG DONG CUA...");
    dieu_khien_goc_servo(0);
    delay(1000);
    dung_motor_cong();
    Serial.println(">> CUA DA DONG AN TOAN.");
}

void cap_nhat_tin_hieu_ngoai_vi() {
    while (Serial1.available() > 0) {
        String tin_nhan = Serial1.readStringUntil('\n');
        tin_nhan.trim();

        if (tin_nhan.length() > 0) {
            Serial.print(">>> [UART1 - ESP SENSOR]: ");
            Serial.println(tin_nhan);
        }

        if (tin_nhan.startsWith("SW") && tin_nhan.length() >= 5) {
            int t = tin_nhan[2] - '0';
            int c = tin_nhan[3] - '0';
            bool trang_thai_sw = (tin_nhan[4] == '1');
            if (t >= 0 && t <= 3 && c >= 1 && c <= 4) {
                sw[t][c] = trang_thai_sw;
            }
        } else if ((tin_nhan.startsWith("IR") || tin_nhan.startsWith("ir")) &&
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
}

void day_den_sw(int row, int pallet, const String &huong, int sw_target) {
    cap_nhat_tin_hieu_ngoai_vi();
    if (sw[row][sw_target]) {
        return;
    }

    gui_lenh_motor(String(row) + String(pallet) + huong);
    unsigned long timeout = millis();
    while (!sw[row][sw_target]) {
        cap_nhat_tin_hieu_ngoai_vi();
        if (millis() - timeout > 10000) {
            gui_lenh_motor("st");
            Serial.println("!!! LOI: MOTOR NGANG KET");
            return;
        }
        delay(10);
    }
    gui_lenh_motor(String(row) + String(pallet) + "ST");
    gui_lenh_motor("st");
    delay(400);

    const int target = rowPallet2SlotID(row, pallet);
    if (huong == "NP") {
        movePalletInGrid(target, 1);
    } else if (huong == "NT") {
        movePalletInGrid(target, 2);
    }
}

void don_duong_vet_can(int row, int cot_trong_yc) {
    Serial.printf("\n--- DON DUONG T%d CHO COT %d ---\n", row, cot_trong_yc);
    if (cot_trong_yc == 1) {
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NP", 3);

        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NP", 2);

        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 2) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NP", 3);

        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 3) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NT", 2);

        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 4) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NT", 2);

        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_UNKNOWN;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NT", 3);

        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_UNKNOWN;
        sendCurrentParkingStatus();
    }
}

void cho_nguoi_dung_xac_nhan() {
    Serial.println(">> DANG CHO BAM NUT XAC NHAN...");
    if (!waitForButtonPress()) {
        Serial.println("!!! LOI: KHONG NHAN DUOC NUT XAC NHAN TRONG THOI GIAN QUI DINH");
        return;
    }

    Serial.println(">> DA NHAN NUT XAC NHAN!");
    beep(2);
    delay(500);
}

void gui_xe(const String &uid) {
    int target = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == "") {
            target = i;
            break;
        }
    }

    if (target == -1) {
        return;
    }

    int t = ds_o[target].row;
    int c = ds_o[target].col;
    ds_o[target].ma_the_uid = uid;

    int pallet_id = rowPallet2SlotID(t, c);
    int slotIndex = rowPallet2SlotIndex(t, c);
    if (pallet_id < 1 || slotIndex < 0) {
        Serial.printf("[VQ] Invalid slot mapping for target=%d (t=%d,c=%d)\n", target, t, c);
        return;
    }

    SlotStatus[slotIndex] = ParkingStatus_Status_PENDING;
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_IN, false);
    Serial.printf("\n>>> GUI XE VAO T%d-C%d\n", t, c);

    if (t > 1) {
        if (t == 2) {
            don_duong_vet_can(2, 4);
        }
        for (int i = 1; i < t; i++) {
            don_duong_vet_can(i, c);
        }

        SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        gui_lenh_motor(String(t) + String(c) + "KD");
        delay(300);
        while (!cam_bien_vi_tri[1][c]) {
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
        while (!cam_bien_vi_tri[t][c]) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    recalcStatus();
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_IN, true);
    beep(1);
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

    SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_OUT, false);
    Serial.printf("\n>>> LAY XE T%d-C%d\n", t, c);

    if (t > 1) {
        if (t == 2) {
            don_duong_vet_can(2, 4);
        }
        for (int i = 1; i < t; i++) {
            don_duong_vet_can(i, c);
        }

        SlotStatus[slotIndex] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();
        gui_lenh_motor(String(t) + String(c) + "KD");
        delay(300);
        while (!cam_bien_vi_tri[1][c]) {
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
        while (!cam_bien_vi_tri[t][c]) {
            cap_nhat_tin_hieu_ngoai_vi();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    ds_o[target].ma_the_uid = "";
    Serial.println(">> HOAN TAT LAY XE. O DA TRONG.");

    recalcStatus();
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, ParkingEvent_EventType_OUT, true);
    beep(2);
}
