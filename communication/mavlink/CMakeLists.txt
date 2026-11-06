# Giao thức MAVLink v2 (codec tự viết, không cần header sinh từ XML).
# Bản tin riêng của sản phẩm khai báo qua mavlink::IDialect ở products/<p>/.
add_library(ep_proto_mavlink STATIC
  src/codec.cpp
  src/mavlink_protocol.cpp)
target_include_directories(ep_proto_mavlink PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_proto_mavlink PUBLIC ep_comm)
