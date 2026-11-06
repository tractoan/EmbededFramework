# Nền tảng mẫu: STM32F407 (Discovery) + FreeRTOS
#
#   cmake -B build-stm32 -DEP_TARGET=stm32f4_freertos \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/arm-none-eabi.cmake \
#         -DEP_CUBE_DIR=/path/to/cubemx_project -DEP_FREERTOS_DIR=/path/to/FreeRTOS-Kernel
#
# Yêu cầu project CubeMX:
#   - Bật "Generate peripheral initialization as a pair of .c/.h files per peripheral"
#     (để MX_USART2_UART_Init, MX_I2C1_Init... nằm ngoài main.c)
#   - Bật USART2 (console) và USART3 (Aux: RS485/telemetry), I2C1 (cảm biến)
#   - Dùng TIMx làm timebase cho HAL (SysTick dành cho FreeRTOS)
#   - main.c của CubeMX bị loại bỏ; SystemClock_Config() copy sang
#     platform/target/stm32f4_freertos/clock_config.c
#   - FreeRTOSConfig.h: configSUPPORT_STATIC_ALLOCATION=1, configSUPPORT_DYNAMIC_ALLOCATION=1
enable_language(ASM)

set(EP_OSAL_PORT freertos)
set(EP_HAL_PORT  stm32f4)
set(EP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(EP_CUBE_DIR     "" CACHE PATH "CubeMX-generated project root (Core/, Drivers/)")
set(EP_FREERTOS_DIR "" CACHE PATH "FreeRTOS-Kernel source root")

set(EP_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
add_compile_options(${EP_CPU_FLAGS} -ffunction-sections -fdata-sections -Os)
add_link_options(${EP_CPU_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
                 -T${EP_CUBE_DIR}/STM32F407VGTX_FLASH.ld)

file(GLOB EP_CUBE_SOURCES
     ${EP_CUBE_DIR}/Core/Src/*.c
     ${EP_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/*.c)
list(FILTER EP_CUBE_SOURCES EXCLUDE REGEX ".*(_template|/main)\\.c$")

add_library(ep_vendor STATIC
  ${EP_CUBE_SOURCES}
  ${EP_CUBE_DIR}/Core/Startup/startup_stm32f407vgtx.s
  ${EP_FREERTOS_DIR}/tasks.c
  ${EP_FREERTOS_DIR}/queue.c
  ${EP_FREERTOS_DIR}/list.c
  ${EP_FREERTOS_DIR}/timers.c
  ${EP_FREERTOS_DIR}/portable/GCC/ARM_CM4F/port.c
  ${EP_FREERTOS_DIR}/portable/MemMang/heap_4.c)
target_include_directories(ep_vendor PUBLIC
  ${EP_CUBE_DIR}/Core/Inc
  ${EP_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
  ${EP_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
  ${EP_CUBE_DIR}/Drivers/CMSIS/Include
  ${EP_FREERTOS_DIR}/include
  ${EP_FREERTOS_DIR}/portable/GCC/ARM_CM4F)
target_compile_definitions(ep_vendor PUBLIC STM32F407xx USE_HAL_DRIVER)

set(EP_OSAL_PORT_LIBS ep_vendor)
set(EP_HAL_PORT_LIBS  ep_vendor)
set(EP_TARGET_LIBS  ep_vendor)
