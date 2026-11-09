# Giao thức MAVLink v1 (codec tối giản + heartbeat, param protocol, command, telemetry).
add_library(ep_proto_mavlink STATIC
  src/mavlink_v1.cpp
  src/mavlink_protocol.cpp)
target_include_directories(ep_proto_mavlink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_proto_mavlink PUBLIC ep_comm)
