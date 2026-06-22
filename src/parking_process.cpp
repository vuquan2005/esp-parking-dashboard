#include "parking_process.h"
#include "hardware.h"
#include "log.h"
#include <Arduino.h>

static const char *TAG_PARKING = "PARKING";
static const char *TAG_PARSER = "PARSER";
static const char *TAG_MOTOR = "MOTOR";
static const char *TAG_SEND_UART = "SEND_UART";
static const char *TAG_SENSOR = "SENSOR";
static const char *TAG_PATH = "PATH";
static const char *TAG_BUTTON = "BUTTON";

static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static const unsigned long BUTTON_PRESS_TIMEOUT_MS = 20000;
static const unsigned int BUTTON_AUTO_TIME_OUT = 5000;

static bool waitForButtonPress(unsigned long timeoutMs) {
    if (timeoutMs == 0) {
        return true;
    }
    unsigned long pressedAt = 0;
    unsigned long startTime = millis();

    while (millis() - startTime < timeoutMs) {
        update_sensor();
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

// Initialize the parking slot lookup array `ds_o`.
// Slots 0..2 map to floor 1, columns 1..3.
// Slots 3..5 map to floor 2, columns 1..3.
// Slots 6..9 map to floor 3, columns 1..4.
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
    LOG_I_INLINE(TAG_SEND_UART, "ACTION: %s", lenh.c_str());
}

void dieu_khien_goc_servo(int goc) {
    int duty = map(goc, 0, 180, 102, 512);
    ledcWrite(KENH_PWM, duty);
}

void dung_motor_cong() {
    ledcWrite(KENH_PWM, 0);
}

void mo_cong() {
    LOG_I_INLINE(TAG_PARKING, "DANG MO CONG... ");
    dieu_khien_goc_servo(90);
    delay(1000);
    dung_motor_cong();
    LOG_I_INLINE(TAG_PARKING, "\033[0;32m Success! \033[0m");
}

void dong_cong() {
    LOG_I_INLINE(TAG_PARKING, "DANG DONG CUA... ");
    dieu_khien_goc_servo(0);
    delay(1000);
    dung_motor_cong();
    LOG_I_INLINE(TAG_PARKING, "\033[0;32m Success! \033[0m");
}

static bool process_grouped_sensor(const String &packet) {
    int firstSpace = packet.indexOf(' ');
    if (firstSpace == -1)
        return false;

    String prefix = packet.substring(0, firstSpace);

    if (prefix.equalsIgnoreCase("SW")) {
        int secondSpace = packet.indexOf(' ', firstSpace + 1);
        if (secondSpace == -1)
            return false;

        String t1 = packet.substring(firstSpace + 1, secondSpace);
        String t2 = packet.substring(secondSpace + 1);

        if (t1.length() >= 4) {
            for (int c = 1; c <= 4; c++)
                sw[1][c] = (t1[c - 1] == '1');
        }
        if (t2.length() >= 4) {
            for (int c = 1; c <= 4; c++)
                sw[2][c] = (t2[c - 1] == '1');
        }
        return true;

    } else if (prefix.equalsIgnoreCase("IR")) {
        int secondSpace = packet.indexOf(' ', firstSpace + 1);
        if (secondSpace == -1)
            return false;

        int thirdSpace = packet.indexOf(' ', secondSpace + 1);
        if (thirdSpace == -1)
            return false;

        String t1 = packet.substring(firstSpace + 1, secondSpace);
        String t2 = packet.substring(secondSpace + 1, thirdSpace);
        String t3 = packet.substring(thirdSpace + 1);

        if (t1.length() >= 4) {
            for (int c = 1; c <= 4; c++)
                cam_bien_vi_tri[1][c] = (t1[c - 1] == '1');
        }
        if (t2.length() >= 3) {
            for (int c = 1; c <= 3; c++)
                cam_bien_vi_tri[2][c] = (t2[c - 1] == '1');
        }
        if (t3.length() >= 4) {
            for (int c = 1; c <= 4; c++)
                cam_bien_vi_tri[3][c] = (t3[c - 1] == '1');
        }
        return true;
    }

    return false;
}

void update_sensor() {
    while (Serial1.available() > 0) {
        String tin_nhan = Serial1.readStringUntil('\n');
        tin_nhan.trim();

        if (tin_nhan.length() == 0)
            continue;

        LOG_D_INLINE(TAG_SENSOR, tin_nhan.c_str());

        if (!process_grouped_sensor(tin_nhan)) {
            LOG_W_INLINE(TAG_PARSER, "Unknown packet format: %s", tin_nhan.c_str());
        }
    }

    parkingHandler.loop();
    webManager.loop();
}

void motor_keo(int t, int c, const String &huong, int timeOut) {
    if (cam_bien_vi_tri[t][c])
        return;
    gui_lenh_motor(String(t) + String(c) + String(huong));
    delay(300);
    unsigned long timeout = millis();
    while (!cam_bien_vi_tri[t][c]) {
        update_sensor();
        if (millis() - timeout > timeOut) {
            gui_lenh_motor("st");
            LOG_E(TAG_MOTOR, "MOTOR KEO KET: SW%d-%d KHONG HOAT DONG!", t, c);
            return;
        }
        delay(10);
    }
    gui_lenh_motor("st");
}

void day_den_sw(int row, int pallet, const String &huong, int sw_target) {
    update_sensor();
    if (sw[row][sw_target]) {
        return;
    }

    gui_lenh_motor(String(row) + String(pallet) + huong);
    unsigned long timeout = millis();
    while (!sw[row][sw_target]) {
        update_sensor();
        if (millis() - timeout > 10000) {
            gui_lenh_motor("st");
            LOG_E(TAG_MOTOR, "MOTOR NGANG KET: SW%d-%d KHONG HOAT DONG!", row, sw_target);
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
    LOG_I(TAG_PATH, "--- DON DUONG T%d CHO COT %d ---", row, cot_trong_yc);
    recalcStatus();
    if (cot_trong_yc == 1) {
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NP", 3);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NP", 2);

        recalcStatus();
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 2) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NP", 3);

        recalcStatus();
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 3) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NT", 2);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NP", 4);

        recalcStatus();
        sendCurrentParkingStatus();
    } else if (cot_trong_yc == 4) {
        SlotStatus[rowPallet2SlotIndex(row, 1)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PENDING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 1, "NT", 1);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 2)] = ParkingStatus_Status_PROCESSING;
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PENDING;
        sendCurrentParkingStatus();

        day_den_sw(row, 2, "NT", 2);

        recalcStatus();
        SlotStatus[rowPallet2SlotIndex(row, 3)] = ParkingStatus_Status_PROCESSING;
        sendCurrentParkingStatus();

        day_den_sw(row, 3, "NT", 3);

        recalcStatus();
        sendCurrentParkingStatus();
    }
}

