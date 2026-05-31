#include "serial_motor_control.h"
#include "parking_process.h"
#include "parking_state.h"
#include "log.h"

static const char *TAG_SM_CTRL = "SM_CTRL";

bool xu_ly_lenh_motor_serial0(const String &cmd) {
    // Mau lenh nhan tu Serial0 co dang: "21NP", "23NT", "21KD", "21KU" (4 ky tu)
    String clean_cmd = cmd;
    clean_cmd.trim();
    clean_cmd.toUpperCase();

    if (clean_cmd.length() != 4) {
        return false; // Khong phai do dai hop le
    }

    char r_char = clean_cmd[0];
    char p_char = clean_cmd[1];
    String huong = clean_cmd.substring(2);

    if (!isdigit(r_char) || !isdigit(p_char)) {
        return false; // Hai ky tu dau phai la chu so
    }

    if (huong != "NP" && huong != "NT" && huong != "KD" && huong != "KU") {
        return false; // Khong phai la huong/hanh dong hop le
    }

    int row = r_char - '0';
    int pallet = p_char - '0';

    // Khong chap nhan gia tri 0 hoac vuot qua gioi han (Hang: 1-3, Pallet: 1-4)
    if (row == 0 || row > 3 || pallet == 0 || pallet > 4) {
        LOG_E(TAG_SM_CTRL, "Tham so dong/pallet khong hop le (khong duoc bang 0 hoac vuot qua gioi han): row=%d, pallet=%d", row, pallet);
        return true; // Dinh dang phu hop nhung tham so sai, tiep tuc danh dau la da xu ly
    }

    // Xu ly cac lenh nang/ha doc: KD (Keo Duoi), KU (Keo Tren)
    if (huong == "KD" || huong == "KU") {
        int target_row = (huong == "KD") ? 1 : row;
        LOG_I(TAG_SM_CTRL, "Nhan lenh nang/ha tu Serial0: %s -> row=%d, col=%d, huong=%s (Kiem tra cam bien doc cam_bien_vi_tri[%d][%d])",
              clean_cmd.c_str(), row, pallet, huong.c_str(), target_row, pallet);

        gui_lenh_motor(clean_cmd);
        unsigned long timeout = millis();
        while (!cam_bien_vi_tri[target_row][pallet]) {
            update_sensor();
            if (millis() - timeout > 10000) {
                gui_lenh_motor("st");
                LOG_E(TAG_SM_CTRL, "MOTOR DOC KET: Row %d, Col %d khong dat vi tri cam bien doc!", target_row, pallet);
                return true;
            }
            delay(10);
        }
        gui_lenh_motor("st");
        LOG_I(TAG_SM_CTRL, "Nang/ha thanh cong, da dung tai cam bien doc.");
        return true;
    }

    // Xu ly cac lenh dich ngang: NP (Ngang Phai), NT (Ngang Trai)
    if (huong == "NP" || huong == "NT") {
        // Tinh toan sw_target tu pallet va huong
        int sw_target = -1;
        if (huong == "NP") {
            sw_target = pallet + 1;
        } else if (huong == "NT") {
            sw_target = pallet;
        }

        if (sw_target < 1 || sw_target > 4) {
            LOG_E(TAG_SM_CTRL, "Loi tinh toan sw_target: %d cho pallet %d huong %s", sw_target, pallet, huong.c_str());
            return true;
        }

        LOG_I(TAG_SM_CTRL, "Nhan lenh dich ngang tu Serial0: %s -> row=%d, pallet=%d, huong=%s (Kiem tra limit switch sw[%d][%d])",
              clean_cmd.c_str(), row, pallet, huong.c_str(), row, sw_target);

        // Goi day_den_sw co san de thuc thi dong co va tu dong dung khi cham limit switch
        day_den_sw(row, pallet, huong, sw_target);
        return true;
    }

    return false;
}
