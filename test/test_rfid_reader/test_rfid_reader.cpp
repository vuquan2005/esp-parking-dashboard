#include "../../src/log.h"
#include <Arduino.h>
#include <unity.h>

// 1. Mock/Stub external dependencies used by rfid_reader.cpp
// Stub for serial motor control
bool xu_ly_lenh_motor_serial0(const String &cmd) {
    if (cmd == "21NP")
        return true;
    return false;
}

// Stubs for logging functions to avoid linking issues
extern "C" {
void log_print(log_level_t level, const char *tag, const char *format, ...) {}
void log_print_inline(log_level_t level, const char *tag, const char *format, ...) {}
void log_flush(void) {}
}

// 2. Include the implementation file directly to test its static functions
#include "../../src/rfid_reader.cpp"

void setUp(void) {
    // Reset the static variables defined in rfid_reader.cpp
    lastRfidUid = "";
    lastRfidCounter = "";
    lastRfidMillis = 0;
}

void tearDown(void) {}

// Test validation of UID format (must be 8 hex characters)
void test_isValidRfidUid(void) {
    TEST_ASSERT_TRUE(isValidRfidUid("12345678", 8));
    TEST_ASSERT_TRUE(isValidRfidUid("ABCDEF09", 8));
    TEST_ASSERT_TRUE(isValidRfidUid("abcdef09", 8));

    TEST_ASSERT_FALSE(isValidRfidUid("1234567", 7));   // Too short
    TEST_ASSERT_FALSE(isValidRfidUid("123456789", 9)); // Too long
    TEST_ASSERT_FALSE(isValidRfidUid("1234567G", 8));  // G is not a hex digit
}

// Test checksum calculation
void test_calculateUidChecksum(void) {
    // '1'(49) + '2'(50) + '3'(51) + '4'(52) + '5'(53) + '6'(54) + '7'(55) + '8'(56) = 420 (0x1A4)
    // Low byte of 0x1A4 is 0xA4
    TEST_ASSERT_EQUAL(0xA4, calculateUidChecksum("12345678", 8));

    // Test with lower case conversion check
    TEST_ASSERT_EQUAL(calculateUidChecksum("ABCDEF09", 8), calculateUidChecksum("abcdef09", 8));
}

// Test parsing of full payload string: "UID|<8-digit-hex>|<2-digit-checksum>" and legacy
// "UID|<8-digit-hex>|<2-digit-checksum>|<counter>" or "UID|<8-digit-hex>|<2-digit-checksum>|<counter>|<isAuto>"
void test_parseRfidPayload(void) {
    String uidResult;
    bool isAutoResult = false;

    // Valid payload with counter
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4|1", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_FALSE(isAutoResult);

    // Duplicate counter should be rejected
    TEST_ASSERT_FALSE(parseRfidPayload("UID|12345678|A4|1", uidResult, isAutoResult));

    // New counter value should be accepted again
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4|2", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_FALSE(isAutoResult);

    // Invalid format (missing prefix)
    TEST_ASSERT_FALSE(parseRfidPayload("12345678|A4|1", uidResult, isAutoResult));

    // Legacy format without counter should still be accepted
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_FALSE(isAutoResult);

    // Payload with empty counter after separator should be rejected
    TEST_ASSERT_FALSE(parseRfidPayload("UID|12345678|A4|", uidResult, isAutoResult));

    // Incorrect checksum
    TEST_ASSERT_FALSE(parseRfidPayload("UID|12345678|A3|3", uidResult, isAutoResult));

    // Valid payload with counter and isAuto = TRUE
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4|3|TRUE", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_TRUE(isAutoResult);

    // Valid payload with counter and isAuto = FALSE
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4|4|FALSE", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_FALSE(isAutoResult);

    // Valid payload with counter and isAuto = other values (treated as false)
    TEST_ASSERT_TRUE(parseRfidPayload("UID|12345678|A4|5|MAYBE", uidResult, isAutoResult));
    TEST_ASSERT_EQUAL_STRING("12345678", uidResult.c_str());
    TEST_ASSERT_FALSE(isAutoResult);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_isValidRfidUid);
    RUN_TEST(test_calculateUidChecksum);
    RUN_TEST(test_parseRfidPayload);
    UNITY_END();
}

void loop() {}
