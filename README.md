# Embedded Platform – Kiến trúc phần mềm nhúng C++ đa nền tảng

Bộ khung firmware C++17 phân lớp rõ ràng **HAL / OSAL / Service & Middleware / Application & Business**.
Mục tiêu:

- **Chuyển nền tảng** (MCU, board, RTOS, Linux) mà **không sửa** code nghiệp vụ.
- **Tùy biến nghiệp vụ** ở nhiều mức: chọn module, cấu hình lúc biên dịch, tham số lúc chạy.
- Phù hợp embedded: không exception, không RTTI, **không cấp phát heap sau khi khởi động**, tài nguyên tĩnh.

---

## 1. Tổng quan các lớp

```
┌────────────────────────────────────────────────────────────────────────┐
│ PRODUCT (products/<name>)   chọn module + cấu hình -> 1 firmware/sp    │
├────────────────────────────────────────────────────────────────────────┤
│ APPLICATION & BUSINESS (app/)                                          │
│  modules/   TemperatureMonitor · AlarmController · <module của bạn>    │
│  framework/ IModule · ModuleManager(Active Object) · StateMachine      │
│             System (Composition Root) · SystemContext (DI)             │
├────────────────────────────────────────────────────────────────────────┤
│ SERVICE & MIDDLEWARE (services/)                                       │
│  EventBus · ParamStore(NV+CRC) · CommandService(CLI) · Logger          │
│  LedService · Device drivers (Tmp102 : ITemperatureSensor)             │
├──────────────────────────────────┬─────────────────────────────────────┤
│ OSAL (osal/)                     │ HAL (hal/)                          │
│  Thread · Mutex · Semaphore      │  IGpio · IUart · II2c · INvStorage  │
│  Queue<T,N> · time · Critical    │  IBoard (ID logic -> chân vật lý)   │
│  ports: posix │ freertos │ ...   │  ports: linux_sim │ stm32f4 │ ...   │
├──────────────────────────────────┴─────────────────────────────────────┤
│ UTILS (utils/) crc32/crc16 · fixed-point format · RingBuffer (SPSC)    │
├────────────────────────────────────────────────────────────────────────┤
│ CORE (core/)   Status · NonCopyable · config.hpp · compiler macros     │
└────────────────────────────────────────────────────────────────────────┘
   Vendor SDK / Kernel: STM32Cube HAL, FreeRTOS, Zephyr, Linux, ESP-IDF...
```

| Lớp | Trách nhiệm | KHÔNG được làm | Cơ chế đa hình |
|---|---|---|---|
| **Core** | Kiểu nền tảng: mã lỗi, cấu hình tĩnh, NonCopyable, macro compiler | Phụ thuộc bất kỳ thứ gì | – |
| **Utils** | Thuật toán & cấu trúc dữ liệu thuần: CRC, định dạng fixed-point, ring buffer | Phụ thuộc OS, phần cứng, nghiệp vụ | – (hàm/template thuần) |
| **HAL** | Trừu tượng ngoại vi: GPIO, UART, I2C, NV storage; `IBoard` ánh xạ ID logic → phần cứng | Gọi OSAL, chứa logic nghiệp vụ, biết tới sản phẩm | **Runtime** (interface ảo) → mock được, nhiều instance |
| **OSAL** | Thread, mutex, semaphore, queue, thời gian, vùng găng | Truy cập phần cứng | **Link-time** (chọn port khi build) → zero-overhead |
| **Service & Middleware** | Dịch vụ dùng chung: event bus, tham số, CLI, log, LED, driver thiết bị | Chứa quy tắc nghiệp vụ của sản phẩm; gọi API vendor trực tiếp | Interface (`ITemperatureSensor`, `ILogSink`, `ICommandHandler`) |
| **Application – Framework** | Vòng đời module, định tuyến event, lập lịch tick, state machine, boot | Chứa nghiệp vụ cụ thể | – |
| **Application – Business** | Quy tắc nghiệp vụ (đo, cảnh báo, điều khiển...) | Include `hal/*`, `osal_port_types.hpp`, header vendor; tự tạo thread | Plug-in qua `IModule` |
| **Product** | Lắp ráp module + cấu hình mặc định cho từng sản phẩm | – | Chọn thư mục khi build |

