# Target: STM32F407 (Discovery) + Micrium uC/OS-II (V2.9x)
#
#   cmake -B build-ucos2 -DEP_TARGET=stm32f4_ucos2 \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
#         -DEP_CUBE_DIR=/path/to/cubemx_project \
#         -DEP_UCOS2_DIR=/path/to/uC-OS2 \
#         -DEP_UCOS2_PORT_DIR=/path/to/uC-OS2/Ports/ARM-Cortex-M/ARMv7-M/GNU \
#         -DEP_UCOS_CFG_DIR=/path/to/your/ucos_config
#
# EP_UCOS_CFG_DIR chứa os_cfg.h (+ app_cfg.h nếu port CPU yêu cầu). Cấu hình tối thiểu cho OSAL:
# xem đầu platform/osal/ports/ucos2/osal_ucos2.cpp. Nên đặt OS_APP_HOOKS_EN = 0
# (hoặc tự cung cấp App_*Hook).
# CubeMX: tắt sinh handler "Pendable request for system service" và "System tick timer"
# trong NVIC > Code generation – target tự định nghĩa PendSV_Handler/SysTick_Handler.
include(${CMAKE_CURRENT_LIST_DIR}/common/stm32f4_cube.cmake)

set(EP_OSAL_PORT ucos2)
set(EP_UCOS2_DIR      "" CACHE PATH "uC/OS-II root (chứa Source/)")
set(EP_UCOS2_PORT_DIR "" CACHE PATH "uC/OS-II CPU port (os_cpu.h, os_cpu_a.S, os_cpu_c.c)")
set(EP_UCOS_CFG_DIR   "" CACHE PATH "Thư mục chứa os_cfg.h, app_cfg.h của sản phẩm")

file(GLOB EP_UCOS2_SOURCES
     ${EP_UCOS2_DIR}/Source/os_*.c
     ${EP_UCOS2_PORT_DIR}/*.c
     ${EP_UCOS2_PORT_DIR}/*.S
     ${EP_UCOS2_PORT_DIR}/*.s)

add_library(ep_vendor STATIC ${EP_STM32_CUBE_SOURCES} ${EP_UCOS2_SOURCES})
target_include_directories(ep_vendor PUBLIC
  ${EP_STM32_CUBE_INCLUDES}
  ${EP_UCOS2_DIR}/Source
  ${EP_UCOS2_PORT_DIR}
  ${EP_UCOS_CFG_DIR})
target_compile_definitions(ep_vendor PUBLIC ${EP_STM32_CUBE_DEFINES})

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS    ep_vendor)
