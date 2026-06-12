# 🚗 ESP32 Automated Parking Dashboard

Hệ thống quản lý và điều khiển bãi đỗ xe tự động (Automated Smart Parking System) chạy trên nền tảng vi điều khiển ESP32 / ESP32-S3 sử dụng PlatformIO. Dự án tích hợp các công nghệ truyền thông hiện đại như **Google Protocol Buffers (Protobuf)** để tuần tự hóa dữ liệu hiệu quả, kết nối **WebSockets** truyền tải dữ liệu thời gian thực và giao diện giám sát web (Dashboard) tiện lợi.

---

## 📌 Tổng quan dự án

Hệ thống điều khiển một bãi đỗ xe dạng lưới gồm **10 ô đỗ (3 tầng)**:
- **Tầng 1 (T1)**: 3 ô đỗ (vị trí 1-3)
- **Tầng 2 (T2)**: 3 ô đỗ (vị trí 1-3)
- **Tầng 3 (T3)**: 4 ô đỗ (vị trí 1-4)

> Lưu ý: mã nguồn hiện tại dùng hai hệ thống định nghĩa vị trí khác nhau.
> - `Grid` là bản đồ 12 phần tử mô tả vị trí cơ học của pallet trong cấu trúc 3x4 (3 hàng, 4 cột). Mỗi mục `Grid[i]` chứa `pallet_id` hoặc `0` nếu ô pallet trống.
> - `ds_o` là mảng 10 ô đỗ logic, mỗi phần tử lưu `rfid` cùng thông tin `row`/`col` trong hệ định vị tầng/ô. `ds_o` chỉ dùng cho 10 ô đỗ thực tế và gán theo floor/column 1-based.
> 
> Nói cách khác, cả hai đều mô tả cùng không gian vật lý, nhưng `Grid` dùng cấu trúc pallet cơ học 12 vị trí, còn `ds_o` chỉ dùng 10 ô đỗ thực tế và ánh xạ khác nhau giữa slot ID, row và column.

Hệ thống hoạt động dựa trên cơ chế:
1. **Quét thẻ RFID** (hoặc nhận lệnh thủ công qua Serial): Nhận diện xe vào/ra bằng [src/rfid_reader.cpp](src/rfid_reader.cpp).
2. **Hệ thống điều khiển cơ khí**:
   - Động cơ dọc (kéo nâng hạ): Đưa pallet xe lên/xuống giữa các tầng.
   - Động cơ ngang (dịch pallet): Di chuyển các pallet ngang để mở đường trống bên dưới cho các xe tầng trên hạ xuống.
   - Cổng chắn (Servo Barrier): Đóng/mở cổng khi xe vào/ra bãi.
3. **Cảm biến giám sát**:
   - Cảm biến tiệm cận hồng ngoại (IR): Xác định pallet đã đạt vị trí chính xác chưa.
   - Công tắc hành trình (Limit Switches - SW): Xác nhận hành trình dịch chuyển ngang của pallet.
4. **Giao diện Giám sát (Web Dashboard)**:
   - Được phục vụ trực tiếp từ ESP32 thông qua tính năng **Captive Portal** (phát Wi-Fi Access Point và tự động chuyển hướng khi truy cập).
   - Sử dụng kết nối **WebSockets** và **Protobuf (Nanopb)** để cập nhật trực quan trạng thái lưới ô đỗ (Grid) và pallet xe thời gian thực.

---

## 📂 Cấu trúc thư mục dự án

```text
.
├── include/
├── lib/
│   ├── webserver/
│   └── wifimanager/
├── proto/
├── src/
│   ├── hardware.h                # Định nghĩa chân phần cứng
│   ├── log.h / log.cpp           # Hệ thống ghi log
│   ├── main.cpp                  # Setup và vòng lặp chính
│   ├── parking_handler.h/.cpp    # Xử lý Protobuf và hàng đợi lệnh
│   ├── parking_process.h/.cpp    # Logic điều khiển đỗ/lấy xe
│   ├── parking_state.h/.cpp      # Quản lý trạng thái lưới đỗ xe
│   ├── rfid_reader.h / .cpp      # Xử lý đọc thẻ RFID
│   └── serial_motor_control.h/.cpp # Điều khiển động cơ qua Serial
├── test/
│   ├── test_parking_state/
│   ├── test_rfid_reader/
│   └── test_serial_motor_control/
├── platformio.ini
└── README.md
```