### Quy tắc phụ thuộc (được CMake ép buộc)

```
core ◄── utils ◄── osal ◄──┐
              ◄── hal  ◄──┴── services ◄── app_framework ◄── modules ◄── product ◄── firmware(main)
```

Mỗi lớp chỉ `target_link_libraries` tới lớp ngay dưới → include sai lớp sẽ **lỗi biên dịch**.
Lớp dưới không bao giờ biết lớp trên; giao tiếp ngược lên chỉ qua **callback/interface/event**.

---

## 2. Cấu trúc thư mục

```
embedded-platform/
├── CMakeLists.txt              # EP_PLATFORM, EP_PRODUCT
├── cmake/platforms/            # linux_sim.cmake, stm32f4_freertos.cmake  (chọn port)
├── cmake/toolchains/           # arm-none-eabi.cmake
├── core/include/core/          # status.hpp, config.hpp, noncopyable.hpp, compiler.hpp
├── utils/include/utils/        # crc.hpp, fixed_point.hpp, ring_buffer.hpp
├── osal/
│   ├── include/osal/           # API trung lập: thread, mutex, semaphore, queue, time, critical
│   └── ports/{posix,freertos}/ # osal_port_types.hpp + osal_<os>.cpp
├── hal/
│   ├── include/hal/            # gpio, uart, i2c, storage, board
│   └── ports/{linux_sim,stm32f4}/
├── services/
│   ├── include/services/       # event_bus, param_store, command_service, logger, led_service
│   └── include/services/drivers/  # temperature_sensor (interface), tmp102 (driver)
├── app/
│   ├── framework/include/app/  # module, module_manager, state_machine, system, product
│   └── modules/                # module nghiệp vụ tái sử dụng + app_events.hpp, app_params.hpp
├── products/
│   ├── thermo_pro/             # 3 module, latch, module riêng StatsReporter
│   └── thermo_lite/            # 2 module, cấu hình nhỏ cho MCU ít RAM
├── platform/{linux,stm32f4_freertos}/main.cpp   # entry point theo nền tảng
└── tests/                      # unit test host + mocks HAL
```

---

## 3. Mô hình thực thi (threading)

