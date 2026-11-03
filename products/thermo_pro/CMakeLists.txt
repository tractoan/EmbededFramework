add_library(ep_product STATIC product.cpp modbus_map.cpp)
target_include_directories(ep_product PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Giao thức chọn lúc build: chỉ thư viện giao thức được link mới có mặt trong firmware.
target_link_libraries(ep_product PUBLIC ep_modules ep_proto_modbus)
