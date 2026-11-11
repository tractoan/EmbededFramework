# Giao thức MAVLink v2 + dialect của sản phẩm (codec tự viết, không cần header sinh từ XML).
# Khác communication/mavlink (v1, bảng ánh xạ telemetry NAMED_VALUE_FLOAT): bản tin msgid 24 bit,
# cắt byte 0 cuối payload, SERIAL_CONTROL, bản tin riêng qua mavlink2::IDialect. Nhận cả khung v1.
# Bản tin riêng của sản phẩm khai báo qua mavlink2::IDialect ở products/<p>/.
add_library(ep_proto_mavlink2 STATIC
  src/codec.cpp
  src/mavlink_protocol.cpp)
target_include_directories(ep_proto_mavlink2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_proto_mavlink2 PUBLIC ep_comm)

# Dialect (định nghĩa bản tin riêng) đi kèm giao thức – mỗi dialect một thư viện header-only.
add_subdirectory(dialects/bringup)   # ep_mavlink2_bringup
