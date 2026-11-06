# Target: STM32F407 (Discovery) + FreeRTOS
#
#   cmake -B build-stm32 -DEP_TARGET=stm32f4_freertos \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
#         -DEP_CUBE_DIR=/path/to/cubemx_project -DEP_FREERTOS_DIR=/path/to/FreeRTOS-Kernel
#
# Yêu cầu chung của CubeMX: xem cmake/targets/common/stm32f4_cube.cmake
# FreeRTOSConfig.h: configSUPPORT_STATIC_ALLOCATION=1, configSUPPORT_DYNAMIC_ALLOCATION=0
# (framework không dùng heap FreeRTOS -> không build heap_x.c).
include(${CMAKE_CURRENT_LIST_DIR}/common/stm32f4_cube.cmake)

set(EP_OSAL_PORT freertos)
set(EP_FREERTOS_DIR "" CACHE PATH "FreeRTOS-Kernel source root")

add_library(ep_vendor STATIC
  ${EP_STM32_CUBE_SOURCES}
  ${EP_FREERTOS_DIR}/tasks.c
  ${EP_FREERTOS_DIR}/queue.c
  ${EP_FREERTOS_DIR}/list.c
  ${EP_FREERTOS_DIR}/timers.c
  ${EP_FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c)
target_include_directories(ep_vendor PUBLIC
  ${EP_STM32_CUBE_INCLUDES}
  ${EP_FREERTOS_DIR}/include
  ${EP_FREERTOS_DIR}/portable/GCC/ARM_CM4F)
target_compile_definitions(ep_vendor PUBLIC ${EP_STM32_CUBE_DEFINES})

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS    ep_vendor)
