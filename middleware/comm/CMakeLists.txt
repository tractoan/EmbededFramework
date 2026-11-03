add_library(ep_comm STATIC
  src/link_manager.cpp
  src/cli_protocol.cpp)
target_include_directories(ep_comm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_comm PUBLIC ep_services)
