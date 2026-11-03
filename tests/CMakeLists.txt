# Unit test chạy trên host (port posix). Không cần framework ngoài.
add_executable(ep_tests
  test_main.cpp
  test_utils.cpp
  test_osal.cpp
  test_event_bus.cpp
  test_param_store.cpp
  test_module_manager.cpp
  test_alarm_controller.cpp
  test_cli_protocol.cpp
  test_modbus.cpp
  test_link_manager.cpp
  test_mavlink.cpp
  test_bringup_services.cpp)
target_include_directories(ep_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ep_tests PRIVATE ep_modules ep_proto_modbus ep_proto_mavlink)

# Module bring-up + dialect MAVLink của hw_bringup cần event payload 128 byte -> chỉ test khi build sản phẩm đó.
if(EP_PRODUCT STREQUAL "hw_bringup")
  target_sources(ep_tests PRIVATE test_bringup_modules.cpp ${EP_PRODUCT_DIR}/mavlink_dialect.cpp)
  target_include_directories(ep_tests PRIVATE ${EP_PRODUCT_DIR})
  target_link_libraries(ep_tests PRIVATE ep_modules_bringup)
endif()
add_test(NAME ep_tests COMMAND ep_tests)

# Kiểm tra chéo CRC_EXTRA/độ dài bản tin C++ với định nghĩa XML (tools/mavlink).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME mavlink_dialect_check
           COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/mavlink/mavlink_dialect.py --check)
endif()
