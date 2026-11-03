# Nền tảng: CPU nhúng chạy Linux, phần cứng THẬT (tty, GPIO chardev, SDLC/HDLC).
#   cmake -B build-cpu -DEP_PLATFORM=linux_cpu -DEP_PRODUCT=hw_bringup
# Biên dịch chéo (vd. ARM64):
#   cmake -B build-arm -DEP_PLATFORM=linux_cpu -DEP_PRODUCT=hw_bringup \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
# Ánh xạ thiết bị: hal/ports/linux_cpu/board_config.hpp (ghi đè lúc chạy bằng EP_BOARD_CONF / EP_SERIAL<i>...)
set(EP_OSAL_PORT posix)
set(EP_HAL_PORT  linux_cpu)
set(EP_MAIN_SOURCE ${PROJECT_SOURCE_DIR}/platform/linux/main.cpp)

find_package(Threads REQUIRED)
set(EP_OSAL_PORT_LIBS Threads::Threads)

# Console là terminal (đã tự echo) hoặc journal của systemd -> tắt echo của CLI
add_compile_definitions(EP_CFG_CLI_ECHO=0)
