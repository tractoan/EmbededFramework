# Target: Zynq UltraScale+ MPSoC – APU (Cortex-A53) chạy Linux (PetaLinux / Yocto)
#
#   cmake -B build-zynqmp -DEP_TARGET=zynqmp_a53_linux \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake
#   cmake --build build-zynqmp -j
#   ctest --test-dir build-zynqmp          # chạy test qua qemu-aarch64 nếu build trên x86
#
# Copy build-zynqmp/firmware lên board; cấu hình thiết bị bằng biến môi trường (xem
# platform/hal/ports/zynqmp_linux/zynqmp_linux_board.cpp). Priority task dùng SCHED_FIFO:
# chạy với quyền root hoặc `setcap cap_sys_nice,cap_ipc_lock+ep firmware`.
set(EP_OSAL_PORT  posix)
set(EP_HAL_PORT   zynqmp_linux)
set(EP_HAL_COMMON "posix;linux")
set(EP_NET_PORT   posix)

find_package(Threads REQUIRED)
set(EP_OSAL_PORT_LIBS Threads::Threads)

add_compile_definitions(
  EP_POSIX_REALTIME=1    # osal::Priority -> SCHED_FIFO
  EP_CFG_CLI_ECHO=0)     # console là terminal (tty tự echo)
add_compile_options(-mcpu=cortex-a53)
