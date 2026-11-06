# Target: STM32F407 (Discovery) + Micrium uC/OS-III (V3.0x) + uC/CPU + uC/LIB
#
#   cmake -B build-ucos3 -DEP_TARGET=stm32f4_ucos3 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
#         -DEP_CUBE_DIR=/path/to/cubemx_project \
#         -DEP_UCOS3_DIR=/path/to/uC-OS3 \
#         -DEP_UCOS3_PORT_DIR=/path/to/uC-OS3/Ports/ARM-Cortex-M/ARMv7-M/GNU \
#         -DEP_UCCPU_DIR=/path/to/uC-CPU \
#         -DEP_UCCPU_PORT_DIR=/path/to/uC-CPU/ARM-Cortex-M/ARMv7-M/GNU \
#         -DEP_UCLIB_DIR=/path/to/uC-LIB \
#         -DEP_UCOS_CFG_DIR=/path/to/your/ucos_config
#
# EP_UCOS_CFG_DIR chứa os_cfg.h, os_cfg_app.h, cpu_cfg.h, lib_cfg.h (+ app_cfg.h, os_app_hooks.c
# nếu dùng). Cấu hình tối thiểu cho OSAL: xem đầu platform/osal/ports/ucos3/osal_ucos3.cpp.
# CubeMX: tắt sinh handler PendSV và SysTick (NVIC > Code generation) – target tự định nghĩa.
include(${CMAKE_CURRENT_LIST_DIR}/common/stm32f4_cube.cmake)

set(EP_OSAL_PORT ucos3)
set(EP_UCOS3_DIR      "" CACHE PATH "uC/OS-III root (chứa Source/)")
set(EP_UCOS3_PORT_DIR "" CACHE PATH "uC/OS-III CPU port (os_cpu.h, os_cpu_a.S, os_cpu_c.c)")
set(EP_UCCPU_DIR      "" CACHE PATH "uC/CPU root (cpu_core.c, cpu_core.h)")
set(EP_UCCPU_PORT_DIR "" CACHE PATH "uC/CPU port (cpu.h, cpu_a.s, cpu_c.c)")
set(EP_UCLIB_DIR      "" CACHE PATH "uC/LIB root (lib_*.c)")
set(EP_UCOS_CFG_DIR   "" CACHE PATH "Thư mục chứa os_cfg.h, os_cfg_app.h, cpu_cfg.h, lib_cfg.h")

file(GLOB EP_UCOS3_SOURCES
     ${EP_UCOS3_DIR}/Source/os_*.c
     ${EP_UCOS3_PORT_DIR}/*.c
     ${EP_UCOS3_PORT_DIR}/*.S
     ${EP_UCOS3_PORT_DIR}/*.s
     ${EP_UCCPU_DIR}/cpu_core.c
     ${EP_UCCPU_PORT_DIR}/*.c
     ${EP_UCCPU_PORT_DIR}/*.S
     ${EP_UCCPU_PORT_DIR}/*.s
     ${EP_UCLIB_DIR}/lib_*.c
     ${EP_UCOS_CFG_DIR}/*.c)

add_library(ep_vendor STATIC ${EP_STM32_CUBE_SOURCES} ${EP_UCOS3_SOURCES})
target_include_directories(ep_vendor PUBLIC
  ${EP_STM32_CUBE_INCLUDES}
  ${EP_UCOS3_DIR}/Source
  ${EP_UCOS3_PORT_DIR}
  ${EP_UCCPU_DIR}
  ${EP_UCCPU_PORT_DIR}
  ${EP_UCLIB_DIR}
  ${EP_UCOS_CFG_DIR})
target_compile_definitions(ep_vendor PUBLIC ${EP_STM32_CUBE_DEFINES})

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS    ep_vendor)