```
 [ISR] ──give_from_isr──► driver/HAL buffer
                                  │
 ┌── thread "cli" ──┐   ┌── thread "led" ──┐    ┌── service khác ──┐
 │ CommandService   │   │ LedService 50ms  │    │ ParamStore.set() │
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
- Service dùng thread riêng khi cần (CLI, LED); dữ liệu chia sẻ sang thread khác dùng `std::atomic` hoặc đi qua event.
- Handler CLI chạy trên thread CLI ⇒ **không sửa trạng thái module trực tiếp**, mà publish event yêu cầu
  (ví dụ `alarm ack` → `kAlarmAckRequest`).
- Event là struct kích thước cố định, copy theo giá trị, payload `trivially_copyable` (kiểm tra bằng `static_assert`).

### Trình tự khởi động (`app::System::init`)

1. `board.init()` → gắn console vào Logger
2. Khởi tạo service: ParamStore, CommandService, LedService
3. Bring-up thiết bị: driver gắn lên bus HAL (`Tmp102` ← `II2c`) → `Devices`
4. `product::register_modules()` → `on_init()` từng module (đăng ký tham số, lệnh CLI)
5. `params.load()` từ NV (CRC sai/không có → giữ mặc định)
6. Start service → `on_start()` từng module (đọc tham số, khởi động FSM) → publish `kSystemStarted`

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

---

## 5. Hướng dẫn mở rộng

### 5.1 Thêm module nghiệp vụ

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

1. Cấp dải ID trong `app/modules/app_params.hpp` (param) và `app_events.hpp` (event).
2. Thêm file `.cpp` vào target `ep_modules` (hoặc giữ trong `products/<p>/` nếu chỉ một sản phẩm dùng).
3. `modules.add(&pump)` trong `product.cpp`.
4. Viết unit test như `tests/test_alarm_controller.cpp` – đưa event vào, kiểm tra kết quả, không cần phần cứng.

### 5.2 Thêm sản phẩm mới

```
products/my_product/
├── CMakeLists.txt              # add_library(ep_product ...) + link ep_modules
├── config/product_config.hpp   # override EP_CFG_* (tùy chọn)
└── product.cpp                 # product::name(), product::register_modules()
```

```bash
cmake -B build-my -DEP_PRODUCT=my_product
```

### 5.3 Port sang board mới (cùng OS)

1. Tạo `hal/ports/<board>/` hiện thực `IGpio/IUart/II2c/INvStorage` bằng SDK của chip.
2. Hiện thực `IBoard` ánh xạ `GpioId::StatusLed` … sang chân vật lý, và `hal::board()`.
3. Tạo `cmake/platforms/<board>_<os>.cmake` đặt `EP_HAL_PORT=<board>`.
   ⇒ Services, App, Product **không đổi một dòng**.

### 5.4 Port sang hệ điều hành mới (Zephyr, ThreadX, CMSIS-RTOS2, ESP-IDF…)

1. Tạo `osal/ports/<os>/include/osal_port_types.hpp` – khai báo `MutexHandle`, `SemaphoreHandle`, `ThreadHandle`.
2. Tạo `osal/ports/<os>/osal_<os>.cpp` – hiện thực các hàm trong `osal/include/osal/*.hpp`
   (tham khảo `osal_freertos.cpp`, ~100 dòng).
3. Đặt `EP_OSAL_PORT=<os>` trong file platform. `Queue<T,N>` tự động hoạt động vì được viết trên Mutex/Semaphore.

### 5.5 Thêm tiện ích dùng chung

Thuật toán / cấu trúc dữ liệu **không phụ thuộc OS, phần cứng hay nghiệp vụ** (checksum, mã hóa
COBS/base64, bộ lọc số, PID, bitfield...) đặt trong `utils/` (namespace `utils`), kèm test trong
`tests/test_utils.cpp`. Mọi lớp từ OSAL/HAL trở lên đều dùng được.
Thứ gì cần mutex/thread (vd. `osal::Queue`) thuộc OSAL; thứ gì gắn với event/tham số thuộc Services.

### 5.6 Thêm loại thiết bị

Định nghĩa interface thiết bị trong `services/include/services/drivers/` (vd. `IPressureSensor`), driver cụ thể
dùng interface HAL, thêm con trỏ vào `app::Devices`, khởi tạo trong `System::bring_up_devices()`.
Nghiệp vụ chỉ phụ thuộc interface → thay chip cảm biến không ảnh hưởng nghiệp vụ.

---

## 6. Build & chạy

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
`EP_SIM_TEMP=72.5`; Flash mô phỏng là file `ep_nvram.bin` (đổi bằng `EP_SIM_NVRAM`).

Lệnh CLI:

```
help                         danh sách lệnh
sys info | modules | log <0-5> | reboot | quit
param list | get <n> | set <n> <v> | save | reset
temp                         nhiệt độ hiện tại, thống kê
alarm status | ack           trạng thái cảnh báo / xác nhận
```

### STM32F407 + FreeRTOS

```bash
cmake -B build-stm32 -DEP_PLATFORM=stm32f4_freertos \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
      -DEP_CUBE_DIR=/path/to/cubemx_project -DEP_FREERTOS_DIR=/path/to/FreeRTOS-Kernel
```

Xem yêu cầu cấu hình CubeMX ở đầu `cmake/platforms/stm32f4_freertos.cmake`.
Port STM32 và FreeRTOS là **mẫu tham khảo** – chưa được biên dịch/kiểm thử trên phần cứng thật trong repo này.

---

## 7. Quy ước code cho môi trường nhúng

- C++17, `-fno-exceptions -fno-rtti`; lỗi trả về `core::Status`.
- Không `new`/`malloc` sau khi khởi động: dùng `std::array`, `static` object, stack cấp phát tĩnh cho thread.
- Không dùng `std::string`, `std::vector`, `iostream` trong firmware (chỉ được dùng trong test / port host).
- Khởi tạo hai pha (`init()` sau constructor) cho object phụ thuộc phần cứng.
- API gọi được từ ISR chỉ có hậu tố `_from_isr`. Logger không được gọi trong ISR.
- Đơn vị cố định trong nghiệp vụ (0.1 °C, ms) dùng số nguyên — tránh float trên MCU không có FPU.

## 8. Design pattern áp dụng

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
