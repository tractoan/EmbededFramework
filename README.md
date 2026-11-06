# Embedded Platform – Kiến trúc phần mềm nhúng C++ đa nền tảng

Bộ khung firmware C++17 phân lớp rõ ràng: **Platform (Core / OSAL / HAL / Device) → Messaging → Services → Communication → Application (Runtime & Business)**.
Mục tiêu:

- **Chuyển nền tảng** (MCU, board, RTOS, Linux) mà **không sửa** code nghiệp vụ.
- **Tùy biến nghiệp vụ** ở nhiều mức: chọn module, cấu hình lúc biên dịch, tham số lúc chạy.
- **Đổi giao thức truyền thông linh hoạt** (CLI, Modbus RTU, MAVLink…) theo sản phẩm, lúc chạy, hoặc tự nhận dạng.
- Phù hợp embedded: không exception, không RTTI, **không cấp phát động sau khi khởi động** (được kiểm chứng lúc chạy
  bằng bộ chặn heap), tài nguyên tĩnh, **priority của từng task cấu hình được** (16 mức, giống nhau trên mọi RTOS).

---

## 1. Tổng quan các lớp

```
┌────────────────────────────────────────────────────────────────────────┐
│ PRODUCT (products/<name>)  chọn module + kênh + cấu hình -> 1 firmware │
├────────────────────────────────────────────────────────────────────────┤
│ APPLICATION (app/)                                                     │
│  business/  TemperatureMonitor · AlarmController · <module của bạn>    │
│  runtime/   IModule · ModuleManager(Active Object) · StateMachine      │
│             System (Composition Root) · SystemContext (DI)             │
├────────────────────────────────────────────────────────────────────────┤
│ COMMUNICATION (communication/)  ngăn xếp truyền thông                  │
│  link/   IByteStream · IProtocol · LinkManager (chọn/đổi giao thức)    │
│  cli/    CliProtocol     modbus/ ModbusRtuProtocol + Map   (mavlink/…) │
├────────────────────────────────────────────────────────────────────────┤
│ SERVICES (services/)  dịch vụ hệ thống: có trạng thái, có chính sách   │
│  ParamStore(NV+CRC) · CommandService(bảng lệnh) · Logger · LedService  │
├────────────────────────────────────────────────────────────────────────┤
│ MESSAGING (messaging/)  lõi giao tiếp, không phụ thuộc nền tảng        │
│  Event · EventBus · Buffer + pool tĩnh (BlockPool/Pool/ChainAllocator) │
╞═════════════════════════ PLATFORM (platform/) ═════════════════════════╡
│ DEVICE (device/)  thiết bị NGOÀI chip: Tmp102 : ITemperatureSensor     │
├──────────────────────────────────┬─────────────────────────────────────┤
│ OSAL (osal/)                     │ HAL (hal/)  ngoại vi TRÊN chip      │
│  Thread · Mutex · Semaphore      │  IGpio · IUart · II2c · INvStorage  │
│  Queue<T,N> · time · Critical    │  IBoard (ID logic -> chân vật lý)   │
│  ports: posix·freertos·ucos2/3   │  ports: linux_sim │ stm32f4 │ ...   │
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
| **Messaging** | Cơ chế giao tiếp của framework: `Event`, `EventBus` (pub/sub, quy ước chia dải ID), payload lớn `Buffer` + pool tĩnh | Định nghĩa event cụ thể của lớp khác; phụ thuộc services trở lên; có port theo nền tảng | Interface (`IEventListener`, `IBufferAllocator`) |
| **Services** | Dịch vụ hệ thống dùng chung, có trạng thái và chính sách: tham số, bảng lệnh, log, LED | Chứa quy tắc nghiệp vụ của sản phẩm; gọi API vendor trực tiếp | Interface (`ILogSink`, `ICommandHandler`) |
| **Communication** | Truyền thông: kênh, giao thức (CLI, Modbus…), chọn/đổi giao thức lúc chạy | Biết tới module nghiệp vụ cụ thể (chỉ dùng Event/Param qua bảng ánh xạ) | **Runtime** (`IByteStream`, `IProtocol`) |
| **Application – Runtime** | Vòng đời module, định tuyến event, lập lịch tick, state machine, boot | Chứa nghiệp vụ cụ thể | – |
| **Application – Business** | Quy tắc nghiệp vụ (đo, cảnh báo, điều khiển...) | Include `hal/*`, `osal_port_types.hpp`, header vendor; tự tạo thread | Plug-in qua `IModule` |
| **Product** | Lắp ráp module + kênh/giao thức + cấu hình mặc định cho từng sản phẩm | – | Chọn thư mục khi build |

### Quy tắc phụ thuộc (được CMake ép buộc)

```
                 ┌───────────── platform ─────────────┐
utils ◄── core ◄─┤ osal ◄──┬── device                 │
                 │ hal  ◄──┴── target                 │
                 └────────────────┬───────────────────┘
                                  ▼
            messaging ◄── services ◄── communication(link ◄ cli, modbus) ◄── app_runtime ◄── business ◄── product
                                                                                                             │
                                                      main.cpp (thư mục gốc) ◄── nối product + target ───────┘
```
(mũi tên `A ◄── B`: B phụ thuộc A)

`messaging` **không** nằm trong `platform/`: nó không có port theo nền tảng và driver/HAL không được phát event
(driver báo lỗi bằng `core::Status`; ISR đưa dữ liệu lên qua `utils::RingBuffer` / `Semaphore::give_from_isr`,
một thread ở tầng trên mới publish). CMake chặn `platform` include `messaging`.

`main.cpp` ở thư mục gốc là entry point **duy nhất**, giống nhau cho mọi target và sản phẩm:

```cpp
int main(int argc, char** argv) {
    target::init(argc, argv);          // khởi động riêng của nền tảng đích
    return target::run(&app_main);     // app_main: app::System + product::setup
}
```

Mỗi lớp chỉ `target_link_libraries` tới lớp ngay dưới → include sai lớp sẽ **lỗi biên dịch**.
Lớp dưới không bao giờ biết lớp trên; giao tiếp ngược lên chỉ qua **callback/interface/event**.

---

## 2. Cấu trúc thư mục

```
EmbededFramework/
├── CMakeLists.txt              # EP_TARGET, EP_PRODUCT
├── main.cpp                    # entry point duy nhất (mọi target, mọi sản phẩm)
├── cmake/targets/              # linux_sim, stm32f4_{freertos,ucos2,ucos3}.cmake (chọn port)
│   └── common/stm32f4_cube.cmake   # phần CubeMX dùng chung cho mọi target STM32F4
├── cmake/toolchains/           # arm-none-eabi.cmake
├── utils/include/utils/        # crc.hpp, fixed_point.hpp, ring_buffer.hpp  (độc lập)
├── platform/                   # TẦNG NỀN TẢNG
│   ├── core/include/core/      # status.hpp, config.hpp, noncopyable.hpp, compiler.hpp
│   ├── osal/
│   │   ├── include/osal/       # API trung lập: thread, mutex, semaphore, queue, time, critical
│   │   └── ports/{posix,freertos,ucos2,ucos3}/   # osal_port_types.hpp + osal_<os>.cpp
│   ├── hal/
│   │   ├── include/hal/        # gpio, uart, i2c, storage, board
│   │   └── ports/{linux_sim,stm32f4}/
│   ├── device/
│   │   └── include/device/     # temperature_sensor (interface), tmp102 (driver)
│   └── target/                 # khởi động theo nền tảng đích: target::init / run / stop_requested
│       ├── include/target/target.hpp
│       ├── linux_sim/          # tham số dòng lệnh, Ctrl+C, --run-for
│       ├── stm32f4_freertos/   # HAL_Init, clock, ngoại vi CubeMX, start scheduler, hook FreeRTOS
│       ├── stm32f4_ucos2/      # ... OSInit/OSStart, SysTick + PendSV cho uC/OS-II
│       ├── stm32f4_ucos3/      # ... CPU_Init/OSInit/OSStart, SysTick + PendSV cho uC/OS-III
│       └── common/stm32f4/     # clock_config.c dùng chung
├── messaging/
│   └── include/messaging/      # event, event_bus, buffer, buffer_allocators     (ep_messaging)
├── services/
│   └── include/services/       # param_store, param_cli, command_service, logger, led_service
├── communication/
│   ├── link/                   # byte_stream, protocol, link_manager      (ep_comm)
│   ├── cli/                    # cli_protocol                             (ep_proto_cli)
│   └── modbus/                 # modbus_map (bảng ánh xạ), modbus_rtu     (ep_proto_modbus)
├── app/
│   ├── runtime/include/app/    # module, module_manager, state_machine, system, product
│   └── business/               # module nghiệp vụ tái sử dụng + app_events.hpp, app_params.hpp
├── products/
│   ├── thermo_pro/             # 3 module, latch, StatsReporter, CLI + Modbus (modbus_map.cpp)
│   └── thermo_lite/            # 2 module, chỉ CLI, cấu hình nhỏ cho MCU ít RAM
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
- Event được copy theo giá trị qua bus/queue. Payload nhỏ nằm inline (`trivially_copyable`, ≤ 16 B, kiểm tra bằng
  `static_assert`); payload lớn đi kèm `msg::Buffer` (xem dưới).

### Payload lớn & kích thước thay đổi: `msg::Buffer`

```
Event { id, size, timestamp, payload[16] (header nhỏ inline), buffer ──► [refs|cap|len|owner][ dữ liệu ... ] }
                                                                      └── cấp từ IBufferAllocator
```

```cpp
msg::Buffer data = msg::Buffer::allocate(n * sizeof(int16_t));           // kích thước bất kỳ, lúc chạy
std::memcpy(data.data(), samples, data.size());                           // GHI trước khi publish
bus.publish(msg::Event::make(kTemperatureBatch, TemperatureBatch{n, ...}, std::move(data)));

void on_event(const msg::Event& e) {                                      // mọi listener đọc CÙNG vùng nhớ
    TemperatureBatch hdr{};
    if (e.id == kTemperatureBatch && e.get(hdr)) process(e.buffer.data(), e.buffer.size());
}
```

- **Đếm tham chiếu, không copy dữ liệu:** copy `Event` (vào queue, cho từng listener) chỉ tăng tham chiếu;
  tham chiếu cuối cùng bị hủy → block tự trả về đúng allocator đã cấp. `osal::Queue` move phần tử ra và xóa slot,
  nên queue không giữ bộ nhớ lại.
- **Quyền sở hữu:** chủ tạo ghi dữ liệu rồi `std::move` vào Event; sau khi publish, buffer là **chỉ đọc**.
  Listener muốn giữ lâu hơn `on_event()` thì copy `Buffer`/`Event`.
- **Allocator thay được theo sản phẩm** (`Buffer::set_default_allocator()` trong `product::setup()`):

| Allocator | Cơ chế | Khi nào dùng |
|---|---|---|
| `PoolAllocator` + `StaticBlockPool<Size, Count>` | Nhiều lớp kích thước, bộ nhớ tĩnh, O(1), không phân mảnh; lớp vừa nhất hết → lấy lớp lớn hơn | Mặc định cho MCU, hệ thời gian thực |
| `ChainAllocator(primary, fallback)` | Thử allocator chính, hết thì dùng dự phòng | Pool chung + pool dự trữ riêng cho dữ liệu quan trọng |

  Không có allocator dùng heap: mọi block đều nằm trong bộ nhớ tĩnh khai báo lúc build. Cấp/trả block từ pool
  lúc chạy là O(1), tất định và **không phải cấp phát động** theo nghĩa heap.

  Cấp phát thất bại trả về `Buffer` rỗng (không crash); số lần thất bại, mức dùng và đỉnh xem bằng `sys buffers`.
  Ví dụ: `thermo_pro` = pool 64 B×16 / 512 B×8 / 2 KB×4 (lô tối đa 1000 mẫu); `thermo_lite` = một pool 128 B×4 (52 mẫu).
  Allocator thread-safe (vùng găng ngắn); **chưa** hỗ trợ cấp phát trong ISR.

### Trình tự khởi động (`app::System::init`)

1. `board.init()` → gắn console vào Logger
2. Khởi tạo service: ParamStore, CommandService, LedService
3. `LinkManager::init()` (đăng ký lệnh `link`)
4. Bring-up thiết bị: driver gắn lên bus HAL (`Tmp102` ← `II2c`) → `Devices`
5. `product::setup()` → thêm module + khai báo kênh/giao thức; rồi `on_init()` từng module (đăng ký tham số, lệnh)
6. `params.load()` từ NV, gồm cả lựa chọn giao thức `comm.<kênh>` (CRC sai/không có → giữ mặc định)
7. Start service → `on_start()` từng module → `links.start()` (mỗi kênh bật giao thức đã chọn) → publish `kSystemStarted`
8. `osal::end_startup()`: **niêm phong bộ nhớ** – từ đây cấm heap và cấm lấy thêm stack (xem dưới)

Nếu bước 7 thất bại (vd. vùng stack không đủ), các thread đã khởi động được dừng sạch và lỗi chỉ rõ task nào,
mức dùng vùng stack, cần sửa `TaskPlan` / `EP_CFG_STACK_POOL_BYTES`.

### Bộ nhớ: không cấp phát động sau khi khởi động

| Loại bộ nhớ | Cơ chế | Khi nào được cấp |
|---|---|---|
| Object của framework, sản phẩm | `static` / thành viên của `System` | Lúc khởi động |
| Stack của thread | Do sản phẩm cấp (`stack_mem`) hoặc lấy từ **vùng stack tĩnh** `EP_CFG_STACK_POOL_BYTES` (`osal::allocate_stack`) | Chỉ lúc khởi động |
| Kernel object (TCB, semaphore, mutex) | Nằm ngay trong wrapper OSAL (`xTaskCreateStatic`, `OS_TCB`, …) | Cùng lúc với object chứa nó |
| Payload lớn | `msg::Buffer` + pool tĩnh | Lúc chạy, O(1), không heap |
| Heap (`new`, `malloc`) | **Không dùng.** FreeRTOS: `configSUPPORT_DYNAMIC_ALLOCATION = 0`, không build `heap_x.c` | – |

**Kiểm chứng lúc chạy:** firmware link `ep_heap_guard` (thay `operator new/delete`; trên STM32 bọc thêm
`malloc/calloc/realloc` bằng `-Wl,--wrap`). Sau `osal::end_startup()`, mọi cấp phát heap là **vi phạm** và
gọi handler của target: Linux in `FATAL` rồi dừng (để lộ lỗi khi phát triển/CI), STM32 reset board.
Thread tạo sau khởi động phải tự mang stack tĩnh, nếu không `Thread::start` trả `NoMemory` trên mọi port.
Port POSIX vẫn trừ stack vào vùng stack chung (dù `std::thread` tự quản stack) để bản mô phỏng phát hiện đúng
lỗi thiếu stack như trên MCU. Lệnh `sys tasks` hiện mức dùng vùng stack và số vi phạm heap.

### Priority của task

`osal::Priority` có **16 mức: 0 (thấp nhất) … 15 (cao nhất)**, giống nhau trên mọi RTOS; tên sẵn có:
`Idle`=0, `Low`=4, `Normal`=8, `High`=12, `Realtime`=15; hoặc `osal::Priority{11}`, `Priority::High.lower(1)`.

Sản phẩm cấu hình priority + stack của **mọi task do framework tạo** trong `product::setup()`:

```cpp
s.tasks.app  = osal::ThreadConfig{"app", osal::Priority::High, 4096, nullptr};   // nghiệp vụ (ModuleManager)
s.tasks.leds = osal::ThreadConfig{"led", osal::Priority::Low, 1024, nullptr};    // LedService
s.links.add_channel("console", &s.console, {&cli}, comm::proto::kCli, osal::Priority{6}, 4096);   // mỗi kênh
```

`stack_mem = nullptr` → lấy từ vùng stack tĩnh; hoặc trỏ tới buffer `static` của sản phẩm (vd. để đặt stack ở
vùng RAM riêng). Mỗi port ánh xạ tuyến tính 16 mức sang priority native:

| Port | Mức 15 → … → mức 0 | Tham số |
|---|---|---|
| FreeRTOS | `configMAX_PRIORITIES-1` … `tskIDLE_PRIORITY` (số lớn = cao) | – |
| uC/OS-II | `EP_UCOS2_PRIO_TOP` (4) … tăng `EP_UCOS2_PRIO_STRIDE` (3) mỗi mức (số nhỏ = cao); tối đa STRIDE task cùng mức (priority native duy nhất) | kiểm tra lúc build so với `OS_LOWEST_PRIO` |
| uC/OS-III | `EP_UCOS3_PRIO_TOP` (4) … +1 mỗi mức; nhiều task cùng mức chạy round-robin | kiểm tra lúc build so với `OS_CFG_PRIO_MAX` |
| POSIX (host) | Bỏ qua (cần quyền realtime); chỉ ghi nhận để hiển thị | – |

---

## 4. Các mức tùy biến nghiệp vụ

| Mức | Cơ chế | Ví dụ |
|---|---|---|
| **Chọn module** | `products/<p>/product.cpp` | thermo_pro có `StatsReporter`, thermo_lite không có |
| **Cấu hình module (compile-time)** | `Module::Config` truyền vào constructor | ngưỡng, chu kỳ mẫu, latch theo từng sản phẩm |
| **Cấu hình hệ thống (compile-time)** | `product_config.hpp` override `EP_CFG_*` | độ sâu queue, số module, mức log tối thiểu |
| **Tham số runtime** | `ParamStore` + CLI `param set/save`, sự kiện `kParamChanged` | đổi `alarm.high` khi đang chạy, lưu Flash |
| **Bộ nhớ payload lớn** | Pool tĩnh của `msg::Buffer` chọn trong `product::setup()` | pool nhỏ cho MCU nhỏ, nhiều lớp pool cho bản cao cấp |
| **Priority & stack của task** | `TaskPlan` (`s.tasks`) + tham số của `add_channel()`; `EP_CFG_STACK_POOL_BYTES` | nghiệp vụ `High`, console `6`, LED `Low` |
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
    void on_event(const msg::Event& e) override {
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
3. Tạo `platform/target/<board>_<os>/` hiện thực `target::init()` (HAL vendor, clock, ngoại vi),
   `target::run()` (gọi `app_main` trong ngữ cảnh đúng, vd. task + start scheduler) và `target::stop_requested()`.
4. Tạo `cmake/targets/<board>_<os>.cmake` đặt `EP_HAL_PORT=<board>`, `EP_OSAL_PORT`, toolchain/cờ CPU.
   Tên file phải trùng tên thư mục ở bước 3. Build bằng `-DEP_TARGET=<board>_<os>`.
   ⇒ `main.cpp`, Services, App, Product **không đổi một dòng**.

Mọi thứ phụ thuộc nền tảng nằm trong `platform/` (Core/OSAL/HAL/Device và khởi động `target/`);
`cmake/targets/` chọn port, toolchain cho mỗi target. Không thư viện nào trong `platform/` include `app/` –
việc nối nền tảng với ứng dụng chỉ diễn ra ở `main.cpp` thư mục gốc.

### 6.4 Port sang hệ điều hành mới (Zephyr, ThreadX, CMSIS-RTOS2, ESP-IDF…)

1. Tạo `platform/osal/ports/<os>/include/osal_port_types.hpp` – khai báo `MutexHandle`, `SemaphoreHandle`,
   `ThreadHandle`, `CriticalState` (header C của RTOS bọc trong `extern "C"`).
2. Tạo `platform/osal/ports/<os>/osal_<os>.cpp` – hiện thực các hàm trong `platform/osal/include/osal/*.hpp`
   (tham khảo `osal_freertos.cpp`, `osal_ucos2.cpp`, `osal_ucos3.cpp`).
3. Đặt `EP_OSAL_PORT=<os>` trong file target (`cmake/targets/*.cmake`). `Queue<T,N>` tự động hoạt động vì được viết trên Mutex/Semaphore.
4. Chạy `tests/test_osal.cpp` trên port mới: các test `osal_*` kiểm tra đúng những ngữ nghĩa mà mọi port phải giữ
   (`kNoWait` không chặn, timeout có hạn, semaphore không vượt `max`, tạo/join thread lặp lại, vùng găng lồng nhau).

Bảng ánh xạ của các port hiện có:

| Ngữ nghĩa OSAL | FreeRTOS | uC/OS-II | uC/OS-III |
|---|---|---|---|
| Chờ mãi / không chờ | `portMAX_DELAY` / 0 | timeout `0` / `OSSemAccept`, `OSMutexAccept` | timeout `0` + `BLOCKING` / `NON_BLOCKING` |
| Priority (16 mức logic) | ánh xạ tuyến tính, chia sẻ | **duy nhất mỗi task** → mỗi mức một dải `EP_UCOS2_PRIO_STRIDE` priority, port lấy chỗ trống | 1 số / mức, round-robin (`EP_UCOS3_PRIO_TOP`) |
| Mutex | mutex có kế thừa priority | `OSMutexCreate(OS_PRIO_MUTEX_CEIL_DIS)`, hoặc semaphore nhị phân nếu bản cũ | `OS_MUTEX` |
| Stack khi `stack_mem == nullptr` | vùng stack tĩnh chung + `xTaskCreateStatic` | vùng stack tĩnh chung | vùng stack tĩnh chung |
| Kết thúc thread / `join` | semaphore `done` + `vTaskDelete(NULL)` | `done` + `OSTaskDel(OS_PRIO_SELF)` | `done`, task tự treo, `join` gọi `OSTaskDel` (TCB nằm trong `Thread`) |
| Vùng găng | `taskENTER_CRITICAL` | `OS_CPU_SR_Save/Restore` (`OS_CRITICAL_METHOD == 3`) | `CPU_SR_Save/Restore` (uC/CPU) |
| `yield()` | `taskYIELD` | `OSTimeDly(1)` (không có task cùng priority) | `OSSchedRoundRobinYield` hoặc `OSTimeDly(1)` |

Trên uC/OS, **mọi object OSAL phải được tạo sau `OSInit()`**: `target::init()` gọi `OSInit` trước tiên, và mọi object
của framework đều được tạo bên trong `app_main`, nên quy tắc này tự thỏa nếu không khai báo `osal::Mutex`/`Semaphore`/
`Queue` ở phạm vi toàn cục (namespace scope).

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
   Cần đẩy dữ liệu chủ động thì kế thừa thêm `msg::IEventListener` và ghi qua `io.write()` (đã thread-safe).
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
sys info | modules | tasks | buffers | log <0-5> | reboot | quit
param list | get <n> | set <n> <v> | save | reset
temp                         nhiệt độ hiện tại, thống kê
alarm status | ack           trạng thái cảnh báo / xác nhận
link list | set <kênh> <off|auto|cli|modbus>   kênh truyền thông & đổi giao thức
```

### STM32F407 + FreeRTOS / uC/OS-II / uC/OS-III

Cùng HAL port `stm32f4`, cùng ứng dụng – chỉ khác RTOS:

```bash
cmake -B build-stm32 -DEP_TARGET=stm32f4_freertos \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
      -DEP_CUBE_DIR=/path/to/cubemx_project -DEP_FREERTOS_DIR=/path/to/FreeRTOS-Kernel
```
```bash
cmake -B build-ucos2 -DEP_TARGET=stm32f4_ucos2 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake -DEP_CUBE_DIR=/path/to/cubemx_project \
      -DEP_UCOS2_DIR=/path/to/uC-OS2 -DEP_UCOS2_PORT_DIR=/path/to/uC-OS2/Ports/ARM-Cortex-M/ARMv7-M/GNU \
      -DEP_UCOS_CFG_DIR=/path/to/ucos_config
```
```bash
cmake -B build-ucos3 -DEP_TARGET=stm32f4_ucos3 \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake -DEP_CUBE_DIR=/path/to/cubemx_project \
      -DEP_UCOS3_DIR=/path/to/uC-OS3 -DEP_UCOS3_PORT_DIR=/path/to/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/GNU \
      -DEP_UCCPU_DIR=/path/to/uC-CPU -DEP_UCCPU_PORT_DIR=/path/to/uC-CPU/ARM-Cortex-M/ARMv7-M/GNU \
      -DEP_UCLIB_DIR=/path/to/uC-LIB -DEP_UCOS_CFG_DIR=/path/to/ucos_config
```

Yêu cầu CubeMX chung: `cmake/targets/common/stm32f4_cube.cmake`. Riêng uC/OS: tắt sinh handler PendSV và SysTick
trong CubeMX (target tự định nghĩa và chuyển cho `OS_CPU_PendSVHandler`/`OS_CPU_SysTickHandler`); cấu hình
`os_cfg.h` tối thiểu ghi ở đầu `osal_ucos2.cpp` / `osal_ucos3.cpp`. Đường dẫn port CPU ở trên theo bố cục
repo mã nguồn mở của Micrium (weston-embedded); bản Micrium cũ có thể đặt khác – chỉ cần trỏ đúng thư mục.

**Mức độ kiểm chứng:** các target STM32 (cả 3 RTOS) là **mẫu tham khảo – chưa được biên dịch với toolchain ARM hay
chạy trên phần cứng thật** trong repo này. Riêng hai port OSAL uC/OS-II/III đã được biên dịch (`-Wall -Wextra`, không
warning) và chạy toàn bộ unit test cùng firmware mô phỏng trên một bản **giả lập API uC/OS bằng pthread** (không
nằm trong repo), nên logic port đã được kiểm tra; các khác biệt của kernel thật (lập lịch theo priority, ngắt,
port CPU) vẫn cần kiểm thử trên board.

---

## 8. Quy ước code cho môi trường nhúng

- C++17, `-fno-exceptions -fno-rtti`; lỗi trả về `core::Status`.
- Không cấp phát động sau khi khởi động (bộ chặn heap kiểm chứng lúc chạy): dùng `std::array`, `static` object,
  stack tĩnh cho thread, pool tĩnh cho payload. Không tạo thread mới lúc chạy trừ khi tự mang stack tĩnh.
  Bộ nhớ động cho payload chỉ đi qua `msg::Buffer` + allocator mà sản phẩm chọn (mặc định nên là pool tĩnh).
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
| Publish–Subscribe | `msg::EventBus` |
| Active Object | `app::ModuleManager` |
| State Machine (state = method) | `app::StateMachine`, `AlarmController` |
| Plug-in / Strategy | `IModule`, `ITemperatureSensor`, `ILogSink`, `ICommandHandler` |
| Command | `CommandService` |
| Adapter / Anti-corruption layer | `IProtocol` + bảng ánh xạ (`modbus::Map`) |
| Strategy chọn lúc chạy | `LinkManager` (tham số `comm.<kênh>`, auto-detect qua `probe()`) |
