# Application Runtime: môi trường chạy module nghiệp vụ – vòng đời, định tuyến event, tick,
# state machine, khởi động hệ thống.
add_library(ep_app_runtime STATIC
  runtime/src/module_manager.cpp
  runtime/src/module_worker.cpp
  runtime/src/system.cpp)
target_include_directories(ep_app_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime/include)
target_link_libraries(ep_app_runtime PUBLIC ep_services ep_comm)

# Business: thư viện module nghiệp vụ tái sử dụng giữa các sản phẩm.
add_library(ep_business STATIC
  business/temperature_monitor/temperature_monitor.cpp
  business/alarm_controller/alarm_controller.cpp)
target_include_directories(ep_business PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/business)
target_link_libraries(ep_business PUBLIC ep_app_runtime)

# Nhóm module bring-up phần cứng (serial/GNSS, I/O số, IMU) – dùng bởi products/hw_bringup.
# Dữ liệu lớn đi qua Event::buffer (svc::Buffer) nên không cần nới EP_CFG_EVENT_PAYLOAD_SIZE.
add_library(ep_business_bringup STATIC
  business/serial_bringup/serial_bringup.cpp
  business/gnss_bringup/gnss_bringup.cpp
  business/io_bringup/io_bringup.cpp
  business/imu_bringup/imu_bringup.cpp)
target_link_libraries(ep_business_bringup PUBLIC ep_business)
