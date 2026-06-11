#include <unity.h>
#include <Arduino.h>
#include "parking_state.h"
#include "../../src/log.h"

// Stub logging functions to avoid linking the full logging module in unit tests.
extern "C" {
void log_print(log_level_t level, const char *tag, const char *format, ...) {}
void log_print_inline(log_level_t level, const char *tag, const char *format, ...) {}
void log_flush(void) {}
}

// Include implementation directly so tested symbols are compiled into this test binary.
#include "../../src/parking_state.cpp"

// Set up before each test
void setUp(void) {
    // Reset or initialize global states if needed
    for (int i = 0; i < 10; i++) {
        ds_o[i].rfid = "";
        ds_o[i].row = 0;
        ds_o[i].col = 0;
    }
}

// Tear down after each test
void tearDown(void) {
}

// Test mapping from row/pallet to Slot ID
void test_rowPallet2SlotID(void) {
    // Valid cases
    TEST_ASSERT_EQUAL(1, rowPallet2SlotID(3, 1));
    TEST_ASSERT_EQUAL(4, rowPallet2SlotID(3, 4));
    TEST_ASSERT_EQUAL(5, rowPallet2SlotID(2, 1));
    TEST_ASSERT_EQUAL(7, rowPallet2SlotID(2, 3));
    TEST_ASSERT_EQUAL(8, rowPallet2SlotID(1, 1));
    TEST_ASSERT_EQUAL(10, rowPallet2SlotID(1, 3));

    // Invalid row
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(0, 1));
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(4, 1));

    // Invalid index
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(3, 0));
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(3, 5));

    // Invalid index in specific rows (row 1 & 2 only allow 1-3 index)
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(2, 4));
    TEST_ASSERT_EQUAL(-1, rowPallet2SlotID(1, 4));
}

// Test finding pallet index in the Grid
void test_findGridIndex(void) {
    // Reset Grid to known test state
    // uint32_t Grid[12] = {1, 2, 3, 4, 5, 6, 7, 0, 8, 9, 10, 0};
    Grid[0] = 1; Grid[1] = 2; Grid[2] = 3; Grid[3] = 4;
    Grid[4] = 5; Grid[5] = 6; Grid[6] = 7; Grid[7] = 0;
    Grid[8] = 8; Grid[9] = 9; Grid[10] = 10; Grid[11] = 0;

    TEST_ASSERT_EQUAL(0, findGridIndex(1));
    TEST_ASSERT_EQUAL(4, findGridIndex(5));
    TEST_ASSERT_EQUAL(10, findGridIndex(10));
    TEST_ASSERT_EQUAL(7, findGridIndex(0)); // empty slot
    TEST_ASSERT_EQUAL(-1, findGridIndex(99)); // non-existent pallet
}

// Test moving pallet in grid
void test_movePalletInGrid(void) {
    Grid[0] = 1; Grid[1] = 2; Grid[2] = 3; Grid[3] = 4;
    Grid[4] = 5; Grid[5] = 6; Grid[6] = 7; Grid[7] = 0;
    Grid[8] = 8; Grid[9] = 9; Grid[10] = 10; Grid[11] = 0;

    // Pallet 7 (at index 6) is in row 1, col 2. Right neighbor (index 7) is 0 (empty).
    // Let's move Pallet 7 to the right (direction = 1)
    int result = movePalletInGrid(7, 1);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, Grid[6]);
    TEST_ASSERT_EQUAL(7, Grid[7]);

    // Pallet 1 (at index 0) is on the top row (row 0). It cannot be moved.
    result = movePalletInGrid(1, 1);
    TEST_ASSERT_EQUAL(-1, result);
}

void setup() {
    // Wait for hardware/serial connection to stabilize
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_rowPallet2SlotID);
    RUN_TEST(test_findGridIndex);
    RUN_TEST(test_movePalletInGrid);
    UNITY_END();
}

void loop() {
    // Empty
}
