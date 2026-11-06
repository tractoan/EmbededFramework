#pragma once
// Port net cho POSIX (Linux/macOS): BSD socket của hệ điều hành.
namespace net::port {

struct SocketHandle {
    int fd = -1;
};

}  // namespace net::port
