#ifndef PARKING_PROCESS_H
#define PARKING_PROCESS_H

#include "hardware.h"
#include "parking_state.h"
#include "websever.h"
#include "wifimanager.h"
#include "parking_handler.h"
#include <Arduino.h>

/**
 * @brief Initialize parking slot coordinates for `ds_o`.
 *
 * Floor and column values are assigned according to the parking layout.
 */
void initParkingPositions();

/**
 * @brief Emit beep sound on the buzzer.
 *
 * @param n Number of beep pulses.
 */
void beep(int n);

/**
 * @brief Send a raw motor command string to the UART motor controller.
 *
 * @param lenh Command string to send.
 */
void gui_lenh_motor(const String &lenh);

/**
 * @brief Control the servo angle for the gate motor.
 *
 * @param goc Target servo angle in degrees (0-180).
 */
void dieu_khien_goc_servo(int goc);

/**
 * @brief Stop the gate motor by setting PWM duty to zero.
 */
void dung_motor_cong();

/**
 * @brief Open the gate.
 *
 * Moves the servo to the open position, waits briefly, then stops the motor.
 */
void mo_cong();

/**
 * @brief Close the gate.
 *
 * Moves the servo to the closed position, waits briefly, then stops the motor.
 */
void dong_cong();

/**
 * @brief Update sensor state from the serial sensor interface.
 *
 * This reads incoming serial packets, parses switch and IR sensor data,
 * then processes parking handler and web manager loops.
 */
void update_sensor();

/**
 * @brief Move a motorized pallet until the target sensor is reached.
 *
 * @param t Row index for the motor command.
 * @param c Column/pallet index for the motor command.
 * @param huong Direction string used by the motor controller.
 * @param timeOut Timeout in milliseconds while waiting for the target sensor.
 */
void motor_keo(int t, int c, const String &huong, int timeOut);

/**
 * @brief Drive the pallet to a target switch position on a row.
 *
 * @param row Row index for the current pallet.
 * @param pallet Pallet identifier in the row.
 * @param huong Direction command string.
 * @param sw_target Target switch index to trigger.
 */
void day_den_sw(int row, int pallet, const String &huong, int sw_target);

/**
 * @brief Clear the path for the requested pallet position.
 *
 * @param row Row index where the path should be cleared.
 * @param cot_trong_yc Request column index to clear toward.
 */
void don_duong_vet_can(int row, int cot_trong_yc);

/**
 * @brief Wait for user confirmation via button press.
 *
 * @param isAuto When true, uses an automatic confirmation timeout instead of waiting for a manual press.
 */
void cho_nguoi_dung_xac_nhan(bool isAuto = false);

/**
 * @brief Handle a vehicle entry request for a target parking bay.
 *
 * @param target Target bay index in the `ds_o` array.
 * @param isAuto If true, perform automatic confirmation behavior.
 */
void gui_xe(int target, bool isAuto = false);

/**
 * @brief Handle a vehicle retrieval request for a target bay.
 *
 * @param target Target bay index in the `ds_o` array.
 * @param isAuto If true, perform automatic confirmation behavior.
 */
void lay_xe(int target, bool isAuto = false);

/**
 * @brief Run an automated parking scenario.
 *
 * This function executes a predefined sequence of parking operations.
 */
void kich_ban_auto();

extern WebManager webManager;
extern WifiManager wifiManager;
extern ParkingHandler parkingHandler;
extern void softDelay(unsigned long ms);

#endif // PARKING_PROCESS_H
