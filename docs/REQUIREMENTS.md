# Đặc tả yêu cầu – Embedded Platform (EmbededFramework)

> Tài liệu sống: mỗi yêu cầu có mã định danh cố định, nguồn gốc, nơi hiện thực, cách kiểm chứng và trạng thái.
> Cập nhật tài liệu này **cùng PR** với thay đổi code tương ứng (xem [§1.4](#14-quy-trình-cập-nhật)).

| | |
|---|---|
| Phiên bản tài liệu | 1.5 |
| Ứng với code | `main` @ `75ae991` (sau PR #11); bổ sung của nhánh `claude/vibrant-dirac-qrpici` (sản phẩm `hw_bringup`, xem §23) |
| Số test tự động | 128 test case (`tests/`) – 126 chạy trên host Linux (102 của `main` + 24 của nhánh `hw_bringup`); target QNX chạy 127 (thêm 2 test riêng QNX, không có test riêng Linux) |

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
13. [Output (bộ chấp hành & chỉ báo) – ACT](#13-output-bộ-chấp-hành--chỉ-báo--act)
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
| SRC-17 | Bổ sung hỗ trợ Nios II |
| SRC-18 | Gộp mọi output của hệ thống vào một cơ chế dùng chung; đổi tên `Actuator` → `Output` (phương án A: `Output<Cmd>`, `OutputHub`, `OutputClass::Actuator/Indicator`; `CommandOutput` → `CommandReply`); gộp LED |
| SRC-19 | Cảm biến cần đường cấu hình; cấu hình phải **áp dụng ngay**, không chờ nhịp lấy mẫu |
| SRC-20 | Thêm tùy chọn cấu hình **không cần ngay** mà chờ nhịp lấy mẫu |
| SRC-21 | Firmware bring-up phần cứng chạy trên CPU, giao tiếp phần mềm giao diện qua MAVLink: 12 serial RS232/485 (1 giao diện, 1 GNSS – không có dữ liệu trong 5 s thì chuyển loopback, 10 loopback), 20 DI, 20 DO, 2 IMU SDLC/HDLC (chi tiết: `products/hw_bringup/REQUIREMENTS.md`) |
| SRC-22 | Giải mã NMEA của máy thu GNSS, gửi thời gian, vĩ độ, kinh độ lên PC |
| SRC-23 | Phân rõ service (cơ chế dùng chung) và business (chính sách); GNSS thành service; định nghĩa bản tin dialect MAVLink vào `communication/` |
| SRC-24 | Build được với header kernel Linux cũ (< 5.10, không có GPIO uAPI v2) |
| SRC-25 | DO của `hw_bringup` dùng cơ chế output của framework (`Output` loại Actuator): an toàn lúc khởi động/tắt, arm/disarm từ giao diện, deadman tùy chọn |
| SRC-26 | GNSS dùng được như một cảm biến của `SensorScheduler` (chỉ đọc): parser gom fix theo dạng `StreamSensorTask` |
| SRC-27 | Bỏ `GnssReceiver`: GNSS chỉ một đường qua cơ chế cảm biến của framework; cổng do `SerialPortBank` quản lý đọc qua adapter `IUart`; lệnh tới máy thu do worker của nghiệp vụ ghi |

| Mã | Quyết định thiết kế rút ra từ thảo luận |
|---|---|
| DD-01 | Service khác business: service = năng lực chung có trạng thái/chính sách; business = quy tắc của sản phẩm |
| DD-02 | Bản tin đường truyền nằm trong từng `communication/<giao_thức>/`; sự kiện miền ở business/services; ánh xạ ở product |
| DD-03 | Việc chậm là năng lực chung → service có thread riêng, API bất đồng bộ; business chỉ dùng worker cho tính toán/điều khiển đặc thù |
| DD-04 | Dữ liệu tần số cao không đi qua EventBus (dùng `Topic`); EventBus cho sự kiện rời rạc |
| DD-05 | Device không tự tạo thread; thread do service/sản phẩm cấp |
| DD-06 | Output nhiều kênh cần đồng bộ (4 motor) → **một** output với lệnh nhiều kênh, không phải nhiều output |
| DD-07 | Không làm "output scheduler" kiểu cảm biến cho tới khi có sản phẩm cần (≥ 3 output có thread cùng nhịp, RAM eo hẹp) |
| DD-08 | Output phân loại theo mức an toàn: Actuator (nguy hiểm: arm, deadman, chốt khi kẹt, failsafe tắt) và Indicator (chỉ báo: failsafe KHÔNG tắt – không tắt đèn cảnh báo khi có sự cố) |
| DD-09 | Cấu hình cảm biến chạy **đồng bộ trên thread gọi**, tuần tự với lấy mẫu bằng khóa thiết bị theo task (không xếp hàng chờ nhịp kế tiếp); task chặn chờ theo lát để nhả khóa |
| DD-10 | Cấu hình chờ nhịp do **luồng lấy mẫu** ghi (đầu `sample()`, trước lần đọc) – không thêm thread/hàng đợi; một ô yêu cầu cố định cho mỗi bộ cấu hình (gộp, mới nhất thắng); "nhịp" của task chặn = một lần `sample()`, không phải một lát |
| DD-11 | Dialect MAVLink tách đôi: định nghĩa bản tin trên dây ở `communication/mavlink2/dialects/<d>/` (không phụ thuộc nghiệp vụ); ánh xạ Event ↔ bản tin ở sản phẩm (`IDialect`) vì cần event của business |
| DD-12 | MAVLink v1 (`communication/mavlink`, bảng ánh xạ telemetry) và v2 + dialect (`communication/mavlink2`) tồn tại song song; sản phẩm chọn lúc build. Gộp là PLN-006 |

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
| MEM-007 | Khởi động thất bại vì thiếu pool: log rõ task, mức dùng pool; dừng sạch các thread đã chạy | SRC-07 | `app/runtime/src/system.cpp` (`task_start_failed`) | `module_worker_start_failure_and_config_override`, `sensor_hub_real_threads_and_start_failure_cleanup`, `output_hub_start_failure_leaves_everything_safe` | `VER` |

Macro cấu hình hiện có (mặc định): `EP_CFG_MAX_EVENT_SUBSCRIBERS` 16 · `EP_CFG_EVENT_PAYLOAD_SIZE` 16 ·
`EP_CFG_MAX_MODULES` 16 · `EP_CFG_APP_QUEUE_DEPTH` 32 · `EP_CFG_MAX_PARAMS` 48 · `EP_CFG_MAX_COMMANDS` 24 ·
`EP_CFG_CLI_LINE_MAX` 96 · `EP_CFG_CLI_MAX_ARGS` 8 · `EP_CFG_CLI_ECHO` 1 · `EP_CFG_COMM_MAX_CHANNELS` 4 ·
`EP_CFG_COMM_MAX_PROTOCOLS` 4 · `EP_CFG_STACK_POOL_BYTES` 16 KB · `EP_CFG_MAX_THREADS` 16 · `EP_CFG_LOG_LINE_MAX` 160 ·
`EP_CFG_LOG_SINKS` 4 · `EP_CFG_LOG_LEVEL` 1 · `EP_CFG_MAX_BUFFER_POOLS` 8 · `EP_CFG_MAX_SENSOR_TASKS` 8 ·
`EP_CFG_MAX_SENSOR_GROUPS` 4 · `EP_CFG_MAX_OUTPUTS` 8.

## 5. Priority & lập lịch task – PRIO

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| PRIO-001 | 16 mức priority 0..15 trung lập (`Idle` 0, `Low` 4, `Normal` 8, `High` 12, `Realtime` 15; `raise/lower` tương đối) | SRC-07 | `platform/osal/include/osal/types.hpp` | `memory_priority_levels` | `VER` |
| PRIO-002 | Ánh xạ tuyến tính sang priority native từng port: FreeRTOS (`configMAX_PRIORITIES`), uC/OS-II (`EP_UCOS2_PRIO_TOP` + `STRIDE`, kiểm tra lúc build), uC/OS-III (`EP_UCOS3_PRIO_TOP`), POSIX realtime `SCHED_FIFO` = `EP_POSIX_RT_PRIO_BASE` + `EP_POSIX_RT_PRIO_STEP` × mức (mặc định 10..85, bật bằng `EP_POSIX_REALTIME`) | SRC-07, SRC-10, SRC-11 | `platform/osal/ports/*/` | POSIX: firmware A53/QNX-shim; uC/OS: kernel giả lập (VER-005) | `SIM` |
| PRIO-003 | Priority/stack mọi task do sản phẩm cấu hình: `TaskPlan` (app, leds, output_supervisor), `add_channel(..., priority, stack)`, cấu hình worker, nhóm cảm biến, output | SRC-07 | `app/runtime/include/app/product.hpp` | `memory_thread_registry_reports_config`, `module_worker_start_failure_and_config_override` | `VER` |
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
| OSAL-007 | Vùng găng port uC/OS-II dùng macro chuẩn `OS_ENTER_CRITICAL/OS_EXIT_CRITICAL` (biến `cpu_sr`) – không gọi `OS_CPU_SR_Save/Restore` (tên riêng của port ARM; port Nios II không có) | SRC-17 | `platform/osal/ports/ucos2/osal_ucos2.cpp` | Biên dịch với `ucos_ii.h`/`os_cpu.h` thật của Nios II; toàn bộ test (81) trên kernel uC/OS-II giả lập | `SIM` |

## 7. HAL – HAL

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| HAL-001 | Interface ngoại vi trên chip: `IGpio`, `IUart`, `II2c` (`write_read` có repeated start), `INvStorage`; `IBoard` ánh xạ ID logic → thiết bị | SRC-01 | `platform/hal/include/hal/` | Mock trong `tests/mocks.hpp` | `VER` |
| HAL-002 | Mã dùng chung theo OS chọn bằng danh sách `EP_HAL_COMMON`: `posix` (console stdio, UART termios, FileStorage), `linux` (`/dev/i2c-N`, GPIO chardev v2, tự lùi v1 – HAL-019), `qnx` (I2C `devctl`, GPIO MMIO), `mmio` (header-only) | SRC-10, SRC-11 | `platform/hal/ports/common/` | `test_posix_hal.cpp`, `test_linux_hal.cpp`, `test_qnx_hal.cpp`, `test_mmio_gpio.cpp` | `VER` |
| HAL-003 | Thiết bị vắng mặt không làm hỏng khởi động: cảnh báo, ngoại vi đó trả lỗi khi dùng | SRC-10 | Board port Linux/QNX | `posix_missing_uart_fails_cleanly`, `linux_missing_i2c_and_gpio_fail_cleanly`, `qnx_missing_i2c_fails_cleanly`; firmware A53/QNX-shim | `VER` |
| HAL-004 | UART termios raw theo POSIX (không dùng `cfmakeraw`), baud/data bits/parity/stop bits áp dụng đúng | SRC-11 | `common/posix/posix_hal.cpp` | `posix_termios_uart_over_pty`, `posix_termios_uart_applies_line_settings` (+ hook `tcsetattr`) | `VER` |
| HAL-005 | FileStorage: vùng chưa ghi đọc ra 0xFF; ghi có `fsync`; ngoài vùng → `OutOfRange` | SRC-10 | `common/posix/posix_hal.cpp` | `posix_file_storage_erased_semantics` | `VER` |
| HAL-006 | GPIO thanh ghi (MMIO) tổng quát: `out/in/dir/dir_n/oen/set/clr/bit/active_low`, cấu hình bằng chuỗi; đọc-sửa-ghi có khóa | SRC-11 | `common/mmio/mmio_gpio.hpp` | `test_mmio_gpio.cpp` (4 test), fuzz (VER-008) | `VER` |
| HAL-007 | QNX: I2C qua `devctl` `DCMD_I2C_SEND/RECV/SENDRECV`; GPIO qua `mmap_device_memory`; thiếu quyền báo rõ `PROCMGR_AID_MEM_PHYS` | SRC-11 | `common/qnx/qnx_hal.cpp`, `ports/qnx/qnx_board.cpp` | Lớp giả lập QNX (VER-006) | `SIM` |
| HAL-008 | Zynq UltraScale+ APU (Linux): mặc định ZCU102, cấu hình lúc chạy `EP_ZYNQMP_*` | SRC-10 | `ports/zynqmp_linux/` | Firmware dưới QEMU | `SIM` |
| HAL-009 | Zynq UltraScale+ RPU (R5F): XGpioPs, XUartPs, XIicPs (polled, repeated start, I2C mux), EEPROM 24xx; flow classic + SDT | SRC-10 | `ports/zynqmp_r5/` | Biên dịch + link với driver embeddedsw | `CMP` |
| HAL-010 | STM32F4: STM32Cube HAL | SRC-01 | `ports/stm32f4/` | – | `REF` |
| HAL-011 | Cấu hình thiết bị lúc chạy bằng biến môi trường (board Linux/QNX), không cần build lại | SRC-10, SRC-11 | `env_or()` | Firmware QNX-shim, A53 | `SIM` |
| HAL-012 | GPIO trên core PIO của Intel: truy cập `IORD/IOWR` (bỏ qua data cache); PIO chỉ-xuất dùng bản sao chung cho mọi chân cùng PIO (không đọc-sửa-ghi từ DATA); `Output/Input/Bidir/InOut`, OUTSET/OUTCLEAR, active-low; vùng găng bằng tắt ngắt (không spinlock) | SRC-17 | `platform/hal/ports/nios2/nios2_pio.hpp` | `test_nios2_pio.cpp` (3 test), mutation 5/5 | `VER` |
| HAL-013 | `FlashRegionStorage`: INvStorage trên vùng flash NOR – vùng phải căn biên block (block đếm từ đầu **vùng** flash, hỗ trợ flash nhiều cỡ block), xóa theo block, chỉ lập trình + đọc lại kiểm tra | SRC-17 | `platform/hal/ports/common/flash/flash_region_storage.hpp` | `test_flash_region_storage.cpp` (4 test, gồm ParamStore lưu/nạp lại) | `VER` |
| HAL-014 | `FdUart`: UART qua file descriptor không chặn chỉ dùng `open/read/write` (HAL Nios II không có `poll`/termios); chờ 1 ms bằng `hal::wait_ms` | SRC-17 | `platform/hal/ports/common/fdio/` | `fd_uart_nonblocking_io_and_timeouts_over_pty` | `VER` |
| HAL-015 | Board port Nios II: console JTAG UART, aux UART, LED/nút PIO, I2C Intel Avalon **hoặc** OpenCores (repeated start, khóa bus chờ có nhường CPU), NV trên HAL flash API (EPCQ/on-chip/CFI); mặc định lấy từ `system.h`, ghi đè bằng `EP_NIOS2_*` | SRC-17 | `platform/hal/ports/nios2/nios2_board.cpp` | Biên dịch với header thật (cả 2 loại I2C) + chạy firmware trên HAL giả lập (VER-012) | `SIM` |
| HAL-016 | Giới hạn: driver OpenCores I2C của Intel chờ vòng không timeout (bus kẹt treo task gọi); `reset()` Nios II chỉ là reset phần mềm (nhảy vector reset) | SRC-17 | Tài liệu | – | `RULE` |
| HAL-017 | Tài nguyên dạng mảng trên `IBoard` (mặc định rỗng – board cũ không phải sửa): `serial(i)`, `digital_input(i)`, `digital_output(i)`, `hdlc(i)`; `IHdlcPort` (cổng SDLC/HDLC theo khung: bitrate, mã hóa, clock, FCS; trạng thái khung crc/overrun/abort) | SRC-21 | `platform/hal/include/hal/board.hpp`, `hal/hdlc.hpp` | Cổng HDLC giả trong `hdlc_imu_blocking_read_with_sensor_scheduler`; board `linux_sim`, `linux_cpu` | `VER` |
| HAL-018 | Port `linux_cpu` (CPU Linux của bo bring-up): tty termios + RS485 (`:rs485`, `:rs485-echo` qua `TIOCSRS485`), GPIO chardev (bias, active-low), HDLC qua SyncLink (`MGSL_IOCSPARAMS` + `N_HDLC`), `nhdlc:`, `chardev:`, `packet:` (AF_PACKET); ánh xạ thiết bị bằng `EP_BOARD_CONF` / `EP_SERIAL<i>`…; thiết bị vắng chỉ cảnh báo | SRC-21 | `platform/hal/ports/linux_cpu/` | Serial qua pty (VER-014); GPIO/HDLC chưa chạy thiết bị thật | `SIM` |
| HAL-019 | GPIO chardev tương thích header/kernel cũ: có `GPIO_V2_GET_LINE_IOCTL` → v2 (mức output đặt ngay khi xin line; kernel trả `ENOTTY` → lùi v1 lúc chạy); chỉ có v1 → `GPIOHANDLE_*` (bias khi header ≥ 5.5); ép v1 bằng `EP_GPIO_UAPI_V1_ONLY` | SRC-24 | `platform/hal/ports/common/linux/gpio_uapi.hpp` (dùng bởi `GpioLine`, `LinuxGpio`) | Build `linux_cpu` với `linux/gpio.h` đã xóa phần v2 (code cũ tái hiện lỗi, code mới sạch + toàn bộ test); build ép v1 + test; chưa chạy chip GPIO thật | `CMP` |
| HAL-020 | Phần cứng bring-up mô phỏng trong `linux_sim`: 12 serial (`EP_SIM_SERIAL<i>` = `udp:` / `gnss[:ms]` / `loop` / `broken` / `silent`), máy thu GNSS giả (GGA+RMC, trả lời lệnh, đổi baud), DI nối vòng DO (`EP_SIM_DI_STUCK`), 2 IMU HDLC 400/100 Hz (`EP_SIM_IMU_OFF`); bộ đệm cố định (không heap lúc chạy) | SRC-21 | `platform/hal/ports/linux_sim/sim_bringup.*` | Firmware `hw_bringup` mô phỏng (ASan/TSan) + công cụ GCS | `IMP` |

## 8. Device – DEV

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| DEV-001 | Interface theo loại thiết bị + driver ngoài chip: `ITemperatureSensor`, `Tmp102` (I2C) | SRC-04 | `platform/device/` | Firmware (TMP102 giả trên QNX-shim) | `IMP` |
| DEV-002 | Báo lỗi bằng `core::Status`; lớp trên quyết định log/phát event | SRC-04 | `platform/device/` | Review | `RULE` |
| DEV-003 | `Tmp102::apply(Tmp102Config)`: tốc độ chuyển đổi (CR 0.25/1/4/8 Hz), chế độ mở rộng 13-bit (EM, tới 150 °C), shutdown (SD); read-modify-write thanh ghi cấu hình, giữ bit cảnh báo; chỉ đổi cách giải mã khi chip nhận cấu hình | SRC-19 | `platform/device/src/tmp102.cpp` | `tmp102_apply_rate_extended_shutdown_and_decoding` (+ mutation) | `VER` |
| DEV-004 | Nhóm cổng số: `IDigitalInputs` / `IDigitalOutputs` (bit i = kênh i, ≤ 32), `GpioInputBank` (đảo mức theo mask), `GpioOutputBank` (ghi theo mask, thread-safe) | SRC-21 | `platform/device/include/device/digital_io.hpp` | `digital_io_banks` | `VER` |
| DEV-005 | Giải mã khung IMU theo bảng `ImuFormat` (offset, độ rộng, endian, LSB, gia số/tốc độ, byte nhận dạng, checksum Sum16/NegSum16/CRC X.25); định dạng tham khảo `ln200`, `hg1700` | SRC-21 | `platform/device/include/device/imu.hpp`, `src/imu_format.cpp` | `imu_decode_formats` (khung mẫu tự dựng – **chưa đối chiếu ICD**, DEF-016) | `VER` |
| DEV-006 | Driver `HdlcImu` không thread: `read(sample, timeout)` chờ một khung hợp lệ (FCS phần cứng hoặc phần mềm), bỏ khung lỗi, thống kê khung/lỗi/tần số; dùng với `BlockingSensorTask` | SRC-21, DD-05 | `platform/device/include/device/hdlc_imu.hpp` | `hdlc_imu_driver_counters_and_software_fcs`, `hdlc_imu_blocking_read_with_sensor_scheduler` | `VER` |
| DEV-007 | Bộ giải mã NMEA 0183 (`NmeaParser`): nhận từng byte, bắt buộc checksum, GGA + RMC mọi talker, số nguyên (lat/lon 1e-7 độ, giờ ms, ngày, thời gian UNIX), chịu rác nhị phân / câu quá dài | SRC-22 | `platform/device/include/device/nmea.hpp` | `nmea_gga_rmc_reference_sentences`, `nmea_hemispheres_talkers_and_invalid_fix`, `nmea_robustness` | `VER` |
| DEV-008 | `NmeaFixParser`: gom câu NMEA thành **một fix mỗi epoch** – Parser của `StreamSensorTask` (`bool feed(byte, GnssFix&)`); GGA ra fix, RMC chỉ ra fix khi không có GGA (chưa thấy GGA, hoặc ≥ 2 RMC từ GGA cuối) – không cần đồng hồ; `GnssFix` là kiểu trivial (đi qua `Topic`) | SRC-26, SNS-004 | `platform/device/include/device/nmea.hpp` | `nmea_fix_parser_one_fix_per_epoch`, `nmea_fix_parser_as_stream_sensor_task` | `VER` |

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
`kSensorRecovered` (+3), `kOutputSafe` (+4), `kOutputResumed` (+5), `kParamsLoaded` (+6) · `app::events::kTemperature` (+1),
`kSensorFault` (+2), `kSensorRecovered` (+3), `kAlarmStateChanged` (+4), `kAlarmAckRequest` (+5), `kTemperatureBatch` (+6).

## 11. Services – SVC

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| SVC-001 | `ParamStore`: đăng ký tham số (id, tên, mặc định, min/max, đơn vị), kiểm tra miền, phát `kParamChanged`, lưu NV có magic/version/CRC32; nạp bỏ qua giá trị ngoài miền/tham số lạ; sau mỗi `load()` phát `kParamsLoaded` (kèm kết quả) vì nạp đặt giá trị im lặng | SRC-01, SRC-19 | `services/src/param_store.cpp` | `param_store_validation_and_events`, `param_store_persistence_with_crc`, `sensor_param_binding_applies_saved_and_changed_params_immediately` | `VER` |
| SVC-002 | `ParamStore` tuần tự hóa save/load (CLI + Modbus cùng lưu không hỏng NV; save sau luôn ghi giá trị mới hơn) | SRC-10 | `param_store.cpp` (`nv_mtx_`) | `param_store_concurrent_saves_keep_nv_valid` (+ mutation) | `VER` |
| SVC-003 | `CommandService`: bảng lệnh tĩnh, phản hồi qua `CommandReply`, tách tham số tại chỗ (không `strtok_r`), `help`; handler chạy trên thread kênh gọi | SRC-01, SRC-10 | `command_service.cpp` | `cli_protocol_line_editing_and_dispatch` | `VER` |
| SVC-004 | Lệnh `param list/get/set/save/reset` | SRC-01 | `param_cli.cpp` | Firmware mô phỏng | `IMP` |
| SVC-005 | `Logger`: mức log, nhiều sink (≤ `EP_CFG_LOG_SINKS`), loại bỏ mức thấp lúc biên dịch (`EP_CFG_LOG_LEVEL`) | SRC-01 | `logger.cpp` | Gián tiếp qua các test | `IMP` |
| SVC-006 | `LedService`: mặt tiền cho nghiệp vụ (`set/get` theo `LedId`, mẫu Off/On/BlinkSlow/BlinkFast/Heartbeat); bên dưới là **một** `LedOutput` loại Indicator, thread 50 ms cho mọi LED; nghiệp vụ không biết chân GPIO | SRC-01, SRC-18 | `services/src/led_service.cpp` | `test_led_output.cpp` (3 test); firmware QNX-shim/Nios-shim (đếm cạnh thanh ghi) | `VER` |
| SVC-007 | Lệnh `sys info / modules / sensors / outputs / tasks / buffers / log / reboot / quit` | SRC-01 | `app/runtime/src/system.cpp` | Firmware mô phỏng | `IMP` |
| SVC-008 | `SerialPortBank` (`ISerialPorts`): mỗi cổng một thread, chế độ đổi lúc chạy Off / Loopback (khung thử 18 B có seq + CRC; PASS/TIMEOUT/MISMATCH/OPEN-ERR; nối chéo cặp cổng) / Stream (ring buffer, ghi từ thread khác, đổi baud) / External; priority/stack theo cổng | SRC-21 | `services/include/services/serial_port_bank.hpp` | `serial_bank_loopback_stream_and_pairs`, `serial_bank_cross_pair_pass` | `VER` |
| SVC-009 | ~~`GnssReceiver` (`IGnssReceiver`): thread riêng trên cổng Stream, giải mã NMEA, tap thô, hàng đợi lệnh~~ – **đã bỏ** (SRC-27), thay bằng SVC-011 + `StreamSensorTask<NmeaFixParser>` (DEV-008) + worker `gnss_tx` của `GnssBringup` | SRC-22, SRC-23, SRC-27 | – | – | `DROP` |
| SVC-010 | `DigitalOutputs`: nhóm cổng số ra (`device::IDigitalOutputs`) là **một** `Output<{mask, value}>` loại Actuator, chế độ trực tiếp; an toàn = tắt mọi kênh (khởi động, disarm/failsafe, deadman, tắt máy); chưa arm → `Busy`; `set()` từ chối kênh không tồn tại (`InvalidArg`), đếm lỗi ghi | SRC-25, DD-06, ACT-003 | `services/include/services/digital_outputs.hpp` | `digital_outputs_is_a_safe_actuator` | `VER` |
| SVC-011 | `SerialStreamPort`: `hal::IUart` trên một cổng Stream của `ISerialPorts` – `read()` chờ tối đa timeout (thăm dò), trả 0 **ngay** khi cổng không còn Stream (task cảm biến nghỉ, không tranh byte với loopback); `write()` chỉ khi Stream; `open()` đặt Stream + baud; tap byte thô một-đọc | SRC-27, SNS-004 | `services/include/services/serial_stream_port.hpp` | `serial_stream_port_reads_taps_and_yields_to_loopback`, `gnss_sensor_task_over_serial_stream_port` | `VER` |

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
| SNS-011 | Cấu hình cảm biến **áp dụng ngay** trên thread gọi: `SensorConfigurator<Device, Config, &Device::apply>::configure()` trả kết quả đồng bộ, không chờ nhịp lấy mẫu (chu kỳ 1000 ms vẫn áp dụng trong ~0 ms) | SRC-19, DD-09 | `sensor_scheduler.hpp` | `sensor_config_applies_immediately_not_at_next_tick` | `VER` |
| SNS-012 | Cấu hình và lấy mẫu **không bao giờ** chạm thiết bị cùng lúc (khóa thiết bị theo task, `SensorTaskBase`) | SRC-19, DD-09 | `sensor_scheduler.hpp/.cpp` | `sensor_config_never_interleaves_with_a_read` (+ mutation) | `VER` |
| SNS-013 | Task chặn / dòng byte chờ theo lát `kSliceMs` = 10 ms, nhả khóa giữa các lát → cấu hình chen vào trong ≤ 1 lát dù timeout dài; driver trả `Timeout` sớm hơn timeout được giao thì vòng lát dừng (không quay bận) | SRC-19 | `sensor_scheduler.hpp` | `sensor_config_reaches_blocking_task_within_a_slice` (1 ms với timeout 500 ms; + mutation), `sensor_blocking_fast_failure_backs_off` | `VER` |
| SNS-014 | Công bằng hai chiều: luồng lấy mẫu nhường khi có cấu hình đang chờ; cấu hình liên tục nhường cho luồng lấy mẫu đang chờ (không bên nào bị bỏ đói) | SRC-19 | `sensor_scheduler.cpp` | `sensor_config_never_interleaves_with_a_read` (cấu hình liên tục 100 ms, `apply` 1 ms: ≥ 10 mẫu – thực đo 21–23; bỏ nhường: 0–1), `sensor_config_reaches_blocking_task_within_a_slice` (bỏ nhường: 54 ms) | `VER` |
| SNS-015 | Chờ khóa thiết bị có giới hạn (`lock_timeout_ms`, mặc định 50 ms): quá hạn → `Busy`, đếm `failures()`; giữ cấu hình hiện hành (`current()`), đếm `applied()` | SRC-19 | `sensor_scheduler.hpp` | `sensor_config_busy_when_device_held_too_long_and_settle_discard` | `VER` |
| SNS-016 | Settling: bỏ `settle_samples` mẫu kế tiếp sau khi đổi cấu hình (`discard_next`), không publish | SRC-19 | `sensor_scheduler.hpp/.cpp` | `sensor_config_busy_when_device_held_too_long_and_settle_discard` (+ mutation) | `VER` |
| SNS-017 | Đổi chu kỳ có hiệu lực ngay: `set_period_ms()` đánh thức scheduler; lần kế tiếp = lần trước + chu kỳ **mới** | SRC-19 | `sensor_scheduler.cpp`, `periodic_schedule.hpp` | `sensor_period_change_takes_effect_immediately` (1000 → 10 ms: mẫu kế tiếp sau ~5 ms; + 2 mutation) | `VER` |
| SNS-019 | `ApplyMode::NextSample`: `configure(cfg, NextSample)` chỉ gửi yêu cầu, trả ngay, không đụng thiết bị; luồng lấy mẫu ghi ở đầu lần lấy mẫu kế tiếp, trước lần đọc (lần đọc đó đã thấy cấu hình mới); trước khi scheduler chạy → ghi ở mẫu đầu tiên; settling áp dụng như Immediate | SRC-20, DD-10 | `sensor_scheduler.hpp` | `sensor_config_next_sample_waits_for_the_tick_and_runs_on_sampler` (chu kỳ 200 ms: ghi sau 179–180 ms, trên luồng lấy mẫu), `sensor_config_next_sample_coalesces_is_superseded_and_settles` (+ mutation) | `VER` |
| SNS-020 | Gộp yêu cầu (mới nhất thắng); trộn với Immediate theo thứ tự gửi (số thứ tự): Immediate thành công hủy NextSample gửi trước, không hủy NextSample gửi sau kể cả khi gửi lúc Immediate đang chờ khóa; ghi lỗi ở nhịp báo qua `last_status()`/`failures()`, không thử lại, giữ cấu hình cũ; mỗi task một bộ cấu hình chờ nhịp (bộ thứ hai → `Busy`) | SRC-20 | `sensor_scheduler.hpp/.cpp` | `sensor_config_next_sample_coalesces_is_superseded_and_settles` (+ 4 mutation) | `VER` |
| SNS-021 | Task chặn / dòng byte: nhịp = lần `sample()` kế tiếp (có dữ liệu hoặc hết timeout), không phải lát 10 ms | SRC-20, DD-10 | `sensor_scheduler.hpp` | `sensor_config_next_sample_on_blocking_task_waits_for_the_next_sample_not_a_slice` (+ mutation) | `VER` |
| SNS-018 | Nối tham số → cảm biến: `SensorParamBinding` (`kParamsLoaded` + `kParamChanged` trong dải id → `configure(cfg, mode)`), `SensorPeriodBinding` (chu kỳ theo tham số); listener trực tiếp. `Immediate`: chặn có giới hạn (`lock_timeout_ms` + thời gian `apply`) – chỉ nối thiết bị có `apply()` ngắn; `NextSample`: không chặn | SRC-19, SRC-20 | `services/include/services/sensor_param_binding.hpp` | `sensor_param_binding_applies_saved_and_changed_params_immediately`, `sensor_param_binding_next_sample_does_not_block_the_publisher` (0 ms khi thiết bị bận 200 ms) (+ mutation) | `VER` |

## 13. Output (bộ chấp hành & chỉ báo) – ACT

> Mã nhóm giữ là `ACT` để không gãy truy vết; tên trong code đã đổi `Actuator` → `Output` (SRC-18).

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| ACT-001 | Output tự chủ `Output<Command>`: tự xử lý phần cứng; bên ngoài chỉ `command()`. Vòng điều khiển nội bộ (vd. PID servo, mẫu LED) nằm trong output | SRC-15, SRC-18 | `services/include/services/output.hpp` | `output_servo_runs_its_own_pid_and_converges` (mô hình động cơ: hội tụ 30.32°/30°, vọt lố 2.9 %) | `VER` |
| ACT-002 | Hai chế độ: **trực tiếp** (`update_period_ms` = 0, ghi ngay trên thread người gọi) và **thread riêng** (lưu lệnh mới nhất, `on_update()` theo lịch không trôi) | SRC-15 | `output.hpp/.cpp` | `output_direct_mode_arm_and_command`, `output_servo_runs_its_own_pid_and_converges` | `VER` |
| ACT-003 | Trạng thái an toàn: ngay sau `product::setup()`; khi tắt máy mọi output (kể cả LED) dừng **đầu tiên** sau kênh; lớp con chỉ viết `write_safe()` | SRC-15, SRC-18 | `system.cpp`, `OutputHub` | `output_hub_supervisor_detects_stall_and_deadman_with_real_threads`, `led_indicator_survives_failsafe_and_turns_off_at_shutdown` | `VER` |
| ACT-004 | Arm/disarm (Actuator): chỉ xuất khi đã arm; lệnh khi chưa arm bị từ chối (`Busy`) | SRC-15 | `output.cpp` | `output_direct_mode_arm_and_command` | `VER` |
| ACT-005 | Deadman `command_timeout_ms`: lệnh quá hạn → an toàn; lệnh mới → tự xuất lại; kiểm tra cả trên thread riêng lẫn supervisor | SRC-15 | `output.cpp` | `output_deadman_forces_safe_and_new_command_resumes`, `output_threaded_deadman_runs_on_own_thread` | `VER` |
| ACT-006 | Thread riêng của **Actuator** kẹt quá `stall_timeout_ms` (mặc định max(4 × chu kỳ, 20 ms)) → thread giám sát độc lập ép an toàn và **chốt** tới khi arm lại; cảnh báo nếu priority giám sát ≤ priority thread actuator | SRC-15 | `OutputHub` | `output_hub_supervisor_detects_stall_and_deadman_with_real_threads` | `VER` |
| ACT-007 | Sau khi `force_safe()` trả về, không có lần ghi giá trị khác an toàn nào (kiểm tra quyền và ghi phần cứng chung một vùng khóa) | SRC-15 | `output.hpp/.cpp` | `output_direct_no_output_after_force_safe_returns` (stress, + mutation) | `VER` |
| ACT-008 | Sự kiện `kOutputSafe` (kèm lý do) / `kOutputResumed`; `sys outputs` (loại, chế độ, trạng thái, thống kê, giám sát) | SRC-15, SRC-18 | `output.cpp`, `system.cpp` | Test ACT-004/005 kiểm tra event; firmware mô phỏng | `VER` |
| ACT-009 | `OutputHub` khởi động lỗi → mọi output về an toàn, không còn thread | SRC-15 | `output.cpp` | `output_hub_start_failure_leaves_everything_safe` | `VER` |
| ACT-010 | Output nhiều kênh cần đồng bộ (motor) dùng một output với lệnh nhiều kênh | DD-06 | Hướng dẫn; `LedOutput` là ví dụ | – | `RULE` |
| ACT-011 | Giới hạn: thread kẹt **đang ghi phần cứng** thì lần ghi đó không ngắt được; về an toàn ở chu kỳ kế tiếp. Motor cần watchdog/enable phần cứng độc lập | SRC-15 | Tài liệu | Test ACT-006 (phần "tỉnh lại") | `RULE` |
| ACT-012 | Phân loại `OutputClass`: **Actuator** (mặc định: arm, chốt khi kẹt, `disarm_all()` tắt) / **Indicator** (luôn được xuất, `disarm_all()`/failsafe **không** tắt, không giám sát kẹt); cả hai về an toàn khi tắt máy | SRC-18, DD-08 | `output.hpp/.cpp` | `output_classes_failsafe_and_stall_policy_differ`, `led_indicator_survives_failsafe_and_turns_off_at_shutdown` | `VER` |
| ACT-013 | `OutputHub` chỉ tạo thread giám sát khi có output cần giám sát (actuator hoặc deadman) – sản phẩm chỉ có LED không tốn thêm thread/stack | SRC-18 | `output.cpp` | `led_indicator_survives_failsafe_and_turns_off_at_shutdown` (đếm thread); firmware thermo_pro: 7 thread, pool 23552 B như trước | `VER` |
| ACT-014 | LED là một output loại Indicator (`LedOutput`, một thread 50 ms cho mọi LED, lệnh = mẫu của từng LED); `LedService` giữ API cũ cho nghiệp vụ; hai nơi đặt hai LED khác nhau không ghi đè nhau | SRC-18 | `services/include/services/led_service.hpp` | `led_output_is_an_indicator_driving_all_leds_in_one_frame`, `led_patterns_levels` | `VER` |

## 14. Application runtime – APP

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| APP-001 | `IModule`: `on_init/on_start/on_stop/on_event/on_tick/worker()`; bảng thread: init/start/stop + event/tick trên thread "app", job/worker_tick trên worker, `on_command` trên thread kênh | SRC-01, SRC-12 | `app/runtime/include/app/module.hpp` | `module_manager_dispatches_events_and_ticks` | `VER` |
| APP-002 | `ModuleManager` (Active Object): nhận mọi event qua `kAny` → hàng đợi `EP_CFG_APP_QUEUE_DEPTH` (`kNoWait`, đầy thì bỏ + đếm); giao cho mọi module theo thứ tự đăng ký; module không cần khóa | SRC-01 | `module_manager.cpp` | `module_manager_dispatches_events_and_ticks`, `buffer_lifecycle_through_module_manager` | `VER` |
| APP-003 | `on_tick` theo `tick_period_ms()` (0 = không tick), đổi được lúc chạy | SRC-01 | `module_manager.cpp` | `module_manager_dispatches_events_and_ticks` | `VER` (xem DEF-002) |
| APP-004 | `ModuleWorker<Depth>` tùy chọn: thread riêng, hàng đợi tĩnh, `post()` không chặn (đầy → đếm), `on_job`, `on_worker_tick` lịch không trôi, đổi chu kỳ lúc chạy; khởi động sau mọi `on_start`, dừng trước `on_stop`; sản phẩm ghi đè cấu hình | SRC-12 | `module_worker.hpp/.cpp` | `test_module_worker.cpp` (6 test) | `VER` |
| APP-005 | `on_command` không sửa trạng thái module trực tiếp: đọc atomic/ParamStore hoặc publish event | SRC-01 | Quy ước (vd. `AlarmController`) | Review | `RULE` |
| APP-006 | `StateMachine` dùng hàm thành viên làm trạng thái, có Entry/Exit | SRC-01 | `state_machine.hpp` | Gián tiếp qua `test_alarm_controller.cpp` | `VER` |
| APP-007 | `System::init`: board → services → link → device → LED vào OutputHub → product setup → output an toàn → `on_init` → nạp NV → cảm biến → output → module → kênh → `kSystemStarted` → `end_startup()`. Dừng: kênh → output (kể cả LED) → cảm biến → module | SRC-01, SRC-07 | `system.cpp` | Firmware mô phỏng, các test start-failure | `VER` |

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
| COMM-008 | MAVLink v2 (`ep_proto_mavlink2`): khung v2 (msgid 24 bit, cắt byte 0 cuối), nhận cả v1, bỏ qua chữ ký; parser bộ đệm quét lại từng byte (STX giả không làm mất khung); HEARTBEAT, PARAM_* ↔ ParamStore, COMMAND_LONG/ACK, `MAV_CMD_PREFLIGHT_STORAGE`, giám sát GCS; bản tin riêng qua `IDialect` | SRC-03, SRC-21, DD-12 | `communication/mavlink2/` | `test_mavlink2.cpp` (4 test, khung mẫu từ pymavlink), VER-013 | `VER` |
| COMM-009 | Dialect `bringup` (62000–62005: BRINGUP_STATUS, SERIAL_LOOPBACK_STATUS, IO_STATUS, OUTPUT_SET, IMU_DATA, GNSS_FIX) + GPS_RAW_INT, SERIAL_CONTROL, COMMAND_LONG `MAV_CMD_COMPONENT_ARM_DISARM` (arm/disarm DO; `IO_STATUS.output_state` là trường mở rộng v2): định nghĩa trên dây ở `communication/`, nguồn XML ở `tools/mavlink/bringup.xml`; CRC_EXTRA/độ dài C++ đối chiếu XML trong ctest | SRC-21, SRC-22, DD-11 | `communication/mavlink2/dialects/bringup/`, `products/hw_bringup/mavlink_dialect.cpp` | `bringup_dialect_maps_events_and_messages`, ctest `mavlink_dialect_check`, VER-013 | `VER` |

## 16. Utils – UTIL

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| UTIL-001 | CRC32, CRC16-CCITT, CRC16-Modbus, CRC16-MCRF4XX (MAVLink, có CRC_EXTRA); tính nối tiếp được | SRC-02 | `utils/include/utils/crc.hpp` | `utils_crc_known_vectors` | `VER` |
| UTIL-002 | Định dạng fixed-point không dùng `printf %f` | SRC-02 | `fixed_point.hpp` | `utils_format_fixed` | `VER` |
| UTIL-003 | `RingBuffer<T,N>` SPSC lock-free (ISR → thread) | SRC-02 | `ring_buffer.hpp` | `utils_ring_buffer_spsc` | `VER` |
| UTIL-004 | CRC-16/X-25 (FCS-16 HDLC/SDLC) | SRC-21 | `crc.hpp` (`crc16_x25`) | `utils_crc_known_vectors` | `VER` |

## 17. Target & toolchain – TGT

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| TGT-001 | `linux_sim`: mô phỏng trên PC (`--run-for`, Ctrl+C, NVRAM file) | SRC-01 | `cmake/targets/linux_sim.cmake`, `platform/target/linux_sim/` | Toàn bộ test + firmware | `VER` |
| TGT-002 | STM32F407 + FreeRTOS / uC/OS-II / uC/OS-III (CubeMX) | SRC-01, SRC-06 | `cmake/targets/stm32f4_*.cmake` | Chưa biên dịch với ARM toolchain; OSAL uC/OS chạy trên kernel giả lập | `REF` |
| TGT-003 | Zynq UltraScale+ APU – `zynqmp_a53_linux` (PetaLinux/Yocto), `SCHED_FIFO`, `mlockall` | SRC-10 | `cmake/targets/zynqmp_a53_linux.cmake` | Cross build; test + firmware dưới qemu-aarch64 | `SIM` |
| TGT-004 | Zynq UltraScale+ RPU – `zynqmp_r5_freertos` (BSP Vitis) | SRC-10 | `cmake/targets/zynqmp_r5_freertos.cmake` | Biên dịch + link với driver embeddedsw (`xparameters.h`/`FreeRTOSConfig.h` giả lập); thiếu symbol nội bộ BSP là dự kiến | `CMP` |
| TGT-005 | QNX Neutrino 7.1 / 8.0 – `qnx` (aarch64le, x86_64), mọi board có BSP QNX | SRC-11 | `cmake/targets/qnx.cmake`, `platform/target/qnx/` | Biên dịch trên Linux với `hw/i2c.h` thật + lớp giả lập; **chưa qcc, chưa QNX thật** | `SIM` |
| TGT-006 | Toolchain: `arm-none-eabi`, `armr5-none-eabi` (đổi tiền tố bằng `EP_ARMR5_PREFIX`; cờ CPU trong target để không mất khi tự đặt `CMAKE_CXX_FLAGS`), `aarch64-linux-gnu` (+ qemu), `qnx` (`QNX_HOST`/`QNX_TARGET`, `EP_QNX_ARCH`), `nios2-elf` (`EP_NIOS2_PREFIX`) | SRC-10, SRC-11, SRC-17 | `cmake/toolchains/` | Build tương ứng (nios2-elf: chưa có toolchain) | `SIM` |
| TGT-007 | Intel Nios II + uC/OS-II – `nios2_ucos2` (BSP UCOSII): không gọi lại `OSInit()` (BSP đã gọi trước `main`), tick do BSP; cờ CPU / định nghĩa / thư viện BSP đọc từ `public.mk`; `OS_LOWEST_PRIO` thiếu → lỗi build có chỉ dẫn | SRC-17 | `cmake/targets/nios2_ucos2.cmake`, `platform/target/nios2_ucos2/` | Biên dịch bằng g++ host với header BSP thật; chạy trên kernel + HAL giả lập; **chưa nios2-elf-gcc, chưa FPGA** | `SIM` |
| TGT-008 | `linux_cpu`: CPU Linux thật của bo bring-up (`EP_HAL_COMMON` posix;linux, TCP/IP, `SCHED_FIFO` qua `EP_POSIX_REALTIME`), chạy như dịch vụ systemd | SRC-21 | `cmake/targets/linux_cpu.cmake`, `platform/target/linux_cpu/` | Build + toàn bộ test; firmware trên pty (VER-014) | `SIM` |

## 18. Sản phẩm – PROD

| Mã | Yêu cầu | Nguồn | Hiện thực | Kiểm chứng | Trạng thái |
|---|---|---|---|---|---|
| PROD-001 | `thermo_pro`: TemperatureMonitor (500 ms, lọc 4, lô 20 mẫu), AlarmController (cảnh báo 60.0 °C, cao 75.0 °C, trễ 2.0 °C, chốt), StatsReporter (worker priority thấp); kênh console (CLI/Modbus), aux (Modbus/MAVLink/CLI), tcp 5760 (MAVLink/Modbus/CLI), udp 14550 (MAVLink); pool Buffer 64×16, 512×8, 2048×4; stack pool 24 KB | SRC-03, SRC-09, SRC-12 | `products/thermo_pro/` | Toàn bộ test (cấu hình mặc định), firmware mô phỏng, pty Modbus, MAVLink TCP | `VER` |
| PROD-002 | `thermo_lite`: 2 module, chỉ CLI console, pool Buffer 128×4, stack pool 8 KB, tối đa 6 thread, log mức 2 | SRC-01 | `products/thermo_lite/` | Toàn bộ test với cấu hình lite | `VER` |
| PROD-003 | Module nghiệp vụ tái sử dụng: `TemperatureMonitor` (chu kỳ, offset, cửa sổ lọc, ngưỡng lỗi, lô mẫu qua Buffer), `AlarmController` (Normal/Warning/Alarm/Fault, trễ, chốt, xác nhận, LED) | SRC-01 | `app/business/` | `alarm_thresholds_hysteresis_and_latch`, `alarm_runtime_reconfiguration_and_fault` | `VER` |
| PROD-004 | Tham số: `0x10xx` nhiệt độ, `0x11xx` cảnh báo, `0x0200 + i` `comm.<kênh>`, `0x8000+` riêng sản phẩm | SRC-01 | `app/business/app_params.hpp`, `link_manager.hpp` | Review | `RULE` |
| PROD-005 | `hw_bringup`: bring-up phần cứng qua MAVLink v2 (serial + UDP 14560) – yêu cầu chi tiết, có mã riêng (SER/GNS/DIO/IMU/MAV…) ở `products/hw_bringup/REQUIREMENTS.md` | SRC-21, SRC-22 | `products/hw_bringup/` | Test module + dialect; firmware mô phỏng + công cụ `tools/mavlink/bringup_gcs.py`, pymavlink | `VER` |
| PROD-006 | Module bring-up tái sử dụng (chỉ chính sách): `SerialBringup` (loopback, vai trò cổng), `GnssBringup` (cửa sổ phát hiện → loopback, fix/thô/lệnh), `IoBringup` (DI; DO qua `svc::DigitalOutputs`: tự arm theo `io.do_arm`, arm/disarm theo yêu cầu, báo khi DO bị ép an toàn), `ImuBringup` (đọc `Topic` của nhóm `SensorScheduler`) | SRC-21, SRC-23 | `app/business/*_bringup/` | `serial_bringup_loopback_report_and_roles`, `gnss_bringup_*`, `io_bringup_outputs_and_inputs`, `imu_bringup_reports_samples_and_failures` | `VER` |
| PROD-007 | Tham số bring-up `0x12xx` (`ser.*` 0x1201–0x1203, `gnss.*` 0x1211–0x1213, `io.*` 0x1221–0x1222, `imu.*` 0x1231–0x1232), tên ≤ 16 ký tự (PARAM_* MAVLink) | SRC-21 | `app/business/app_params.hpp` | Review | `RULE` |

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
| VER-009 | Mutation test thủ công: cài lỗi có chủ đích, test phải bắt. Kết quả: MMIO 7/7, `QnxI2c` 4/4, worker 3/3, sensor 7/7 + 6/6, actuator 10/10, output class + LED 6/6, PIO Nios II 5/5, flash 2/2 (+1 mutation tương đương), `FdUart` 1/1, cấu hình cảm biến 9/9 + chờ nhịp 10/10 | Các tính năng mới | `VER` |
| VER-010 | Giới hạn công cụ đã biết: TSan của GCC 13 không chặn `pthread_mutex_clocklock` (`try_lock_for`) → code cần TSan kiểm tra dùng `try_lock(kNoWait)` + thử lại | VER-002 | `RULE` |
| VER-011 | Chưa kiểm chứng: CPU ARM thật (thứ tự bộ nhớ yếu), phần cứng thật mọi target, qcc, BSP Vitis hoàn chỉnh, toolchain STM32, `nios2-elf-gcc` + FPGA | – | `PLN` |
| VER-012 | Kiểm chứng Nios II không có toolchain (ngoài repo): biên dịch bằng g++ host với header thật của BSP Nios II công khai (DECA: `system.h`, HAL, driver, uC/OS-II bản Nios; driver I2C Avalon từ `hal_altera`, OpenCores từ BSP công khai), builtin nios2 thay bằng stub; chạy firmware với kernel uC/OS-II giả lập + HAL giả (thanh ghi trong bộ nhớ, flash lưu file, TMP102 trên I2C OpenCores, JTAG UART = pty) | TGT-007, HAL-015 | `SIM` |
| VER-013 | MAVLink v2: khung mẫu sinh bằng pymavlink trong test; firmware mô phỏng đọc bằng pymavlink + dialect sinh bởi mavgen (0 lỗi CRC); `tools/mavlink/mavlink_dialect.py --check` trong ctest | COMM-008, COMM-009 | `VER` |
| VER-014 | Port `linux_cpu` trên pty (ngoài repo): 12 pty, 10 có "giắc" loopback, 1 máy thu GNSS giả, 1 cầu nối GCS ↔ UDP | HAL-018, TGT-008 | `SIM` |

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
| DEF-009 | `Topic`/`SensorScheduler`/`Output` (actuator) chưa được sản phẩm nào dùng (chỉ test) – **đã xử lý** cho phần đã có sản phẩm: `hw_bringup` đọc IMU qua `SensorScheduler` (Blocking) + `Topic`, DO là `Output` loại Actuator (`svc::DigitalOutputs`, PLN-013) | SNS, ACT | Tích hợp vào sản phẩm (DEF-008, PLN-013) | **Đã sửa** trên nhánh hw_bringup (`VER`) |
| DEF-010 | Không xóa được branch trên GitHub từ môi trường phát triển (proxy chặn) – branch `feature/*` cũ còn trên remote | Vận hành | Xóa thủ công; bật "Automatically delete head branches" | `DEF` |
| DEF-011 | Nhường khóa thiết bị (SNS-013/014) dùng `sleep_ms(1)`: trên RTOS tick thô (vd. 10 ms) mỗi lần nhường là một tick → cấu hình chen vào task chặn mất tới ~1 tick + 1 lát, lấy mẫu dưới cấu hình liên tục chậm lại; chỉ đo trên host Linux (1 ms) | SNS-013/014, RTOS tick > 1 ms | Nhường bằng `yield`/sự kiện "đã lấy khóa" thay vì ngủ; đo trên target thật | `DEF` |
| DEF-012 | `ModbusRtuProtocol::poll` so `now_ms - last_rx_ms_` khi `last_rx_ms_` có thể mới hơn `now_ms` (lấy trước `on_rx`) → tràn số không dấu, khung bị tách giữa hai lần đọc có thể bị bỏ | COMM-004 | So sánh dạng cộng `now_ms < last_rx_ms_ + timeout` | **Đã sửa** trên nhánh hw_bringup (`IMP`) |
| DEF-013 | `System::shutdown()` gọi `leds_.tick()` khi thread LED còn chạy (race, TSan) | SVC-006, APP-007 | Dừng thread LED trước rồi mới `tick()` | **Đã sửa** – trên nhánh hw_bringup, rồi được thay bởi PR #10 (LED là output, `OutputHub::stop()` dừng thread rồi mới ghi trạng thái an toàn); test TSan 0 cảnh báo (`VER`) |
| DEF-014 | `SimUdpUart`: địa chỉ peer ghi ở thread đọc, đọc ở thread ghi không khóa (race, TSan) | TGT-001 | Khóa `peer_mtx_` | **Đã sửa** trên nhánh hw_bringup (`VER`) |
| DEF-015 | GPIO chardev chỉ viết cho uAPI v2 → không build được với header kernel < 5.10 (`gpio_v2_line_values was not declared`) | HAL-002, HAL-018 | Lớp tương thích v1/v2 | **Đã sửa**: HAL-019 (`CMP`) |
| DEF-016 | Định dạng IMU `ln200` / `hg1700` dựng theo tài liệu công khai, **chưa đối chiếu ICD** (thứ tự byte, offset, LSB, checksum) | DEV-005, PROD-005 | Đối chiếu ICD; khung thô luôn gửi kèm (`IMU_DATA.raw`) | `DEF` |
| DEF-017 | Hai codec MAVLink (v1 và v2) trùng phần khung/CRC/param | COMM-005, COMM-008 | Gộp thành một giao thức v2 có chế độ v1 (PLN-006) | `DEF` |
| DEF-018 | Test `mavlink_gcs_link_supervision_and_probe` trộn mốc thời gian cố định (`poll(100 + …)`) với dấu thời gian thật (`osal::now_ms()` lúc nhận HEARTBEAT) → hỏng khi các test trước chạy lâu hơn ~3 s | VER-001 | Dùng `osal::now_ms()` làm mốc | **Đã sửa** trên nhánh hw_bringup (`VER`) |

## 21. Backlog – PLN

| Mã | Nội dung | Liên quan | Trạng thái |
|---|---|---|---|
| PLN-001 | HAL: `IPwm` / output timer (DShot), `ISpi`, `IAdc`, encoder / timer capture (+ bản mô phỏng) | SRC-15, drone | `PLN` |
| PLN-002 | Parser thật cho `StreamSensorTask`: NMEA, UBX (GNSS), SBUS/CRSF (máy thu RC) – NMEA **đã có** (`device::NmeaFixParser`, DEV-008; `hw_bringup` dùng qua `SerialStreamPort`, SVC-011); còn UBX, SBUS/CRSF | SNS-004, DEV-007, DEV-008 | `PLN` |
| PLN-003 | Kích hoạt theo mẫu mới: `Topic` có thông báo (semaphore) để vòng điều khiển chạy ngay khi có mẫu IMU | MSG-005, drone | `PLN` |
| PLN-004 | Output: gom nhiều output dùng chung một thread (khi ≥ 3 output có thread cùng nhịp, RAM eo hẹp) | DD-07 | `PLN` |
| PLN-005 | Output: phân xử nhiều nguồn lệnh theo priority (failsafe > tay > tự động), giới hạn/ramp | ACT | `PLN` |
| PLN-006 | MAVLink v2; tách `communication/mavlink/messages/`; sinh code từ XML dialect; mission protocol – v2 + dialect đã có ở `communication/mavlink2` (COMM-008/009); còn: gộp v1/v2 (DEF-017), sinh code từ XML, mission | COMM-005, COMM-008, DD-02 | `PLN` |
| PLN-007 | Service bất đồng bộ kiểu yêu cầu/phản hồi (cặp event có `request_id`) | MSG-003 | `PLN` |
| PLN-008 | Mô hình executor: sản phẩm gán module vào nhiều thread ứng dụng | APP-002, SMP | `PLN` |
| PLN-009 | Giải quyết DEF-001 … DEF-008, DEF-011 | DEF | `PLN` |
| PLN-010 | Chạy trên phần cứng thật: ZCU102 (APU + RPU), board QNX, STM32F407, board FPGA có Nios II (DE10-Lite, DECA…) | VER-011 | `PLN` |
| PLN-011 | Nios II đã ngừng phát triển (Intel khuyến nghị Nios V – RISC-V): cân nhắc target Nios V (HAL tương tự, CPU RISC-V) | TGT-007 | `PLN` |
| PLN-012 | Nios II: target FreeRTOS (port `portable/GCC/NiosII`), lwIP qua Triple-Speed Ethernet | TGT-007 | `PLN` |
| PLN-013 | DO của `hw_bringup` → `Output<Cmd>` loại `OutputClass::Actuator` (direct) – **đã làm** (SVC-010): tự arm khi khởi động (`io.do_arm` = 1), arm/disarm qua `MAV_CMD_COMPONENT_ARM_DISARM` / CLI `io arm|disarm`, deadman `hw::kOutputCommandTimeoutMs` (mặc định 0 = tắt) | ACT, PROD-005, SVC-010 | `VER` |
| PLN-014 | Phát hiện máy thu GNSS chặt hơn: chỉ khi nhận câu NMEA/UBX hợp lệ (GNS-07 của hw_bringup) | DEV-008, SVC-011 | `PLN` |

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
| `test_nios2_pio.cpp` | `nios2_pio_output_only_shares_shadow_between_pins`, `nios2_pio_active_low_set_clear_and_reset_value`, `nios2_pio_input_and_bidir_direction` | HAL-012 |
| `test_flash_region_storage.cpp` | `flash_region_validates_alignment_and_device`, `flash_region_nor_semantics_and_cross_block_write`, `flash_region_spanning_regions_erases_right_blocks`, `flash_region_backs_param_store_across_restart` | HAL-013, SVC-001 |
| `test_fd_uart.cpp` | `fd_uart_nonblocking_io_and_timeouts_over_pty` | HAL-014 |
| `test_utils.cpp` (nhánh hw_bringup) | `utils_crc_known_vectors` (thêm X-25) | UTIL-004 |
| `test_mavlink2.cpp` | `mavlink_encode_matches_pymavlink`, `mavlink_parser_resync_and_errors`, `mavlink_protocol_params_commands_and_events`, `mavlink_auto_detect_on_link` | COMM-008 |
| `test_nmea.cpp` | `nmea_gga_rmc_reference_sentences`, `nmea_hemispheres_talkers_and_invalid_fix`, `nmea_robustness` | DEV-007 |
| | `nmea_fix_parser_one_fix_per_epoch`, `nmea_fix_parser_as_stream_sensor_task` | DEV-008, SNS-004 |
| `test_bringup_services.cpp` | `serial_bank_loopback_stream_and_pairs`, `serial_bank_cross_pair_pass` | SVC-008 |
| | `imu_decode_formats`, `hdlc_imu_driver_counters_and_software_fcs`, `hdlc_imu_blocking_read_with_sensor_scheduler` | DEV-005, DEV-006, HAL-017, SNS-003 |
| | `digital_io_banks` | DEV-004 |
| | `digital_outputs_is_a_safe_actuator` | SVC-010, ACT-003 |
| `test_bringup_modules.cpp` | `serial_stream_port_reads_taps_and_yields_to_loopback`, `gnss_sensor_task_over_serial_stream_port` | SVC-011, DEV-008 |
| | `serial_bringup_loopback_report_and_roles`, `gnss_bringup_absent_switches_port_to_loopback`, `gnss_bringup_connected_publishes_fix_raw_and_commands`, `io_bringup_outputs_and_inputs`, `imu_bringup_reports_samples_and_failures` | PROD-006 |
| | `bringup_dialect_maps_events_and_messages` | COMM-009, PROD-005 |
| ctest `mavlink_dialect_check` | đối chiếu CRC_EXTRA/độ dài C++ ↔ XML (16 bản tin) | COMM-009 |
| `test_sensor_scheduler.cpp` | `topic_latest_value_and_stamp`, `topic_concurrent_readers_never_see_torn_values` | MSG-005, MSG-006 |
| | `sensor_scheduler_runs_each_task_at_its_period`, `sensor_scheduler_does_not_drift_and_resets_after_overrun` | SNS-002 |
| | `sensor_scheduler_fault_and_recovery_events`, `sensor_scheduler_validation`, `sensor_task_adapts_interface_method`, `sensor_hub_real_threads_and_start_failure_cleanup` | SNS-005, SNS-009, SNS-001, SNS-007 |
| `test_sensor_blocking.cpp` | `sensor_blocking_task_must_own_its_group`, `sensor_stream_task_parses_across_reads_and_resyncs`, `sensor_blocking_drdy_fault_recovery_and_stop_latency`, `sensor_blocking_fast_failure_backs_off` | SNS-003, SNS-004, SNS-005, SNS-006 |
| `test_sensor_config.cpp` | `tmp102_apply_rate_extended_shutdown_and_decoding` | DEV-003 |
| | `sensor_config_applies_immediately_not_at_next_tick`, `sensor_config_never_interleaves_with_a_read`, `sensor_config_busy_when_device_held_too_long_and_settle_discard`, `sensor_config_reaches_blocking_task_within_a_slice` | SNS-011 … SNS-016 |
| | `sensor_period_change_takes_effect_immediately`, `sensor_param_binding_applies_saved_and_changed_params_immediately` | SNS-017, SNS-018, SVC-001 |
| | `sensor_config_next_sample_waits_for_the_tick_and_runs_on_sampler`, `sensor_config_next_sample_coalesces_is_superseded_and_settles`, `sensor_config_next_sample_on_blocking_task_waits_for_the_next_sample_not_a_slice`, `sensor_param_binding_next_sample_does_not_block_the_publisher` | SNS-019 … SNS-021, SNS-018 |
| `test_output.cpp` | `output_direct_mode_arm_and_command`, `output_direct_no_output_after_force_safe_returns`, `output_deadman_forces_safe_and_new_command_resumes`, `output_servo_runs_its_own_pid_and_converges`, `output_threaded_deadman_runs_on_own_thread`, `output_hub_supervisor_detects_stall_and_deadman_with_real_threads`, `output_hub_start_failure_leaves_everything_safe`, `output_classes_failsafe_and_stall_policy_differ` | ACT-001 … ACT-012 |
| `test_led_output.cpp` | `led_patterns_levels`, `led_output_is_an_indicator_driving_all_leds_in_one_frame`, `led_indicator_survives_failsafe_and_turns_off_at_shutdown` | SVC-006, ACT-003, ACT-012…014 |

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
| [#8](https://github.com/tractoan/EmbededFramework/pull/8) | Tài liệu yêu cầu | SRC-16 |
| [#9](https://github.com/tractoan/EmbededFramework/pull/9) | Hỗ trợ Intel Nios II + uC/OS-II; sửa vùng găng port uC/OS-II | SRC-17, OSAL-007, HAL-012…016, TGT-006/007 |
| [#10](https://github.com/tractoan/EmbededFramework/pull/10) | Gộp output: `Actuator` → `Output`, `OutputClass` Actuator/Indicator, LED là output, giám sát chỉ khi cần; `CommandOutput` → `CommandReply` | SRC-18, DD-08, ACT-001…014, SVC-003/006/007, APP-007 |
| [#11](https://github.com/tractoan/EmbededFramework/pull/11) | Cấu hình cảm biến áp dụng ngay: khóa thiết bị theo task, `SensorConfigurator`, chờ theo lát cho task chặn, đổi chu kỳ ngay, settling; `kParamsLoaded`; `SensorParamBinding`; cấu hình TMP102; tùy chọn chờ nhịp `ApplyMode::NextSample` | SRC-19/20, DD-09/10, SNS-011…021, DEV-003, SVC-001 |
| nhánh `claude/vibrant-dirac-qrpici` (chưa merge) | Sản phẩm `hw_bringup`: HAL mảng + HDLC, port/target `linux_cpu`, GPIO v1/v2, MAVLink v2 + dialect, `SerialPortBank`, GNSS là cảm biến của framework (`SerialStreamPort` + `StreamSensorTask<NmeaFixParser>`), IMU qua `SensorScheduler`, DO qua `svc::DigitalOutputs` (Output loại Actuator); sửa DEF-012…015, DEF-018 | HAL-017…020, DEV-004…008, SVC-008…011, COMM-008/009, UTIL-004, TGT-008, PROD-005…007, VER-013/014 |
