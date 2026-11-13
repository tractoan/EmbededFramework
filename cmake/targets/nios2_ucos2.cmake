# Target: Intel (Altera) Nios II + uC/OS-II, BSP sinh bằng Nios II Software Build Tools (loại "UCOSII").
#
# 1. Tạo BSP (Nios II Command Shell):
#      nios2-bsp ucosii build/bsp <thiết_kế>.sopcinfo \
#        --set ucosii.os_lowest_prio 63 --set hal.enable_c_plus_plus true --set hal.make.bsp_cflags_optimization -O2
#      make -C build/bsp
#    Bắt buộc: ucosii.os_lowest_prio >= 53 (port OSAL dùng priority native 4..52; mặc định của BSP là 20);
#    os_max_tasks đủ cho số thread của sản phẩm (sys tasks); OS_MUTEX_EN/OS_SEM_ACCEPT_EN/OS_TASK_CREATE_EXT_EN = 1
#    (mặc định của BSP). Thread được tạo bằng stack tĩnh – stack hệ thống (hal.sys_clk...) không đổi.
# 2. Build firmware:
#      cmake -B build-nios2 -DEP_TARGET=nios2_ucos2 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/nios2-elf.cmake \
#            -DEP_NIOS2_BSP_DIR=build/bsp -DEP_NIOS2_DEFINES="EP_NIOS2_I2C_OPENCORES=1;EP_NIOS2_I2C_BASE=I2C_0_BASE"
#      cmake --build build-nios2 -j
#      nios2-download -g build-nios2/firmware.elf && nios2-terminal
#
# Cờ CPU (-mhw-mul, -mno-hw-div, FPU custom...), định nghĩa (-D__ucosii__...) và tên thư viện BSP được ĐỌC TỪ
# public.mk của BSP – khớp đúng cấu hình CPU trong Platform Designer. Ghi đè: -DEP_NIOS2_CPU_FLAGS="...".
# Thiết bị (UART, PIO, I2C, flash): xem platform/hal/ports/nios2/nios2_board.cpp.
set(EP_OSAL_PORT  ucos2)
set(EP_HAL_PORT   nios2)
set(EP_HAL_COMMON "fdio")      # UART qua file descriptor HAL (không cần poll/termios)
set(EP_NET_PORT   "")          # TCP/IP: thêm port lwip (Nios II + Triple-Speed Ethernet) khi cần
set(EP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(EP_NIOS2_BSP_DIR   "" CACHE PATH "Nios II BSP directory (system.h, public.mk, lib*_bsp.a, linker.x)")
set(EP_NIOS2_DEFINES   "" CACHE STRING "Board defines for platform/hal/ports/nios2 (;-separated)")
set(EP_NIOS2_CPU_FLAGS "" CACHE STRING "Override CPU flags (default: ALT_CFLAGS -m* from public.mk)")

if(NOT EP_NIOS2_BSP_DIR OR NOT EXISTS ${EP_NIOS2_BSP_DIR}/system.h OR NOT EXISTS ${EP_NIOS2_BSP_DIR}/public.mk)
  message(FATAL_ERROR "EP_NIOS2_BSP_DIR must point to a generated Nios II BSP (system.h, public.mk)")
endif()

# ---- đọc public.mk: ALT_CFLAGS (-m...), ALT_CPPFLAGS (-D...), BSP_SYS_LIB
file(STRINGS ${EP_NIOS2_BSP_DIR}/public.mk _ep_public_mk)
set(_ep_cpu_flags "")
set(_ep_bsp_defines "")
set(_ep_sys_lib "hal_bsp")
foreach(_line IN LISTS _ep_public_mk)
  if(_line MATCHES "^ALT_CFLAGS[ \t]*\\+=[ \t]*(-m[^ \t]+)")
    list(APPEND _ep_cpu_flags ${CMAKE_MATCH_1})
  elseif(_line MATCHES "^ALT_CPPFLAGS[ \t]*\\+=[ \t]*-D([^ \t]+)")
    list(APPEND _ep_bsp_defines ${CMAKE_MATCH_1})
  elseif(_line MATCHES "^BSP_SYS_LIB[ \t]*:=[ \t]*([^ \t]+)")
    set(_ep_sys_lib ${CMAKE_MATCH_1})
  endif()
endforeach()
if(EP_NIOS2_CPU_FLAGS)
  separate_arguments(_ep_cpu_flags UNIX_COMMAND "${EP_NIOS2_CPU_FLAGS}")
endif()
if(NOT _ep_sys_lib STREQUAL "ucosii_bsp")
  message(WARNING "BSP library is '${_ep_sys_lib}', expected 'ucosii_bsp': target nios2_ucos2 needs a UCOSII BSP")
endif()
message(STATUS "EP: Nios II BSP ${EP_NIOS2_BSP_DIR}: cpu flags [${_ep_cpu_flags}], defines [${_ep_bsp_defines}], lib ${_ep_sys_lib}")

add_compile_options(${_ep_cpu_flags} -ffunction-sections -fdata-sections -O2)
add_link_options(${_ep_cpu_flags} -Wl,--gc-sections
                 -T${EP_NIOS2_BSP_DIR}/linker.x
                 -msys-crt0=${EP_NIOS2_BSP_DIR}/obj/HAL/src/crt0.o
                 -msys-lib=${_ep_sys_lib}
                 -L${EP_NIOS2_BSP_DIR})

add_library(ep_vendor INTERFACE)
set(_ep_bsp_includes ${EP_NIOS2_BSP_DIR} ${EP_NIOS2_BSP_DIR}/HAL/inc ${EP_NIOS2_BSP_DIR}/drivers/inc)
if(EXISTS ${EP_NIOS2_BSP_DIR}/UCOSII/inc)
  list(APPEND _ep_bsp_includes ${EP_NIOS2_BSP_DIR}/UCOSII/inc)
endif()
target_include_directories(ep_vendor INTERFACE ${_ep_bsp_includes})
target_compile_definitions(ep_vendor INTERFACE ${_ep_bsp_defines} ${EP_NIOS2_DEFINES})

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS    ep_vendor)

# Bộ chặn heap bọc cả malloc của newlib (như các MCU khác). HAL Nios cấp phát trong alt_sys_init() – trước
# main(), tức trước end_startup(): hợp lệ.
set(EP_HEAP_GUARD_DEFINES EP_HEAP_GUARD_WRAP_MALLOC)
add_link_options(-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
