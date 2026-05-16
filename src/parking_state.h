#ifndef PARKING_STATE_H
#define PARKING_STATE_H

#include "parking.pb.h"
#include "parking_handler.h"
#include <Arduino.h>

struct O_Do {
    String rfid;
    int row;
    int col;
};

#define ma_the_uid rfid
#define tang row
#define cot col

extern O_Do ds_o[10];
extern bool ir_cu[10];
extern bool sw[4][5];
extern bool cam_bien_vi_tri[4][5];
extern bool cua_da_dong_hoan_toan;
extern bool cua_da_mo_hoan_toan;

extern ParkingStatus_Status SlotStatus[10];
extern uint32_t Grid[12];

void recalcStatus();
int rowPallet2SlotID(int row, int indexInRow);
int rowPallet2SlotIndex(int row, int indexInRow);
int findGridIndex(int PalletId);
int movePalletInGrid(int PalletId, int direction);

void sendCurrentParkingStatus();
void sendCurrentParkingEvent(uint32_t pallet_id, ParkingEvent_EventType event_type,
                             bool is_done = false);

extern ParkingHandler parkingHandler;

#endif // PARKING_STATE_H
