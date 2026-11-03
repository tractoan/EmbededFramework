# Utils: thuật toán & cấu trúc dữ liệu thuần (CRC, fixed-point, ring buffer...).
# Không phụ thuộc phần cứng / OS / nghiệp vụ -> mọi lớp phía trên đều được dùng.
add_library(ep_utils STATIC
  src/crc.cpp
  src/fixed_point.cpp)
target_include_directories(ep_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_utils PUBLIC ep_core)
