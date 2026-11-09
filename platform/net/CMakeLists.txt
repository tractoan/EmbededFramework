# Net: trừu tượng hóa TCP/IP (IPv4). Port chọn bằng EP_NET_PORT trong file target:
#   posix : BSD socket của host (Linux/macOS)
#   lwip  : lwIP socket API trên MCU (target cung cấp lwIP qua EP_NET_PORT_LIBS)
# Target không có mạng (vd. STM32F4-Discovery không có PHY Ethernet) để trống EP_NET_PORT:
# khi đó ep_net không tồn tại và sản phẩm tự bỏ các kênh mạng (xem products/thermo_pro).
if(NOT EP_NET_PORT)
  return()
endif()
add_library(ep_net STATIC ports/${EP_NET_PORT}/net_${EP_NET_PORT}.cpp)
target_include_directories(ep_net PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_NET_PORT}/include)
target_link_libraries(ep_net PUBLIC ep_osal ${EP_NET_PORT_LIBS})
