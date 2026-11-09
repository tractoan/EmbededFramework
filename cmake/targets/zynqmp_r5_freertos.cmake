# Target: Zynq UltraScale+ MPSoC – RPU (Cortex-R5F) + FreeRTOS, dùng BSP "freertos" của Vitis
#
# 1. Trong Vitis: tạo platform cho psu_cortexr5_0, OS = freertos, rồi build BSP.
#    FreeRTOSConfig (BSP settings): configSUPPORT_STATIC_ALLOCATION = 1,
#    configCHECK_FOR_STACK_OVERFLOW = 2; framework không dùng heap FreeRTOS.
# 2. Build firmware:
#   cmake -B build-r5 -DEP_TARGET=zynqmp_r5_freertos \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/armr5-none-eabi.cmake \
#         -DEP_XILINX_BSP_DIR=<platform>/psu_cortexr5_0/freertos_domain/bsp/psu_cortexr5_0 \
#         -DEP_XILINX_LINKER_SCRIPT=<app>/src/lscript.ld
#   cmake --build build-r5 -j          # -> build-r5/firmware.elf, nạp bằng XSCT/JTAG hoặc BOOT.BIN
#
# EP_XILINX_BSP_DIR chứa include/ (xparameters.h, xgpiops.h, FreeRTOS.h...) và lib/ (libxil.a,
# libfreertos.a...). Với flow System Device Tree (Vitis Unified) thêm -DEP_XILINX_SDT=ON.
# Gán thiết bị (UART/I2C/GPIO/EEPROM): xem platform/hal/ports/zynqmp_r5/zynqmp_r5_board.cpp,
# ghi đè bằng -DEP_XILINX_DEFINES="EP_ZYNQMP_R5_CONSOLE_UART=0;EP_ZYNQMP_R5_EEPROM_ENABLE=0".
set(EP_OSAL_PORT  freertos)
set(EP_HAL_PORT   zynqmp_r5)
set(EP_NET_PORT   "")          # TCP/IP: thêm port lwip + lwip213 của BSP khi cần
set(EP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(EP_XILINX_BSP_DIR       "" CACHE PATH "Vitis BSP directory (include/, lib/)")
set(EP_XILINX_LINKER_SCRIPT "" CACHE FILEPATH "Linker script (lscript.ld from a Vitis app)")
set(EP_XILINX_BSP_LIBS      "xil;freertos" CACHE STRING "BSP libraries to link (those that exist in lib/)")
set(EP_XILINX_DEFINES       "" CACHE STRING "Extra board defines (;-separated)")
option(EP_XILINX_SDT "BSP built with the System Device Tree flow (XPAR_*_BASEADDR keys)" OFF)

if(NOT EP_XILINX_BSP_DIR OR NOT EXISTS ${EP_XILINX_BSP_DIR}/include/xparameters.h)
  message(FATAL_ERROR "EP_XILINX_BSP_DIR must point to a built Vitis BSP (include/xparameters.h)")
endif()

set(EP_CPU_FLAGS -mcpu=cortex-r5 -mfloat-abi=hard -mfpu=vfpv3-d16)   # khớp cờ BSP của Vitis
add_compile_options(${EP_CPU_FLAGS} -ffunction-sections -fdata-sections -O2)
add_link_options(${EP_CPU_FLAGS} -Wl,--gc-sections --specs=nosys.specs)
if(EP_XILINX_LINKER_SCRIPT)
  add_link_options(-T${EP_XILINX_LINKER_SCRIPT})
endif()

# Thư viện BSP (driver + standalone + kernel FreeRTOS) đã build sẵn bởi Vitis.
add_library(ep_vendor INTERFACE)
target_include_directories(ep_vendor INTERFACE ${EP_XILINX_BSP_DIR}/include)
# ARMR5: macro Vitis luôn định nghĩa cho RPU; header BSP (xil_io.h, sleep.h...) rẽ nhánh theo nó.
target_compile_definitions(ep_vendor INTERFACE ARMR5 ${EP_XILINX_DEFINES} $<$<BOOL:${EP_XILINX_SDT}>:SDT>)
set(_ep_bsp_libs "")
foreach(_lib ${EP_XILINX_BSP_LIBS})
  if(EXISTS ${EP_XILINX_BSP_DIR}/lib/lib${_lib}.a)
    list(APPEND _ep_bsp_libs ${EP_XILINX_BSP_DIR}/lib/lib${_lib}.a)
  endif()
endforeach()
# Nhóm để giải quyết phụ thuộc vòng (libxil <-> libfreertos <-> libc).
target_link_libraries(ep_vendor INTERFACE -Wl,--start-group ${_ep_bsp_libs} -lgcc -lc -Wl,--end-group)

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS    ep_vendor)

# Bộ chặn heap bọc cả malloc của newlib (như STM32).
set(EP_HEAP_GUARD_DEFINES EP_HEAP_GUARD_WRAP_MALLOC)
add_link_options(-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
