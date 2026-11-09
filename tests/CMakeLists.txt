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
  test_module_worker.cpp
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

# GPIO thanh ghi (header-only, không phụ thuộc OS) – test trên mọi host
target_sources(ep_tests PRIVATE test_mmio_gpio.cpp)
target_include_directories(ep_tests PRIVATE ${PROJECT_SOURCE_DIR}/platform/hal/ports/common)

# HAL dùng chung theo OS (EP_HAL_COMMON của target): posix (UART termios, FileStorage), linux, qnx
if("posix" IN_LIST EP_HAL_COMMON)
  target_sources(ep_tests PRIVATE test_posix_hal.cpp)
endif()
if("linux" IN_LIST EP_HAL_COMMON)
  target_sources(ep_tests PRIVATE test_linux_hal.cpp)
endif()
if("qnx" IN_LIST EP_HAL_COMMON)
  target_sources(ep_tests PRIVATE test_qnx_hal.cpp)
endif()
