# Application Framework: vòng đời module, định tuyến event, state machine, khởi động hệ thống.
add_library(ep_app_framework STATIC
  framework/src/module_manager.cpp
  framework/src/system.cpp)
target_include_directories(ep_app_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/framework/include)
target_link_libraries(ep_app_framework PUBLIC ep_services)

# Business modules: thư viện module nghiệp vụ tái sử dụng giữa các sản phẩm.
add_library(ep_modules STATIC
  modules/temperature_monitor/temperature_monitor.cpp
  modules/alarm_controller/alarm_controller.cpp)
target_include_directories(ep_modules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/modules)
target_link_libraries(ep_modules PUBLIC ep_app_framework)
