#include "comm/net_streams.hpp"

#include <algorithm>
#include <cstring>

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace comm {

namespace {
constexpr const char* kTag = "net";

unsigned octet(const net::Endpoint& e, int i) { return (e.address >> (24 - 8 * i)) & 0xFFu; }
}  // namespace

// ========================================================== TcpServerStream
core::Status TcpServerStream::open(std::uint16_t port, bool loopback_only) {
    const core::Status st = listener_.open(port, loopback_only);
    if (core::is_ok(st)) {
        LOGI(kTag, "tcp server listening on port %u", static_cast<unsigned>(listener_.local_port()));
    } else {
        LOGE(kTag, "tcp server: cannot listen on port %u (%s)", static_cast<unsigned>(port), core::to_string(st));
    }
    return st;
}

void TcpServerStream::drop() {
    osal::LockGuard g(mtx_);
    conn_.close();
    connected_ = false;
}

std::size_t TcpServerStream::write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    osal::LockGuard g(mtx_);
    return connected_.load() ? conn_.send(data, len, timeout_ms) : 0;   // chưa có client: bỏ dữ liệu
}

std::size_t TcpServerStream::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (!listener_.is_open()) {
        osal::sleep_ms(timeout_ms);
        return 0;
    }
    if (!connected_.load()) {
        net::TcpConnection incoming;
        if (!core::is_ok(listener_.accept(incoming, timeout_ms))) return 0;
        const net::Endpoint p = incoming.peer();
        {
            osal::LockGuard g(mtx_);
            conn_.swap(incoming);   // không cấp phát: chỉ đổi handle
            connected_ = true;
        }
        sessions_.fetch_add(1);
        LOGI(kTag, "tcp client %u.%u.%u.%u:%u connected", octet(p, 0), octet(p, 1), octet(p, 2), octet(p, 3),
             static_cast<unsigned>(p.port));
        return 0;
    }
    std::size_t got       = 0;
    const core::Status st = conn_.receive(data, len, got, timeout_ms);   // chỉ thread này đóng conn_ -> an toàn
    if (st == core::Status::Closed || st == core::Status::Error) {
        LOGI(kTag, "tcp client disconnected");
        drop();
        return 0;
    }
    return got;
}

// ========================================================== TcpClientStream
void TcpClientStream::drop() {
    osal::LockGuard g(mtx_);
    conn_.close();
    connected_ = false;
}

std::size_t TcpClientStream::write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    osal::LockGuard g(mtx_);
    return connected_.load() ? conn_.send(data, len, timeout_ms) : 0;
}

std::size_t TcpClientStream::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (!connected_.load()) {
        const std::uint64_t now = osal::now_ms();
        if (remote_.port == 0 || now < next_try_ms_) {
            osal::sleep_ms(timeout_ms);
            return 0;
        }
        net::TcpConnection c;
        if (!core::is_ok(c.connect(remote_, timeout_ms))) {
            next_try_ms_ = now + retry_ms_;
            return 0;
        }
        {
            osal::LockGuard g(mtx_);
            conn_.swap(c);
            connected_ = true;
        }
        LOGI(kTag, "tcp connected to %u.%u.%u.%u:%u", octet(remote_, 0), octet(remote_, 1), octet(remote_, 2),
             octet(remote_, 3), static_cast<unsigned>(remote_.port));
        return 0;
    }
    std::size_t got       = 0;
    const core::Status st = conn_.receive(data, len, got, timeout_ms);
    if (st == core::Status::Closed || st == core::Status::Error) {
        LOGW(kTag, "tcp connection lost, retry in %lu ms", static_cast<unsigned long>(retry_ms_));
        drop();
        next_try_ms_ = osal::now_ms() + retry_ms_;
        return 0;
    }
    return got;
}

// ================================================================ UdpStream
core::Status UdpStream::open(std::uint16_t local_port, bool loopback_only) {
    const core::Status st = sock_.open(local_port, loopback_only);
    if (core::is_ok(st)) {
        LOGI(kTag, "udp bound to port %u", static_cast<unsigned>(sock_.local_port()));
    } else {
        LOGE(kTag, "udp: cannot bind port %u (%s)", static_cast<unsigned>(local_port), core::to_string(st));
    }
    return st;
}

void UdpStream::set_remote(const net::Endpoint& remote) {
    osal::LockGuard g(mtx_);
    peer_         = remote;
    has_peer_     = true;
    fixed_remote_ = true;
}

std::size_t UdpStream::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    net::Endpoint to{};
    {
        osal::LockGuard g(mtx_);
        if (!has_peer_) return 0;   // chưa biết gửi cho ai: bỏ dữ liệu
        to = peer_;
    }
    return core::is_ok(sock_.send_to(to, data, len)) ? len : 0;
}

std::size_t UdpStream::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    if (!sock_.is_open()) {
        osal::sleep_ms(timeout_ms);
        return 0;
    }
    if (rx_pos_ >= rx_len_) {
        net::Endpoint from{};
        std::size_t got = 0;
        if (!core::is_ok(sock_.receive_from(rx_, sizeof(rx_), got, from, timeout_ms))) return 0;
        rx_len_ = got;
        rx_pos_ = 0;
        osal::LockGuard g(mtx_);
        if (!fixed_remote_) {   // chế độ "server": trả lời người gửi gần nhất
            peer_     = from;
            has_peer_ = true;
        }
    }
    const std::size_t n = std::min(len, rx_len_ - rx_pos_);
    std::memcpy(data, rx_ + rx_pos_, n);
    rx_pos_ += n;
    return n;
}

}  // namespace comm
