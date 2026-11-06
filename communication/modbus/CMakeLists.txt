add_library(ep_proto_modbus STATIC src/modbus_rtu.cpp)
target_include_directories(ep_proto_modbus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_proto_modbus PUBLIC ep_comm)
