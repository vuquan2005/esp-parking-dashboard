/**
 * @file serial_motor_control.h
 * @brief Serial command parsing for motor control from Serial0.
 */
#ifndef SERIAL_MOTOR_CONTROL_H
#define SERIAL_MOTOR_CONTROL_H

#include <Arduino.h>

/**
 * @brief Process a serial motor command from Serial0.
 *
 * Recognizes commands of the form "21NP", "23NT", "21KD", "21KU".
 * If the payload is a valid motor command, it is executed and the function
 * returns true to indicate the command was handled.
 *
 * @param cmd Input command string received from Serial0.
 * @return true if the command was recognized and handled, false otherwise.
 */
bool xu_ly_lenh_motor_serial0(const String &cmd);

#endif // SERIAL_MOTOR_CONTROL_H
