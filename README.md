# Embedded Platform – Kiến trúc phần mềm nhúng C++ đa nền tảng

Bộ khung firmware C++17 phân lớp rõ ràng: **Platform (Core / OSAL / HAL / Device) → Services → Communication → Application (Framework & Business)**.
Mục tiêu:

- **Chuyển nền tảng** (MCU, board, RTOS, Linux) mà **không sửa** code nghiệp vụ.
- **Tùy biến nghiệp vụ** ở nhiều mức: chọn module, cấu hình lúc biên dịch, tham số lúc chạy.
- **Đổi giao thức truyền thông linh hoạt** (CLI, Modbus RTU, MAVLink…) theo sản phẩm, lúc chạy, hoặc tự nhận dạng.
- Phù hợp embedded: không exception, không RTTI, **không cấp phát heap sau khi khởi động**, tài nguyên tĩnh.

---

## 1. Tổng quan các lớp

```
┌────────────────────────────────────────────────────────────────────────┐
│ PRODUCT (products/<name>)  chọn module + kênh + cấu hình -> 1 firmware │
├────────────────────────────────────────────────────────────────────────┤
│ APPLICATION (app/)                                                     │
│  business/  TemperatureMonitor · AlarmController · <module của bạn>    │
│  framework/ IModule · ModuleManager(Active Object) · StateMachine      │
│             System (Composition Root) · SystemContext (DI)             │
├────────────────────────────────────────────────────────────────────────┤
│ COMMUNICATION (communication/)  ngăn xếp truyền thông                  │
│  link/   IByteStream · IProtocol · LinkManager (chọn/đổi giao thức)    │
│  cli/    CliProtocol     modbus/ ModbusRtuProtocol + Map   (mavlink/…) │
├────────────────────────────────────────────────────────────────────────┤
│ SERVICES (services/)                                                   │
│  EventBus · ParamStore(NV+CRC) · CommandService(bảng lệnh) · Logger    │
│  LedService                                                            │
╞═════════════════════════ PLATFORM (platform/) ═════════════════════════╡
│ DEVICE (device/)  thiết bị NGOÀI chip: Tmp102 : ITemperatureSensor     │
├──────────────────────────────────┬─────────────────────────────────────┤
│ OSAL (osal/)                     │ HAL (hal/)  ngoại vi TRÊN chip      │
│  Thread · Mutex · Semaphore      │  IGpio · IUart · II2c · INvStorage  │
│  Queue<T,N> · time · Critical    │  IBoard (ID logic -> chân vật lý)   │
│  ports: posix │ freertos │ ...   │  ports: linux_sim │ stm32f4 │ ...   │
├──────────────────────────────────┴─────────────────────────────────────┤
│ CORE (core/)   Status · NonCopyable · config.hpp · compiler macros     │
└────────────────────────────────────────────────────────────────────────┘
 UTILS (utils/) – thư viện độc lập, mọi lớp đều dùng được:
   crc32/crc16 · fixed-point format · RingBuffer (SPSC)
   Vendor SDK / Kernel: STM32Cube HAL, FreeRTOS, Zephyr, Linux, ESP-IDF...
```

