# Core: kiểu nền tảng của ngôn ngữ/hệ thống (Status, config, NonCopyable, macro compiler).
# Thuật toán & cấu trúc dữ liệu thuần nằm ở utils/ (thư viện độc lập).
add_library(ep_core INTERFACE)
target_include_directories(ep_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${EP_PRODUCT_DIR}/config)          # product_config.hpp: override cấu hình theo sản phẩm
target_link_libraries(ep_core INTERFACE ep_options)
