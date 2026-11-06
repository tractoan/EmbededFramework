# Unit test chạy trên host (port posix). Không cần framework ngoài.
add_executable(ep_tests
  test_main.cpp
  test_utils.cpp
  test_buffer.cpp
  test_osal.cpp
  test_memory.cpp
  test_event_bus.cpp
  test_param_store.cpp
  test_module_manager.cpp
  test_alarm_controller.cpp
  test_cli_protocol.cpp
  test_modbus.cpp
  test_link_manager.cpp
  test_mavlink.cpp
  test_bringup_services.cpp
  test_bringup_modules.cpp
  ${PROJECT_SOURCE_DIR}/products/hw_bringup/mavlink_dialect.cpp)
target_include_directories(ep_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(ep_tests PRIVATE ${PROJECT_SOURCE_DIR}/products/hw_bringup)   # dialect MAVLink của hw_bringup
target_link_libraries(ep_tests PRIVATE ep_heap_guard ep_business ep_business_bringup ep_proto_cli ep_proto_modbus
                      ep_proto_mavlink)
add_test(NAME ep_tests COMMAND ep_tests)

# Kiểm tra chéo CRC_EXTRA/độ dài bản tin C++ với định nghĩa XML (tools/mavlink).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME mavlink_dialect_check
           COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/mavlink/mavlink_dialect.py --check)
endif()
