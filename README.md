# Embedded Platform – Kiến trúc phần mềm nhúng C++ đa nền tảng

Bộ khung firmware C++17 phân lớp rõ ràng: **Platform (Core / OSAL / HAL / Device) → Messaging → Services → Communication → Application (Runtime & Business)**.
Mục tiêu:

- **Chuyển nền tảng** (MCU, board, RTOS, Linux) mà **không sửa** code nghiệp vụ.
- **Tùy biến nghiệp vụ** ở nhiều mức: chọn module, cấu hình lúc biên dịch, tham số lúc chạy.
- **Đổi giao thức truyền thông linh hoạt** (CLI, Modbus RTU, MAVLink v1…) qua UART, **TCP hoặc UDP**, theo sản phẩm,
  lúc chạy, hoặc tự nhận dạng.
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
│             Serial/Gnss/Io/ImuBringup (bring-up phần cứng)             │
│  runtime/   IModule · ModuleManager(Active Object) · StateMachine      │
│             System (Composition Root) · SystemContext (DI)             │
├────────────────────────────────────────────────────────────────────────┤
│ COMMUNICATION (communication/)  ngăn xếp truyền thông                  │
│  link/   IByteStream · IProtocol · LinkManager (chọn/đổi giao thức)    │
│  net/    TcpServerStream · TcpClientStream · UdpStream                 │
│  cli/ CliProtocol · modbus/ ModbusRtu+Map · mavlink/ MavlinkV1+Map     │
│  mavlink2/ Mavlink2Protocol (v2, msgid 24 bit) + IDialect của sản phẩm │
├────────────────────────────────────────────────────────────────────────┤
│ SERVICES (services/)  dịch vụ hệ thống: có trạng thái, có chính sách   │
│  ParamStore(NV+CRC) · CommandService(bảng lệnh) · Logger · LedService  │
│  SerialPortBank · GnssReceiver (NMEA) · HdlcImu (khung SDLC/HDLC)      │
├────────────────────────────────────────────────────────────────────────┤
│ MESSAGING (messaging/)  lõi giao tiếp, không phụ thuộc nền tảng        │
│  Event · EventBus · Buffer + pool tĩnh (BlockPool/Pool/ChainAllocator) │
╞═════════════════════════ PLATFORM (platform/) ═════════════════════════╡
│ DEVICE (device/)  thiết bị NGOÀI chip: Tmp102 : ITemperatureSensor     │
│  GpioInput/OutputBank : IDigitalInputs/Outputs · ImuFormat (giải mã)   │
│ NET (net/)  TCP/IP IPv4: TcpListener · TcpConnection · UdpSocket       │
│             ports: posix (BSD socket) │ lwip (MCU)   – EP_NET_PORT     │
├──────────────────────────────────┬─────────────────────────────────────┤
│ OSAL (osal/)                     │ HAL (hal/)  ngoại vi TRÊN chip      │
│  Thread · Mutex · Semaphore      │  IGpio · IUart · II2c · INvStorage  │
│                                  │  IHdlcPort (SDLC/HDLC)              │
│  Queue<T,N> · time · Critical    │  IBoard (ID logic -> chân vật lý)   │
│  ports: posix·freertos·ucos2/3   │  ports: sim·stm32f4·zynqmp·qnx      │
│                                  │         linux_cpu (bring-up)        │
├──────────────────────────────────┴─────────────────────────────────────┤
│ CORE (core/)   Status · NonCopyable · config.hpp · compiler macros     │
└────────────────────────────────────────────────────────────────────────┘
 UTILS (utils/) – thư viện độc lập, mọi lớp đều dùng được:
   crc32/crc16 · fixed-point format · RingBuffer (SPSC)
   Vendor SDK / Kernel: STM32Cube HAL, FreeRTOS, Zephyr, Linux, QNX, ESP-IDF...
