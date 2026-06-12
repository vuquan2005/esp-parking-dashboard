#ifndef PARKING_STATE_H
#define PARKING_STATE_H

#include "parking.pb.h"
#include "parking_handler.h"
#include "websever.h"
#include "wifimanager.h"
#include <Arduino.h>

struct O_Do {
    String rfid;
    int row;
    int col;
};

#define ma_the_uid rfid
#define tang row
#define cot col

// Initialize the parking slot lookup array `ds_o`.
// Slots 0..2 map to floor 1, columns 1..3.
// Slots 3..5 map to floor 2, columns 1..3.
// Slots 6..9 map to floor 3, columns 1..4.
extern O_Do ds_o[10];
extern bool sw[4][5];
extern bool cam_bien_vi_tri[4][5];

extern ParkingStatus_Status SlotStatus[10];
extern uint32_t Grid[12];

/**
 * @brief Recalculate each slot status from the current `ds_o` occupancy.
 *
 * This updates `SlotStatus` entries to `EMPTY` or `OCCUPIED`
 * depending on whether `ds_o[i].rfid` is empty.
 */
void recalcStatus();

/**
 * @brief Convert a row and index-in-row to a global pallet ID.
 *
 * @param row Parking row number (1-3).
 * @param indexInRow Index within the row (1-3 or 1-4 for row 3).
 * @return Global pallet ID in range 1-10, or -1 on invalid input.
 */
int rowPallet2SlotID(int row, int indexInRow);

/**
 * @brief Convert a row and index-in-row to a zero-based slot index.
 *
 * @param row Parking row number (1-3).
 * @param indexInRow Index within the row.
 * @return Zero-based slot index, or -1 on invalid input.
 */
int rowPallet2SlotIndex(int row, int indexInRow);

/**
 * @brief Find a pallet's index within the `Grid` array.
 *
 * @param PalletId Pallet identifier to search for.
 * @return Index in `Grid` if found, or -1 if not present.
 */
int findGridIndex(int PalletId);

/**
 * @brief Move a pallet within the internal grid representation.
 *
 * @param PalletId Pallet identifier to move.
 * @param direction Direction code: 1 = right, 2 = left.
 * @return 0 on success, -1 on invalid move/input, -2 if target cell is occupied.
 */
int movePalletInGrid(int PalletId, int direction);

/**
 * @brief Send the current parking status to connected clients.
 *
 * This encodes the current `Grid` and `SlotStatus` values into a ParkingStatus message.
 */
void sendCurrentParkingStatus();

/**
 * @brief Send a parking event for a specific pallet.
 *
 * @param pallet_id Pallet ID between 1 and 10.
 * @param event_type Event type to send.
 * @param is_done If true, the event is marked completed and increments the internal event counter.
 */
void sendCurrentParkingEvent(uint32_t pallet_id, ParkingEvent_EventType event_type,
                             bool is_done = false);

extern WebManager webManager;
extern WifiManager wifiManager;
extern ParkingHandler parkingHandler;

#endif // PARKING_STATE_H
