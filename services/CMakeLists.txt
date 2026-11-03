# Service & Middleware: dịch vụ hệ thống dùng chung, không chứa nghiệp vụ sản phẩm.
# Chỉ phụ thuộc HAL (interface) + OSAL.
add_library(ep_services STATIC
  src/logger.cpp
  src/event_bus.cpp
  src/param_store.cpp
  src/param_cli.cpp
  src/command_service.cpp
  src/led_service.cpp
  src/drivers/tmp102.cpp)
target_include_directories(ep_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_services PUBLIC ep_hal ep_osal)
