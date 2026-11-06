# Application Framework: vòng đời module, định tuyến event, state machine, khởi động hệ thống.
add_library(ep_app_framework STATIC
  framework/src/module_manager.cpp
  framework/src/system.cpp)
target_include_directories(ep_app_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/framework/include)
target_link_libraries(ep_app_framework PUBLIC ep_services ep_comm)

# Business: thư viện module nghiệp vụ tái sử dụng giữa các sản phẩm.
add_library(ep_business STATIC
  business/temperature_monitor/temperature_monitor.cpp
  business/alarm_controller/alarm_controller.cpp)
target_include_directories(ep_business PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/business)
target_link_libraries(ep_business PUBLIC ep_app_framework)
