# Application Runtime: môi trường chạy module nghiệp vụ – vòng đời, định tuyến event, tick,
# state machine, khởi động hệ thống.
add_library(ep_app_runtime STATIC
  runtime/src/module_manager.cpp
  runtime/src/system.cpp)
target_include_directories(ep_app_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/runtime/include)
target_link_libraries(ep_app_runtime PUBLIC ep_services ep_comm)

# Business: thư viện module nghiệp vụ tái sử dụng giữa các sản phẩm.
add_library(ep_business STATIC
  business/temperature_monitor/temperature_monitor.cpp
  business/alarm_controller/alarm_controller.cpp)
target_include_directories(ep_business PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/business)
target_link_libraries(ep_business PUBLIC ep_app_runtime)
