#include "hardware.h"
#include "log.h"
#include "parking_handler.h"
#include "parking_process.h"
#include "parking_state.h"
#include "rfid_reader.h"
#include "websever.h"
#include "wifimanager.h"
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/uart.h>
#include <sys/time.h>
#include <time.h>

SET_LOOP_TASK_STACK_SIZE(16384);

static const char *TAG_MAIN = "MAIN";

WebManager webManager;
WifiManager wifiManager;
ParkingHandler parkingHandler(wifiManager);

int trangThaiNutTruocDo = HIGH;
unsigned long thoiGianDebounceCuoiCung = 0;
const unsigned long doTreDebounce = 50;
int cheDoTuDong = 0; // 0 = Manual, 1 = Auto

// -1 for non-delay functions, otherwise delay in ms
void softDelay(unsigned long ms) {
    unsigned long start = millis();
    parkingHandler.loop();
    webManager.loop();
    while (millis() - start < ms) {
        parkingHandler.loop();
        webManager.loop();
        delay(5);
    }
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, PIN_UART_RX2, PIN_UART_TX2);
    Serial1.begin(115200, SERIAL_8N1, PIN_UART_RX1, PIN_UART_TX1);

    LOG_I_INLINE(TAG_MAIN, "Setup ");

    wifiManager.begin();
    webManager.begin();

    parkingHandler.setSendFn(
        [](const uint8_t *data, size_t len) { webManager.sendBinary(data, len); });
    parkingHandler.setClientCountFn([]() -> size_t { return webManager.clientCount(); });

    webManager.setOnBinary(
        [](const uint8_t *data, size_t len) { parkingHandler.enqueueBinary(data, len); });
    webManager.setOnConnect([]() {
        sendCurrentParkingStatus();
        parkingHandler.sendStatus();
    });

    parkingHandler.begin();

    LOG_I_INLINE(TAG_MAIN, "\033[0;32m web communication initialized! \033[0m");

    ledcSetup(KENH_PWM, TAN_SO_PWM, DO_PHAN_GIAI);
    ledcAttachPin(PIN_SERVO_CONG, KENH_PWM);
    dung_motor_cong();

    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_NUT_XAC_NHAN, INPUT_PULLUP);
    pinMode(PIN_CHON_CHE_DO, INPUT_PULLUP); // GND = Auto, HIGH = Manual

    memset(sw, 0, sizeof(sw));
    memset(cam_bien_vi_tri, 0, sizeof(cam_bien_vi_tri));

    LOG_I_INLINE(TAG_MAIN, "\033[0;32m pin setup completed! \033[0m");

    initParkingPositions();

    Serial.println();
    Serial1.println();
    Serial2.println();

    softDelay(12000);

    motor_keo(2, 1, "KD", 800);

    motor_keo(2, 1, "KU", 8000);
    motor_keo(2, 2, "KU", 8000);
    motor_keo(2, 3, "KU", 8000);

    motor_keo(3, 1, "KU", 13000);
    motor_keo(3, 2, "KU", 13000);
    motor_keo(3, 3, "KU", 13000);
    motor_keo(3, 4, "KU", 13000);

    don_duong_vet_can(1, 1);
    don_duong_vet_can(2, 1);
    LOG(TAG_MAIN, "\033[0;32m Parking positions initialized \033[0m");

    beep(1);
}
void loop() {
    static unsigned long thoiGianBatDauNhan = 0;

    // Kiem tra xem nut co dang bi bam giu hay khong (muc LOW)
    if (digitalRead(PIN_CHON_CHE_DO) == LOW) {
        if (thoiGianBatDauNhan == 0) {
            thoiGianBatDauNhan = millis();
        } else if (millis() - thoiGianBatDauNhan >= 2000) {
            LOG(TAG_MAIN, "Mode switched: AUTO (Locked)");
            Serial.println("Da chuyen sang che do: TU DONG. He thong da khoa.");
            Serial.println("Nhan nut EN/RST tren ESP32 de quay ve THU CONG.");

            digitalWrite(PIN_BUZZER, HIGH);
            delay(3000);
            digitalWrite(PIN_BUZZER, LOW);

            while (true) {
                kich_ban_auto();
                delay(10);
            }
        }
    } else {
        thoiGianBatDauNhan = 0;
    }

    parkingHandler.loop();
    webManager.loop();

    update_sensor();

    String uid = "";
    if (!readRfidFromSerial(uid)) {
        return;
    }

    LOG(TAG_MAIN, "RFID scanned: %s", uid.c_str());

    int vi_tri_tim_thay = -1;
    for (int i = 0; i < 10; i++) {
        if (ds_o[i].ma_the_uid == uid) {
            vi_tri_tim_thay = i;
            break;
        }
    }

    if (vi_tri_tim_thay != -1) {
        lay_xe(vi_tri_tim_thay, false);
        ds_o[vi_tri_tim_thay].ma_the_uid = "";
    } else {
        int muc_tieu = -1;
        for (int i = 0; i < 10; i++) {
            if (ds_o[i].ma_the_uid == "") {
                muc_tieu = i;
                break;
            }
        }

        if (muc_tieu != -1) {
            ds_o[muc_tieu].ma_the_uid = uid;
            gui_xe(muc_tieu, false);
        }
    }
    uart_flush_input(UART_NUM_0);

    delay(500);
}