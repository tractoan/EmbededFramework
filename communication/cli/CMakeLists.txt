add_library(ep_proto_cli STATIC src/cli_protocol.cpp)
target_include_directories(ep_proto_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_proto_cli PUBLIC ep_comm)
