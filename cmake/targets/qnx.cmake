# Target: QNX Neutrino 7.1 / 8.0 trên mọi board có BSP QNX (i.MX8, Zynq UltraScale+, TI AM6x/J7,
# Raspberry Pi 4, x86_64...). Driver là resource manager của BSP (devc-ser*, i2c-*); board port
# platform/hal/ports/qnx cấu hình bằng biến môi trường lúc chạy.
#
#   source <qnx-sdp>/qnxsdp-env.sh
#   cmake -B build-qnx -DEP_TARGET=qnx -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/qnx.cmake [-DEP_QNX_ARCH=x86_64]
#   cmake --build build-qnx -j
# Copy build-qnx/firmware (và build-qnx/tests/ep_tests) lên target, chạy với quyền root (hoặc cấp ability
# PROCMGR_AID_PRIORITY, PROCMGR_AID_MEM_LOCK, PROCMGR_AID_MEM_PHYS cho GPIO).
set(EP_OSAL_PORT  posix)          # QNX là POSIX: pthread, condition variable, clock_gettime
set(EP_HAL_PORT   qnx)
set(EP_HAL_COMMON "posix;qnx")
set(EP_NET_PORT   posix)          # BSD socket (io-pkt / io-sock)

find_package(Threads REQUIRED)
set(EP_OSAL_PORT_LIBS Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
  set(EP_NET_PORT_LIBS socket)    # QNX: socket API nằm trong libsocket
endif()

add_compile_definitions(
  EP_POSIX_REALTIME=1             # osal::Priority -> SCHED_FIFO
  EP_POSIX_RT_PRIO_BASE=10        # mức 0..15 -> priority 10..85 (đổi ở đây nếu xung đột với driver BSP)
  EP_POSIX_RT_PRIO_STEP=5
  EP_CFG_CLI_ECHO=0)              # console là terminal (tự echo)
