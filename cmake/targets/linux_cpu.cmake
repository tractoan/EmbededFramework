# Target: CPU nhúng chạy Linux, phần cứng THẬT (tty, GPIO chardev, SDLC/HDLC).
#   cmake -B build-cpu -DEP_TARGET=linux_cpu -DEP_PRODUCT=hw_bringup
# Biên dịch chéo (vd. ARM64):
#   cmake -B build-arm -DEP_TARGET=linux_cpu -DEP_PRODUCT=hw_bringup \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
# Ánh xạ thiết bị: platform/hal/ports/linux_cpu/board_config.hpp (ghi đè lúc chạy bằng EP_BOARD_CONF / EP_SERIAL<i>...)
# Khởi động: platform/target/linux_cpu/target_linux_cpu.cpp
# Chạy với root hoặc `setcap cap_sys_nice+ep firmware` để priority task có hiệu lực (SCHED_FIFO).
set(EP_OSAL_PORT  posix)
set(EP_HAL_PORT   linux_cpu)
set(EP_HAL_COMMON linux)    # StdioUart, FileStorage dùng chung với linux_sim / zynqmp_linux
set(EP_NET_PORT   posix)    # TCP/IP: kênh MAVLink qua UDP cho phần mềm giao diện

find_package(Threads REQUIRED)
set(EP_OSAL_PORT_LIBS Threads::Threads)

add_compile_definitions(
  EP_POSIX_REALTIME=1    # osal::Priority -> SCHED_FIFO (cần CAP_SYS_NICE; không có thì giữ SCHED_OTHER)
  EP_CFG_CLI_ECHO=0)     # console là terminal (tự echo) hoặc journal của systemd
