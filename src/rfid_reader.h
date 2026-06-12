/**
 * @file rfid_reader.h
 * @brief RFID input parser for reading UID payloads from the serial interface.
 */
#ifndef RFID_READER_H
#define RFID_READER_H

#include <Arduino.h>

/**
 * @brief Read an RFID UID from the UART serial port.
 *
 * This function reads an incoming serial line, validates the UID payload,
 * filters duplicate tags within a debounce window, and returns the parsed UID.
 *
 * @param uid Output parameter filled with the valid UID string if successful.
 * @return true when a new valid UID is read, false otherwise.
 */
bool readRfidFromSerial(String &uid);

#endif // RFID_READER_H
