#include "../../src/log.h"
#include <Arduino.h>
#include <unity.h>

// 1. Declare & Mock external variables and functions used by serial_motor_control.cpp
bool sw[4][5];
bool cam_bien_vi_tri[4][5];

// Track calls to mock functions
String last_motor_cmd = "";
int call_count_gui_lenh_motor = 0;
int call_count_update_sensor = 0;

int last_day_den_sw_row = -1;
int last_day_den_sw_pallet = -1;
String last_day_den_sw_huong = "";
int last_day_den_sw_target = -1;

void gui_lenh_motor(const String &lenh) {
    last_motor_cmd = lenh;
    call_count_gui_lenh_motor++;
}

void update_sensor() {
    call_count_update_sensor++;
    // Simulate that sensor was triggered on next read to exit while loops
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 5; c++) {
            cam_bien_vi_tri[r][c] = true;
        }
    }
}

void day_den_sw(int row, int pallet, const String &huong, int sw_target) {
    last_day_den_sw_row = row;
    last_day_den_sw_pallet = pallet;
    last_day_den_sw_huong = huong;
    last_day_den_sw_target = sw_target;
}

// Stubs for logging functions to avoid linking issues
extern "C" {
void log_print(log_level_t level, const char *tag, const char *format, ...) {}
void log_print_inline(log_level_t level, const char *tag, const char *format, ...) {}
void log_flush(void) {}
}

// 2. Include the implementation file directly to test its functions
#include "../../src/serial_motor_control.cpp"

void setUp(void) {
    // Reset mocks and global state
    last_motor_cmd = "";
    call_count_gui_lenh_motor = 0;
    call_count_update_sensor = 0;

    last_day_den_sw_row = -1;
    last_day_den_sw_pallet = -1;
    last_day_den_sw_huong = "";
    last_day_den_sw_target = -1;

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 5; c++) {
            sw[r][c] = false;
            cam_bien_vi_tri[r][c] = false;
        }
    }
}

void tearDown(void) {}

// Test validation of various input formats
void test_invalid_command_formats(void) {
    // Command too short or too long
    TEST_ASSERT_FALSE(xu_ly_lenh_motor_serial0("21N"));
    TEST_ASSERT_FALSE(xu_ly_lenh_motor_serial0("21NPP"));

    // First two chars are not numbers
    TEST_ASSERT_FALSE(xu_ly_lenh_motor_serial0("A1NP"));
    TEST_ASSERT_FALSE(xu_ly_lenh_motor_serial0("2BNP"));

    // Invalid direction prefix
    TEST_ASSERT_FALSE(xu_ly_lenh_motor_serial0("21XX"));
}

// Test command parsing with invalid parameters (row/pallet out of range or 0)
// Note: Code should return true (marked as handled) but print error
void test_out_of_range_parameters(void) {
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("41NP")); // Row 4 (1-3 valid)
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("25NP")); // Pallet 5 (1-4 valid)

    // Gia tri 0 cho hang hoac cot (pallet) khong hop le
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("01NP")); // Row 0 is invalid
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("20NP")); // Pallet 0 is invalid
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("00NP")); // Both are 0, invalid
}

// Test horizontal movement commands (NP / NT)
void test_horizontal_movement_parsing(void) {
    // Ngang Phai (NP): row 2, pallet 1 -> target sw should be pallet + 1 = 2
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("21NP"));
    TEST_ASSERT_EQUAL(2, last_day_den_sw_row);
    TEST_ASSERT_EQUAL(1, last_day_den_sw_pallet);
    TEST_ASSERT_EQUAL_STRING("NP", last_day_den_sw_huong.c_str());
    TEST_ASSERT_EQUAL(2, last_day_den_sw_target);

    // Ngang Trai (NT): row 3, pallet 4 -> target sw should be pallet = 4
    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("34NT"));
    TEST_ASSERT_EQUAL(3, last_day_den_sw_row);
    TEST_ASSERT_EQUAL(4, last_day_den_sw_pallet);
    TEST_ASSERT_EQUAL_STRING("NT", last_day_den_sw_huong.c_str());
    TEST_ASSERT_EQUAL(4, last_day_den_sw_target);
}

// Test vertical movement commands (KD / KU)
void test_vertical_movement_parsing(void) {
    // Keo Duoi (KD): row 2, pallet 3 -> target row should be 1
    // Set sensor to true immediately so loop exits without calling update_sensor
    cam_bien_vi_tri[1][3] = true;

    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("23KD"));
    TEST_ASSERT_EQUAL_STRING("st", last_motor_cmd.c_str()); // Stopped after reaching sensor
    TEST_ASSERT_EQUAL(
        2, call_count_gui_lenh_motor); // 1: gui_lenh_motor("23KD"), 2: gui_lenh_motor("st")
    TEST_ASSERT_EQUAL(
        0, call_count_update_sensor); // did not need to poll because sensor was already true

    // Keo Tren (KU): row 3, pallet 2 -> target row should be row = 3
    // Here we let sensor start at false, and update_sensor mock will set it to true
    cam_bien_vi_tri[3][2] = false;

    TEST_ASSERT_TRUE(xu_ly_lenh_motor_serial0("32KU"));
    TEST_ASSERT_EQUAL_STRING("st", last_motor_cmd.c_str());
    TEST_ASSERT_TRUE(call_count_update_sensor > 0); // polled sensors
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_invalid_command_formats);
    RUN_TEST(test_out_of_range_parameters);
    RUN_TEST(test_horizontal_movement_parsing);
    RUN_TEST(test_vertical_movement_parsing);
    UNITY_END();
}

void loop() {}
