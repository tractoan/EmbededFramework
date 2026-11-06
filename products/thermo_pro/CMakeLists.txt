add_library(ep_product STATIC product.cpp modbus_map.cpp mavlink_map.cpp)
target_include_directories(ep_product PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Giao thức chọn lúc build: chỉ thư viện giao thức được link mới có mặt trong firmware.
target_link_libraries(ep_product PUBLIC ep_business ep_proto_cli ep_proto_modbus ep_proto_mavlink)
# Kênh TCP/UDP chỉ khi target có mạng (EP_NET_PORT); không có thì sản phẩm chạy với UART.
if(TARGET ep_comm_net)
  target_link_libraries(ep_product PUBLIC ep_comm_net)
  target_compile_definitions(ep_product PRIVATE EP_HAS_NET=1)
endif()
