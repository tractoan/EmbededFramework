# Utils: thuật toán & cấu trúc dữ liệu thuần (CRC, fixed-point, ring buffer...).
# Hoàn toàn độc lập (chỉ thư viện chuẩn C++) -> mọi lớp, kể cả foundation, đều được dùng.
add_library(ep_utils STATIC
  src/crc.cpp
  src/fixed_point.cpp)
target_include_directories(ep_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_utils PUBLIC ep_options)
