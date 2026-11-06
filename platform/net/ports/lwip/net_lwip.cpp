// Port net cho lwIP 2.x qua socket API (lwip_*), chạy trên OS port của lwIP (FreeRTOS, uC/OS...).
//
// lwipopts.h tối thiểu: LWIP_SOCKET = 1, LWIP_SO_RCVTIMEO tùy ý (port dùng select), LWIP_TCP = 1,
// LWIP_UDP = 1, MEMP_NUM_NETCONN >= số socket đồng thời. lwIP dùng pool bộ nhớ riêng (memp) cấp
// lúc khởi động – phù hợp chính sách không cấp phát động của framework nếu MEM_LIBC_MALLOC = 0.
// Netif (Ethernet/PPP...) và tcpip_init() do target khởi tạo trong target::init()/task khởi động.
extern "C" {
#include "lwip/inet.h"
#include "lwip/sockets.h"
}

#include <cerrno>
#include <cstring>

#include "net/net.hpp"
#include "osal/time.hpp"

namespace net {

namespace {

sockaddr_in to_sockaddr(const Endpoint& e) {
    sockaddr_in a{};
    a.sin_len         = sizeof(a);
    a.sin_family      = AF_INET;
    a.sin_port        = lwip_htons(e.port);
    a.sin_addr.s_addr = lwip_htonl(e.address);
    return a;
}

Endpoint from_sockaddr(const sockaddr_in& a) { return Endpoint{lwip_ntohl(a.sin_addr.s_addr), lwip_ntohs(a.sin_port)}; }

// 1 = sẵn sàng, 0 = timeout, -1 = lỗi
int wait_fd(int fd, bool for_write, std::uint32_t timeout_ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{};
    tv.tv_sec  = static_cast<long>(timeout_ms / 1000u);
    tv.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);
    const int r = lwip_select(fd + 1, for_write ? nullptr : &set, for_write ? &set : nullptr, nullptr, &tv);
    return r < 0 ? -1 : (r == 0 ? 0 : 1);
}

void close_fd(port::SocketHandle& h) {
    if (h.fd >= 0) lwip_close(h.fd);
    h.fd = -1;
}

std::uint16_t bound_port(int fd) {
    sockaddr_in a{};
    socklen_t n = sizeof(a);
    return lwip_getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n) == 0 ? lwip_ntohs(a.sin_port) : 0;
}

int open_socket(int type) {
    const int fd = lwip_socket(AF_INET, type, 0);
    if (fd >= 0) {
        int one = 1;
        lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
    return fd;
}

}  // namespace

bool Endpoint::parse(const char* dotted_ipv4, std::uint16_t port, Endpoint& out) {
    ip4_addr_t a;
    if (dotted_ipv4 == nullptr || !ip4addr_aton(dotted_ipv4, &a)) return false;
    out = Endpoint{lwip_ntohl(ip4_addr_get_u32(&a)), port};
    return true;
}

// ----------------------------------------------------------- TcpConnection
bool TcpConnection::is_open() const { return h_.fd >= 0; }

core::Status TcpConnection::connect(const Endpoint& remote, std::uint32_t timeout_ms) {
    close();
    h_.fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (h_.fd < 0) return core::Status::Error;
    const int flags = lwip_fcntl(h_.fd, F_GETFL, 0);
    lwip_fcntl(h_.fd, F_SETFL, flags | O_NONBLOCK);
    const sockaddr_in a = to_sockaddr(remote);
    int r               = lwip_connect(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    if (r != 0 && errno == EINPROGRESS) {
        if (wait_fd(h_.fd, true, timeout_ms) <= 0) {
            close();
            return core::Status::Timeout;
        }
        int err       = 0;
        socklen_t len = sizeof(err);
        lwip_getsockopt(h_.fd, SOL_SOCKET, SO_ERROR, &err, &len);
        r = err == 0 ? 0 : -1;
    }
    if (r != 0) {
        close();
        return core::Status::Error;
    }
    lwip_fcntl(h_.fd, F_SETFL, flags);
    int one = 1;
    lwip_setsockopt(h_.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    peer_ = remote;
    return core::Status::Ok;
}

std::size_t TcpConnection::send(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::size_t sent           = 0;
    const std::uint64_t until  = osal::now_ms() + timeout_ms;
    while (h_.fd >= 0 && sent < len) {
        const std::uint64_t now = osal::now_ms();
        if (now > until || wait_fd(h_.fd, true, static_cast<std::uint32_t>(until - now)) <= 0) break;
        const ssize_t n = lwip_send(h_.fd, data + sent, len - sent, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    return sent;
}

core::Status TcpConnection::receive(std::uint8_t* buf, std::size_t cap, std::size_t& got, std::uint32_t timeout_ms) {
    got = 0;
    if (h_.fd < 0) return core::Status::Closed;
    const int w = wait_fd(h_.fd, false, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    const ssize_t n = lwip_recv(h_.fd, buf, cap, 0);
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
    if (lwip_bind(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0 || lwip_listen(h_.fd, 2) != 0) {
        close();
        return core::Status::Busy;
    }
    local_port_ = bound_port(h_.fd);
    return core::Status::Ok;
}

bool TcpListener::is_open() const { return h_.fd >= 0; }

core::Status TcpListener::accept(TcpConnection& out, std::uint32_t timeout_ms) {
    if (h_.fd < 0) return core::Status::Error;
    const int w = wait_fd(h_.fd, false, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    sockaddr_in a{};
    socklen_t n  = sizeof(a);
    const int fd = lwip_accept(h_.fd, reinterpret_cast<sockaddr*>(&a), &n);
    if (fd < 0) return core::Status::Timeout;
    out.close();
    out.h_.fd = fd;
    out.peer_ = from_sockaddr(a);
    int one   = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
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
    if (lwip_bind(h_.fd, reinterpret_cast<const sockaddr*>(&a), sizeof(a)) != 0) {
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
    const ssize_t n     = lwip_sendto(h_.fd, data, len, 0, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
    return n == static_cast<ssize_t>(len) ? core::Status::Ok : core::Status::Error;
}

core::Status UdpSocket::receive_from(std::uint8_t* buf, std::size_t cap, std::size_t& got, Endpoint& from,
                                     std::uint32_t timeout_ms) {
    got = 0;
    if (h_.fd < 0) return core::Status::Error;
    const int w = wait_fd(h_.fd, false, timeout_ms);
    if (w == 0) return core::Status::Timeout;
    if (w < 0) return core::Status::Error;
    sockaddr_in a{};
    socklen_t n     = sizeof(a);
    const ssize_t r = lwip_recvfrom(h_.fd, buf, cap, 0, reinterpret_cast<sockaddr*>(&a), &n);
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