```

| Lớp | Trách nhiệm | KHÔNG được làm | Cơ chế đa hình |
|---|---|---|---|
| **Utils** | Thuật toán & cấu trúc dữ liệu thuần: CRC, định dạng fixed-point, ring buffer | Phụ thuộc bất kỳ thư mục nào khác của dự án | – (hàm/template thuần) |
| **Platform › Core** | Kiểu nền tảng: mã lỗi, cấu hình tĩnh, NonCopyable, macro compiler | Phụ thuộc bất kỳ thứ gì (trừ utils) | – |
| **Platform › OSAL** | Thread, mutex, semaphore, queue, thời gian, vùng găng | Truy cập phần cứng | **Link-time** (chọn port khi build) → zero-overhead |
| **Platform › HAL** | Ngoại vi **trên chip**: GPIO, UART, I2C, NV storage; `IBoard` ánh xạ ID logic → phần cứng. Cần chờ thì gọi `hal::wait_ms()` (target cài `osal::sleep_ms` vào sau khi kernel chạy – đảo phụ thuộc) | Gọi OSAL trực tiếp, chứa logic nghiệp vụ, biết tới sản phẩm | **Runtime** (interface ảo) → mock được, nhiều instance |
| **Platform › Net** | TCP/IP (IPv4): `TcpListener`, `TcpConnection`, `UdpSocket`, timeout trên mọi thao tác, không heap | Biết giao thức ứng dụng; target không có mạng thì không có lớp này (`EP_NET_PORT` trống) | **Link-time** (port `posix`, `lwip`) |
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
            messaging ◄── services ◄── communication(link ◄ net, cli, modbus, mavlink, mavlink2) ◄── app_runtime ◄── business ◄── product
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
├── cmake/targets/              # linux_sim, linux_cpu, stm32f4_{freertos,ucos2,ucos3}, zynqmp_{a53_linux,r5_freertos}, qnx
│   └── common/stm32f4_cube.cmake   # phần CubeMX dùng chung cho mọi target STM32F4
├── cmake/toolchains/           # arm-none-eabi, armr5-none-eabi, aarch64-linux-gnu, qnx (qcc)
├── utils/include/utils/        # crc.hpp, fixed_point.hpp, ring_buffer.hpp  (độc lập)
├── platform/                   # TẦNG NỀN TẢNG
│   ├── core/include/core/      # status.hpp, config.hpp, noncopyable.hpp, compiler.hpp
│   ├── osal/
│   │   ├── include/osal/       # API trung lập: thread, mutex, semaphore, queue, time, critical
│   │   └── ports/{posix,freertos,ucos2,ucos3}/   # osal_port_types.hpp + osal_<os>.cpp
│   ├── hal/
│   │   ├── include/hal/        # gpio, uart, i2c, storage, hdlc, board, wait
│   │   └── ports/
│   │       ├── common/         # mã dùng chung, chọn bằng danh sách EP_HAL_COMMON:
│   │       │   ├── posix/      #   mọi OS POSIX: console stdio, UART termios, FileStorage
│   │       │   ├── linux/      #   Linux: /dev/i2c-N, GPIO chardev v2
│   │       │   ├── qnx/        #   QNX: I2C resource manager (devctl), GPIO thanh ghi (mmap_device_memory)
│   │       │   └── mmio/       #   GPIO thanh ghi memory-mapped, header-only (test được trên host)
│   │       ├── linux_sim/      # PC: cảm biến/GPIO giả lập (+ 12 serial, 20 DI/DO, 2 IMU cho hw_bringup)
│   │       ├── linux_cpu/      # CPU Linux của bo bring-up: tty RS485, GPIO bias/active-low, SDLC/HDLC
│   │       ├── stm32f4/        # STM32Cube HAL
│   │       ├── zynqmp_linux/   # Zynq UltraScale+ APU (Cortex-A53) chạy Linux
│   │       ├── zynqmp_r5/      # Zynq UltraScale+ RPU (Cortex-R5F): XGpioPs, XUartPs, XIicPs, EEPROM 24xx
│   │       └── qnx/            # mọi board có BSP QNX: cấu hình thiết bị bằng biến môi trường
│   ├── net/                    # TCP/IP: include/net/net.hpp, ports/{posix,lwip}
│   ├── device/
│   │   └── include/device/     # temperature_sensor, tmp102, digital_io, imu (+ImuFormat), nmea
│   └── target/                 # khởi động theo nền tảng đích: target::init / run / stop_requested
│       ├── include/target/target.hpp
│       ├── linux_sim/          # tham số dòng lệnh, Ctrl+C, --run-for
│       ├── linux_cpu/          # như linux_sim, chạy như dịch vụ (systemd)
│       ├── stm32f4_freertos/   # HAL_Init, clock, ngoại vi CubeMX, start scheduler, hook FreeRTOS
│       ├── stm32f4_ucos2/      # ... OSInit/OSStart, SysTick + PendSV cho uC/OS-II
│       ├── stm32f4_ucos3/      # ... CPU_Init/OSInit/OSStart, SysTick + PendSV cho uC/OS-III
│       ├── zynqmp_a53_linux/   # như linux_sim + mlockall, kiểm tra quyền SCHED_FIFO
│       ├── zynqmp_r5_freertos/ # task init + scheduler (BSP Xilinx khởi tạo clock/GIC/tick)
│       ├── qnx/                # như linux_sim + mlockall, kiểm tra quyền priority, bỏ qua SIGPIPE
│       └── common/{stm32f4,freertos}/   # clock_config.c; hook cấp phát tĩnh FreeRTOS dùng chung
├── messaging/
│   └── include/messaging/      # event, event_bus, buffer, buffer_allocators     (ep_messaging)
├── services/
│   └── include/services/       # param_store, param_cli, command_service, logger, led_service,
│                               # serial_port_bank, gnss_receiver, hdlc_imu
├── communication/
│   ├── link/                   # byte_stream, protocol, link_manager      (ep_comm)
│   ├── net/                    # tcp_server/tcp_client/udp stream         (ep_comm_net, khi có mạng)
│   ├── cli/                    # cli_protocol                             (ep_proto_cli)
│   ├── modbus/                 # modbus_map (bảng ánh xạ), modbus_rtu     (ep_proto_modbus)
│   ├── mavlink/                # mavlink_v1 (codec), mavlink_map, mavlink_protocol (ep_proto_mavlink)
│   └── mavlink2/               # codec v2, common, mavlink_protocol + IDialect     (ep_proto_mavlink2)
│       └── dialects/bringup/   # bản tin dialect bringup.xml 62000..62005 (header-only, ep_mavlink2_bringup)
├── app/
│   ├── runtime/include/app/    # module, module_manager, module_worker, state_machine, system, product
│   └── business/               # module nghiệp vụ tái sử dụng + app_events.hpp, app_params.hpp
├── products/
│   ├── thermo_pro/             # 3 module, latch, StatsReporter; CLI/Modbus/MAVLink qua UART, TCP, UDP
│   ├── thermo_lite/            # 2 module, chỉ CLI, cấu hình nhỏ cho MCU ít RAM
│   └── hw_bringup/             # bring-up phần cứng trên CPU Linux: 12 serial, GNSS, 20 DI/DO, 2 IMU, MAVLink v2
├── tools/                      # modbus_udp_client.py, mavlink_client.py (trạm mặt đất tối giản),
│                               # check_mavlink_pymavlink.py (đối chiếu bằng pymavlink)
│   └── mavlink/                # bringup.xml (dialect), bringup_gcs.py, mavlink_dialect.py (hw_bringup)
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
                    └──────────────────────┬────────────────────────┘
                                           │ worker_.post(Event)   (tùy chọn, từng module)
                                           ▼
                    ┌──── ModuleWorker: thread riêng của module ────┐
                    │ Queue<Event,Depth> ─► on_job()                │
                    │ lịch không trôi    ─► on_worker_tick()        │──► publish(Event) kết quả
                    └───────────────────────────────────────────────┘
```

