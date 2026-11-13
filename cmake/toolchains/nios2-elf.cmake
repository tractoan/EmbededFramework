# Toolchain Nios II (nios2-elf-gcc của Nios II EDS / Quartus Prime Standard/Lite).
# Chạy cmake trong "Nios II Command Shell" (nios2_command_shell.sh) để có nios2-elf-* trong PATH,
# hoặc đặt -DEP_NIOS2_PREFIX=/đường/dẫn/nios2-elf-
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR nios2)

set(EP_NIOS2_PREFIX "nios2-elf-" CACHE STRING "Nios II cross toolchain prefix")
set(CMAKE_C_COMPILER   ${EP_NIOS2_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${EP_NIOS2_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${EP_NIOS2_PREFIX}gcc)
set(CMAKE_OBJCOPY      ${EP_NIOS2_PREFIX}objcopy)
set(CMAKE_SIZE         ${EP_NIOS2_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