---

## 🛠️ Chi tiết các thành phần chính

### 1. Cấu hình PlatformIO & Môi trường biên dịch
Tệp cấu hình [platformio.ini](platformio.ini) thiết lập hai môi trường biên dịch chính:
- **`env:esp32`**: Hướng tới vi điều khiển ESP32 tiêu chuẩn (board `esp32dev`).
- **`env:esp32s3`**: Hướng tới ESP32-S3 (board `esp32-s3-devkitc-1`).
- **Thư viện phụ thuộc**:
  - `mathieucarbou/ESPAsyncWebServer`: Web server bất đối xứng phục vụ Dashboard và WebSockets.
  - `nanopb/Nanopb`: Thư viện Protobuf tối ưu hóa cho vi điều khiển.
  - `miguelbalboa/MFRC522`: Thư viện RFID (khai báo sẵn trong dependencies).

### 2. Giao thức Protobuf & Nanopb
Tệp cấu hình giao thức [proto/parking.proto](proto/parking.proto) định nghĩa các cấu trúc dữ liệu gửi giữa ESP32 và Web Dashboard:
- `Parking`: Message bọc ngoài (chứa payloads khác nhau qua khối `oneof`).
- `ParkingStatus`: Cập nhật lưới vị trí đỗ xe (`pallet_grid`) và trạng thái của từng ô đỗ (`slots`).
- `DeviceStatus`: Gửi thông số phần cứng bao gồm RAM trống, thời gian chạy (`uptime`), độ mạnh tín hiệu Wi-Fi (`rssi`), địa chỉ IP.
- `WifiConfig`: Cho phép cấu hình SSID và mật khẩu của trạm phát Wi-Fi (AP) hoặc trạm thu (STA).

Tệp [proto/parking.options](proto/parking.options) giới hạn kích thước động nhằm tối ưu hóa bộ nhớ tĩnh cho Nanopb trên ESP32:
- `ParkingStatus.pallet_grid`: giới hạn tối đa 12 phần tử.
- `ParkingStatus.slots`: giới hạn tối đa 10 phần tử.
- Các chuỗi ký tự (SSID, IP, Password) bị giới hạn từ 16 đến 64 ký tự.

