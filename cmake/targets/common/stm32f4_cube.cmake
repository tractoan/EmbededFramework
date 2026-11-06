# Phần dùng chung cho mọi target STM32F407 (Discovery) dựa trên project STM32CubeMX,
# bất kể RTOS nào (FreeRTOS, uC/OS-II, uC/OS-III). File target include file này rồi thêm RTOS.
#
# Yêu cầu project CubeMX:
#   - Bật "Generate peripheral initialization as a pair of .c/.h files per peripheral"
#     (để MX_USART2_UART_Init, MX_I2C1_Init... nằm ngoài main.c)
#   - Bật USART2 (console), USART3 (Aux: RS485/telemetry), I2C1 (cảm biến)
#   - Dùng TIMx làm timebase cho HAL (SysTick dành cho RTOS)
#   - main.c của CubeMX bị loại bỏ; SystemClock_Config() đặt ở
#     platform/target/common/stm32f4/clock_config.c (dùng chung cho mọi target STM32F4)
#
# Đầu ra: EP_STM32_CUBE_SOURCES, EP_STM32_CUBE_INCLUDES, EP_STM32_CUBE_DEFINES
enable_language(ASM)

set(EP_HAL_PORT stm32f4)
set(EP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EP_CUBE_DIR "" CACHE PATH "CubeMX-generated project root (Core/, Drivers/)")

set(EP_CPU_FLAGS -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard)
add_compile_options(${EP_CPU_FLAGS} -ffunction-sections -fdata-sections -Os)
add_link_options(${EP_CPU_FLAGS} -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs
                 -T${EP_CUBE_DIR}/STM32F407VGTX_FLASH.ld)

file(GLOB EP_STM32_CUBE_SOURCES
     ${EP_CUBE_DIR}/Core/Src/*.c
     ${EP_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver/Src/*.c)
list(FILTER EP_STM32_CUBE_SOURCES EXCLUDE REGEX ".*(_template|/main)\\.c$")
list(APPEND EP_STM32_CUBE_SOURCES ${EP_CUBE_DIR}/Core/Startup/startup_stm32f407vgtx.s)

set(EP_STM32_CUBE_INCLUDES
    ${EP_CUBE_DIR}/Core/Inc
    ${EP_CUBE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${EP_CUBE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${EP_CUBE_DIR}/Drivers/CMSIS/Include)
set(EP_STM32_CUBE_DEFINES STM32F407xx USE_HAL_DRIVER)

# Mã khởi động dùng chung, biên dịch vào ep_target cùng với platform/target/<EP_TARGET>/
set(EP_TARGET_EXTRA_SOURCES ${PROJECT_SOURCE_DIR}/platform/target/common/stm32f4/clock_config.c)
