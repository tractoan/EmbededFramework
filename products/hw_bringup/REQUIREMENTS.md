# Yêu cầu – Firmware bring-up phần cứng (`hw_bringup`)

> Yêu cầu của **framework** (lớp, bộ nhớ, HAL, giao thức…) nằm ở [`docs/REQUIREMENTS.md`](../../docs/REQUIREMENTS.md);
> sản phẩm này tương ứng PROD-005…007 ở đó. File này giữ yêu cầu **của sản phẩm** (SER/GNS/DIO/IMU/MAV…).

> **Cách dùng file này**
> - Đây là nguồn yêu cầu chính của sản phẩm `products/hw_bringup`. Sửa trực tiếp các mục bên dưới, rồi đưa file vào prompt, ví dụ:
>   *"Cập nhật firmware theo `products/hw_bringup/REQUIREMENTS.md`, chỉ những mục có trạng thái `Mới` hoặc `Sửa`."*
> - Mỗi yêu cầu có **mã** (vd. `SER-03`) để tham chiếu trong prompt, commit và test.
> - Cột **Trạng thái**: `Đã làm` · `Mới` (cần làm) · `Sửa` (đổi so với bản đã làm) · `Bỏ` (gỡ khỏi firmware) · `Chờ` (cần thêm thông tin).
> - Giá trị trong cột **Mặc định hiện tại** là lựa chọn đang có trong code. Muốn đổi: sửa giá trị và đặt trạng thái `Sửa`.
> - Mục **7. Câu hỏi mở** là các thông tin còn thiếu; trả lời ngay trong file.
> - Ghi lại mỗi lần đổi yêu cầu ở mục **9. Lịch sử thay đổi**.

---

## 1. Tổng quan

| Mã | Yêu cầu | Trạng thái |
|---|---|---|
| GEN-01 | Phần mềm nhúng chạy trên **CPU** (Linux) để bring-up phần cứng, xây dựng trên framework `EmbededFramework` (đúng phân lớp HAL / OSAL / Services / Middleware / App / Product). | Đã làm |
| GEN-02 | Giao tiếp với **phần mềm giao diện** (GUI) qua **MAVLink v2** trên một cổng serial. | Đã làm |
| GEN-03 | Chạy được trên **bản mô phỏng** (PC, không cần phần cứng) để phát triển GUI và test tự động. | Đã làm |
| GEN-04 | Một phần cứng hỏng/thiếu **không làm dừng** firmware; các phần còn lại vẫn được kiểm tra và báo cáo. | Đã làm |
| GEN-05 | Mọi sự kiện quan trọng được báo cho người vận hành (GUI: `STATUSTEXT`; console: log). | Đã làm |

### Phần cứng cần kiểm tra

| Nhóm | Số lượng | Giao tiếp | Ghi chú |
|---|---|---|---|
| Cổng serial | 12 | RS232 / RS485 | 1 cổng GUI, 1 cổng GNSS, 10 cổng loopback |
| Digital input | 20 | GPIO | |
| Digital output | 20 | GPIO | |
| IMU | 2 (2 loại khác nhau) | SDLC / HDLC | xem mục 5 |
| Máy thu GNSS | 0 hoặc 1 | serial | có thể không được lắp |

---

## 2. Cổng serial

| Mã | Yêu cầu | Mặc định hiện tại | Trạng thái |
|---|---|---|---|
| SER-01 | Có 12 cổng serial, đánh chỉ số logic 0..11. | 12 | Đã làm |
| SER-02 | 1 cổng dành cho phần mềm giao diện (MAVLink). | cổng **0**, 115200 bps, 8N1 | Đã làm |
| SER-03 | 1 cổng dành cho máy thu GNSS. | cổng **1**, `gnss.baud` = 115200 | Đã làm |
| SER-04 | 10 cổng còn lại kiểm tra **loopback** liên tục. | cổng 2..11, 115200 bps | Đã làm |
| SER-05 | Khung thử loopback có số thứ tự + CRC; PASS khi nhận lại đúng nguyên khung. | 18 byte: `55 AA port seq` + 12 byte mẫu + CRC16 | Đã làm |
| SER-06 | Phân loại kết quả: PASS / TIMEOUT / MISMATCH / OPEN-ERR; đếm số lần pass/fail từng cổng. | | Đã làm |
| SER-07 | Chu kỳ và thời gian chờ loopback cấu hình được lúc chạy. | `ser.period_ms` = 200, `ser.timeout_ms` = 50 | Đã làm |
| SER-08 | Hỗ trợ RS485 2 dây: tự nghe lại (echo) hoặc nối chéo từng cặp cổng. | không cặp nào; giắc TX→RX từng cổng | Đã làm |
| SER-09 | Báo người vận hành khi một cổng đổi PASS ↔ FAIL, chống báo dồn dập khi cổng chập chờn. | tối đa 1 thông báo / 5 s / cổng | Đã làm |
| SER-10 | _(thêm yêu cầu mới ở đây)_ | | |

