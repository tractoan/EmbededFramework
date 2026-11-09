# hw_bringup – firmware bring-up phần cứng giao tiếp MAVLink

Yêu cầu (cập nhật được, dùng làm đầu vào cho prompt): [`REQUIREMENTS.md`](REQUIREMENTS.md).

Firmware chạy trên **CPU nhúng (Linux)** để kiểm tra bo mạch, báo kết quả lên **phần mềm giao diện qua MAVLink v2**.

| Phần cứng | Kiểm tra |
|---|---|
| 12 cổng serial RS232/RS485 | cổng `kGcsPort` (0) = phần mềm giao diện; cổng `kGnssPort` (1) = máy thu GNSS; 10 cổng còn lại: loopback liên tục |
| Máy thu GNSS | trong `gnss.detect_ms` (5 s) sau khởi động: có byte nào → **CONNECTED** (chuyển tiếp dữ liệu lên giao diện, nhận lệnh cấu hình); không có → **ABSENT**, cổng chuyển sang kiểm tra loopback |
| 20 digital input | đọc 20 ms/lần, báo khi thay đổi + định kỳ `io.report_ms` |
| 20 digital output | đặt theo lệnh từ giao diện (trạng thái an toàn lúc khởi động: tắt hết) |
| 2 IMU SDLC/HDLC | đọc từng khung, giải mã theo bảng định dạng (`ln200`, `hg1700`), đếm khung/lỗi, đo tần số, gửi `imu.rate_hz` |

## Kiến trúc (theo các lớp của framework)

```
 products/hw_bringup   product.cpp (lắp ráp) · bringup_config.hpp (ánh xạ cổng/IMU) · mavlink_dialect.cpp (Event <-> bản tin)
 app/business          SerialBringup (loopback + phát hiện GNSS) · IoBringup · ImuBringup   (chỉ nói chuyện qua Event/Param)
 communication/mavlink2 Mavlink2Protocol: khung v2 (+nhận v1), heartbeat, PARAM_* <-> ParamStore, COMMAND_LONG/ACK, IDialect
 communication/net     UdpStream: kênh MAVLink thứ hai qua mạng (khi target có TCP/IP)
 services              SerialPortBank (thread/cổng: Loopback|Stream|External) · HdlcImu (thread đọc khung)
 platform/device       GpioInput/OutputBank (DI/DO) · ImuFormat + decode_imu_frame (giải mã theo bảng)
 platform/hal          IBoard::serial(i) / digital_input(i) / digital_output(i) / hdlc(i) · IHdlcPort
   ports/linux_cpu     termios(+RS485) · GPIO chardev v2 · SyncLink/N_HDLC/chardev/AF_PACKET · file NV
   ports/linux_sim     12 serial (UDP/loopback/GNSS giả lập) · DI nối vòng DO · 2 IMU giả lập
 platform/target       linux_cpu (khởi động trên CPU thật) · linux_sim
```

Luồng: thread cổng/IMU → service (atomic, ring buffer) → module (thread `app`, tick 20 ms) → `EventBus`
→ `MavlinkProtocol` + `BringupDialect` → cổng giao diện. Chiều ngược lại: bản tin → Event yêu cầu → module.

### Bộ nhớ & task (theo chính sách của framework)

- **Không cấp phát động sau khởi động**: mọi dữ liệu lớn (mẫu IMU, bảng loopback, gói GNSS, STATUSTEXT) đi trong
  `Event::buffer` lấy từ **pool tĩnh** khai báo ở `setup_buffers()` (64 B×32, 128 B×64, 256 B×16, 1024 B×16);
  payload inline giữ mặc định 16 B. Hết pool → gói đó bị bỏ và được đếm (`sys buffers`, `gnss`).
  Bộ đệm của phần cứng mô phỏng và port `linux_cpu` cũng là mảng cố định.
- **Stack**: 18 task lấy stack từ vùng tĩnh `EP_CFG_STACK_POOL_BYTES` = 56 KB (dùng ~45 KB), `EP_CFG_MAX_THREADS` = 24.
- **Priority** (0..15, `product.cpp`): IMU 12 · cổng GNSS 10 · kênh giao diện (serial + UDP) 9 · nghiệp vụ `app` 8 · cổng loopback 5 ·
  console 4 · LED 4. Xem lúc chạy: `sys tasks`. Trên target `linux_cpu` priority là SCHED_FIFO thật
  (`EP_POSIX_REALTIME`; cần root hoặc `setcap cap_sys_nice+ep firmware`, không có quyền thì chạy SCHED_OTHER).

> Framework có hai giao thức MAVLink: `communication/mavlink/` (v1, bảng ánh xạ telemetry `NAMED_VALUE_FLOAT`, dùng bởi
> thermo_pro) và `communication/mavlink2/` (v2 + dialect). Sản phẩm này cần v2 vì bản tin riêng có msgid 62000–62004
> (v1 chỉ có msgid 8 bit) và dùng `SERIAL_CONTROL`, nên chỉ link `ep_proto_mavlink2`.