- **Toàn bộ nghiệp vụ chạy trên 1 thread** (Active Object) → module **không cần mutex**, hành vi xác định, dễ test.
- Module có việc chậm/chặn (ghi Flash, tính toán nặng, I/O đồng bộ) hoặc cần priority/chu kỳ riêng có thể
  **thêm một thread riêng** (`app::ModuleWorker`, xem 6.1) – không làm đứng các module khác.
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
| POSIX (Linux, QNX) | Mặc định bỏ qua (chỉ ghi nhận để hiển thị). Với `EP_POSIX_REALTIME=1` (target `zynqmp_a53_linux`, `qnx`): `SCHED_FIFO` 85 … 10 (`EP_POSIX_RT_PRIO_BASE + EP_POSIX_RT_PRIO_STEP·mức`, mặc định 10 + 5·mức); thiếu quyền thì giữ chính sách mặc định và target in cảnh báo | Linux: `CAP_SYS_NICE` hoặc root. QNX: priority > 63 cần `PROCMGR_AID_PRIORITY` (root). Target gọi thêm `mlockall` |

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
 ① IProtocol        CliProtocol | ModbusRtuProtocol(+Map) | MavlinkV1Protocol(+Map)
                           │  codec + phiên + ánh xạ Event/Param ↔ message
 ② LinkManager      kênh "console", "aux"...: 1 stream + giao thức cho phép + tham số comm.<kênh>
                           │
 ③ IByteStream      UartStream | TcpServerStream | TcpClientStream | UdpStream | (UsbCdc...)
                           │
 HAL / Net          IUart (Console, Aux) · TcpListener/TcpConnection/UdpSocket
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