**Cần điền:** cổng nào là RS232, cổng nào RS485 (2 dây / 4 dây), tên thiết bị Linux tương ứng → mục 7.

---

## 3. Máy thu GNSS

| Mã | Yêu cầu | Mặc định hiện tại | Trạng thái |
|---|---|---|---|
| GNS-01 | Khi khởi động, trong **5 giây** nếu cổng GNSS không nhận được dữ liệu → coi như không có máy thu, chuyển cổng đó sang kiểm tra loopback. | `gnss.detect_ms` = 5000 | Đã làm |
| GNS-02 | Có dữ liệu (bất kỳ byte nào) trong cửa sổ trên → trạng thái CONNECTED. | điều kiện: ≥ 1 byte | Đã làm |
| GNS-03 | Khi CONNECTED: chuyển tiếp dữ liệu máy thu lên GUI. | `SERIAL_CONTROL` (GPS1, REPLY); bật/tắt bằng `gnss.forward` | Đã làm |
| GNS-04 | Nhận **lệnh cấu hình** từ GUI và gửi tới máy thu (chỉ khi có kết nối máy thu). | `SERIAL_CONTROL` (GPS1); không có máy thu → bỏ lệnh + báo | Đã làm |
| GNS-05 | GUI đổi được baudrate cổng máy thu. | trường `baudrate` của `SERIAL_CONTROL` | Đã làm |
| GNS-06 | Chỉ dò máy thu lúc khởi động; cắm máy thu sau 5 s thì phải khởi động lại. | | Đã làm |
| GNS-07 | _(vd. nhận dạng chặt hơn: chỉ coi là GNSS khi nhận đúng câu NMEA/UBX hợp lệ?)_ | | Chờ |
| GNS-08 | Giải mã NMEA của máy thu, gửi **thời gian (UTC), vĩ độ, kinh độ** lên PC. | GGA + RMC, mọi talker, bắt buộc checksum; `GNSS_FIX` (62005) + `GPS_RAW_INT` mỗi chu kỳ đo | Đã làm |

---

## 4. Digital input / output

| Mã | Yêu cầu | Mặc định hiện tại | Trạng thái |
|---|---|---|---|
| DIO-01 | Đọc 20 input, gửi trạng thái lên GUI. | đọc mỗi 20 ms; gửi khi đổi + mỗi `io.report_ms` = 200 ms | Đã làm |
| DIO-02 | GUI đặt được 20 output (từng kênh hoặc nhiều kênh một lúc). | `OUTPUT_SET` (mask/value), `MAV_CMD_DO_SET_RELAY` | Đã làm |
| DIO-03 | Sau mỗi lệnh output, gửi lại trạng thái input/output ngay. | `IO_STATUS` | Đã làm |
| DIO-04 | Trạng thái an toàn khi khởi động: tắt mọi output. | tắt hết | Đã làm |
| DIO-05 | Input tích cực mức thấp cấu hình được theo kênh. | không kênh nào đảo | Đã làm |
| DIO-06 | _(vd. tự động thử DO→DI bằng jig, chạy lần lượt từng kênh và báo kết quả?)_ | | Chờ |
| DIO-07 | _(vd. chống dội (debounce) input? bao nhiêu ms?)_ | không có | Chờ |

---

## 5. IMU (SDLC/HDLC)

| Mã | Yêu cầu | Mặc định hiện tại | Trạng thái |
|---|---|---|---|
| IMU-01 | 2 IMU, mỗi IMU một cổng SDLC/HDLC. | IMU0 → HDLC 0, IMU1 → HDLC 1 | Đã làm |
| IMU-02 | Giải mã khung theo định dạng của từng loại IMU. | IMU0 = `ln200`, IMU1 = `hg1700` (**giả định**, xem 7) | Chờ |
| IMU-03 | Gửi dữ liệu IMU lên GUI: gyro, accel, status, tần số khung đo được, số khung, số lỗi. | `IMU_DATA`, `imu.rate_hz` = 10 Hz mỗi IMU | Đã làm |
| IMU-04 | Gửi kèm khung thô để đối chiếu với ICD khi bring-up. | 48 byte đầu khung | Đã làm |
| IMU-05 | Báo khi IMU mất dữ liệu / có lại dữ liệu / không mở được cổng. | ngưỡng `imu.timeout_ms` = 500 | Đã làm |
| IMU-06 | Thông số đường truyền SDLC. | 1 Mbit/s, NRZ, clock ngoài, FCS-16 kiểm bằng phần cứng | Chờ |
| IMU-07 | _(vd. gửi lệnh khởi động/BIT tới IMU? IMU có cần lệnh để bắt đầu phát không?)_ | không gửi gì | Chờ |
| IMU-08 | _(vd. ghi log dữ liệu IMU đầy đủ tần số ra file?)_ | không | Chờ |