## Giao diện MAVLink

Dialect: [`tools/mavlink/bringup.xml`](../../tools/mavlink/bringup.xml) (include `common.xml`) – phần mềm giao diện sinh mã
bằng `mavgen` (C#, C++, Python...). System/component của firmware: 1/1; heartbeat 1 Hz (`MAV_TYPE_ONBOARD_CONTROLLER`).

| Chiều | Bản tin | Nội dung |
|---|---|---|
| ↑ | `BRINGUP_STATUS` (62000) 1 Hz | trạng thái GNSS (DETECTING/CONNECTED/ABSENT), byte rx/tx, baudrate, cổng GCS/GNSS |
| ↑ | `SERIAL_LOOPBACK_STATUS` (62001) `ser.report_ms` | `ok_mask`, `tested_mask`, vai trò + kết quả + số lần pass/fail từng cổng |
| ↑ | `IO_STATUS` (62002) | `input_mask`, `output_mask` (khi input đổi, sau mỗi lệnh output, định kỳ) |
| ↑ | `IMU_DATA` (62004) `imu.rate_hz` | gyro rad/s, accel m/s², tần số, số khung/lỗi, cờ, 48 byte khung thô |
| ↑ | `SERIAL_CONTROL` (device=GPS1, flags=REPLY) | dữ liệu từ máy thu GNSS (tắt bằng `gnss.forward=0`) |
| ↑ | `STATUSTEXT` | sự kiện: GNSS phát hiện/vắng, cổng PASS↔FAIL, IMU mất/có dữ liệu |
| ↓ | `OUTPUT_SET` (62003) | `out = (out & ~mask) \| (value & mask)` → trả `IO_STATUS` |
| ↓ | `COMMAND_LONG` `MAV_CMD_DO_SET_RELAY` | param1 = kênh output, param2 = 0/1 |
| ↓ | `SERIAL_CONTROL` (device=GPS1) | lệnh cấu hình gửi tới máy thu (`data[count]`); `baudrate`≠0 → đổi tốc độ cổng GNSS trước |
| ↓↑ | `PARAM_REQUEST_LIST/READ`, `PARAM_SET` → `PARAM_VALUE` | tham số (kiểu INT32, mã hóa C-cast) |
| ↓ | `COMMAND_LONG` `MAV_CMD_PREFLIGHT_STORAGE` | param1: 0 đọc NV, 1 lưu NV, 2 mặc định |

Lệnh GNSS chỉ được gửi khi máy thu ở trạng thái CONNECTED (ngược lại: bỏ và báo `STATUSTEXT`).

### Tham số

| Tên | Mặc định | Ý nghĩa |
|---|---|---|
| `ser.period_ms` / `ser.timeout_ms` | 200 / 50 | chu kỳ một lần thử loopback / thời gian chờ (cộng thời gian truyền khung) |
| `ser.report_ms` | 500 | chu kỳ `SERIAL_LOOPBACK_STATUS` |
| `gnss.detect_ms` | 5000 | cửa sổ phát hiện máy thu sau khởi động |
| `gnss.baud` | 115200 | baudrate cổng máy thu |
| `gnss.forward` | 1 | chuyển tiếp dữ liệu máy thu lên giao diện |
| `io.report_ms` | 200 | chu kỳ `IO_STATUS` khi không đổi |
| `imu.rate_hz` / `imu.timeout_ms` | 10 / 500 | tần số gửi `IMU_DATA` / ngưỡng báo mất dữ liệu |
| `comm.console`, `comm.gcs`, `comm.gcs_udp` | cli, mavlink, mavlink | giao thức trên từng kênh (`link set gcs cli` để dùng terminal trên cổng giao diện) |

## Loopback serial

Khung thử 18 byte `55 AA <cổng> <seq> <12 byte LFSR theo seq> <CRC16>`; PASS khi nhận lại đúng nguyên khung.
Kết quả: `PASS`, `TIMEOUT` (không nhận gì), `MISMATCH` (sai/thiếu byte), `OPEN-ERR` (không mở được cổng).
- RS232 / RS422 / RS485 4 dây: giắc TX→RX trên chính cổng.
- RS485 2 dây: spec `/dev/ttyX:rs485-echo` (nhận lại chính dữ liệu phát) hoặc nối chéo từng cặp cổng với
  `kLoopbackPeer` trong `bringup_config.hpp` (cổng chỉ số nhỏ điều khiển cả hai chiều).

## IMU

`device::ImuFormat` mô tả khung bằng bảng (offset trường, độ rộng, endian, LSB, checksum, byte nhận dạng).
Hai định dạng tham khảo trong `platform/device/src/imu_format.cpp`:
- `ln200`: 13 word 16-bit, Δv 2⁻¹⁴ m/s, Δθ 2⁻¹⁹ rad, 400 Hz, checksum tổng 16-bit.
- `hg1700`: địa chỉ 0x0E + mã bản tin 0x01, Δθ 2⁻³³ rad, Δv 2⁻²⁷ ft/s (int32), 100 Hz.

> **Phải đối chiếu ICD** đúng phiên bản IMU (thứ tự byte, vị trí trường, hệ số, checksum, tần số) trước khi tin số liệu đã giải mã.
> Khung thô luôn có trong `IMU_DATA.raw` và lệnh CLI `imu` để đối chiếu. Đổi định dạng: sửa/ thêm `ImuFormat` và chọn trong `bringup_config.hpp`.
> FCS-16 của HDLC mặc định do phần cứng kiểm; nếu bộ điều khiển trả cả FCS, đặt `kImuSoftwareFcs = true`.

## Build & chạy

Mô phỏng trên PC (cổng serial 0 = UDP `127.0.0.1:14555`):

```bash
cmake -B build-bringup -DEP_PRODUCT=hw_bringup
```
```bash
cmake --build build-bringup -j && ctest --test-dir build-bringup --output-on-failure
```
```bash
./build-bringup/firmware
```
```bash
tools/mavlink/bringup_gcs.py monitor
```

Phần mềm giao diện cũng kết nối được qua mạng (kênh `gcs_udp`, cổng `kGcsUdpPort` = 14560 trong `bringup_config.hpp`;
firmware trả lời địa chỉ gửi gần nhất, nên phía giao diện gửi HEARTBEAT trước). QGroundControl: thêm UDP link với
server `<ip-bo-mạch>:14560`. Telemetry được gửi trên mọi kênh MAVLink đang hoạt động:

```bash
tools/mavlink/bringup_gcs.py --udp 127.0.0.1:14560 monitor
```

Kịch bản mô phỏng: `EP_SIM_SERIAL1=loop` (không có máy thu → loopback PASS sau 5 s), `EP_SIM_SERIAL1=gnss:7000`
(máy thu bật muộn), `EP_SIM_SERIAL5=broken`, `EP_SIM_SERIAL7=silent`, `EP_SIM_DI_STUCK=8` (DI3 kẹt), `EP_SIM_IMU_OFF=2` (mất IMU1).
Chi tiết ở đầu `platform/hal/ports/linux_sim/sim_bringup.hpp`.

Bo mạch thật (CPU Linux):

```bash
cmake -B build-cpu -DEP_TARGET=linux_cpu -DEP_PRODUCT=hw_bringup
```
```bash
cmake --build build-cpu -j
```
```bash
EP_BOARD_CONF=products/hw_bringup/deploy/ep_board.conf.example ./build-cpu/firmware
```
```bash
tools/mavlink/bringup_gcs.py --serial /dev/ttyUSB0 --baud 115200 monitor
```

Biên dịch chéo ARM64: thêm `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake`.
Chạy như dịch vụ: `deploy/ep-bringup.service`.

Công cụ `tools/mavlink/bringup_gcs.py` (chỉ cần Python 3; `pyserial` khi dùng cổng COM) cũng là mẫu cho phần mềm giao diện:
`monitor`, `output <kênh> <0|1>`, `outputs <hex>`, `relay`, `gnss "<lệnh>" [--gnss-baud B]`, `params`, `param-set`, `save`.

### Lệnh CLI trên console

```
serial [reset]               bảng vai trò / kết quả loopback / byte từng cổng
gnss [send <text> | baud <bps>]  trạng thái máy thu, gửi lệnh (tự thêm CR LF)
io [set <ch> <0|1> | out <hex> | all <0|1>]   DI/DO
imu                          thống kê khung, mẫu cuối, khung thô
link list | set gcs <mavlink|cli|auto>
sys tasks | buffers           task (priority, stack), pool buffer
param list | set <n> <v> | save
```

## Cấu hình phần cứng

- Chỉ số cổng & chức năng: `bringup_config.hpp` (cổng GCS/GNSS, baudrate, cặp loopback, invert DI, định dạng + cấu hình HDLC của IMU).
- Thiết bị Linux tương ứng: `platform/hal/ports/linux_cpu/board_config.hpp`, ghi đè lúc chạy bằng `EP_BOARD_CONF` hoặc `EP_SERIAL<i>`, `EP_DI<i>`,
  `EP_DO<i>`, `EP_HDLC<i>`, `EP_NVRAM` (ví dụ: `deploy/ep_board.conf.example`).
- Backend HDLC: `synclink:` (card SyncLink, cấu hình MGSL + N_HDLC), `nhdlc:` (tty đã cấu hình đồng bộ), `chardev:` (driver trả 1 khung/`read`),
  `packet:` (netdev generic-HDLC, cần `CAP_NET_RAW`).