### 5.4 TCP/IP

`platform/net/` trừu tượng hóa socket theo cùng mô hình OSAL (port chọn lúc build bằng `EP_NET_PORT`):
`posix` (Linux/macOS) và `lwip` (MCU, socket API của lwIP 2.x; netif/`tcpip_init()` do target khởi tạo).
Trên đó, `communication/net/` cung cấp ba `IByteStream`, nên **mọi giao thức** chạy được qua mạng:

| Stream | Hành vi |
|---|---|
| `TcpServerStream` | Lắng nghe một cổng, một client tại một thời điểm; client ngắt thì chờ client kế tiếp |
| `TcpClientStream` | Kết nối tới một địa chỉ, tự kết nối lại sau `retry_ms` khi mất kết nối |
| `UdpStream` | Mỗi datagram là một khối byte; gửi tới đích cố định (`set_remote`) hoặc trả lời người gửi gần nhất |

Thread RX của kênh là nơi duy nhất mở/đóng kết nối; ghi từ thread khác được khóa chung với việc đóng/mở,
nên không bao giờ ghi vào socket đã đóng. Không cấp phát heap (không dùng `getaddrinfo`).
Target không có mạng để trống `EP_NET_PORT`: `ep_net`/`ep_comm_net` không được build và sản phẩm tự bỏ
kênh mạng (`EP_HAS_NET`). Kênh của `thermo_pro` trên mô phỏng:

| Kênh | Stream | Giao thức cho phép (mặc định in đậm) |
|---|---|---|
| `console` | UART (terminal) | **cli**, modbus |
| `aux` | UART (UDP 15020 trên mô phỏng) | **modbus**, mavlink, cli – `auto` phân biệt Modbus/MAVLink |
| `tcp` | `TcpServerStream` cổng 5760 | **mavlink**, modbus, cli |
| `udp` | `UdpStream` gửi tới 127.0.0.1:14550 (cổng của QGroundControl) | **mavlink** |

### 5.5 MAVLink v1

`communication/mavlink/` là codec MAVLink v1 tối giản (không cấp phát, không cần thư viện sinh mã):
khung `0xFE LEN SEQ SYSID COMPID MSGID PAYLOAD CRC`, CRC-16/MCRF4XX + `CRC_EXTRA` của từng message;
parser chịu được rác, khung bị chia nhỏ, CRC sai (tự tái đồng bộ). Message hỗ trợ: HEARTBEAT,
PARAM_REQUEST_LIST/READ, PARAM_SET, PARAM_VALUE, COMMAND_LONG, COMMAND_ACK, NAMED_VALUE_FLOAT, STATUSTEXT
(độ dài và `CRC_EXTRA` đối chiếu với `pymavlink` dialects/v10/common.py).

`MavlinkV1Protocol` làm gì:

| Chức năng | Cơ chế |
|---|---|
| HEARTBEAT 1 Hz | `system_status` lấy từ Event qua `Map::states` (thermo_pro: ALARM/FAULT → `MAV_STATE_CRITICAL`) |
| Param protocol | Tự động cho **mọi** tham số `ParamStore`; giá trị gửi dạng `REAL32`, `PARAM_SET` làm tròn về số nguyên, kiểm tra miền; luôn trả lại giá trị hiện tại |
| COMMAND_LONG → COMMAND_ACK | Bảng `Map::commands` (thermo_pro: `MAV_CMD_USER_1` = xác nhận cảnh báo, `MAV_CMD_PREFLIGHT_STORAGE` = lưu/nạp tham số) |
| Telemetry | Event → `NAMED_VALUE_FLOAT` (`Map::telemetry`), Event → `STATUSTEXT` (`Map::status_texts`) |
| Giám sát liên kết | Nhận HEARTBEAT của trạm mặt đất → log kết nối / mất kết nối (3 s) |

Bảng ánh xạ của sản phẩm: `products/thermo_pro/mavlink_map.cpp` (sysid 1, compid 191). Thử trên mô phỏng:

```bash
tools/mavlink_client.py --tcp 127.0.0.1:5760 watch 3          # HEARTBEAT, temp, STATUSTEXT
```
```bash
tools/mavlink_client.py --tcp 127.0.0.1:5760 params           # 13/13 tham số
```
```bash
tools/mavlink_client.py --tcp 127.0.0.1:5760 set alarm.high 700
```
```bash
tools/mavlink_client.py --tcp 127.0.0.1:5760 cmd 31010 1      # xác nhận cảnh báo
```
```bash
tools/mavlink_client.py --udp 14550 watch 3                   # như QGroundControl
```