void cho_nguoi_dung_xac_nhan(bool isAuto) {
    unsigned long timeoutMs = isAuto ? BUTTON_AUTO_TIME_OUT : BUTTON_PRESS_TIMEOUT_MS;
    LOG_I_INLINE(TAG_BUTTON, "Wait for button press...");
    if (!waitForButtonPress(timeoutMs)) {
        LOG_E(TAG_BUTTON, "TIMEOUT CHO NUT XAC NHAN!");
        return;
    }

    if (!isAuto) {
        LOG_I_INLINE(TAG_BUTTON, "\033[0;32m Button pressed! \033[0m");
        beep(2);
        delay(500);
    } else {
        LOG_I_INLINE(TAG_BUTTON, "Auto confirmation.");
    }
}

static void xu_ly_xe_chung(int target, bool is_gui, bool isAuto) {
    int t = ds_o[target].row;
    int c = ds_o[target].col;

    int pallet_id = rowPallet2SlotID(t, c);
    int slotIndex = rowPallet2SlotIndex(t, c);
    if (pallet_id < 1 || slotIndex < 0) {
        LOG_W(TAG_PARSER, "Invalid slot mapping for target=%d (t=%d,c=%d)", target, t, c);
        return;
    }

    recalcStatus();
    SlotStatus[slotIndex] = is_gui ? ParkingStatus_Status_PENDING : ParkingStatus_Status_PROCESSING;
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, is_gui ? ParkingEvent_EventType_IN : ParkingEvent_EventType_OUT, false);
    
    if (is_gui) {
        LOG_I(TAG_PARKING, "GUI XE VAO T%d-C%d", t, c);
    } else {
        LOG_I(TAG_PARKING, "LAY XE T%d-C%d", t, c);
    }

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
            update_sensor();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    mo_cong();
    cho_nguoi_dung_xac_nhan(isAuto);
    dong_cong();

    if (t > 1) {
        gui_lenh_motor(String(t) + String(c) + "KU");
        delay(300);
        while (!cam_bien_vi_tri[t][c]) {
            update_sensor();
            delay(10);
        }
        gui_lenh_motor("st");
    }

    if (!is_gui) {
        LOG_I(TAG_PARKING, "HOAN TAT LAY XE. O DA TRONG.");
    }

    recalcStatus();
    sendCurrentParkingStatus();
    sendCurrentParkingEvent(pallet_id, is_gui ? ParkingEvent_EventType_IN : ParkingEvent_EventType_OUT, true);
    beep(is_gui ? 1 : 2);
}

void gui_xe(int target, bool isAuto) {
    xu_ly_xe_chung(target, true, isAuto);
}

void lay_xe(int target, bool isAuto) {
    xu_ly_xe_chung(target, false, isAuto);
}

void kich_ban_auto() {
    
    gui_xe(6, true);

    gui_xe(3, true);

    softDelay(6000);
}