# Cross-compile cho Linux AArch64 (APU Cortex-A53 của Zynq UltraScale+, và các SoC ARMv8 khác).
# Với PetaLinux/Yocto SDK: `source environment-setup-cortexa72-cortexa53-xilinx-linux` rồi dùng
# file toolchain của SDK (OE_CMAKE_TOOLCHAIN_FILE) thay cho file này để có đúng sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(EP_CROSS_PREFIX "aarch64-linux-gnu-" CACHE STRING "Tiền tố toolchain")
set(CMAKE_C_COMPILER   ${EP_CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${EP_CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Chạy test/firmware trên máy x86 qua QEMU user-mode (ctest dùng biến này)
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-aarch64 -L /usr/aarch64-linux-gnu)
