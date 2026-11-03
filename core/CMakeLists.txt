# Core: kiểu dữ liệu & tiện ích dùng chung, KHÔNG phụ thuộc phần cứng/OS (header-only)
add_library(ep_core INTERFACE)
target_include_directories(ep_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${EP_PRODUCT_DIR}/config)          # product_config.hpp: override cấu hình theo sản phẩm
target_link_libraries(ep_core INTERFACE ep_options)