Đối chiếu bằng thư viện chính thức (cần `pip install pymavlink`):

```bash
tools/check_mavlink_pymavlink.py tcp:127.0.0.1:5760
```

QGroundControl / Mission Planner kết nối được qua UDP 14550 hoặc TCP 5760 (thiết bị hiện là component
không phải bộ lái – `MAV_AUTOPILOT_INVALID` – nên GCS chủ yếu hiển thị tham số, STATUSTEXT và giá trị đặt tên).

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

#### Module có thread riêng (`app::ModuleWorker`)

Khi một phần nghiệp vụ chậm hoặc cần priority khác, module **sở hữu** một worker: hàng đợi tĩnh + thread
(stack lấy từ pool lúc khởi động). `ModuleManager` khởi động worker sau `on_start()` của mọi module và dừng
nó **trước** `on_stop()`.

```cpp
class DataLogger final : public app::IModule, private app::IWorkerHandler {
public:
    app::ModuleWorkerBase* worker() override { return &worker_; }
    void on_event(const msg::Event& e) override {               // thread "app": chỉ chuyển việc
        if (e.id == app::events::kTemperatureBatch) worker_.post(e);   // Buffer: không copy dữ liệu
    }
private:
    void on_job(const msg::Event& job) override { /* ghi Flash – được phép chậm */ }
    std::uint32_t worker_period_ms() const override { return 60000; }   // 0 = không tick
    void on_worker_tick(std::uint64_t now) override { /* flush định kỳ */ }
    /* ...dữ liệu do worker sở hữu... */
    app::ModuleWorker<8> worker_{*this, {"logger", osal::Priority::Low, 2048, nullptr}};   // khai báo CUỐI
};
```

| Callback | Thread |
|---|---|
| `on_init`, `on_start`, `on_stop` | "app" – worker chưa / không còn chạy |
| `on_event`, `on_tick` | "app" (chung, tuần tự) |
| `on_job`, `on_worker_tick` | worker riêng của module |
| `on_command` | thread của kênh nhận lệnh |

Quy tắc: dữ liệu do worker sở hữu chỉ chạm trên worker; hai phía trao đổi bằng `worker_.post()` (app → worker),
`bus.publish()` (worker → app/module khác) hoặc `std::atomic` cho cấu hình đơn giản (vd. chu kỳ).
`on_worker_tick` có lịch **không trôi** (mốc kế tiếp = mốc trước + chu kỳ; trễ quá một chu kỳ thì bỏ lần lỡ).
`post()` trả `false` khi hàng đợi đầy (đếm trong `sys modules`). Sản phẩm ghi đè priority/stack của worker:
`s.modules.add(&logger, &cfg)`; nhớ cộng stack worker vào `EP_CFG_STACK_POOL_BYTES` và số thread vào
`EP_CFG_MAX_THREADS`. Ví dụ thật: `products/thermo_pro/stats_reporter.hpp`.

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
   Board chạy **Linux** (Raspberry Pi, i.MX, Zynq APU…): đặt thêm `EP_HAL_COMMON "posix;linux"` và chỉ viết file
   `IBoard` ánh xạ ID → `/dev/ttyX`, `/dev/i2c-N`, GPIO line (xem `ports/zynqmp_linux/`) – driver đã có sẵn.
   Board chạy **QNX**: thường không cần port mới – target `qnx` cấu hình mọi thiết bị bằng biến môi trường.
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

### 6.7 Thêm giao thức mới (vd. CANopen, giao thức riêng)

1. Tạo `communication/<proto>/` với target `ep_proto_<proto>` link `ep_comm`; thư viện bên thứ ba (vd. header
   MAVLink sinh từ XML) đặt ở `third_party/` và **chỉ** target này được include.
2. Hiện thực `comm::IProtocol`: `id()` (giá trị ổn định, vd. `proto::kMavlink` hoặc `proto::kUserBase + n`),
   `attach/detach`, `on_rx` (parse), `poll` (heartbeat/timeout), `probe` (nhận dạng chặt chẽ, có CRC).
   Cần đẩy dữ liệu chủ động thì kế thừa thêm `msg::IEventListener` và ghi qua `io.write()` (đã thread-safe).
3. Đặt bảng ánh xạ Event/Param ↔ message ở sản phẩm (như `modbus_map.cpp`), không viết vào nghiệp vụ.
4. Trong `product::setup()`: tạo instance (mỗi kênh một instance) và thêm vào danh sách của kênh.
5. Test bằng `mock::ScriptedStream` + khung mẫu (golden frame), như `tests/test_modbus.cpp`, `tests/test_link_manager.cpp`.

