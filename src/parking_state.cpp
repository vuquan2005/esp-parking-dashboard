#include "parking_state.h"
#include "hardware.h"
#include "log.h"

#include <Arduino.h>

static const char *TAG_STATE = "PARKING_STATE";
static const char *TAG_WS = "PARKING_WS";

O_Do ds_o[10];
bool sw[4][5];
bool cam_bien_vi_tri[4][5];

ParkingStatus_Status SlotStatus[10] = {ParkingStatus_Status_UNKNOWN};
uint32_t Grid[12] = {1, 2, 3, 4, 5, 6, 7, 0, 8, 9, 10, 0};

static uint32_t event_id_counter = 1;

void recalcStatus() {
    for (int i = 0; i < 10; i++) {
        int idx = rowPallet2SlotIndex(ds_o[i].row, ds_o[i].col);
        if (idx < 0) {
            continue;
        }
        SlotStatus[idx] =
            ds_o[i].rfid.isEmpty() ? ParkingStatus_Status_EMPTY : ParkingStatus_Status_OCCUPIED;
    }
}

int rowPallet2SlotID(int row, int indexInRow) {
    if (row < 1 || row > 3) {
        LOG_E(TAG_STATE, "Invalid row: %d", row);
        return -1;
    }
    if (indexInRow < 1 || indexInRow > 4) {
        LOG_E(TAG_STATE, "Invalid index: %d", indexInRow);
        return -1;
    }
    if (row < 3 && indexInRow > 3) {
        LOG_E(TAG_STATE, "Invalid index for row %d: %d", row, indexInRow);
        return -1;
    }
    const int number = (row == 3) ? indexInRow : (row == 2 ? indexInRow + 4 : indexInRow + 7);
    if (number < 1 || number > 10) {
        LOG_E(TAG_STATE, "Invalid slot number: %d", number);
        return -1;
    }
    return number;
}

int rowPallet2SlotIndex(int row, int indexInRow) {
    return rowPallet2SlotID(row, indexInRow) - 1;
}

int findGridIndex(int PalletId) {
    for (size_t i = 0; i < 12; ++i) {
        if (Grid[i] == PalletId) {
            return i;
        }
    }
    return -1;
}

int movePalletInGrid(int PalletId, int direction) {
    int gridIndex = findGridIndex(PalletId);
    if (gridIndex == -1) {
        LOG_E(TAG_STATE, "Pallet ID %d not found in grid", PalletId);
        return -1;
    }
    int row = gridIndex / 4;
    int col = gridIndex % 4;

    if (row == 0) {
        LOG_W(TAG_STATE, "Pallet ID %d is on the top row and cannot be moved", PalletId);
        return -1;
    }

    if (direction == 1 && col < 3) {
        if (Grid[gridIndex + 1] != 0) {
            LOG_W(TAG_STATE,
                  "Cannot move Pallet ID %d to the right because the target position is not empty",
                  PalletId);
            return -2;
        }
        std::swap(Grid[gridIndex], Grid[gridIndex + 1]);
    } else if (direction == 2 && col > 0) {
        if (Grid[gridIndex - 1] != 0) {
            LOG_W(TAG_STATE,
                  "Cannot move Pallet ID %d to the left because the target position is not empty",
                  PalletId);
            return -2;
        }
        std::swap(Grid[gridIndex], Grid[gridIndex - 1]);
    } else {
        LOG_W(TAG_STATE, "Invalid move for Pallet ID %d in direction %d (row: %d, col: %d)",
              PalletId, direction, row, col);
        return -1;
    }
    return 0;
}

void sendCurrentParkingStatus() {
    parkingHandler.sendParkingStatus(Grid, 12, SlotStatus, 10);
}

void sendCurrentParkingEvent(uint32_t pallet_id, ParkingEvent_EventType event_type, bool is_done) {
    if (pallet_id < 1 || pallet_id > 10) {
        LOG_E(TAG_WS, "Invalid pallet_id=%u passed to sendCurrentParkingEvent", pallet_id);
        return;
    }

    uint32_t event_id = event_id_counter;
    if (is_done) {
        event_id_counter++;
    }

    parkingHandler.sendParkingEvent(event_id, pallet_id, event_type, is_done);
}