| Lớp | Trách nhiệm | KHÔNG được làm | Cơ chế đa hình |
|---|---|---|---|
| **Utils** | Thuật toán & cấu trúc dữ liệu thuần: CRC, định dạng fixed-point, ring buffer | Phụ thuộc bất kỳ thư mục nào khác của dự án | – (hàm/template thuần) |
| **Platform › Core** | Kiểu nền tảng: mã lỗi, cấu hình tĩnh, NonCopyable, macro compiler | Phụ thuộc bất kỳ thứ gì (trừ utils) | – |
| **Platform › OSAL** | Thread, mutex, semaphore, queue, thời gian, vùng găng | Truy cập phần cứng | **Link-time** (chọn port khi build) → zero-overhead |
| **Platform › HAL** | Ngoại vi **trên chip**: GPIO, UART, I2C, NV storage; `IBoard` ánh xạ ID logic → phần cứng | Gọi OSAL, chứa logic nghiệp vụ, biết tới sản phẩm | **Runtime** (interface ảo) → mock được, nhiều instance |
| **Platform › Device** | Driver thiết bị **ngoài chip** trên board (cảm biến, EEPROM, màn hình…) + interface theo loại thiết bị | Dùng services (EventBus, ParamStore, Logger), tự tạo thread, gọi API vendor | **Runtime** (`ITemperatureSensor`…) |
| **Services** | Dịch vụ hệ thống dùng chung: event bus, tham số, bảng lệnh, log, LED | Chứa quy tắc nghiệp vụ của sản phẩm; gọi API vendor trực tiếp | Interface (`ILogSink`, `ICommandHandler`) |
| **Communication** | Truyền thông: kênh, giao thức (CLI, Modbus…), chọn/đổi giao thức lúc chạy | Biết tới module nghiệp vụ cụ thể (chỉ dùng Event/Param qua bảng ánh xạ) | **Runtime** (`IByteStream`, `IProtocol`) |
| **Application – Framework** | Vòng đời module, định tuyến event, lập lịch tick, state machine, boot | Chứa nghiệp vụ cụ thể | – |
| **Application – Business** | Quy tắc nghiệp vụ (đo, cảnh báo, điều khiển...) | Include `hal/*`, `osal_port_types.hpp`, header vendor; tự tạo thread | Plug-in qua `IModule` |
| **Product** | Lắp ráp module + kênh/giao thức + cấu hình mặc định cho từng sản phẩm | – | Chọn thư mục khi build |

### Quy tắc phụ thuộc (được CMake ép buộc)

```
                 ┌───────────── platform ─────────────┐
utils ◄── core ◄─┤ osal ◄──┐                          │
                 │ hal  ◄──┴── device                 │
                 └────────────────┬───────────────────┘
                                  ▼
            services ◄── communication(link ◄ cli, modbus) ◄── app_framework ◄── business ◄── product ◄── firmware(main)
```
(mũi tên `A ◄── B`: B phụ thuộc A)

Mỗi lớp chỉ `target_link_libraries` tới lớp ngay dưới → include sai lớp sẽ **lỗi biên dịch**.
Lớp dưới không bao giờ biết lớp trên; giao tiếp ngược lên chỉ qua **callback/interface/event**.

---

## 2. Cấu trúc thư mục

```
EmbededFramework/
├── CMakeLists.txt              # EP_TARGET, EP_PRODUCT
├── cmake/targets/              # linux_sim.cmake, stm32f4_freertos.cmake  (chọn port)
├── cmake/toolchains/           # arm-none-eabi.cmake
├── utils/include/utils/        # crc.hpp, fixed_point.hpp, ring_buffer.hpp  (độc lập)
├── platform/                   # TẦNG NỀN TẢNG
│   ├── core/include/core/      # status.hpp, config.hpp, noncopyable.hpp, compiler.hpp
│   ├── osal/
│   │   ├── include/osal/       # API trung lập: thread, mutex, semaphore, queue, time, critical
│   │   └── ports/{posix,freertos}/   # osal_port_types.hpp + osal_<os>.cpp
│   ├── hal/
│   │   ├── include/hal/        # gpio, uart, i2c, storage, board
│   │   └── ports/{linux_sim,stm32f4}/
│   └── device/
│       └── include/device/     # temperature_sensor (interface), tmp102 (driver)
├── services/
│   └── include/services/       # event_bus, param_store, command_service, logger, led_service
├── communication/
│   ├── link/                   # byte_stream, protocol, link_manager      (ep_comm)
│   ├── cli/                    # cli_protocol                             (ep_proto_cli)
│   └── modbus/                 # modbus_map (bảng ánh xạ), modbus_rtu     (ep_proto_modbus)
├── app/
│   ├── framework/include/app/  # module, module_manager, state_machine, system, product
│   └── business/               # module nghiệp vụ tái sử dụng + app_events.hpp, app_params.hpp
├── products/
│   ├── thermo_pro/             # 3 module, latch, StatsReporter, CLI + Modbus (modbus_map.cpp)
│   └── thermo_lite/            # 2 module, chỉ CLI, cấu hình nhỏ cho MCU ít RAM
├── target/{linux,stm32f4_freertos}/main.cpp     # entry point theo nền tảng đích
├── tools/modbus_udp_client.py  # client Modbus RTU-over-UDP để thử bản mô phỏng
└── tests/                      # unit test host + mocks HAL/stream
```

