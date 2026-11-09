# Toolchain cho Cortex-R5F (RPU của Zynq UltraScale+).
# Vitis cài sẵn armr5-none-eabi-*; có thể dùng arm-none-eabi-* (Arm GNU Toolchain):
#   -DEP_ARMR5_PREFIX=arm-none-eabi-
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(EP_ARMR5_PREFIX "armr5-none-eabi-" CACHE STRING "Cross toolchain prefix")
set(CMAKE_C_COMPILER   ${EP_ARMR5_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${EP_ARMR5_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${EP_ARMR5_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${EP_ARMR5_PREFIX}objcopy)
set(CMAKE_SIZE         ${EP_ARMR5_PREFIX}size)

# Cờ CPU dùng cho try_compile của CMake; build thật lấy cờ từ cmake/targets/zynqmp_r5_freertos.cmake
# (add_compile_options) để không bị mất khi người dùng tự đặt CMAKE_CXX_FLAGS.
set(CMAKE_C_FLAGS_INIT   "-mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
