# Dialect MAVLink "bringup" (tools/mavlink/bringup.xml): định nghĩa bản tin trên dây, header-only.
# Không phụ thuộc nghiệp vụ; sản phẩm tự ánh xạ Event <-> bản tin qua mavlink2::IDialect.
add_library(ep_mavlink2_bringup INTERFACE)
target_include_directories(ep_mavlink2_bringup INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_mavlink2_bringup INTERFACE ep_proto_mavlink2)