---

## 3. Mô hình thực thi (threading)

```
 [ISR] ──give_from_isr──► driver/HAL buffer
                                  │
 ┌ thread kênh comm ┐   ┌── thread "led" ──┐    ┌── service khác ──┐
 │ console/aux: CLI,│   │ LedService 50ms  │    │ ParamStore.set() │
 │ Modbus... (RX)   │   │                  │    │                  │
 └───────┬──────────┘   └──────────────────┘    └───────┬──────────┘
         │ publish(Event)                               │ publish(kParamChanged)
         ▼                                              ▼
   ┌─────────────────────────── EventBus (đồng bộ, không chặn) ───────────────┐
   └──────────────────────────────────┬───────────────────────────────────────┘
                                      ▼ on_event() -> queue_.send(kNoWait)
                    ┌───────── ModuleManager: thread "app" ─────────┐
                    │ Queue<Event,N> ─► on_event() mọi module       │
                    │ lịch tick      ─► on_tick() đúng chu kỳ       │
                    └───────────────────────────────────────────────┘
```

- **Toàn bộ nghiệp vụ chạy trên 1 thread** (Active Object) → module **không cần mutex**, hành vi xác định, dễ test.
- Mỗi kênh truyền thông có một thread RX riêng; LED có thread riêng. Dữ liệu chia sẻ sang thread khác dùng `std::atomic` hoặc đi qua event.
- Handler lệnh / giao thức chạy trên thread của kênh ⇒ **không sửa trạng thái module trực tiếp**, mà publish event yêu cầu
  (ví dụ `alarm ack` hoặc coil Modbus 0 → `kAlarmAckRequest`).
- Event là struct kích thước cố định, copy theo giá trị, payload `trivially_copyable` (kiểm tra bằng `static_assert`).

### Trình tự khởi động (`app::System::init`)

1. `board.init()` → gắn console vào Logger
2. Khởi tạo service: ParamStore, CommandService, LedService
3. `LinkManager::init()` (đăng ký lệnh `link`)
4. Bring-up thiết bị: driver gắn lên bus HAL (`Tmp102` ← `II2c`) → `Devices`
5. `product::setup()` → thêm module + khai báo kênh/giao thức; rồi `on_init()` từng module (đăng ký tham số, lệnh)
6. `params.load()` từ NV, gồm cả lựa chọn giao thức `comm.<kênh>` (CRC sai/không có → giữ mặc định)
7. Start service → `on_start()` từng module → `links.start()` (mỗi kênh bật giao thức đã chọn) → publish `kSystemStarted`

---

## 4. Các mức tùy biến nghiệp vụ

| Mức | Cơ chế | Ví dụ |
|---|---|---|
| **Chọn module** | `products/<p>/product.cpp` | thermo_pro có `StatsReporter`, thermo_lite không có |
| **Cấu hình module (compile-time)** | `Module::Config` truyền vào constructor | ngưỡng, chu kỳ mẫu, latch theo từng sản phẩm |
| **Cấu hình hệ thống (compile-time)** | `product_config.hpp` override `EP_CFG_*` | độ sâu queue, số module, mức log tối thiểu |
| **Tham số runtime** | `ParamStore` + CLI `param set/save`, sự kiện `kParamChanged` | đổi `alarm.high` khi đang chạy, lưu Flash |
| **Mở rộng hành vi** | Module mới / state mới trong `StateMachine` | thêm trạng thái `Maintenance` |
| **Module riêng của sản phẩm** | Đặt trong `products/<p>/` | `products/thermo_pro/stats_reporter.hpp` |
| **Giao thức truyền thông** | Kênh + giao thức trong `product::setup()`, tham số `comm.<kênh>` | xem mục 5 |

