# Báo Cáo Kiểu Dữ Liệu & Cách Sử Dụng

## 📋 Tổng Quan

Dự án quản lý bãi đỗ xe 3 tầng. Tài liệu này tóm tắt các kiểu dữ liệu chính và nơi chúng được thay đổi.

---

## 1. `O_Do` – Thông Tin Ô Đỗ

```cpp
struct O_Do {
    String ma_the_uid;
    bool co_xe; // không dùng
    int tang;
    int cot;
};
```

- `ma_the_uid`: UID RFID, chuỗi trống nếu ô trống.
- `tang`, `cot`: vị trí trong bãi.
- `co_xe`: không dùng trong logic hiện tại.

### Thay đổi chính

- `setup()`: khởi tạo `ma_the_uid = ""`, `tang`, `cot`
- `gui_xe()`: gán `ds_o[target].ma_the_uid = uid`
- `lay_xe()`: gán `ds_o[idx].ma_the_uid = ""`

---

## 2. `ds_o[10]` – Danh sách ô đỗ

- `ds_o[0..2]`: tầng 1, cột 1-3
- `ds_o[3..5]`: tầng 2, cột 1-3
- `ds_o[6..9]`: tầng 3, cột 1-4

### Luồng sử dụng

- `setup()`: gán `tang`/`cot` cho mỗi ô
- `gui_xe(uid)`: tìm ô trống (`ma_the_uid == ""` và IR cao), lưu UID
- `lay_xe(idx)`: xóa UID khi xe ra
- `loop()`: tìm UID trong `ds_o` để quyết định xe vào hay ra

---

## 3. `ir_cu[10]` – Trạng thái IR cũ

- Lưu trạng thái cảm biến IR từ lần quét trước.
- `true` = có xe (`digitalRead(MANG_IR[i]) == LOW`)
- `false` = trống (`HIGH`)

### Sử dụng

- `setup()`: khởi tạo `ir_cu[i]`
- `loop()`: nếu trạng thái hiện tại khác `ir_cu[i]`, cập nhật và in log

---

## 4. `sw[4][5]` – Trạng thái công tắc hành trình

- `sw[t][c]` lưu trạng thái công tắc ở tầng `t`, cột `c`.
- `true` = pallet đã chạm công tắc, `false` = chưa chạm.

### Sử dụng chính

- `doc_sensor_uart()`: đọc thông điệp `SWtcx` từ UART và gán `sw[t][c]`
- `day_den_sw()`: chờ `sw[t][sw_target]` chuyển sang `true`
- `don_duong_vet_can()`: gọi `day_den_sw()` để dọn đường theo cột yêu cầu

---

## 5. Tóm tắt biến thay đổi

- `ds_o[].ma_the_uid`: `setup()` = "", `gui_xe()` = uid, `lay_xe()` = ""
- `ds_o[].tang`, `ds_o[].cot`: `setup()` khởi tạo vị trí
- `ir_cu[]`: `setup()` khởi tạo, `loop()` cập nhật khi IR thay đổi
- `sw[][]`: cập nhật trong `doc_sensor_uart()` khi nhận UART

---

## 6. Luồng chính

- Xe mới: `loop()` không tìm UID → `gui_xe(uid)` → chọn ô trống → ghi `ma_the_uid` → dọn đường nếu cần → lệnh điều khiển
- Xe ra: `loop()` tìm UID → `lay_xe(idx)` → dọn đường → xóa `ma_the_uid`

---

## 7. Kiểm tra toàn vẹn

So sánh `ds_o[i].ma_the_uid` với trạng thái IR để phát hiện dữ liệu sai khác giữa phần mềm và cảm biến.
