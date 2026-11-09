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
  test_mavlink.cpp)
target_include_directories(ep_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ep_tests PRIVATE ep_heap_guard ep_business ep_proto_cli ep_proto_modbus ep_proto_mavlink)
add_test(NAME ep_tests COMMAND ep_tests)

# Test TCP/IP chỉ khi target có mạng
if(TARGET ep_comm_net)
  target_sources(ep_tests PRIVATE test_net.cpp)
  target_link_libraries(ep_tests PRIVATE ep_comm_net)
endif()

# HAL Linux dùng chung (TermiosUart, I2cDev, GpioLine, FileStorage) khi target dùng nó
if(EP_HAL_COMMON STREQUAL "linux")
  target_sources(ep_tests PRIVATE test_linux_hal.cpp)
  target_include_directories(ep_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/hal/ports/common)
  target_link_libraries(ep_tests PRIVATE util)   # openpty (glibc < 2.34)
endif()
