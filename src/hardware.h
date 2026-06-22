/**
 * @file hardware.h
 * @brief GPIO and PWM hardware definitions for the ESP parking dashboard.
 */
#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

/** Buzzer output pin. */
#define PIN_BUZZER 4
/** Confirmation button input pin. */
#define PIN_NUT_XAC_NHAN 14
/** Servo gate control PWM pin. */
#define PIN_SERVO_CONG 32 // cant use in eso32s3

/** UART RX pin for Serial2. */
#define PIN_UART_RX2 16
/** UART TX pin for Serial2. */
#define PIN_UART_TX2 17
/** UART RX pin for Serial1. */
#define PIN_UART_RX1 35
/** UART TX pin for Serial1 (disabled). */
#define PIN_UART_TX1 -1

/** PWM channel used for servo/gate control. */
static const int KENH_PWM = 0;
/** PWM frequency in Hz. */
static const int TAN_SO_PWM = 50;
/** PWM resolution bits. */
static const int DO_PHAN_GIAI = 12;

#define PIN_CHON_CHE_DO 18


#endif // HARDWARE_H
