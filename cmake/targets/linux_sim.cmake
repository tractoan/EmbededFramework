# Nền tảng: Linux host + phần cứng mô phỏng (dùng để phát triển nghiệp vụ, CI, unit test)
set(EP_OSAL_PORT posix)
set(EP_HAL_PORT  linux_sim)

find_package(Threads REQUIRED)
set(EP_OSAL_PORT_LIBS Threads::Threads)

# Console mô phỏng dùng terminal (đã tự echo) -> tắt echo của CLI
add_compile_definitions(EP_CFG_CLI_ECHO=0)