Ví dụ đầy đủ để tham khảo: `communication/mavlink/` (codec + protocol + bảng ánh xạ ở `products/thermo_pro/mavlink_map.cpp`)
và `tests/test_mavlink.cpp`; với bản tin riêng của sản phẩm: `communication/mavlink2/` + `products/hw_bringup/mavlink_dialect.cpp`
(`mavlink2::IDialect`) + `tests/test_mavlink2.cpp` (khung mẫu sinh bằng pymavlink).

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

### CPU Linux – sản phẩm bring-up phần cứng (MAVLink v2)

```bash
cmake -B build-cpu -DEP_TARGET=linux_cpu -DEP_PRODUCT=hw_bringup
```

Mô phỏng: `-DEP_PRODUCT=hw_bringup` với target mặc định `linux_sim`. Sản phẩm dùng `communication/mavlink2/`
(MAVLink v2 + dialect `tools/mavlink/bringup.xml`, msgid 24 bit, `SERIAL_CONTROL` cho GNSS) thay vì
`communication/mavlink/` (v1), trên cổng serial và – khi target có mạng – UDP 14560.
Chi tiết: [`products/hw_bringup/README.md`](products/hw_bringup/README.md); yêu cầu:
[`products/hw_bringup/REQUIREMENTS.md`](products/hw_bringup/REQUIREMENTS.md).

### Xilinx Zynq UltraScale+ MPSoC (ZCU102, ZCU104, Kria…)

Hai target, dùng được riêng hoặc cùng lúc (AMP – mỗi lõi chạy một firmware):

| | APU – `zynqmp_a53_linux` | RPU – `zynqmp_r5_freertos` |
|---|---|---|
| CPU / OS | Cortex-A53, Linux (PetaLinux/Yocto) | Cortex-R5F, FreeRTOS (BSP `freertos` của Vitis) |
| OSAL | `posix` + `EP_POSIX_REALTIME`: Priority → `SCHED_FIFO` 10..85 | `freertos` (chỉ cấp phát tĩnh) |
| HAL | `zynqmp_linux` + `common/linux`: driver kernel chuẩn | `zynqmp_r5`: driver standalone XGpioPs/XUartPs/XIicPs, polled |
| Console | stdin/stdout (ttyPS0 hoặc ssh) | PS UART1 |
| Aux UART | `/dev/ttyPS1` | tắt (bật bằng `EP_ZYNQMP_R5_AUX_UART=<id>`) |
| I2C cảm biến | `/dev/i2c-1` | PS I2C1, 100 kHz, qua I2C mux 0x74 (kênh 0) |
| LED / nút | PS GPIO: MIO23 (status), EMIO0 = line 78 (alarm, cần design PL), MIO22 (nút) | như APU |
| NV storage | `/var/lib/embededframework/nvram.bin` (hoặc file EEPROM sysfs) | EEPROM 24xx @0x54 trên bus cảm biến |
| Reset | thoát tiến trình (`systemd Restart=always`) | soft reset hệ thống (`CRL_APB.RESET_CTRL`) |

Giá trị mặc định theo ZCU102 – **đối chiếu với device tree / schematic của board bạn**.

**APU (Linux)** – cấu hình lúc chạy bằng biến môi trường, không cần build lại:
`EP_ZYNQMP_AUX_UART` (`""` = tắt), `EP_ZYNQMP_SENSOR_I2C`, `EP_ZYNQMP_GPIOCHIP` (đường dẫn hoặc nhãn chip),
`EP_ZYNQMP_LED_STATUS`, `EP_ZYNQMP_LED_ALARM`, `EP_ZYNQMP_BUTTON` (số line), `EP_ZYNQMP_NVRAM`.
Thiết bị nào thiếu thì in cảnh báo và chạy tiếp (vd. thiếu cảm biến → cảnh báo vào `FAULT`).

```bash
cmake -B build-zynqmp -DEP_TARGET=zynqmp_a53_linux \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
cmake --build build-zynqmp -j
ctest --test-dir build-zynqmp            # trên PC x86: chạy qua qemu-aarch64
```
```bash
# trên board: SCHED_FIFO + mlockall cần root hoặc capability
sudo setcap cap_sys_nice,cap_ipc_lock+ep ./firmware
EP_ZYNQMP_AUX_UART=/dev/ttyUL0 ./firmware
```
Có thể dùng SDK của PetaLinux thay toolchain trên: `source environment-setup-cortexa72-cortexa53-xilinx-linux`
rồi bỏ `-DCMAKE_TOOLCHAIN_FILE` (SDK tự đặt compiler và sysroot).

