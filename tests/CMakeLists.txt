# Unit test chạy trên host (port posix). Không cần framework ngoài.
add_executable(ep_tests
  test_main.cpp
  test_osal.cpp
  test_event_bus.cpp
  test_param_store.cpp
  test_module_manager.cpp
  test_alarm_controller.cpp)
target_include_directories(ep_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ep_tests PRIVATE ep_modules)
add_test(NAME ep_tests COMMAND ep_tests)