### 3. Bộ xử lý Web Server & Captive Portal
- **[webserver.cpp](lib/webserver/webserver.cpp)**:
  - Khởi tạo một HTTP Server trên cổng 80 và một DNS Server thông qua lớp [WebManager](lib/webserver/websever.h#L16) để thực thi tính năng **Captive Portal**. Khi người dùng kết nối vào Wi-Fi do ESP32 phát ra, bất kỳ yêu cầu web nào cũng bị bắt lại và chuyển hướng về trang chủ `http://<IP_ESP32>/`.
  - Phục vụ tệp [index.minify.h](lib/webserver/index.minify.h) chứa mã nguồn trang Dashboard đã được nén GZIP nhằm tăng tốc độ tải trang cực kỳ nhanh chóng.
  - Quản lý kênh giao tiếp thời gian thực qua WebSockets (đường dẫn `/ws`).

### 4. Quản lý Trạng thái & Cơ chế Dọn đường
- **[parking_state.cpp](src/parking_state.cpp)**:
  - Quản lý mảng toàn cục `Grid[12]` thể hiện vị trí cơ học của các pallet. Giá trị `0` biểu thị ô trống cơ học, các giá trị `1-10` thể hiện Pallet ID.
  - Hàm `movePalletInGrid` tính toán hoán vị các ô trống khi di chuyển pallet sang trái hoặc sang phải.
- **[parking_process.cpp](src/parking_process.cpp)**:
  - Hàm [don_duong_vet_can](src/parking_process.cpp#L218): Khi cần đỗ xe ([gui_xe](src/parking_process.cpp#L333)) hoặc lấy xe ([lay_xe](src/parking_process.cpp#L403)) ở tầng 2 hoặc tầng 3, hệ thống sẽ tự động tìm kiếm vị trí các pallet bên dưới để dịch chuyển chúng sang các ô trống bên cạnh. Quá trình này giúp tạo ra một cột trống thông suốt từ tầng mong muốn xuống tầng 1, tạo lối cho pallet hạ xuống.
  - Lệnh điều khiển nâng hạ dọc (`KD` - Kéo Dưới, `KU` - Kéo Trên) và di chuyển ngang (`NP` - Ngang Phải, `NT` - Ngang Trai) được gửi trực tiếp tới động cơ thông qua `Serial2` bằng hàm [gui_lenh_motor](src/parking_process.cpp#L66).
  - Thẻ RFID quét được qua UART (hoặc các lệnh điều khiển thô trực tiếp từ người dùng qua `Serial0`) được phân tích cú pháp bởi [src/rfid_reader.cpp](src/rfid_reader.cpp) và [src/serial_motor_control.cpp](src/serial_motor_control.cpp) với hàm [xu_ly_lenh_motor_serial0](src/serial_motor_control.cpp#L8).

---

## 🔌 Sơ đồ chân Phần cứng (Pinout)

Được định nghĩa trong tệp [hardware.h](file:///D:/Development/esp-parking-dashboard/src/hardware.h):

| Tên Pin | Số chân ESP32 | Chức năng / Thiết bị kết nối |
| :--- | :---: | :--- |
| `PIN_BUZZER` | `4` | Còi chip thông báo trạng thái (bíp khi nhận thẻ/xong quy trình) |
| `PIN_NUT_XAC_NHAN` | `14` | Nút nhấn xác nhận vật lý (chế độ kéo tay/cảnh báo) |
| `PIN_SERVO_CONG` | `32` | Điều khiển Servo mở/đóng barrier |
| `PIN_UART_RX2` | `16` | Cổng UART2 RX - Nhận phản hồi động cơ |
| `PIN_UART_TX2` | `17` | Cổng UART2 TX - Gửi lệnh điều khiển động cơ nâng hạ/dịch |
| `PIN_UART_RX1` | `35` | Cổng UART1 RX - Nhận dữ liệu cảm biến tiệm cận IR và công tắc SW |
| `PIN_UART_TX1` | `-1` | Không sử dụng |

---

## 🚀 Hướng dẫn biên dịch và nạp chương trình

Dự án sử dụng PlatformIO. Bạn có thể xây dựng và nạp chương trình bằng giao diện VS Code PlatformIO extension hoặc qua CLI:

### 2. Biên dịch dự án (Build)
```bash
# Biên dịch cho ESP32 chuẩn
pio run -e esp32

# Biên dịch cho ESP32-S3
# pio run -e esp32s3
```

### 3. Nạp chương trình lên Board (Upload)
Kết nối ESP32 với máy tính qua cổng USB và chạy lệnh nạp:
```bash
# Nạp cho ESP32
pio run -t upload -e esp32

# Nạp cho ESP32-S3
# pio run -t upload -e esp32s3
```

### 4. Giám sát Serial Log
Hệ thống sử dụng tốc độ truyền `115200` baud. Bạn có thể bật màn hình giám sát bằng lệnh:
```bash
pio device monitor -b 115200
```

---

## 🧪 Chạy các bài kiểm thử (Unit Tests)

Dự án cung cấp các bộ test tại thư mục `test/` để xác thực hoạt động của các thành phần cốt lõi. Chạy test trên phần cứng thực tế qua cổng COM đã chỉ định bằng lệnh:
```bash
pio test -e esp32s3 -vv
```