---

## 5. Truyền thông & đổi giao thức linh hoạt

Nguyên tắc: **nghiệp vụ chỉ nói chuyện bằng mô hình dữ liệu chuẩn** (Event miền + ParamStore).
Mỗi giao thức chỉ là một *cách biểu diễn* khác của mô hình đó, nên thêm/đổi giao thức không đụng tới nghiệp vụ.

```
 Nghiệp vụ          TemperatureMonitor, AlarmController ...   (không include Modbus/MAVLink)
                           │  Event miền, Param
 ═════════════════ EventBus · ParamStore · CommandService ═════════════  ← hợp đồng ổn định
                           │
 ① IProtocol        CliProtocol | ModbusRtuProtocol(+Map) | MavlinkProtocol (tương lai)
                           │  codec + phiên + ánh xạ Event/Param ↔ message
 ② LinkManager      kênh "console", "aux"...: 1 stream + giao thức cho phép + tham số comm.<kênh>
                           │
 ③ IByteStream      UartStream | (UdpStream, UsbCdcStream ...)
                           │
 HAL                IUart (Console, Aux), ...
```

| Muốn đổi… | Chỉ thay | Không động tới |
|---|---|---|
| Giao thức (CLI → Modbus → MAVLink) | `IProtocol` ① | Nghiệp vụ, đường truyền |
| Đường truyền (UART → RS485 → UDP) | `IByteStream` / HAL ③ | Giao thức, nghiệp vụ |
| Thanh ghi/message nào ứng với dữ liệu nào | Bảng ánh xạ của sản phẩm | Giao thức, nghiệp vụ |

### 5.1 Bốn mức chọn giao thức

| Mức | Cách làm | Ví dụ trong repo |
|---|---|---|
| **Build** | Sản phẩm link thư viện giao thức nào thì chỉ giao thức đó có trong firmware | `thermo_pro` link `ep_proto_cli` + `ep_proto_modbus`; `thermo_lite` chỉ `ep_proto_cli` → không tốn Flash cho Modbus |
| **Sản phẩm** | `product::setup()` khai báo kênh + danh sách giao thức được phép + mặc định | `console`: {modbus, cli} mặc định CLI; `aux`: {modbus, cli} mặc định Modbus |
| **Runtime** | Tham số `comm.<kênh>` = `0` off, `1` auto, hoặc ID giao thức (`2` cli, `3` modbus, `4` mavlink); đổi bằng `link set`, `param set`, hoặc **qua chính giao thức** (holding register Modbus 6/7). `param save` để giữ sau khởi động lại | `link set aux cli` |
| **Auto-detect** | `comm.<kênh> = 1`: kênh nghe dữ liệu đầu tiên, hỏi `probe()` của từng giao thức (bỏ qua rác ở đầu); im lặng 10 s thì nhận dạng lại | Cắm terminal → CLI; cắm Modbus master → Modbus |

Giao thức được chọn là giao thức không có trên kênh (vd. `4` khi kênh không có MAVLink) → kênh giữ nguyên
và tham số tự trả về giá trị cũ. Chuyển giao thức luôn diễn ra trên thread của kênh, an toàn kể cả khi lệnh
chuyển được gửi tới **qua chính kênh đó**.

### 5.2 Ánh xạ Modbus của `thermo_pro` (`products/thermo_pro/modbus_map.cpp`)

