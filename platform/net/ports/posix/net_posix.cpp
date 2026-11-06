// Port net cho POSIX: BSD socket + poll(). Không cấp phát heap (không dùng getaddrinfo).
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "net/net.hpp"

namespace net {

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;   // không nhận SIGPIPE khi phía kia đã đóng
#else
constexpr int kSendFlags = 0;
#endif

int to_poll_ms(std::uint32_t ms) { return ms > 60000u ? 60000 : static_cast<int>(ms); }

sockaddr_in to_sockaddr(const Endpoint& e) {
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_port        = htons(e.port);
    a.sin_addr.s_addr = htonl(e.address);
    return a;
}

Endpoint from_sockaddr(const sockaddr_in& a) { return Endpoint{ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)}; }

// 1 = sẵn sàng, 0 = timeout, -1 = lỗi
int wait_fd(int fd, short events, std::uint32_t timeout_ms) {
    pollfd p{fd, events, 0};
    for (;;) {
        const int r = ::poll(&p, 1, to_poll_ms(timeout_ms));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return r;
        return (p.revents & (POLLERR | POLLNVAL)) && !(p.revents & events) ? -1 : 1;
    }
}

void close_fd(port::SocketHandle& h) {
    if (h.fd >= 0) ::close(h.fd);
    h.fd = -1;
}

std::uint16_t bound_port(int fd) {
    sockaddr_in a{};
    socklen_t n = sizeof(a);
    return ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n) == 0 ? ntohs(a.sin_port) : 0;
}

int open_socket(int type) {
    const int fd = ::socket(AF_INET, type, 0);
    if (fd >= 0) {
        const int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    return fd;
}

}  // namespace

// ---------------------------------------------------------------- Endpoint
bool Endpoint::parse(const char* dotted_ipv4, std::uint16_t port, Endpoint& out) {
    in_addr a{};
    if (dotted_ipv4 == nullptr || ::inet_pton(AF_INET, dotted_ipv4, &a) != 1) return false;
    out = Endpoint{ntohl(a.s_addr), port};
    return true;
}

// ----------------------------------------------------------- TcpConnection
bool TcpConnection::is_open() const { return h_.fd >= 0; }

core::Status TcpConnection::connect(const Endpoint& remote, std::uint32_t timeout_ms) {
    close();
    h_.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (h_.fd < 0) return core::Status::Error;
    const int flags = ::fcntl(h_.fd, F_GETFL, 0);
    ::fcntl(h_.fd, F_SETFL, flags | O_NONBLOCK);   // connect không chặn để áp timeout
    const sockaddr_in a = to_sockaddr(remote);
    int r               = ::connect(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    if (r != 0 && errno == EINPROGRESS) {
        if (wait_fd(h_.fd, POLLOUT, timeout_ms) <= 0) {
            close();
            return core::Status::Timeout;
        }
        int err       = 0;
        socklen_t len = sizeof(err);
        ::getsockopt(h_.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        r = err == 0 ? 0 : -1;
    }
    if (r != 0) {
        close();
        return core::Status::Error;
    }
    ::fcntl(h_.fd, F_SETFL, flags);
    const int one = 1;
    ::setsockopt(h_.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));   // giao thức điều khiển: ưu tiên độ trễ
    peer_ = remote;
    return core::Status::Ok;
}

std::size_t TcpConnection::send(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (h_.fd >= 0 && sent < len) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() < 0 || wait_fd(h_.fd, POLLOUT, static_cast<std::uint32_t>(left.count())) <= 0) break;
        const ssize_t n = ::send(h_.fd, data + sent, len - sent, kSendFlags);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    return sent;
}

core::Status TcpConnection::receive(std::uint8_t* buf, std::size_t cap, std::size_t& got, std::uint32_t timeout_ms) {
    got = 0;
    if (h_.fd < 0) return core::Status::Closed;
    const int w = wait_fd(h_.fd, POLLIN, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    const ssize_t n = ::recv(h_.fd, buf, cap, 0);
    if (n == 0) return core::Status::Closed;
    if (n < 0) return (errno == EINTR || errno == EAGAIN) ? core::Status::Timeout : core::Status::Closed;
    got = static_cast<std::size_t>(n);
    return core::Status::Ok;
}

void TcpConnection::close() { close_fd(h_); }

// ------------------------------------------------------------- TcpListener
core::Status TcpListener::open(std::uint16_t port, bool loopback_only) {
    close();
    h_.fd = open_socket(SOCK_STREAM);
    if (h_.fd < 0) return core::Status::Error;
    const sockaddr_in a = to_sockaddr(loopback_only ? Endpoint::loopback(port) : Endpoint::any(port));
    if (::bind(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || ::listen(h_.fd, 2) != 0) {
        close();
        return core::Status::Busy;
    }
    local_port_ = bound_port(h_.fd);
    return core::Status::Ok;
}

bool TcpListener::is_open() const { return h_.fd >= 0; }

core::Status TcpListener::accept(TcpConnection& out, std::uint32_t timeout_ms) {
    if (h_.fd < 0) return core::Status::Error;
    const int w = wait_fd(h_.fd, POLLIN, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    sockaddr_in a{};
    socklen_t n  = sizeof(a);
    const int fd = ::accept(h_.fd, reinterpret_cast<sockaddr*>(&a), &n);
    if (fd < 0) return core::Status::Timeout;
    out.close();
    out.h_.fd     = fd;
    out.peer_     = from_sockaddr(a);
    const int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return core::Status::Ok;
}

void TcpListener::close() {
    close_fd(h_);
    local_port_ = 0;
}

// --------------------------------------------------------------- UdpSocket
core::Status UdpSocket::open(std::uint16_t local_port, bool loopback_only) {
    close();
    h_.fd = open_socket(SOCK_DGRAM);
    if (h_.fd < 0) return core::Status::Error;
    const sockaddr_in a = to_sockaddr(loopback_only ? Endpoint::loopback(local_port) : Endpoint::any(local_port));
    if (::bind(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
        close();
        return core::Status::Busy;
    }
    local_port_ = bound_port(h_.fd);
    return core::Status::Ok;
}

bool UdpSocket::is_open() const { return h_.fd >= 0; }

core::Status UdpSocket::send_to(const Endpoint& to, const std::uint8_t* data, std::size_t len) {
    if (h_.fd < 0) return core::Status::Error;
    const sockaddr_in a = to_sockaddr(to);
    const ssize_t n     = ::sendto(h_.fd, data, len, kSendFlags, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    return n == static_cast<ssize_t>(len) ? core::Status::Ok : core::Status::Error;
}

core::Status UdpSocket::receive_from(std::uint8_t* buf, std::size_t cap, std::size_t& got, Endpoint& from,
                                     std::uint32_t timeout_ms) {
    got = 0;
    if (h_.fd < 0) return core::Status::Error;
    const int w = wait_fd(h_.fd, POLLIN, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    sockaddr_in a{};
    socklen_t n     = sizeof(a);
    const ssize_t r = ::recvfrom(h_.fd, buf, cap, 0, reinterpret_cast<sockaddr*>(&a), &n);
    if (r < 0) return (errno == EINTR || errno == EAGAIN) ? core::Status::Timeout : core::Status::Error;
    got  = static_cast<std::size_t>(r);
    from = from_sockaddr(a);
    return core::Status::Ok;
}

void UdpSocket::close() {
    close_fd(h_);
    local_port_ = 0;
}

}  // namespace net
