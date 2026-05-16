#ifndef PARKING_PROCESS_H
#define PARKING_PROCESS_H

#include "hardware.h"
#include "parking_state.h"
#include <Arduino.h>

void initParkingPositions();
void beep(int n);
void gui_lenh_motor(const String &lenh);
void dieu_khien_goc_servo(int goc);
void dung_motor_cong();
void mo_cong();
void dong_cua_chinh();
void cap_nhat_tin_hieu_ngoai_vi();
void day_den_sw(int row, int pallet, const String &huong, int sw_target);
void don_duong_vet_can(int row, int cot_trong_yc);
void cho_nguoi_dung_xac_nhan();
void gui_xe(const String &uid);
void lay_xe(int target);

#endif // PARKING_PROCESS_H