| Loại | Địa chỉ | Dữ liệu chuẩn |
|---|---|---|
| Input register (FC04) | 0 | nhiệt độ 0.1 °C (`kTemperature`) |
| | 1 | mức cảnh báo 0..3 (`kAlarmStateChanged`) |
| Holding register (FC03/06/16) | 0..5 | `alarm.warn/high/hyst/latch`, `temp.period_ms/offset` (qua ParamStore: kiểm tra miền, phát `kParamChanged`) |
| | 6, 7 | `comm.console`, `comm.aux` — đổi giao thức từ xa |
| Coil (FC05) | 0 | xác nhận cảnh báo (`kAlarmAckRequest`) |
| | 1 | lưu tham số xuống Flash |

Đơn vị (unit id) = 1. Lỗi trả về exception chuẩn: 01 function, 02 địa chỉ, 03 giá trị (ngoài miền ParamStore), 04 thiết bị.
Đóng khung theo độ dài suy ra từ function code + CRC, tự tái đồng bộ khi có rác, nên chạy được cả trên UART, RS485 lẫn RTU-over-UDP.

### 5.3 Thử trên bản mô phỏng

Cổng `aux` của board mô phỏng là UDP `127.0.0.1:15020` (đổi bằng `EP_SIM_AUX_PORT`):

```bash
./build/firmware
```
```bash
tools/modbus_udp_client.py read-input 0 2        # nhiệt độ, mức cảnh báo
```
```bash
tools/modbus_udp_client.py write 1 700           # alarm.high = 70.0 °C
```
```bash
tools/modbus_udp_client.py write 7 2             # comm.aux = CLI: đổi giao thức từ xa
```
```bash
tools/modbus_udp_client.py text "link set aux auto"   # giờ aux là CLI: gửi lệnh văn bản
```
```bash
tools/modbus_udp_client.py coil 0 on             # auto: Modbus được tự nhận dạng, xác nhận cảnh báo
```

---

## 6. Hướng dẫn mở rộng

### 6.1 Thêm module nghiệp vụ

```cpp
class PumpController final : public app::IModule {
public:
    const char* name() const override { return "pump"; }
    core::Status on_init(app::SystemContext& ctx) override {
        ctx_ = &ctx;
        return ctx.params.register_params(defs_, 1);          // tham số của module
    }
    void on_event(const svc::Event& e) override {
        app::events::TemperatureSample s{};
        if (e.id == app::events::kTemperature && e.get(s)) { /* quy tắc nghiệp vụ */ }
    }
    std::uint32_t tick_period_ms() const override { return 1000; }
    void on_tick(std::uint64_t now) override { /* tác vụ định kỳ */ }
private:
    app::SystemContext* ctx_ = nullptr;
    const svc::ParamDef defs_[1] = {{0x1201, "pump.min_on_s", 30, 5, 600, "s"}};
};
```

1. Cấp dải ID trong `app/business/app_params.hpp` (param) và `app_events.hpp` (event).
2. Thêm file `.cpp` vào target `ep_business` (hoặc giữ trong `products/<p>/` nếu chỉ một sản phẩm dùng).
3. `s.modules.add(&pump)` trong `product::setup()`.
4. Viết unit test như `tests/test_alarm_controller.cpp` – đưa event vào, kiểm tra kết quả, không cần phần cứng.

### 6.2 Thêm sản phẩm mới

```
products/my_product/
├── CMakeLists.txt              # add_library(ep_product ...) + link ep_business + ep_proto_* cần dùng
├── config/product_config.hpp   # override EP_CFG_* (tùy chọn)
└── product.cpp                 # product::name(), product::setup(): module + kênh/giao thức
```

```bash
cmake -B build-my -DEP_PRODUCT=my_product
```

### 6.3 Port sang board mới (cùng OS)

