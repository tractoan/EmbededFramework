# Đặc tả yêu cầu – Embedded Platform (EmbededFramework)

> Tài liệu sống: mỗi yêu cầu có mã định danh cố định, nguồn gốc, nơi hiện thực, cách kiểm chứng và trạng thái.
> Cập nhật tài liệu này **cùng PR** với thay đổi code tương ứng (xem [§1.4](#14-quy-trình-cập-nhật)).

| | |
|---|---|
| Phiên bản tài liệu | 1.0 |
| Ứng với code | `main` @ `b4e6d3d` (sau PR #7) |
| Số test tự động | 81 test case (`tests/`) – 79 chạy trên host; +2 riêng QNX |

---

## Mục lục

1. [Quy ước](#1-quy-ước)
2. [Nguồn yêu cầu](#2-nguồn-yêu-cầu)
3. [Kiến trúc & phân lớp – ARCH](#3-kiến-trúc--phân-lớp--arch)
4. [Bộ nhớ – MEM](#4-bộ-nhớ--mem)
5. [Priority & lập lịch task – PRIO](#5-priority--lập-lịch-task--prio)
6. [OSAL](#6-osal--osal)
7. [HAL](#7-hal--hal)
8. [Device](#8-device--dev)
9. [Mạng TCP/IP – NET](#9-mạng-tcpip--net)
10. [Messaging – MSG](#10-messaging--msg)
11. [Services – SVC](#11-services--svc)
12. [Lấy mẫu cảm biến – SNS](#12-lấy-mẫu-cảm-biến--sns)
13. [Bộ chấp hành – ACT](#13-bộ-chấp-hành--act)
14. [Application runtime – APP](#14-application-runtime--app)
15. [Truyền thông – COMM](#15-truyền-thông--comm)
16. [Utils – UTIL](#16-utils--util)
17. [Target & toolchain – TGT](#17-target--toolchain--tgt)
18. [Sản phẩm – PROD](#18-sản-phẩm--prod)
19. [Kiểm chứng – VER](#19-kiểm-chứng--ver)
20. [Lỗi & nợ kỹ thuật đã biết – DEF](#20-lỗi--nợ-kỹ-thuật-đã-biết--def)
21. [Backlog – PLN](#21-backlog--pln)
22. [Ma trận truy vết ngược: test → yêu cầu](#22-ma-trận-truy-vết-ngược-test--yêu-cầu)
23. [Lịch sử bàn giao (PR → yêu cầu)](#23-lịch-sử-bàn-giao-pr--yêu-cầu)

---

## 1. Quy ước

### 1.1 Mã yêu cầu

`<NHÓM>-<số 3 chữ số>`, ví dụ `SNS-003`. Mã **không bao giờ dùng lại**: yêu cầu bị bỏ thì giữ dòng, đổi trạng thái
thành `DROP` và ghi lý do. Yêu cầu tách đôi thì giữ mã cũ cho phần chính, phần mới nhận mã mới.

| Nhóm | Phạm vi |
|---|---|
| ARCH | Phân lớp, quy tắc phụ thuộc, cấu trúc thư mục, chọn nền tảng lúc build |
| MEM | Chính sách bộ nhớ (không cấp phát động sau khởi động) |
| PRIO | Priority, stack, cấu hình task |
| OSAL / HAL / DEV / NET | Các thành phần trong `platform/` |
| MSG | `messaging/` – Event, EventBus, Buffer, Topic |
| SVC / SNS / ACT | `services/` – dịch vụ chung, lấy mẫu cảm biến, bộ chấp hành |
| APP | `app/runtime` – module, ModuleManager, ModuleWorker, System |
| COMM | `communication/` – kênh, giao thức |
| UTIL | `utils/` |
| TGT | Target, toolchain, port phần cứng/OS |
| PROD | Sản phẩm mẫu (`products/`) |
| VER | Chiến lược và hạ tầng kiểm chứng |
| DEF | Lỗi / nợ kỹ thuật đã biết (chưa sửa) |
| PLN | Backlog đã thảo luận, chưa làm |

### 1.2 Trạng thái

| Mã | Ý nghĩa |
|---|---|
| `VER` | Đã hiện thực, **có test tự động** chạy trên host (và các target mô phỏng nếu áp dụng) |
| `SIM` | Đã hiện thực, kiểm chứng trên **môi trường mô phỏng** (QEMU, lớp giả lập OS) – chưa trên phần cứng thật |
| `CMP` | Đã hiện thực, **chỉ biên dịch/link** với SDK thật – chưa chạy |
| `REF` | Mã tham khảo, **chưa biên dịch** với toolchain của nền tảng đó |
| `IMP` | Đã hiện thực, kiểm chứng gián tiếp (build, chạy firmware) nhưng chưa có test riêng |
| `RULE` | Quy ước thiết kế, chưa có cơ chế ép buộc tự động |
| `DEF` | Lỗi / nợ kỹ thuật đã biết |
| `PLN` | Đã thảo luận, chưa làm |
| `DROP` | Đã bỏ (giữ lại để truy vết) |

### 1.3 Cột trong bảng

**Nguồn** → mục [§2](#2-nguồn-yêu-cầu) (`SRC-xx`) hoặc quyết định thiết kế (`DD-xx`).
**Hiện thực** → file/thư mục chính. **Kiểm chứng** → tên test (`tests/test_*.cpp`) hoặc phương pháp ở [§19](#19-kiểm-chứng--ver).

### 1.4 Quy trình cập nhật

1. Yêu cầu mới: thêm dòng với mã kế tiếp của nhóm, trạng thái `PLN`, điền **Nguồn**.
2. Khi hiện thực: điền **Hiện thực**, **Kiểm chứng**; đổi trạng thái theo mức kiểm chứng thực tế (không "nâng" trạng
   thái nếu chưa chạy trên nền tảng tương ứng).
3. Test mới phải ghi vào cột **Kiểm chứng** của yêu cầu và vào ma trận [§22](#22-ma-trận-truy-vết-ngược-test--yêu-cầu).
4. Lỗi phát hiện: thêm `DEF-xxx`, liên kết tới yêu cầu bị ảnh hưởng; khi sửa xong đổi `DEF` → trạng thái mới, ghi PR.
5. Mỗi PR ghi vào [§23](#23-lịch-sử-bàn-giao-pr--yêu-cầu) các mã yêu cầu nó bàn giao.

---

## 2. Nguồn yêu cầu

| Mã | Yêu cầu gốc (tóm tắt) |
|---|---|
| SRC-01 | Kiến trúc phần mềm nhúng C++, chuyển đổi được giữa nhiều phần cứng / hệ điều hành, tùy biến nghiệp vụ cao, phân lớp rõ HAL, OSAL, Service & Middleware, Application & Business |
| SRC-02 | CRC và tiện ích tương tự tách thành thành phần `utils` |
| SRC-03 | Hỗ trợ chuẩn truyền thông (MAVLink…); đổi giao thức linh hoạt; hiện thực vào repo |
| SRC-04 | Đặt tên/cấu trúc: `framework`→`business`; tách driver thiết bị khỏi service (`device`); gộp core/osal/hal/device vào `platform`; `target` vào `platform`; `main.cpp` ra thư mục gốc; `framework`→`runtime` |
| SRC-05 | Message passing: payload nhiều kích thước, có thể rất lớn, cấp phát linh hoạt |
| SRC-06 | Hỗ trợ uC/OS-II và uC/OS-III |
| SRC-07 | Hướng realtime: loại bỏ hoàn toàn cấp phát động (trừ lúc khởi động); cấu hình được priority các task |
| SRC-08 | Tách `messaging` khỏi `services` (làm trên `main`) |
| SRC-09 | Hỗ trợ TCP/IP và MAVLink v1 |
| SRC-10 | Hỗ trợ Xilinx Zynq UltraScale+ |
| SRC-11 | Hỗ trợ QNX; kiểm thử thêm |
| SRC-12 | Module nghiệp vụ có thể có thread riêng |
| SRC-13 | `ISensorTask` / `SensorScheduler` / `SensorTask<>` (plug-in, tuân thủ OCP) |
| SRC-14 | Cảm biến hỗ trợ nhiều cơ chế lấy mẫu: polling định kỳ và read blocking |
| SRC-15 | Output/actuator có thể có thread riêng để tự xử lý (vd. servo với PID nội bộ); **không** đẩy PID lên business; target hướng tới platform cho drone |
| SRC-16 | Tài liệu yêu cầu chi tiết để cập nhật và truy vết (tài liệu này) |

| Mã | Quyết định thiết kế rút ra từ thảo luận |
|---|---|
| DD-01 | Service khác business: service = năng lực chung có trạng thái/chính sách; business = quy tắc của sản phẩm |
| DD-02 | Bản tin đường truyền nằm trong từng `communication/<giao_thức>/`; sự kiện miền ở business/services; ánh xạ ở product |
| DD-03 | Việc chậm là năng lực chung → service có thread riêng, API bất đồng bộ; business chỉ dùng worker cho tính toán/điều khiển đặc thù |
| DD-04 | Dữ liệu tần số cao không đi qua EventBus (dùng `Topic`); EventBus cho sự kiện rời rạc |
| DD-05 | Device không tự tạo thread; thread do service/sản phẩm cấp |
| DD-06 | Output nhiều kênh cần đồng bộ (4 motor) → **một** actuator với lệnh nhiều kênh, không phải nhiều actuator |
| DD-07 | Không làm "output scheduler" kiểu cảm biến cho tới khi có sản phẩm cần (≥ 3 actuator thread cùng nhịp, RAM eo hẹp) |

---

## 3. Kiến trúc & phân lớp – ARCH

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| ARCH-001 | Hệ thống chia lớp: `utils` (độc lập) · `platform` (core, osal, hal, device, net, target) · `messaging` · `services` · `communication` · `app` (runtime, business) · `products` · `main.cpp` ở gốc | SRC-01, SRC-04, SRC-08 | `CMakeLists.txt`, cây thư mục | Build mọi target | `VER` |
| ARCH-002 | Phụ thuộc một chiều: utils ◄ platform ◄ messaging ◄ services ◄ communication ◄ app ◄ product. Lớp dưới không thấy header lớp trên | SRC-01 | `target_link_libraries` từng thư mục (include path chỉ lan theo link) | Build; thử vi phạm trong lúc phát triển | `VER` |
| ARCH-003 | HAL không dùng OSAL; khi cần chờ gọi `hal::wait_ms()` – target cài `osal::sleep_ms` sau khi kernel chạy (đảo phụ thuộc) | SRC-01 | `platform/hal/include/hal/wait.hpp`, `platform/target/zynqmp_r5_freertos/` | Build R5; dùng trong HAL R5 | `CMP` |
| ARCH-004 | Device chỉ dùng HAL, OSAL (delay, khóa bus), utils; **không** dùng services/messaging | SRC-04, DD-05 | `platform/device/CMakeLists.txt` | Build (không link `ep_services`) | `VER` |
| ARCH-005 | Device không tự tạo thread | DD-05 | Quy ước trong `platform/device/CMakeLists.txt`, README | – (chưa ép buộc, xem DEF-006) | `RULE` |
| ARCH-006 | Business không include `hal/*`, `osal_port_types.hpp`, header vendor; không tự tạo thread (dùng `ModuleWorker`) | SRC-01 | `app/business/` | Review | `RULE` |
| ARCH-007 | `main.cpp` duy nhất cho mọi target/sản phẩm; target hiện thực `target::init / run / stop_requested` | SRC-04 | `main.cpp`, `platform/target/include/target/target.hpp` | Build mọi target | `VER` |
| ARCH-008 | Chọn nền tảng lúc build: `EP_TARGET` (`cmake/targets/<t>.cmake`) đặt `EP_OSAL_PORT`, `EP_HAL_PORT`, `EP_HAL_COMMON` (danh sách), `EP_NET_PORT`; `EP_PRODUCT` chọn sản phẩm | SRC-01 | `CMakeLists.txt`, `cmake/targets/` | Build 7 target | `VER` |
| ARCH-009 | C++17, `-fno-exceptions -fno-rtti`, `-Wall -Wextra` không warning trên mọi target | SRC-07 | `CMakeLists.txt` (`ep_options`) | Build `-Wall -Wextra` | `VER` |
| ARCH-010 | Đổi nền tảng (board/OS) không sửa `main.cpp`, services, app, product | SRC-01 | Toàn bộ | 7 target dùng chung code ứng dụng | `VER` |

## 4. Bộ nhớ – MEM

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| MEM-001 | Không cấp phát heap sau khởi động: `osal::end_startup()` niêm phong; mọi cấp phát sau đó bị phát hiện (operator new/delete; trên MCU bọc thêm `malloc/calloc/realloc` bằng `-Wl,--wrap`) | SRC-07 | `platform/osal/src/heap_guard.cpp`, `memory.cpp`; `EP_HEAP_GUARD_WRAP_MALLOC` | `memory_heap_guard_detects_allocation_after_startup`, `module_worker_no_heap_after_startup`; firmware log "heap sealed" | `VER` |
| MEM-002 | Vi phạm heap: Linux/QNX `abort()` (lộ lỗi khi phát triển); MCU reset board | SRC-07 | `platform/target/*/target_*.cpp` | Review | `IMP` |
| MEM-003 | Stack thread lấy từ pool tĩnh `EP_CFG_STACK_POOL_BYTES` hoặc buffer static của sản phẩm; sau khởi động cấm lấy stack mới (`NoMemory`) | SRC-07 | `osal::allocate_stack`, `ThreadConfig::stack_mem` | `memory_stack_pool_only_during_startup` | `VER` |
| MEM-004 | Mọi bảng/hàng đợi kích thước tĩnh, cấu hình bằng macro `EP_CFG_*` (mặc định trong `core/config.hpp`, ghi đè theo `products/<p>/config/product_config.hpp`) | SRC-07 | `platform/core/include/core/config.hpp` | Build 2 sản phẩm | `VER` |
| MEM-005 | Payload lớn cấp từ pool tĩnh (`StaticBlockPool`, `PoolAllocator`, `ChainAllocator`), không heap | SRC-05, SRC-07 | `messaging/include/messaging/buffer_allocators.hpp` | `buffer_pool_picks_smallest_fitting_class_and_falls_back`, `buffer_chain_of_pools` | `VER` |
| MEM-006 | Cấp phát nội bộ chỉ có trên host (`std::thread`) được miễn qua `HostAllocationExemption` | SRC-07 | `platform/osal/ports/posix/` | Chạy test + firmware không vi phạm | `VER` |
| MEM-007 | Khởi động thất bại vì thiếu pool: log rõ task, mức dùng pool; dừng sạch các thread đã chạy | SRC-07 | `app/runtime/src/system.cpp` (`task_start_failed`) | `module_worker_start_failure_and_config_override`, `sensor_hub_real_threads_and_start_failure_cleanup`, `actuator_hub_start_failure_leaves_everything_safe` | `VER` |

Macro cấu hình hiện có (mặc định): `EP_CFG_MAX_EVENT_SUBSCRIBERS` 16 · `EP_CFG_EVENT_PAYLOAD_SIZE` 16 ·
`EP_CFG_MAX_MODULES` 16 · `EP_CFG_APP_QUEUE_DEPTH` 32 · `EP_CFG_MAX_PARAMS` 48 · `EP_CFG_MAX_COMMANDS` 24 ·
`EP_CFG_CLI_LINE_MAX` 96 · `EP_CFG_CLI_MAX_ARGS` 8 · `EP_CFG_CLI_ECHO` 1 · `EP_CFG_COMM_MAX_CHANNELS` 4 ·
`EP_CFG_COMM_MAX_PROTOCOLS` 4 · `EP_CFG_STACK_POOL_BYTES` 16 KB · `EP_CFG_MAX_THREADS` 16 · `EP_CFG_LOG_LINE_MAX` 160 ·
`EP_CFG_LOG_SINKS` 4 · `EP_CFG_LOG_LEVEL` 1 · `EP_CFG_MAX_BUFFER_POOLS` 8 · `EP_CFG_MAX_SENSOR_TASKS` 8 ·
`EP_CFG_MAX_SENSOR_GROUPS` 4 · `EP_CFG_MAX_ACTUATORS` 8.

## 5. Priority & lập lịch task – PRIO

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| PRIO-001 | 16 mức priority 0..15 trung lập (`Idle` 0, `Low` 4, `Normal` 8, `High` 12, `Realtime` 15; `raise/lower` tương đối) | SRC-07 | `platform/osal/include/osal/types.hpp` | `memory_priority_levels` | `VER` |
| PRIO-002 | Ánh xạ tuyến tính sang priority native từng port: FreeRTOS (`configMAX_PRIORITIES`), uC/OS-II (`EP_UCOS2_PRIO_TOP` + `STRIDE`, kiểm tra lúc build), uC/OS-III (`EP_UCOS3_PRIO_TOP`), POSIX realtime `SCHED_FIFO` = `EP_POSIX_RT_PRIO_BASE` + `EP_POSIX_RT_PRIO_STEP` × mức (mặc định 10..85, bật bằng `EP_POSIX_REALTIME`) | SRC-07, SRC-10, SRC-11 | `platform/osal/ports/*/` | POSIX: firmware A53/QNX-shim; uC/OS: kernel giả lập (VER-005) | `SIM` |
| PRIO-003 | Priority/stack mọi task do sản phẩm cấu hình: `TaskPlan` (app, leds, actuator_supervisor), `add_channel(..., priority, stack)`, cấu hình worker, nhóm cảm biến, actuator | SRC-07 | `app/runtime/include/app/product.hpp` | `memory_thread_registry_reports_config`, `module_worker_start_failure_and_config_override` | `VER` |
| PRIO-004 | Thiếu quyền realtime / mlockall: cảnh báo và chạy tiếp (Linux: `CAP_SYS_NICE`, `CAP_IPC_LOCK`; QNX: `PROCMGR_AID_PRIORITY`, `MEM_LOCK`) | SRC-10, SRC-11 | `platform/target/zynqmp_a53_linux/`, `platform/target/qnx/` | Chạy QEMU / shim không quyền | `SIM` |
| PRIO-005 | `sys tasks` liệt kê thread, priority, stack | SRC-07 | `app/runtime/src/system.cpp` | Firmware mô phỏng | `IMP` |

## 6. OSAL – OSAL

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| OSAL-001 | API trung lập: `Thread`, `Mutex` (`try_lock` có timeout), `Semaphore` (`give_from_isr`), `Queue<T,N>` (tĩnh, move-out nhả tài nguyên), `now_ms/sleep_ms/yield`, `CriticalSection` (lồng nhau được), registry thread | SRC-01 | `platform/osal/include/osal/` | `test_osal.cpp` (6 test) | `VER` |
| OSAL-002 | Port: `posix`, `freertos` (chỉ cấp phát tĩnh), `ucos2`, `ucos3` – chọn lúc link, không chi phí ảo | SRC-01, SRC-06 | `platform/osal/ports/` | posix: test; ucos2/3: kernel giả lập; freertos: biên dịch R5 | `SIM` |
| OSAL-003 | `kNoWait` không bao giờ chặn trên mọi port (uC/OS: timeout 0 = chờ mãi → xử lý riêng) | SRC-06 | `platform/osal/ports/ucos*/` | `osal_semaphore_respects_max_and_timeouts` | `VER` |
| OSAL-004 | Thread khởi động lại được, dùng lại tài nguyên | SRC-07 | `osal::Thread` | `osal_thread_restart_reuses_resources` | `VER` |
| OSAL-005 | Hook FreeRTOS cấp phát tĩnh dùng chung (idle/timer task memory, stack overflow, malloc failed) | SRC-10 | `platform/target/common/freertos/freertos_static_hooks.inc` | Biên dịch STM32 (tham khảo), R5 | `CMP` |
| OSAL-006 | Đồng hồ hệ thống độ phân giải ms (không có µs) | – | `osal/time.hpp` | – | `VER` |

## 7. HAL – HAL

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| HAL-001 | Interface ngoại vi trên chip: `IGpio`, `IUart`, `II2c` (`write_read` có repeated start), `INvStorage`; `IBoard` ánh xạ ID logic → thiết bị | SRC-01 | `platform/hal/include/hal/` | Mock trong `tests/mocks.hpp` | `VER` |
| HAL-002 | Mã dùng chung theo OS chọn bằng danh sách `EP_HAL_COMMON`: `posix` (console stdio, UART termios, FileStorage), `linux` (`/dev/i2c-N`, GPIO chardev v2), `qnx` (I2C `devctl`, GPIO MMIO), `mmio` (header-only) | SRC-10, SRC-11 | `platform/hal/ports/common/` | `test_posix_hal.cpp`, `test_linux_hal.cpp`, `test_qnx_hal.cpp`, `test_mmio_gpio.cpp` | `VER` |
| HAL-003 | Thiết bị vắng mặt không làm hỏng khởi động: cảnh báo, ngoại vi đó trả lỗi khi dùng | SRC-10 | Board port Linux/QNX | `posix_missing_uart_fails_cleanly`, `linux_missing_i2c_and_gpio_fail_cleanly`, `qnx_missing_i2c_fails_cleanly`; firmware A53/QNX-shim | `VER` |
| HAL-004 | UART termios raw theo POSIX (không dùng `cfmakeraw`), baud/data bits/parity/stop bits áp dụng đúng | SRC-11 | `common/posix/posix_hal.cpp` | `posix_termios_uart_over_pty`, `posix_termios_uart_applies_line_settings` (+ hook `tcsetattr`) | `VER` |
| HAL-005 | FileStorage: vùng chưa ghi đọc ra 0xFF; ghi có `fsync`; ngoài vùng → `OutOfRange` | SRC-10 | `common/posix/posix_hal.cpp` | `posix_file_storage_erased_semantics` | `VER` |
| HAL-006 | GPIO thanh ghi (MMIO) tổng quát: `out/in/dir/dir_n/oen/set/clr/bit/active_low`, cấu hình bằng chuỗi; đọc-sửa-ghi có khóa | SRC-11 | `common/mmio/mmio_gpio.hpp` | `test_mmio_gpio.cpp` (4 test), fuzz (VER-008) | `VER` |
| HAL-007 | QNX: I2C qua `devctl` `DCMD_I2C_SEND/RECV/SENDRECV`; GPIO qua `mmap_device_memory`; thiếu quyền báo rõ `PROCMGR_AID_MEM_PHYS` | SRC-11 | `common/qnx/qnx_hal.cpp`, `ports/qnx/qnx_board.cpp` | Lớp giả lập QNX (VER-006) | `SIM` |
| HAL-008 | Zynq UltraScale+ APU (Linux): mặc định ZCU102, cấu hình lúc chạy `EP_ZYNQMP_*` | SRC-10 | `ports/zynqmp_linux/` | Firmware dưới QEMU | `SIM` |
| HAL-009 | Zynq UltraScale+ RPU (R5F): XGpioPs, XUartPs, XIicPs (polled, repeated start, I2C mux), EEPROM 24xx; flow classic + SDT | SRC-10 | `ports/zynqmp_r5/` | Biên dịch + link với driver embeddedsw | `CMP` |
| HAL-010 | STM32F4: STM32Cube HAL | SRC-01 | `ports/stm32f4/` | – | `REF` |
| HAL-011 | Cấu hình thiết bị lúc chạy bằng biến môi trường (board Linux/QNX), không cần build lại | SRC-10, SRC-11 | `env_or()` | Firmware QNX-shim, A53 | `SIM` |

## 8. Device – DEV

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| DEV-001 | Interface theo loại thiết bị + driver ngoài chip: `ITemperatureSensor`, `Tmp102` (I2C) | SRC-04 | `platform/device/` | Firmware (TMP102 giả trên QNX-shim) | `IMP` |
| DEV-002 | Báo lỗi bằng `core::Status`; lớp trên quyết định log/phát event | SRC-04 | `platform/device/` | Review | `RULE` |

## 9. Mạng TCP/IP – NET

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| NET-001 | API IPv4: `TcpListener`, `TcpConnection`, `UdpSocket`; timeout trên mọi thao tác; không heap | SRC-09 | `platform/net/include/net/net.hpp` | `net_endpoint_parse`, `net_tcp_loopback_send_receive_close`, `net_udp_loopback` | `VER` |
| NET-002 | Port `posix` (BSD socket, QNX link `libsocket`); target không mạng để trống `EP_NET_PORT` → sản phẩm tự bỏ kênh mạng | SRC-09, SRC-11 | `platform/net/ports/posix/` | `test_net.cpp` (host, A53, QNX-shim) | `VER` |
| NET-004 | Port `lwip` (lwIP 2.x socket API) cho MCU | SRC-09 | `platform/net/ports/lwip/` | Chỉ kiểm tra cú pháp với header lwIP, chưa chạy | `CMP` |
| NET-003 | Kết nối bị đóng phía bên kia không làm chết tiến trình (SIGPIPE) | SRC-11 | `MSG_NOSIGNAL` / `SIGPIPE` ignore (QNX) | Review | `IMP` |

## 10. Messaging – MSG

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| MSG-001 | `Event`: id, size, timestamp, payload inline ≤ `EP_CFG_EVENT_PAYLOAD_SIZE` (trivially copyable, kiểm tra lúc biên dịch), `Buffer` tùy chọn; copy theo giá trị | SRC-05 | `messaging/include/messaging/event.hpp` | `event_bus_routes_by_id_and_wildcard` | `VER` |
| MSG-002 | Dải EventId theo lớp: `kSystemBase` 0x0000–0x00FF (runtime), `kServiceBase` 0x0100–0x0FFF (services), `kAppBase` 0x1000–0xFFFE (business), `kAny` 0xFFFF | SRC-08 | `event.hpp` | Review | `RULE` |
| MSG-003 | `EventBus` pub/sub đồng bộ, gọi listener ngoài vùng khóa, wildcard `kAny`, bảng subscriber tĩnh | SRC-01 | `event_bus.hpp` | `event_bus_routes_by_id_and_wildcard` | `VER` |
| MSG-004 | `Buffer` đếm tham chiếu: chia sẻ không copy, tự trả về pool khi tham chiếu cuối hủy; allocator thay được, thống kê `PoolStats` | SRC-05 | `buffer.hpp`, `buffer_allocators.hpp` | `buffer_reference_counting_shares_memory`, `buffer_released_after_bus_queue_and_listeners`, `buffer_lifecycle_through_module_manager` | `VER` |
| MSG-005 | `Topic<T>`: giá trị mới nhất cho dữ liệu tần số cao; seqlock trên atomic (acquire/release, không fence riêng); một bên ghi, nhiều bên đọc; không bản chụp rách | SRC-13, DD-04 | `topic.hpp` | `topic_latest_value_and_stamp`, `topic_concurrent_readers_never_see_torn_values`, TSan | `VER` |
| MSG-006 | `Topic::read()` giới hạn số lần thử, trả `Busy` thay vì quay vô hạn (tránh treo trên RTOS đơn lõi); bộ đếm không quay về 0 | SRC-13 | `topic.hpp` | Review + mutation (VER-009) | `VER` |
| MSG-007 | EventBus không gọi được từ ISR; ISR dùng `utils::RingBuffer` rồi thread phát event | SRC-01 | Quy ước | – | `RULE` |

Event đang dùng: `sys::kSystemStarted/kSystemShutdown` · `svc::evt::kParamChanged` (+1), `kSensorFault` (+2),
`kSensorRecovered` (+3), `kActuatorSafe` (+4), `kActuatorResumed` (+5) · `app::events::kTemperature` (+1),
`kSensorFault` (+2), `kSensorRecovered` (+3), `kAlarmStateChanged` (+4), `kAlarmAckRequest` (+5), `kTemperatureBatch` (+6).

## 11. Services – SVC

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| SVC-001 | `ParamStore`: đăng ký tham số (id, tên, mặc định, min/max, đơn vị), kiểm tra miền, phát `kParamChanged`, lưu NV có magic/version/CRC32; nạp bỏ qua giá trị ngoài miền/tham số lạ | SRC-01 | `services/src/param_store.cpp` | `param_store_validation_and_events`, `param_store_persistence_with_crc` | `VER` |
| SVC-002 | `ParamStore` tuần tự hóa save/load (CLI + Modbus cùng lưu không hỏng NV; save sau luôn ghi giá trị mới hơn) | SRC-10 | `param_store.cpp` (`nv_mtx_`) | `param_store_concurrent_saves_keep_nv_valid` (+ mutation) | `VER` |
| SVC-003 | `CommandService`: bảng lệnh tĩnh, tách tham số tại chỗ (không `strtok_r`), `help`; handler chạy trên thread kênh gọi | SRC-01, SRC-10 | `command_service.cpp` | `cli_protocol_line_editing_and_dispatch` | `VER` |
| SVC-004 | Lệnh `param list/get/set/save/reset` | SRC-01 | `param_cli.cpp` | Firmware mô phỏng | `IMP` |
| SVC-005 | `Logger`: mức log, nhiều sink (≤ `EP_CFG_LOG_SINKS`), loại bỏ mức thấp lúc biên dịch (`EP_CFG_LOG_LEVEL`) | SRC-01 | `logger.cpp` | Gián tiếp qua các test | `IMP` |
| SVC-006 | `LedService`: mẫu Off/On/BlinkSlow/BlinkFast/Heartbeat, thread riêng 50 ms; nghiệp vụ không biết chân GPIO | SRC-01 | `led_service.cpp` | Firmware QNX-shim (đếm cạnh thanh ghi) | `SIM` |
| SVC-007 | Lệnh `sys info / modules / sensors / actuators / tasks / buffers / log / reboot / quit` | SRC-01 | `app/runtime/src/system.cpp` | Firmware mô phỏng | `IMP` |

## 12. Lấy mẫu cảm biến – SNS

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| SNS-001 | Plug-in: `ISensorTask` là một nguồn dữ liệu; `SensorScheduler` không biết loại cảm biến → thêm cảm biến không sửa scheduler (OCP) | SRC-13 | `services/include/services/sensor_scheduler.hpp` | `sensor_task_adapts_interface_method` | `VER` |
| SNS-002 | **Polling định kỳ** (`SensorTask<Device, Sample, &Device::read>`): lịch không trôi; trễ ≥ 1 chu kỳ → đếm overrun, đặt lại lịch, không chạy dồn; nhiều task chung một thread | SRC-13, SRC-14 | `sensor_scheduler.cpp`, `periodic_schedule.hpp` | `sensor_scheduler_runs_each_task_at_its_period`, `sensor_scheduler_does_not_drift_and_resets_after_overrun` | `VER` |
| SNS-003 | **Read blocking** (`BlockingSensorTask<..., &Device::read(Sample&, timeout)>`): task tự chờ dữ liệu tối đa timeout; timestamp lúc dữ liệu đến; task chặn chiếm trọn nhóm (`add()` từ chối trộn) | SRC-14 | `sensor_scheduler.hpp/.cpp` | `sensor_blocking_task_must_own_its_group`, `sensor_blocking_drdy_fault_recovery_and_stop_latency` | `VER` |
| SNS-004 | **Dòng byte** (`StreamSensorTask<Parser, Sample>`): đọc `hal::IUart`, parser `bool feed(byte, Sample&)`; ghép mẫu bị cắt, publish từng mẫu khi nhiều mẫu một lần đọc, đồng bộ lại sau rác; đọc 0 byte thì dừng (không quay bận) | SRC-14 | `sensor_scheduler.hpp` | `sensor_stream_task_parses_across_reads_and_resyncs`, `sensor_blocking_fast_failure_backs_off` | `VER` |
| SNS-005 | Sức khỏe từng task: ok, lỗi, lỗi liên tiếp, overrun, thời gian đọc dài nhất, lần tốt gần nhất; N lỗi liên tiếp → `kSensorFault`, tốt lại → `kSensorRecovered`; topic giữ giá trị tốt cuối | SRC-13 | `sensor_scheduler.cpp` | `sensor_scheduler_fault_and_recovery_events` | `VER` |
| SNS-006 | Task chặn trả lỗi ngay (không chờ) → nghỉ phần còn lại của timeout (không chiếm CPU); ngủ đủ, không bị cắt ở lát 50 ms | SRC-14 | `sensor_scheduler.cpp` | `sensor_blocking_fast_failure_backs_off` (+ mutation) | `VER` |
| SNS-007 | `SensorHub` do `System` quản lý: khởi động trước module, dừng sau kênh; lỗi khởi động dừng sạch nhóm đã chạy | SRC-13 | `sensor_scheduler.cpp`, `system.cpp` | `sensor_hub_real_threads_and_start_failure_cleanup` | `VER` |
| SNS-008 | Đồng hồ truyền vào được (test tất định); `sample()` nhận đồng hồ | SRC-13 | `SensorScheduler::Config::clock` | Các test SNS dùng đồng hồ giả | `VER` |
| SNS-009 | Kiểm tra tham số: task null, chu kỳ 0, quá `EP_CFG_MAX_SENSOR_TASKS`, start nhóm rỗng | SRC-13 | `sensor_scheduler.cpp` | `sensor_scheduler_validation` | `VER` |
| SNS-010 | Độ phân giải lịch định kỳ 1 ms; nhanh hơn 1 kHz / jitter < 1 ms dùng task chặn chờ ngắt DRDY | SRC-14 | Tài liệu | – | `RULE` |

## 13. Bộ chấp hành – ACT

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| ACT-001 | Actuator tự chủ `Actuator<Command>`: tự xử lý phần cứng; bên ngoài chỉ `command()`. Vòng điều khiển nội bộ (vd. PID servo) nằm trong actuator | SRC-15 | `services/include/services/actuator.hpp` | `actuator_servo_runs_its_own_pid_and_converges` (mô hình động cơ: hội tụ 30.32°/30°, vọt lố 2.9 %) | `VER` |
| ACT-002 | Hai chế độ: **trực tiếp** (`update_period_ms` = 0, ghi ngay trên thread người gọi) và **thread riêng** (lưu setpoint mới nhất, `on_update()` theo lịch không trôi) | SRC-15 | `actuator.hpp/.cpp` | `actuator_direct_mode_arm_and_command`, `actuator_servo_runs_its_own_pid_and_converges` | `VER` |
| ACT-003 | Trạng thái an toàn: ngay sau `product::setup()`; khi tắt máy actuator dừng **đầu tiên** sau kênh; lớp con chỉ viết `write_safe()` | SRC-15 | `system.cpp`, `ActuatorHub` | `actuator_hub_supervisor_detects_stall_and_deadman_with_real_threads` (phần tắt máy) | `VER` |
| ACT-004 | Arm/disarm: chỉ xuất khi đã arm; lệnh khi chưa arm bị từ chối (`Busy`) | SRC-15 | `actuator.cpp` | `actuator_direct_mode_arm_and_command` | `VER` |
| ACT-005 | Deadman `command_timeout_ms`: lệnh quá hạn → an toàn; lệnh mới → tự xuất lại; kiểm tra cả trên thread riêng lẫn supervisor | SRC-15 | `actuator.cpp` | `actuator_deadman_forces_safe_and_new_command_resumes`, `actuator_threaded_deadman_runs_on_own_thread` | `VER` |
| ACT-006 | Thread riêng kẹt quá `stall_timeout_ms` (mặc định max(4 × chu kỳ, 20 ms)) → thread giám sát độc lập ép an toàn và **chốt** tới khi arm lại; cảnh báo nếu priority giám sát ≤ priority thread actuator | SRC-15 | `ActuatorHub` | `actuator_hub_supervisor_detects_stall_and_deadman_with_real_threads` | `VER` |
| ACT-007 | Sau khi `force_safe()` trả về, không có lần ghi giá trị khác an toàn nào (kiểm tra quyền và ghi phần cứng chung một vùng khóa) | SRC-15 | `actuator.hpp/.cpp` | `actuator_direct_no_output_after_force_safe_returns` (stress, + mutation) | `VER` |
| ACT-008 | Sự kiện `kActuatorSafe` (kèm lý do) / `kActuatorResumed`; `sys actuators` | SRC-15 | `actuator.cpp`, `system.cpp` | Test ACT-004/005 kiểm tra event | `VER` |
| ACT-009 | `ActuatorHub` khởi động lỗi → mọi actuator về an toàn, không còn thread | SRC-15 | `actuator.cpp` | `actuator_hub_start_failure_leaves_everything_safe` | `VER` |
| ACT-010 | Output nhiều kênh cần đồng bộ (motor) dùng một actuator với lệnh nhiều kênh | DD-06 | Hướng dẫn | – | `RULE` |
| ACT-011 | Giới hạn: thread kẹt **đang ghi phần cứng** thì lần ghi đó không ngắt được; về an toàn ở chu kỳ kế tiếp. Motor cần watchdog/enable phần cứng độc lập | SRC-15 | Tài liệu | Test ACT-006 (phần "tỉnh lại") | `RULE` |

## 14. Application runtime – APP

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| APP-001 | `IModule`: `on_init/on_start/on_stop/on_event/on_tick/worker()`; bảng thread: init/start/stop + event/tick trên thread "app", job/worker_tick trên worker, `on_command` trên thread kênh | SRC-01, SRC-12 | `app/runtime/include/app/module.hpp` | `module_manager_dispatches_events_and_ticks` | `VER` |
| APP-002 | `ModuleManager` (Active Object): nhận mọi event qua `kAny` → hàng đợi `EP_CFG_APP_QUEUE_DEPTH` (`kNoWait`, đầy thì bỏ + đếm); giao cho mọi module theo thứ tự đăng ký; module không cần khóa | SRC-01 | `module_manager.cpp` | `module_manager_dispatches_events_and_ticks`, `buffer_lifecycle_through_module_manager` | `VER` |
| APP-003 | `on_tick` theo `tick_period_ms()` (0 = không tick), đổi được lúc chạy | SRC-01 | `module_manager.cpp` | `module_manager_dispatches_events_and_ticks` | `VER` (xem DEF-002) |
| APP-004 | `ModuleWorker<Depth>` tùy chọn: thread riêng, hàng đợi tĩnh, `post()` không chặn (đầy → đếm), `on_job`, `on_worker_tick` lịch không trôi, đổi chu kỳ lúc chạy; khởi động sau mọi `on_start`, dừng trước `on_stop`; sản phẩm ghi đè cấu hình | SRC-12 | `module_worker.hpp/.cpp` | `test_module_worker.cpp` (6 test) | `VER` |
| APP-005 | `on_command` không sửa trạng thái module trực tiếp: đọc atomic/ParamStore hoặc publish event | SRC-01 | Quy ước (vd. `AlarmController`) | Review | `RULE` |
| APP-006 | `StateMachine` dùng hàm thành viên làm trạng thái, có Entry/Exit | SRC-01 | `state_machine.hpp` | Gián tiếp qua `test_alarm_controller.cpp` | `VER` |
| APP-007 | `System::init`: board → services → link → device → product setup → `on_init` → nạp NV → LED → cảm biến → actuator → module → kênh → `kSystemStarted` → `end_startup()`. Dừng: kênh → actuator → cảm biến → module → LED | SRC-01, SRC-07 | `system.cpp` | Firmware mô phỏng, các test start-failure | `VER` |

## 15. Truyền thông – COMM

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| COMM-001 | Tách vận chuyển (`IByteStream`) và giao thức (`IProtocol`); `LinkManager` quản lý kênh (≤ `EP_CFG_COMM_MAX_CHANNELS`), mỗi kênh một thread RX và danh sách giao thức cho phép (≤ `EP_CFG_COMM_MAX_PROTOCOLS`) | SRC-03 | `communication/link/` | `link_manager_switches_protocol_at_runtime` | `VER` |
| COMM-002 | Bốn mức chọn giao thức: lúc build (sản phẩm), tham số `comm.<kênh>` (`0` off, `1` auto, `2` CLI, `3` Modbus RTU, `4` MAVLink), lệnh `link set`, qua chính giao thức (Modbus holding 6/7); `auto` nhận dạng bằng `probe()` | SRC-03 | `link_manager.cpp`, `protocol.hpp` | `link_manager_switches_protocol_at_runtime`, `cli_protocol_probe`, `modbus_probe_requires_valid_frame`, `mavlink_gcs_link_supervision_and_probe` | `VER` |
| COMM-003 | CLI: soạn dòng, byte nhị phân reset dòng, echo cấu hình được (`EP_CFG_CLI_ECHO`) | SRC-03 | `communication/cli/` | `cli_protocol_line_editing_and_dispatch` | `VER` |
| COMM-004 | Modbus RTU slave: FC 03, 04, 05, 06, 16; CRC16; đồng bộ lại khung sau rác; bảng ánh xạ input/holding/coil ở sản phẩm; ghi thanh ghi đi qua ParamStore | SRC-03 | `communication/modbus/`, `products/thermo_pro/modbus_map.cpp` | `test_modbus.cpp` (5 test), pty master (VER-007) | `VER` |
| COMM-005 | MAVLink v1: HEARTBEAT, PARAM_REQUEST_READ/LIST, PARAM_VALUE, PARAM_SET, COMMAND_LONG/ACK, NAMED_VALUE_FLOAT, STATUSTEXT; CRC_EXTRA; parser đồng bộ lại; giám sát liên kết GCS | SRC-09 | `communication/mavlink/`, `products/thermo_pro/mavlink_map.cpp` | `test_mavlink.cpp` (6 test), client độc lập (VER-007) | `VER` |
| COMM-006 | Luồng mạng: `TcpServerStream` (phiên), `TcpClientStream` (tự kết nối lại), `UdpStream` (trả lời người gửi) | SRC-09 | `communication/net/` | `net_tcp_server_stream_sessions`, `net_tcp_client_stream_reconnects`, `net_udp_stream_remote_and_reply_to_sender` | `VER` |
| COMM-007 | Ánh xạ miền ↔ đường truyền ở sản phẩm; `communication` không biết module nghiệp vụ | SRC-03, DD-02 | `products/*/..._map.cpp` | Review | `RULE` |

## 16. Utils – UTIL

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| UTIL-001 | CRC32, CRC16-CCITT, CRC16-Modbus, CRC16-MCRF4XX (MAVLink, có CRC_EXTRA); tính nối tiếp được | SRC-02 | `utils/include/utils/crc.hpp` | `utils_crc_known_vectors` | `VER` |
| UTIL-002 | Định dạng fixed-point không dùng `printf %f` | SRC-02 | `fixed_point.hpp` | `utils_format_fixed` | `VER` |
| UTIL-003 | `RingBuffer<T,N>` SPSC lock-free (ISR → thread) | SRC-02 | `ring_buffer.hpp` | `utils_ring_buffer_spsc` | `VER` |

## 17. Target & toolchain – TGT

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| TGT-001 | `linux_sim`: mô phỏng trên PC (`--run-for`, Ctrl+C, NVRAM file) | SRC-01 | `cmake/targets/linux_sim.cmake`, `platform/target/linux_sim/` | Toàn bộ test + firmware | `VER` |
| TGT-002 | STM32F407 + FreeRTOS / uC/OS-II / uC/OS-III (CubeMX) | SRC-01, SRC-06 | `cmake/targets/stm32f4_*.cmake` | Chưa biên dịch với ARM toolchain; OSAL uC/OS chạy trên kernel giả lập | `REF` |
| TGT-003 | Zynq UltraScale+ APU – `zynqmp_a53_linux` (PetaLinux/Yocto), `SCHED_FIFO`, `mlockall` | SRC-10 | `cmake/targets/zynqmp_a53_linux.cmake` | Cross build; test + firmware dưới qemu-aarch64 | `SIM` |
| TGT-004 | Zynq UltraScale+ RPU – `zynqmp_r5_freertos` (BSP Vitis) | SRC-10 | `cmake/targets/zynqmp_r5_freertos.cmake` | Biên dịch + link với driver embeddedsw (`xparameters.h`/`FreeRTOSConfig.h` giả lập); thiếu symbol nội bộ BSP là dự kiến | `CMP` |
| TGT-005 | QNX Neutrino 7.1 / 8.0 – `qnx` (aarch64le, x86_64), mọi board có BSP QNX | SRC-11 | `cmake/targets/qnx.cmake`, `platform/target/qnx/` | Biên dịch trên Linux với `hw/i2c.h` thật + lớp giả lập; **chưa qcc, chưa QNX thật** | `SIM` |
| TGT-006 | Toolchain: `arm-none-eabi`, `armr5-none-eabi` (đổi tiền tố bằng `EP_ARMR5_PREFIX`; cờ CPU trong target để không mất khi tự đặt `CMAKE_CXX_FLAGS`), `aarch64-linux-gnu` (+ qemu), `qnx` (`QNX_HOST`/`QNX_TARGET`, `EP_QNX_ARCH`) | SRC-10, SRC-11 | `cmake/toolchains/` | Build tương ứng | `SIM` |

## 18. Sản phẩm – PROD

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| PROD-001 | `thermo_pro`: TemperatureMonitor (500 ms, lọc 4, lô 20 mẫu), AlarmController (cảnh báo 60.0 °C, cao 75.0 °C, trễ 2.0 °C, chốt), StatsReporter (worker priority thấp); kênh console (CLI/Modbus), aux (Modbus/MAVLink/CLI), tcp 5760 (MAVLink/Modbus/CLI), udp 14550 (MAVLink); pool Buffer 64×16, 512×8, 2048×4; stack pool 24 KB | SRC-03, SRC-09, SRC-12 | `products/thermo_pro/` | Toàn bộ test (cấu hình mặc định), firmware mô phỏng, pty Modbus, MAVLink TCP | `VER` |
| PROD-002 | `thermo_lite`: 2 module, chỉ CLI console, pool Buffer 128×4, stack pool 8 KB, tối đa 6 thread, log mức 2 | SRC-01 | `products/thermo_lite/` | Toàn bộ test với cấu hình lite | `VER` |
| PROD-003 | Module nghiệp vụ tái sử dụng: `TemperatureMonitor` (chu kỳ, offset, cửa sổ lọc, ngưỡng lỗi, lô mẫu qua Buffer), `AlarmController` (Normal/Warning/Alarm/Fault, trễ, chốt, xác nhận, LED) | SRC-01 | `app/business/` | `alarm_thresholds_hysteresis_and_latch`, `alarm_runtime_reconfiguration_and_fault` | `VER` |
| PROD-004 | Tham số: `0x10xx` nhiệt độ, `0x11xx` cảnh báo, `0x0200 + i` `comm.<kênh>`, `0x8000+` riêng sản phẩm | SRC-01 | `app/business/app_params.hpp`, `link_manager.hpp` | Review | `RULE` |

## 19. Kiểm chứng – VER

| Mã | Phương pháp | Áp dụng | Trạng thái |
|---|---|---|---|
| VER-001 | Unit test không phụ thuộc ngoài (`tests/test_framework.hpp`), chạy theo từng target (host, qemu-aarch64, QNX-shim); pool stack trả về mốc giữa các test | Mọi lớp | `VER` |
| VER-002 | Sanitizer: ASan + UBSan, ThreadSanitizer (0 cảnh báo là điều kiện merge) | Mọi lớp | `VER` |
| VER-003 | Build và test lại **từ `git archive HEAD`** trước khi push/merge (không dùng cây làm việc) | Mọi PR | `VER` |
| VER-004 | Ma trận target trước merge: host `-Wall -Wextra`, ASan, TSan, thermo_lite, A53 (QEMU), QNX-shim, R5 (biên dịch/link) | Mọi PR | `VER` |
| VER-005 | Kernel uC/OS-II/III giả lập trên pthread (ngoài repo) để chạy OSAL + test + firmware | OSAL-002 | `SIM` |
| VER-006 | Lớp giả lập QNX (ngoài repo): `devctl` với TMP102 giả, `mmap_device_memory` trả vùng nhớ dùng chung theo địa chỉ vật lý, luồng lấy mẫu cạnh thanh ghi; header `hw/i2c.h` thật | HAL-007, TGT-005 | `SIM` |
| VER-007 | Kiểm thử chéo giao thức bằng client độc lập: Modbus master qua pty, trạm mặt đất MAVLink tối giản (thư viện chuẩn, đối chiếu định dạng với pymavlink) | COMM-004/005 | `VER` |
| VER-008 | Fuzz chuỗi cấu hình GPIO (1 triệu chuỗi dưới ASan/UBSan) | HAL-006 | `VER` |
| VER-009 | Mutation test thủ công: cài lỗi có chủ đích, test phải bắt. Kết quả: MMIO 7/7, `QnxI2c` 4/4, worker 3/3, sensor 7/7 + 6/6, actuator 10/10 | Các tính năng mới | `VER` |
| VER-010 | Giới hạn công cụ đã biết: TSan của GCC 13 không chặn `pthread_mutex_clocklock` (`try_lock_for`) → code cần TSan kiểm tra dùng `try_lock(kNoWait)` + thử lại | VER-002 | `RULE` |
| VER-011 | Chưa kiểm chứng: CPU ARM thật (thứ tự bộ nhớ yếu), phần cứng thật mọi target, qcc, BSP Vitis hoàn chỉnh, toolchain STM32 | – | `PLN` |

## 20. Lỗi & nợ kỹ thuật đã biết – DEF

| Mã | Mô tả | Ảnh hưởng | Đề xuất | Trạng thái |
|---|---|---|---|---|
| DEF-001 | Spinlock đọc-sửa-ghi của GPIO MMIO quay không nhường CPU: với `SCHED_FIFO`, thread priority cao trên cùng lõi với thread đang giữ khóa có thể quay mãi | HAL-006 (QNX/Linux realtime) | Dùng mutex của OS hoặc `sched_yield` khi quay | `DEF` |
| DEF-002 | Lịch `on_tick` của `ModuleManager` là `now + period` → chu kỳ trôi (vd. 1000 ms + 5 ms trễ ≈ 18 s/giờ); jitter theo event đang xử lý | APP-003, PROD-003 (`kTemperatureBatch` giả định cách đều) | Dùng `svc::PeriodicSchedule` | `DEF` |
| DEF-003 | Mutex port POSIX (`std::mutex`) không có priority inheritance trên Linux; port uC/OS-II tạo mutex với `OS_PRIO_MUTEX_CEIL_DIS` | PRIO-002 (đảo priority) | `pthread_mutex` + `PTHREAD_PRIO_INHERIT` khi `EP_POSIX_REALTIME`; bật ceiling uC/OS-II | `DEF` |
| DEF-004 | `ThreadConfig` không có CPU affinity | Tính tất định trên SMP | Thêm `core_affinity`, mỗi port ánh xạ (`pthread_setaffinity_np`, `ThreadCtl`, `vTaskCoreAffinitySet`, `k_thread_cpu_pin`) | `DEF` |
| DEF-005 | `PoolAllocator` dùng `CriticalSection` toàn cục (FreeRTOS SMP: khóa kernel mọi lõi) | MSG-004, hiệu năng SMP | Khóa theo pool hoặc free-list lock-free | `DEF` |
| DEF-006 | Luật "device không tạo thread" chưa được build ép buộc | ARCH-005 | Kiểm tra CMake: lỗi nếu `platform/device/` include `osal/thread.hpp` / `osal/osal.hpp` | `DEF` |
| DEF-007 | Hàng đợi event của ModuleManager không có làn ưu tiên: quá tải thì event quan trọng (lỗi, cảnh báo) có thể bị bỏ | APP-002 | Hàng đợi riêng cho event ưu tiên | `DEF` |
| DEF-008 | `TemperatureMonitor::on_tick` đọc I2C (chặn) trên thread "app" | APP-002 | Chuyển sang `SensorScheduler` + `Topic` | `DEF` |
| DEF-009 | `Topic`/`SensorScheduler`/`Actuator` chưa được sản phẩm nào dùng (chỉ test) | SNS, ACT | Tích hợp vào sản phẩm (DEF-008) | `DEF` |
| DEF-010 | Không xóa được branch trên GitHub từ môi trường phát triển (proxy chặn) – branch `feature/*` cũ còn trên remote | Vận hành | Xóa thủ công; bật "Automatically delete head branches" | `DEF` |

## 21. Backlog – PLN

| Mã | Nội dung | Liên quan | Trạng thái |
|---|---|---|---|
| PLN-001 | HAL: `IPwm` / output timer (DShot), `ISpi`, `IAdc`, encoder / timer capture (+ bản mô phỏng) | SRC-15, drone | `PLN` |
| PLN-002 | Parser thật cho `StreamSensorTask`: NMEA, UBX (GNSS), SBUS/CRSF (máy thu RC) | SNS-004 | `PLN` |
| PLN-003 | Kích hoạt theo mẫu mới: `Topic` có thông báo (semaphore) để vòng điều khiển chạy ngay khi có mẫu IMU | MSG-005, drone | `PLN` |
| PLN-004 | Actuator: gom nhiều actuator dùng chung một thread (khi ≥ 3 actuator cùng nhịp, RAM eo hẹp) | DD-07 | `PLN` |
| PLN-005 | Actuator: phân xử nhiều nguồn lệnh theo priority (failsafe > tay > tự động), giới hạn/ramp | ACT | `PLN` |
| PLN-006 | MAVLink v2; tách `communication/mavlink/messages/`; sinh code từ XML dialect; mission protocol | COMM-005, DD-02 | `PLN` |
| PLN-007 | Service bất đồng bộ kiểu yêu cầu/phản hồi (cặp event có `request_id`) | MSG-003 | `PLN` |
| PLN-008 | Mô hình executor: sản phẩm gán module vào nhiều thread ứng dụng | APP-002, SMP | `PLN` |
| PLN-009 | Giải quyết DEF-001 … DEF-008 | DEF | `PLN` |
| PLN-010 | Chạy trên phần cứng thật: ZCU102 (APU + RPU), board QNX, STM32F407 | VER-011 | `PLN` |

---

## 22. Ma trận truy vết ngược: test → yêu cầu

| File test | Test case | Yêu cầu |
|---|---|---|
| `test_utils.cpp` | `utils_crc_known_vectors`, `utils_format_fixed`, `utils_ring_buffer_spsc` | UTIL-001, UTIL-002, UTIL-003 |
| `test_buffer.cpp` | `buffer_pool_picks_smallest_fitting_class_and_falls_back`, `buffer_reference_counting_shares_memory`, `buffer_chain_of_pools`, `buffer_released_after_bus_queue_and_listeners`, `buffer_lifecycle_through_module_manager` | MSG-004, MEM-005, APP-002 |
| `test_osal.cpp` | `osal_queue_fifo_and_timeout`, `osal_thread_consumes_queue`, `osal_semaphore_respects_max_and_timeouts`, `osal_mutex_try_lock_from_other_thread`, `osal_thread_restart_reuses_resources`, `osal_critical_section_nests` | OSAL-001, OSAL-003, OSAL-004 |
| `test_memory.cpp` | `memory_priority_levels`, `memory_stack_pool_only_during_startup`, `memory_heap_guard_detects_allocation_after_startup`, `memory_thread_registry_reports_config` | PRIO-001, MEM-003, MEM-001, PRIO-003 |
| `test_event_bus.cpp` | `event_bus_routes_by_id_and_wildcard` | MSG-001, MSG-003 |
| `test_param_store.cpp` | `param_store_validation_and_events`, `param_store_persistence_with_crc` | SVC-001 |
| `test_module_manager.cpp` | `module_manager_dispatches_events_and_ticks` | APP-001, APP-002, APP-003 |
| `test_module_worker.cpp` | `module_worker_runs_jobs_on_own_thread_zero_copy`, `module_worker_queue_full_counts_dropped`, `module_worker_tick_does_not_drift`, `module_worker_period_change_at_runtime`, `module_worker_start_failure_and_config_override`, `module_worker_no_heap_after_startup` | APP-004, MEM-001, MEM-007 |
| `test_alarm_controller.cpp` | `alarm_thresholds_hysteresis_and_latch`, `alarm_runtime_reconfiguration_and_fault` | PROD-003, APP-006 |
| `test_cli_protocol.cpp` | `cli_protocol_line_editing_and_dispatch`, `cli_protocol_probe` | COMM-003, SVC-003, COMM-002 |
| `test_modbus.cpp` | `modbus_read_input_and_holding`, `modbus_write_registers_go_through_param_store`, `modbus_coil_triggers_action_event`, `modbus_framing_robustness`, `modbus_probe_requires_valid_frame` | COMM-004, COMM-002 |
| `test_link_manager.cpp` | `link_manager_switches_protocol_at_runtime` | COMM-001, COMM-002 |
| `test_mavlink.cpp` | `mavlink_codec_roundtrip_and_layout`, `mavlink_parser_resyncs_through_garbage`, `mavlink_heartbeat_and_system_state`, `mavlink_param_protocol_bridges_param_store`, `mavlink_command_long_and_telemetry`, `mavlink_gcs_link_supervision_and_probe` | COMM-005, COMM-002 |
| `test_net.cpp` | `net_endpoint_parse`, `net_tcp_loopback_send_receive_close`, `net_udp_loopback`, `net_tcp_server_stream_sessions`, `net_tcp_client_stream_reconnects`, `net_udp_stream_remote_and_reply_to_sender` | NET-001, COMM-006 |
| `test_posix_hal.cpp` | `posix_termios_uart_over_pty`, `posix_termios_uart_applies_line_settings`, `posix_missing_uart_fails_cleanly`, `posix_file_storage_erased_semantics`, `param_store_concurrent_saves_keep_nv_valid` | HAL-004, HAL-003, HAL-005, SVC-002 |
| `test_linux_hal.cpp` | `linux_missing_i2c_and_gpio_fail_cleanly` | HAL-002, HAL-003 |
| `test_qnx_hal.cpp` (chỉ target qnx) | `qnx_missing_i2c_fails_cleanly`, `qnx_mmio_gpio_unconfigured_or_invalid` | HAL-007, HAL-003 |
| `test_mmio_gpio.cpp` | `mmio_gpio_spec_parsing`, `mmio_gpio_rmw_dir_oen_and_shared_register`, `mmio_gpio_inverted_dir_set_clr_active_low`, `mmio_gpio_concurrent_rmw_keeps_both_updates` | HAL-006 |
| `test_sensor_scheduler.cpp` | `topic_latest_value_and_stamp`, `topic_concurrent_readers_never_see_torn_values` | MSG-005, MSG-006 |
| | `sensor_scheduler_runs_each_task_at_its_period`, `sensor_scheduler_does_not_drift_and_resets_after_overrun` | SNS-002 |
| | `sensor_scheduler_fault_and_recovery_events`, `sensor_scheduler_validation`, `sensor_task_adapts_interface_method`, `sensor_hub_real_threads_and_start_failure_cleanup` | SNS-005, SNS-009, SNS-001, SNS-007 |
| `test_sensor_blocking.cpp` | `sensor_blocking_task_must_own_its_group`, `sensor_stream_task_parses_across_reads_and_resyncs`, `sensor_blocking_drdy_fault_recovery_and_stop_latency`, `sensor_blocking_fast_failure_backs_off` | SNS-003, SNS-004, SNS-005, SNS-006 |
| `test_actuator.cpp` | `actuator_direct_mode_arm_and_command`, `actuator_direct_no_output_after_force_safe_returns`, `actuator_deadman_forces_safe_and_new_command_resumes`, `actuator_servo_runs_its_own_pid_and_converges`, `actuator_threaded_deadman_runs_on_own_thread`, `actuator_hub_supervisor_detects_stall_and_deadman_with_real_threads`, `actuator_hub_start_failure_leaves_everything_safe` | ACT-001 … ACT-009 |

## 23. Lịch sử bàn giao (PR → yêu cầu)

| Bàn giao | Nội dung | Yêu cầu |
|---|---|---|
| Commit trực tiếp `main` (`5713852` … `16b7bca`) | Khung HAL/OSAL/Services/App; utils; comm + đổi giao thức (CLI, Modbus RTU); device; tái cấu trúc platform/target/runtime; `main.cpp` gốc; Buffer payload lớn | ARCH-001…010, UTIL, COMM-001…004, MSG-004, DEV-001 |
| [#1](https://github.com/tractoan/EmbededFramework/pull/1) | Port OSAL uC/OS-II/III + target STM32; không cấp phát sau khởi động; priority cấu hình được | OSAL-002/003, MEM-001…007, PRIO-001…003, TGT-002 |
| `0bc5780` (trực tiếp `main`) | Tách `messaging/` khỏi `services/` | ARCH-001, MSG-001…003 |
| [#2](https://github.com/tractoan/EmbededFramework/pull/2) | TCP/IP + MAVLink v1 | NET-001/002/004, COMM-005/006 |
| [#3](https://github.com/tractoan/EmbededFramework/pull/3) | Zynq UltraScale+ (APU Linux + RPU FreeRTOS); sửa `strtok_r`, khóa NV ParamStore, test pool | HAL-002/004/005/008/009, TGT-003/004/006, SVC-002/003, ARCH-003 |
| [#4](https://github.com/tractoan/EmbededFramework/pull/4) | QNX Neutrino; HAL tách posix/linux/qnx/mmio; test bổ sung | HAL-002…007/011, TGT-005, NET-002/003, PRIO-002/004 |
| [#5](https://github.com/tractoan/EmbededFramework/pull/5) | `ModuleWorker` – thread riêng cho module | APP-001/004 |
| [#6](https://github.com/tractoan/EmbededFramework/pull/6) | `Topic`, `SensorScheduler` (định kỳ + chặn + dòng byte) | MSG-005/006, SNS-001…010 |
| [#7](https://github.com/tractoan/EmbededFramework/pull/7) | Actuator tự chủ + `ActuatorHub`; `PeriodicSchedule` dùng chung | ACT-001…011, APP-007 |
| (PR này) | Tài liệu yêu cầu | SRC-16 |