**RPU (FreeRTOS)** – trong Vitis tạo platform cho `psu_cortexr5_0`, OS = freertos, đặt
`configSUPPORT_STATIC_ALLOCATION=1`, `configCHECK_FOR_STACK_OVERFLOW=2`, build BSP; lấy `lscript.ld` từ một app mẫu:

```bash
cmake -B build-r5 -DEP_TARGET=zynqmp_r5_freertos \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armr5-none-eabi.cmake \
      -DEP_XILINX_BSP_DIR=<platform>/psu_cortexr5_0/freertos_domain/bsp/psu_cortexr5_0 \
      -DEP_XILINX_LINKER_SCRIPT=<app>/src/lscript.ld
cmake --build build-r5 -j                 # -> firmware.elf: nạp bằng XSCT/JTAG hoặc đóng vào BOOT.BIN
```
Gán ngoại vi khác mặc định bằng `-DEP_XILINX_DEFINES="EP_ZYNQMP_R5_CONSOLE_UART=0;EP_ZYNQMP_R5_EEPROM_ENABLE=0"`
(danh sách macro ở đầu `platform/hal/ports/zynqmp_r5/zynqmp_r5_board.cpp`). BSP theo flow System Device Tree
(Vitis Unified IDE): thêm `-DEP_XILINX_SDT=ON` (driver tra cứu theo `XPAR_*_BASEADDR` thay vì `DEVICE_ID`).
Dùng Arm GNU Toolchain thay toolchain của Vitis: `-DEP_ARMR5_PREFIX=arm-none-eabi-`.

**AMP (Linux + FreeRTOS cùng lúc):** mỗi ngoại vi chỉ được **một** bên dùng. Ví dụ: APU giữ UART0 + I2C0,
RPU giữ UART1 + I2C1 + EEPROM – tắt các node tương ứng trong device tree của Linux và cấu hình firmware R5
bằng các macro trên. Hai bên có thể nói chuyện với nhau qua một kênh đã có (UART nối chéo, UDP qua mạng)
bằng chính các giao thức CLI/Modbus/MAVLink của framework; OpenAMP/RPMsg chưa có sẵn – thêm bằng một
`IByteStream` mới (xem 6.7).

**Mức độ kiểm chứng:**
- APU: cross build AArch64 không warning; toàn bộ unit test (kể cả test HAL Linux: UART qua pty, FileStorage,
  thiết bị vắng mặt) và firmware chạy dưới **qemu-aarch64** – Modbus RTU qua pty, MAVLink qua TCP, priority
  realtime, 0 vi phạm heap. **Chưa chạy trên board thật** (GPIO chardev/I2C thật chỉ được kiểm tra nhánh lỗi).
- RPU: HAL, target và toàn bộ framework **biên dịch** cho Cortex-R5F (`arm-none-eabi-g++`) với header thật
  của Xilinx embeddedsw (driver gpiops/uartps/iicps, standalone, FreeRTOS port ARM_CR5); mọi symbol driver mà
  framework dùng đều link được với các driver đó. `xparameters.h`/`FreeRTOSConfig.h` là bản giả lập theo ZCU102,
  chưa link với một BSP Vitis hoàn chỉnh. **Chưa chạy trên phần cứng.**

### QNX Neutrino 7.1 / 8.0

Target `qnx` chạy trên **mọi board có BSP QNX** (i.MX8, Zynq UltraScale+, TI AM6x/J7, Raspberry Pi 4,
x86_64…) mà không cần port riêng: driver là resource manager của BSP, firmware chỉ ánh xạ ID logic → thiết bị.