1. Tạo `platform/hal/ports/<board>/` hiện thực `IGpio/IUart/II2c/INvStorage` bằng SDK của chip.
2. Hiện thực `IBoard` ánh xạ `GpioId::StatusLed` … sang chân vật lý, và `hal::board()`.
3. Tạo `cmake/targets/<board>_<os>.cmake` đặt `EP_HAL_PORT=<board>`, `EP_OSAL_PORT`, `EP_MAIN_SOURCE`,
   và entry point `target/<board>_<os>/main.cpp` (khởi tạo clock/ngoại vi của vendor, chạy `app::System`).
   Build bằng `-DEP_TARGET=<board>_<os>`.
   ⇒ Services, App, Product **không đổi một dòng**.

Phân biệt hai thư mục: `platform/` là **lớp phần mềm trừu tượng hóa nền tảng** (Core/OSAL/HAL/Device, có port
cho từng OS/board); `target/` + `cmake/targets/` là **cấu hình build cho một nền tảng đích cụ thể**
(chọn port nào, toolchain, entry point `main`).

### 6.4 Port sang hệ điều hành mới (Zephyr, ThreadX, CMSIS-RTOS2, ESP-IDF…)

1. Tạo `platform/osal/ports/<os>/include/osal_port_types.hpp` – khai báo `MutexHandle`, `SemaphoreHandle`, `ThreadHandle`.
2. Tạo `platform/osal/ports/<os>/osal_<os>.cpp` – hiện thực các hàm trong `platform/osal/include/osal/*.hpp`
   (tham khảo `osal_freertos.cpp`, ~100 dòng).
3. Đặt `EP_OSAL_PORT=<os>` trong file target (`cmake/targets/*.cmake`). `Queue<T,N>` tự động hoạt động vì được viết trên Mutex/Semaphore.

### 6.5 Thêm tiện ích dùng chung

Thuật toán / cấu trúc dữ liệu **không phụ thuộc OS, phần cứng hay nghiệp vụ** (checksum, mã hóa
COBS/base64, bộ lọc số, PID, bitfield...) đặt trong `utils/` (namespace `utils`), kèm test trong
`tests/test_utils.cpp`. `utils/` không phụ thuộc thư mục nào khác nên mọi lớp, kể cả platform, đều dùng được.
Thứ gì cần mutex/thread (vd. `osal::Queue`) thuộc OSAL; thứ gì gắn với event/tham số thuộc Services.

### 6.6 Thêm loại thiết bị

1. Interface theo loại thiết bị trong `platform/device/include/device/` (vd. `device::IPressureSensor`).
2. Driver chip cụ thể (vd. `device::Bmp280`) chỉ dùng bus HAL (`hal::II2c`, `hal::ISpi`…), có thể dùng OSAL
   (delay, khóa bus dùng chung) và utils; **không** dùng services – báo lỗi bằng `core::Status`, không tự tạo thread.
3. Thêm con trỏ interface vào `app::Devices`, khởi tạo driver trong `System::bring_up_devices()`.

Nghiệp vụ chỉ phụ thuộc interface → thay chip cảm biến không ảnh hưởng nghiệp vụ. HAL khác theo board,
còn driver thiết bị giống nhau trên mọi board và có thể dùng lại ở dự án khác.

### 6.7 Thêm giao thức mới (vd. MAVLink, giao thức riêng)

1. Tạo `communication/<proto>/` với target `ep_proto_<proto>` link `ep_comm`; thư viện bên thứ ba (vd. header
   MAVLink sinh từ XML) đặt ở `third_party/` và **chỉ** target này được include.
2. Hiện thực `comm::IProtocol`: `id()` (giá trị ổn định, vd. `proto::kMavlink` hoặc `proto::kUserBase + n`),
   `attach/detach`, `on_rx` (parse), `poll` (heartbeat/timeout), `probe` (nhận dạng chặt chẽ, có CRC).
   Cần đẩy dữ liệu chủ động thì kế thừa thêm `svc::IEventListener` và ghi qua `io.write()` (đã thread-safe).
