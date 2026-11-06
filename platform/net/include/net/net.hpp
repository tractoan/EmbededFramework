#pragma once
// Trừu tượng hóa TCP/IP (IPv4) – cùng mô hình OSAL: API trung lập ở đây, port chọn lúc build
// (EP_NET_PORT: posix = BSD socket của host, lwip = lwIP socket API trên MCU).
//
// Mọi thao tác chặn đều có timeout (ms) và không cấp phát heap.
// Một socket được phép: 1 thread đọc + nhiều thread ghi, với điều kiện chỉ thread đọc mở/đóng nó
// (comm::TcpServerStream... tuân theo quy tắc này).
#include <cstddef>
#include <cstdint>

#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "net_port_types.hpp"   // net::port::SocketHandle – do port cung cấp

namespace net {

// Địa chỉ IPv4 + cổng, theo thứ tự byte của máy (host order).
struct Endpoint {
    std::uint32_t address = 0;   // vd. 127.0.0.1 = 0x7F000001
    std::uint16_t port    = 0;

    static constexpr Endpoint any(std::uint16_t port) { return Endpoint{0, port}; }
    static constexpr Endpoint loopback(std::uint16_t port) { return Endpoint{0x7F000001u, port}; }
    static bool parse(const char* dotted_ipv4, std::uint16_t port, Endpoint& out);   // "a.b.c.d"
    bool operator==(const Endpoint& o) const { return address == o.address && port == o.port; }
};

class TcpConnection : core::NonCopyable {
public:
    TcpConnection() = default;
    ~TcpConnection() { close(); }

    core::Status connect(const Endpoint& remote, std::uint32_t timeout_ms);
    bool is_open() const;
    const Endpoint& peer() const { return peer_; }

    // Gửi toàn bộ len byte trong timeout; trả số byte đã gửi (< len nếu lỗi/timeout/đã đóng).
    std::size_t send(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms);
    // Ok (got > 0) | Timeout (got = 0) | Closed (phía kia đóng) | Error
    core::Status receive(std::uint8_t* buf, std::size_t cap, std::size_t& got, std::uint32_t timeout_ms);
    void close();
    // Đổi handle với kết nối khác (không cấp phát) – để thay kết nối dưới khóa trong thời gian ngắn nhất.
    void swap(TcpConnection& other) {
        const port::SocketHandle h = h_;
        const Endpoint p           = peer_;
        h_                         = other.h_;
        peer_                      = other.peer_;
        other.h_                   = h;
        other.peer_                = p;
    }

private:
    friend class TcpListener;
    port::SocketHandle h_{};
    Endpoint peer_{};
};

class TcpListener : core::NonCopyable {
public:
    TcpListener() = default;
    ~TcpListener() { close(); }

    // port = 0 -> hệ thống chọn cổng trống (xem local_port()); loopback_only -> chỉ 127.0.0.1
    core::Status open(std::uint16_t port, bool loopback_only = false);
    core::Status accept(TcpConnection& out, std::uint32_t timeout_ms);   // Ok | Timeout | Error
    bool is_open() const;
    std::uint16_t local_port() const { return local_port_; }
    void close();

private:
    port::SocketHandle h_{};
    std::uint16_t local_port_ = 0;
};

class UdpSocket : core::NonCopyable {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }

    core::Status open(std::uint16_t local_port, bool loopback_only = false);
    core::Status send_to(const Endpoint& to, const std::uint8_t* data, std::size_t len);
    // Một datagram; datagram dài hơn cap bị cắt. Ok | Timeout | Error
    core::Status receive_from(std::uint8_t* buf, std::size_t cap, std::size_t& got, Endpoint& from,
                              std::uint32_t timeout_ms);
    bool is_open() const;
    std::uint16_t local_port() const { return local_port_; }
    void close();

private:
    port::SocketHandle h_{};
    std::uint16_t local_port_ = 0;
};

}  // namespace net
