add_library(ep_product STATIC product.cpp mavlink_dialect.cpp)
target_include_directories(ep_product PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Chỉ CLI (console) + MAVLink (giao diện) được link vào firmware (không Modbus).
target_link_libraries(ep_product PUBLIC ep_business_bringup ep_proto_cli ep_proto_mavlink)
