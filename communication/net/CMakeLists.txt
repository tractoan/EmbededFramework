# Stream TCP/IP cho LinkManager (TcpServerStream, TcpClientStream, UdpStream).
# Chỉ có khi target có mạng (EP_NET_PORT -> ep_net).
if(NOT TARGET ep_net)
  return()
endif()
add_library(ep_comm_net STATIC src/net_streams.cpp)
target_include_directories(ep_comm_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_comm_net PUBLIC ep_comm ep_net)