| Thành phần | Hiện thực | Ghi chú |
|---|---|---|
| OSAL | `posix` (pthread) + `EP_POSIX_REALTIME` → `SCHED_FIFO` 10..85 | đổi dải bằng `EP_POSIX_RT_PRIO_BASE/STEP` trong `cmake/targets/qnx.cmake` nếu đè lên driver của BSP |
| Console | stdin/stdout | chạy trên console serial hoặc qua ssh/telnet |
| Aux UART | `TermiosUart` trên `/dev/ser2` (devc-ser*) | `EP_QNX_AUX_UART` (`""` = tắt) |
| I2C | `QnxI2c`: `devctl(DCMD_I2C_SEND/RECV/SENDRECV)` tới `/dev/i2c1` (i2c-imx, i2c-cdns…), repeated start | `EP_QNX_SENSOR_I2C`, `EP_QNX_I2C_SPEED` (Hz) |
| GPIO | `MmioGpio`: QNX không có API GPIO chuẩn → ánh xạ thanh ghi khối GPIO (`mmap_device_memory`) | `EP_QNX_GPIO_STATUS_LED`, `EP_QNX_GPIO_ALARM_LED`, `EP_QNX_GPIO_BUTTON`; trống = không dùng |
| NV storage | `FileStorage` `/data/embededframework/nvram.bin` | `EP_QNX_NVRAM` – đặt trên filesystem ghi được, bền vững (qnx6fs) |
| Mạng | port `posix` (BSD socket, io-pkt/io-sock), link `libsocket` | – |
| Reset | thoát tiến trình | để SLM (System Launch and Monitor) khởi động lại |

Chuỗi GPIO mô tả thanh ghi của khối GPIO (`platform/hal/ports/common/mmio/mmio_gpio.hpp`): `base` (địa chỉ vật
lý), `out`, `in`, `dir` (1 = output) hoặc `dir_n` (0 = output), `oen` (output enable), `set`/`clr` (thanh ghi
W1S/W1C – ghi atomic), `bit`, `active_low`. Pull-up/down thuộc pinmux của SoC – cấu hình trong BSP/startup.

```bash
source <qnx-sdp>/qnxsdp-env.sh
cmake -B build-qnx -DEP_TARGET=qnx -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/qnx.cmake   # -DEP_QNX_ARCH=x86_64
cmake --build build-qnx -j
```
```bash
# trên target (root, hoặc cấp ability PROCMGR_AID_PRIORITY / MEM_LOCK / MEM_PHYS)
EP_QNX_AUX_UART=/dev/ser1 EP_QNX_SENSOR_I2C=/dev/i2c0 \
EP_QNX_GPIO_STATUS_LED="base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=23" \
EP_QNX_NVRAM=/data/ep/nvram.bin ./firmware
./ep_tests                      # unit test (kể cả test HAL POSIX/QNX) chạy ngay trên target
```
Ví dụ chuỗi GPIO: ZynqMP MIO23 như trên; i.MX8M GPIO1_IO13 `base=0x30200000,out=0x0,in=0x8,dir=0x4,bit=13`;
TI AM335x GPIO0_21 `base=0x44E07000,out=0x13C,in=0x138,dir_n=0x134,set=0x194,clr=0x190,bit=21`.
Khối GPIO kiểu "function select" (Raspberry Pi BCM2711) không theo mô hình này – cần một `IGpio` riêng.
Đọc-sửa-ghi thanh ghi được khóa trong tiến trình; nếu một driver QNX khác cũng dùng cùng bank GPIO, hãy chọn
chân có thanh ghi `set`/`clr` hoặc dành cả bank cho firmware.

**Mức độ kiểm chứng:** chưa có QNX SDP (cần license) nên **chưa biên dịch bằng qcc và chưa chạy trên QNX
thật**. Mã QNX được biên dịch (`-Wall -Wextra`, không warning) với header `hw/i2c.h` thật của QNX (bản trong BSP
công khai) và một lớp giả lập `devctl`/`mmap_device_memory` trên Linux (không nằm trong repo), trên đó:
- toàn bộ unit test pass; `QnxI2c` được thử riêng: bố cục gói tin, SEND/RECV/SENDRECV, NACK, giới hạn kích thước;
- firmware: đọc nhiệt độ qua `QnxI2c` → TMP102 giả; Modbus RTU qua pty; MAVLink qua TCP (đọc/ghi tham số);
  tham số giữ được sau khởi động lại; LED cảnh báo tắt / nháy chậm / nháy nhanh đúng theo mức cảnh báo trong
  khi LED trạng thái cùng thanh ghi vẫn nháy đều (không ghi đè nhau); thiếu quyền, thiếu I2C, chuỗi GPIO sai
  đều chỉ cảnh báo và chạy tiếp.

Logic GPIO thanh ghi có unit test riêng chạy trên mọi host (kể cả hai thread cùng đọc-sửa-ghi một thanh ghi),
được fuzz 1 triệu chuỗi cấu hình dưới ASan/UBSan và kiểm chứng bằng mutation test (mọi lỗi cài vào đều bị test
bắt). Toàn bộ unit test chạy sạch dưới ThreadSanitizer.

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
