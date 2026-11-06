#pragma once
// Port net cho lwIP (socket API, LWIP_SOCKET = 1). Socket lwIP cũng là số nguyên.
namespace net::port {

struct SocketHandle {
    int fd = -1;
};

}  // namespace net::port