---

## 6. Giao diện với phần mềm giao diện (MAVLink)

| Mã | Yêu cầu | Mặc định hiện tại | Trạng thái |
|---|---|---|---|
| MAV-01 | MAVLink v2; nhận cả v1. | sysid 1, compid 1, heartbeat 1 Hz | Đã làm |
| MAV-02 | Dialect riêng cho phép GUI sinh mã bằng mavgen. | `tools/mavlink/bringup.xml`, ID 62000–62004 | Đã làm |
| MAV-03 | Gửi lên: trạng thái loopback. | `SERIAL_LOOPBACK_STATUS` mỗi `ser.report_ms` = 500 ms | Đã làm |
| MAV-04 | Gửi lên: trạng thái input (và output). | `IO_STATUS` | Đã làm |
| MAV-05 | Gửi lên: dữ liệu IMU. | `IMU_DATA` | Đã làm |
| MAV-06 | Gửi lên: trạng thái chung (GNSS, cổng GUI/GNSS). | `BRINGUP_STATUS` 1 Hz | Đã làm |
| MAV-07 | Nhận xuống: thiết lập output. | `OUTPUT_SET`, `MAV_CMD_DO_SET_RELAY` | Đã làm |
| MAV-08 | Nhận xuống: lệnh cấu hình máy thu GNSS. | `SERIAL_CONTROL` (GPS1) | Đã làm |
| MAV-09 | Đọc/ghi/lưu tham số từ GUI. | `PARAM_*`, `MAV_CMD_PREFLIGHT_STORAGE` | Đã làm |
| MAV-10 | Phát hiện mất kết nối GUI. | không thấy heartbeat GUI 3 s | Đã làm |
| MAV-12 | GUI kết nối được qua mạng (UDP) ngoài cổng serial, khi CPU có Ethernet. | UDP 14560, trả lời địa chỉ gửi gần nhất | Đã làm |
| MAV-11 | _(vd. GUI cần lệnh bắt đầu/dừng kiểm tra, reset bộ đếm, xuất báo cáo PASS/FAIL tổng?)_ | | Chờ |

---

## 7. Câu hỏi mở (cần trả lời để hoàn thiện)

