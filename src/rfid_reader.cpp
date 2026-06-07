#include "rfid_reader.h"
#include "log.h"
#include "serial_motor_control.h"

#include <Arduino.h>
#include <ctype.h>

static const char *TAG_RFID = "RFID";

static const unsigned long RFID_DEBOUNCE_MS = 1000;
static String lastRfidUid = "";
static String lastRfidCounter = "";
static unsigned long lastRfidMillis = 0;

static bool isValidRfidUid(const char *uid, size_t len) {
    if (len != 8) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!isxdigit((unsigned char)uid[i])) {
            return false;
        }
    }
    return true;
}

static uint8_t calculateUidChecksum(const char *uidString, size_t len) {
    if (len != 8) {
        return 0;
    }
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum += (uint8_t)toupper((unsigned char)uidString[i]);
    }
    return checksum;
}

static bool copyAndTrimUpper(const char *start, size_t length, char *dest, size_t destSize) {
    size_t begin = 0;
    while (begin < length && isspace((unsigned char)start[begin])) {
        begin++;
    }
    size_t end = length;
    while (end > begin && isspace((unsigned char)start[end - 1])) {
        end--;
    }

    size_t trimmedLen = end - begin;
    if (trimmedLen >= destSize) {
        return false;
    }

    for (size_t i = 0; i < trimmedLen; ++i) {
        dest[i] = toupper((unsigned char)start[begin + i]);
    }
    dest[trimmedLen] = '\0';
    return true;
}

static bool parseRfidPayload(const String &payload, String &uid, bool &isAuto) {
    const char *data = payload.c_str();
    size_t payloadLen = payload.length();
    if (payloadLen == 0) {
        return false;
    }

    const char *fieldStarts[5];
    size_t fieldLens[5];
    size_t fieldCount = 0;
    const char *p = data;
    const char *fieldStart = data;

    while (*p) {
        if (*p == '|') {
            if (fieldCount >= 5) {
                return false;
            }
            fieldStarts[fieldCount] = fieldStart;
            fieldLens[fieldCount] = p - fieldStart;
            fieldCount++;
            p++;
            fieldStart = p;
            continue;
        }
        p++;
    }

    if (fieldCount >= 5) {
        return false;
    }
    fieldStarts[fieldCount] = fieldStart;
    fieldLens[fieldCount] = p - fieldStart;
    fieldCount++;

    if (fieldCount != 2 && fieldCount != 5) {
        return false;
    }

    char field0[4] = {0};
    char uidString[9] = {0};

    if (!copyAndTrimUpper(fieldStarts[0], fieldLens[0], field0, sizeof(field0)) || strcmp(field0, "UID") != 0) {
        return false;
    }

    if (!copyAndTrimUpper(fieldStarts[1], fieldLens[1], uidString, sizeof(uidString)) || !isValidRfidUid(uidString, strlen(uidString))) {
        return false;
    }

    if (fieldCount == 2) {
        lastRfidCounter = "";
        uid = String(uidString);
        isAuto = false;
        return true;
    }

    char checksumString[3] = {0};
    char counterString[17] = {0};
    char isAutoString[6] = {0};

    if (!copyAndTrimUpper(fieldStarts[2], fieldLens[2], checksumString, sizeof(checksumString)) || strlen(checksumString) != 2) {
        return false;
    }

    if (!copyAndTrimUpper(fieldStarts[3], fieldLens[3], counterString, sizeof(counterString)) || strlen(counterString) == 0) {
        return false;
    }

    if (!copyAndTrimUpper(fieldStarts[4], fieldLens[4], isAutoString, sizeof(isAutoString))) {
        return false;
    }

    if (lastRfidCounter.equals(counterString)) {
        return false;
    }

    uint8_t checksum = calculateUidChecksum(uidString, strlen(uidString));
    char expected[3] = {0};
    const char *hexDigits = "0123456789ABCDEF";
    expected[0] = hexDigits[(checksum >> 4) & 0x0F];
    expected[1] = hexDigits[checksum & 0x0F];

    if (checksumString[0] != expected[0] || checksumString[1] != expected[1]) {
        return false;
    }

    lastRfidCounter = counterString;
    uid = String(uidString);
    isAuto = (strcmp(isAutoString, "TRUE") == 0);
    return true;
}

bool readRfidFromSerial(String &uid, bool &isAuto) {
    while (Serial.available() > 0) {
        String tin_nhan = Serial.readStringUntil('\n');
        tin_nhan.trim();
        tin_nhan.toUpperCase();

        if (tin_nhan.length() == 0) {
            return false;
        }

        // Intercept and handle motor commands (e.g. 21NP, 23NT, 21KD, 21KU)
        if (xu_ly_lenh_motor_serial0(tin_nhan)) {
            return false;
        }

        String parsedUid;
        if (!tin_nhan.startsWith("UID|") || !parseRfidPayload(tin_nhan, parsedUid, isAuto)) {
            LOG_E(TAG_RFID, "invalid payload: %s", tin_nhan.c_str());
            return false;
        }

        unsigned long now = millis();
        if (parsedUid == lastRfidUid && now - lastRfidMillis < RFID_DEBOUNCE_MS) {
            LOG_D(TAG_RFID, "duplicate UID ignored within %lu ms debounce window",
                  RFID_DEBOUNCE_MS);
            return false;
        }

        lastRfidUid = parsedUid;
        lastRfidMillis = now;
        LOG_D(TAG_RFID, "%s", parsedUid.c_str());
        uid = parsedUid;
        return true;
    }
    return false;
}
