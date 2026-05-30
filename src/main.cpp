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

    ledcSetup(KENH_PWM, TAN_SO_PWM, DO_PHAN_GIAI);
    ledcAttachPin(PIN_SERVO_CONG, KENH_PWM);
    dung_motor_cong();

    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_NUT_XAC_NHAN, INPUT_PULLUP);

    memset(sw, 0, sizeof(sw));
    memset(cam_bien_vi_tri, 0, sizeof(cam_bien_vi_tri));

    initParkingPositions();

    LOG_I_INLINE(TAG_MAIN, "\033[0;32m completed! \033[0m");

    Serial.println();
    Serial1.println();
    Serial2.println();
    delay(5000);

    don_duong_vet_can(1, 4);
    don_duong_vet_can(2, 4);
    LOG(TAG_MAIN, "\033[0;32m Parking positions initialized \033[0m");

    beep(1);
}

void loop() {
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
        lay_xe(vi_tri_tim_thay);
        uart_flush_input(UART_NUM_0);
    } else {
        gui_xe(uid);
        uart_flush_input(UART_NUM_0);
    }

    delay(500);
}
