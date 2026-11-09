add_library(ep_product STATIC product.cpp mavlink_dialect.cpp)
target_include_directories(ep_product PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# CLI (console) + MAVLink v2 với dialect bring-up (giao diện). Không Modbus, không MAVLink v1.
target_link_libraries(ep_product PUBLIC ep_business_bringup ep_proto_cli ep_proto_mavlink2)
# Target có mạng (EP_NET_PORT): thêm kênh MAVLink qua UDP cho phần mềm giao diện.
if(TARGET ep_comm_net)
  target_link_libraries(ep_product PUBLIC ep_comm_net)
  target_compile_definitions(ep_product PRIVATE EP_HAS_NET=1)
endif()
