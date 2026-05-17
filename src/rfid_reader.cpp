#include "rfid_reader.h"
#include "log.h"

#include <Arduino.h>

static const char *TAG_RFID = "RFID";

static const unsigned long RFID_DEBOUNCE_MS = 5000;
static String lastRfidUid = "";
static unsigned long lastRfidMillis = 0;

static bool isValidRfidUid(const String &uid) {
    if (uid.length() != 8) {
        return false;
    }
    for (size_t i = 0; i < uid.length(); ++i) {
        if (!isxdigit(uid[i])) {
            return false;
        }
    }
    return true;
}

static uint8_t calculateUidChecksum(const String &uidString) {
    if (uidString.length() != 8) {
        return 0;
    }
    String normalized = uidString;
    normalized.toUpperCase();
    uint8_t checksum = 0;
    for (size_t i = 0; i < normalized.length(); ++i) {
        checksum += normalized[i];
    }
    return checksum;
}

static bool parseRfidPayload(const String &payload, String &uid) {
    if (!payload.startsWith("UID|")) {
        return false;
    }
    int secondSep = payload.indexOf('|', 4);
    if (secondSep < 0) {
        return false;
    }
    String uidString = payload.substring(4, secondSep);
    String checksumString = payload.substring(secondSep + 1);
    uidString.trim();
    checksumString.trim();
    uidString.toUpperCase();
    checksumString.toUpperCase();
    if (!isValidRfidUid(uidString) || checksumString.length() != 2) {
        return false;
    }
    String expected = String(calculateUidChecksum(uidString), HEX);
    expected.toUpperCase();
    if (expected.length() == 1) {
        expected = "0" + expected;
    }
    if (checksumString != expected) {
        return false;
    }
    uid = uidString;
    return true;
}

bool readRfidFromSerial(String &uid) {
    while (Serial.available() > 0) {
        String tin_nhan = Serial.readStringUntil('\n');
        tin_nhan.trim();
        tin_nhan.toUpperCase();

        if (tin_nhan.length() == 0) {
            return false;
        }

        String parsedUid;
        if (!tin_nhan.startsWith("UID|") || !parseRfidPayload(tin_nhan, parsedUid)) {
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