3. Đặt bảng ánh xạ Event/Param ↔ message ở sản phẩm (như `modbus_map.cpp`), không viết vào nghiệp vụ.
4. Trong `product::setup()`: tạo instance (mỗi kênh một instance) và thêm vào danh sách của kênh.
5. Test bằng `mock::ScriptedStream` + khung mẫu (golden frame), như `tests/test_modbus.cpp`, `tests/test_link_manager.cpp`.

---

## 7. Build & chạy

### Linux (mô phỏng phần cứng)

```bash
cmake -S . -B build
```
```bash
cmake --build build -j
```
```bash
ctest --test-dir build --output-on-failure
```
```bash
./build/firmware            # Ctrl+C để dừng; hoặc --run-for <giây>
```

Phần cứng mô phỏng: cảm biến TMP102 cho nhiệt độ hình sin 15–85 °C (chu kỳ 40 s), hoặc cố định bằng
`EP_SIM_TEMP=72.5`; Flash mô phỏng là file `ep_nvram.bin` (đổi bằng `EP_SIM_NVRAM`); cổng `aux` là
UDP `127.0.0.1:15020` (đổi bằng `EP_SIM_AUX_PORT`).

Lệnh CLI:

```
help                         danh sách lệnh
sys info | modules | log <0-5> | reboot | quit
param list | get <n> | set <n> <v> | save | reset
temp                         nhiệt độ hiện tại, thống kê
alarm status | ack           trạng thái cảnh báo / xác nhận
link list | set <kênh> <off|auto|cli|modbus>   kênh truyền thông & đổi giao thức
```

### STM32F407 + FreeRTOS

```bash
cmake -B build-stm32 -DEP_TARGET=stm32f4_freertos \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
      -DEP_CUBE_DIR=/path/to/cubemx_project -DEP_FREERTOS_DIR=/path/to/FreeRTOS-Kernel
```

Xem yêu cầu cấu hình CubeMX ở đầu `cmake/targets/stm32f4_freertos.cmake`.
Port STM32 và FreeRTOS là **mẫu tham khảo** – chưa được biên dịch/kiểm thử trên phần cứng thật trong repo này.

---

## 8. Quy ước code cho môi trường nhúng

- C++17, `-fno-exceptions -fno-rtti`; lỗi trả về `core::Status`.
- Không `new`/`malloc` sau khi khởi động: dùng `std::array`, `static` object, stack cấp phát tĩnh cho thread.
- Không dùng `std::string`, `std::vector`, `iostream` trong firmware (chỉ được dùng trong test / port host).
- Khởi tạo hai pha (`init()` sau constructor) cho object phụ thuộc phần cứng.
- API gọi được từ ISR chỉ có hậu tố `_from_isr`. Logger không được gọi trong ISR.
- Đơn vị cố định trong nghiệp vụ (0.1 °C, ms) dùng số nguyên — tránh float trên MCU không có FPU.
- Module nghiệp vụ không include header của giao thức (Modbus, MAVLink…); dữ liệu ra/vào chỉ qua Event/Param.

## 9. Design pattern áp dụng

| Pattern | Ở đâu |
|---|---|
| Layered Architecture + Dependency Rule | Toàn bộ, ép bằng CMake |
| Lock-free SPSC buffer (ISR → thread) | `utils::RingBuffer` |
| Adapter / Bridge | HAL ports, OSAL ports |
| Abstract Factory | `hal::IBoard` |
| Dependency Injection / Composition Root | `SystemContext`, `app::System` |
| Publish–Subscribe | `svc::EventBus` |
| Active Object | `app::ModuleManager` |
| State Machine (state = method) | `app::StateMachine`, `AlarmController` |
| Plug-in / Strategy | `IModule`, `ITemperatureSensor`, `ILogSink`, `ICommandHandler` |
| Command | `CommandService` |
| Adapter / Anti-corruption layer | `IProtocol` + bảng ánh xạ (`modbus::Map`) |
| Strategy chọn lúc chạy | `LinkManager` (tham số `comm.<kênh>`, auto-detect qua `probe()`) |
