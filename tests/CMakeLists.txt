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
  test_link_manager.cpp)
target_include_directories(ep_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ep_tests PRIVATE ep_business ep_proto_modbus)
add_test(NAME ep_tests COMMAND ep_tests)
