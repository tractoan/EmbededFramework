# Services: dịch vụ hệ thống dùng chung (có trạng thái, có chính sách), không chứa nghiệp vụ sản phẩm.
# Đứng trên messaging và platform (core, osal, hal, device).
add_library(ep_services STATIC
  src/logger.cpp
  src/param_store.cpp
  src/param_cli.cpp
  src/command_service.cpp
  src/led_service.cpp
  src/sensor_scheduler.cpp
  src/actuator.cpp
  src/serial_port_bank.cpp   # nhiều cổng serial: loopback / stream (thread mỗi cổng)
  src/gnss_receiver.cpp)     # máy thu GNSS: thread đọc/giải mã NMEA, gửi lệnh
target_include_directories(ep_services PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_services PUBLIC ep_messaging ep_device)