| # | Câu hỏi | Trả lời |
|---|---|---|
| Q1 | Tên/model chính xác của 2 IMU? Có ICD (tài liệu định dạng khung) không? | |
| Q2 | Định dạng khung từng IMU: độ dài, thứ tự byte, vị trí trường, hệ số LSB, checksum, tần số, byte địa chỉ/mã bản tin. | |
| Q3 | Bộ điều khiển SDLC/HDLC trên bo mạch là gì (card SyncLink, FPGA, chip SCC…)? Driver Linux nào? Có trả về FCS trong khung không? | |
| Q4 | Bitrate, mã hóa đường truyền (NRZ/NRZI/FM), nguồn clock của từng IMU. | |
| Q5 | CPU/bo mạch và hệ điều hành (x86/ARM, bản Linux, kernel)? Biên dịch chéo hay biên dịch trên máy? | |
| Q6 | Ánh xạ 12 cổng serial → `/dev/tty*`, cổng nào RS232 / RS485 2 dây / RS485 4 dây. | |
| Q7 | Ánh xạ 20 DI / 20 DO → `gpiochip:line`; kênh nào tích cực mức thấp? | |
| Q8 | Baudrate cổng GUI? (115200 chỉ đủ cho IMU ~10 Hz; IMU 100 Hz cần ≥ 460800.) | |
| Q9 | Model máy thu GNSS, giao thức (NMEA/UBX/khác), baudrate mặc định? | |
| Q10 | Jig test có nối DO[i] → DI[i] không (để tự động thử IO)? | |
| Q11 | Phần mềm giao diện viết bằng ngôn ngữ gì (C#, Qt/C++, Python)? Cần sinh mã MAVLink cho ngôn ngữ nào? | |
| Q12 | Tiêu chí PASS tổng thể của bo mạch (vd. mỗi cổng ≥ N lần pass liên tiếp, IMU đủ tần số danh định…)? | |

---

## 8. Ràng buộc kỹ thuật

| Mã | Ràng buộc | Trạng thái |
|---|---|---|
| CON-01 | C++17, không exception, không RTTI; không cấp phát heap sau khởi động (theo quy ước framework). | Đã làm |
| CON-02 | Nghiệp vụ không truy cập HAL trực tiếp; chỉ qua Event/Param/interface thiết bị. | Đã làm |
| CON-03 | Ánh xạ thiết bị Linux đổi được lúc chạy, không cần build lại (`EP_BOARD_CONF`, `EP_SERIAL<i>`…). | Đã làm |
| CON-04 | Có unit test cho logic quan trọng (phát hiện GNSS 5 s, loopback, giải mã IMU, MAVLink). | Đã làm |
| CON-05 | Chạy như dịch vụ systemd, tự khởi động lại. | Đã làm |
| CON-06 | Không cấp phát heap sau khởi động (bộ chặn heap của framework); dữ liệu lớn dùng `msg::Buffer` + pool tĩnh. | Đã làm |
| CON-07 | Priority và stack của từng task cấu hình được (IMU 12, GNSS 10, giao diện 9, app 8, loopback 5, console 4). | Đã làm |

---

## 9. Lịch sử thay đổi

| Ngày | Người | Thay đổi |
|---|---|---|
| 2026-11-03 | | Bản đầu: 12 serial (1 GUI, 1 GNSS, 10 loopback), GNSS 5 s → loopback, 20 DI / 20 DO, 2 IMU SDLC/HDLC, MAVLink. |
| 2026-11-06 | | Nâng cấp theo framework mới (nhánh main): cấu trúc platform/communication/app.business, target `linux_cpu`, Event + Buffer, CON-06, CON-07. |
| 2026-11-09 | | Nâng cấp theo main mới: lớp messaging (`msg::`), MAVLink v2 tách thành `communication/mavlink2` (song song MAVLink v1 của framework), HAL Linux dùng chung, priority SCHED_FIFO trên `linux_cpu`, MAV-12 (UDP). |
| 2026-11-09 | | Cập nhật theo main (QNX): HAL dùng chung tách `common/posix` + `common/linux`, `linux_cpu` dùng `EP_HAL_COMMON="posix;linux"`. Không đổi yêu cầu. |
| 2026-11-09 | | Cập nhật theo main (ModuleWorker): ghi lệnh GNSS chuyển sang worker `gnss_tx` riêng của `SerialBringup`. Không đổi yêu cầu. |
| 2026-11-09 | | GNS-08: giải mã NMEA (GGA/RMC) gửi giờ UTC, lat, lon lên PC qua `GNSS_FIX` + `GPS_RAW_INT`. |
| 2026-11-09 | | Tách GNSS: `svc::GnssReceiver` (service: đọc/giải mã NMEA, gửi lệnh) + `app::GnssBringup` (business: luật 5 s, báo cáo); `SerialBringup` chỉ còn loopback. Không đổi yêu cầu. |
| 2026-11-13 | | Cập nhật theo main (SensorScheduler, Topic, Actuator): IMU đọc qua `SensorScheduler` + `msg::Topic`, driver `device::HdlcImu` không thread. DO chưa chuyển sang `Actuator` (cần quyết định arm/deadman). Không đổi yêu cầu. |
| 2026-11-13 | | Cập nhật theo main (Nios II, PR #9): các mã framework của nhánh bị trùng được đánh số lại – SRC-18…21, HAL-017…020, TGT-008, VER-013/014, PLN-013/014. Không đổi yêu cầu. |
| 2026-11-13 | | Cập nhật theo main (đặc tả yêu cầu framework `docs/REQUIREMENTS.md`): bổ sung các yêu cầu tầng framework của nhánh (HAL-017…020, DEV-003…006, SVC-008/009, COMM-008/009, TGT-008, PROD-005…007, DEF-011…016). |
| | | |

---

## Phụ lục – Mẫu prompt

```
Đọc products/hw_bringup/REQUIREMENTS.md.
Thực hiện các yêu cầu có trạng thái "Mới" hoặc "Sửa": <liệt kê mã, vd. DIO-06, IMU-02>.
Dùng câu trả lời ở mục 7 (Q1, Q2...) làm thông tin đầu vào.
Giữ nguyên các mục "Đã làm" khác. Cập nhật test, README của sản phẩm, và đổi trạng thái các mục đã xong thành "Đã làm".
```
