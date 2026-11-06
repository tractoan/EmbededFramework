#pragma once
// IByteStream trên TCP/IP – để MỌI giao thức (CLI, Modbus, MAVLink...) chạy qua mạng không cần sửa.
//
// Quy tắc luồng (giống mọi kênh của LinkManager): read() chỉ do thread RX của kênh gọi và là nơi
// DUY NHẤT mở/đóng kết nối; write() gọi được từ mọi thread. write() và việc đóng/mở kết nối được
// khóa chung, nên write() không bao giờ dùng một socket đã đóng (hoặc đã bị tái sử dụng).
#include <atomic>

#include "comm/byte_stream.hpp"
#include "net/net.hpp"
#include "osal/mutex.hpp"

namespace comm {

// TCP server một client: chấp nhận kết nối trong read(); client ngắt -> chờ client kế tiếp.
class TcpServerStream final : public IByteStream, private core::NonCopyable {
public:
    core::Status open(std::uint16_t port, bool loopback_only = false);
    std::uint16_t local_port() const { return listener_.local_port(); }
    bool connected() const { return connected_.load(); }
    std::uint32_t sessions() const { return sessions_.load(); }   // số client đã từng kết nối

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    void drop();
    net::TcpListener listener_;
    net::TcpConnection conn_;
    osal::Mutex mtx_;
    std::atomic<bool> connected_{false};
    std::atomic<std::uint32_t> sessions_{0};
};

// TCP client: tự kết nối lại (sau retry_ms) khi mất kết nối.
class TcpClientStream final : public IByteStream, private core::NonCopyable {
public:
    void configure(const net::Endpoint& remote, std::uint32_t retry_ms = 2000) {
        remote_   = remote;
        retry_ms_ = retry_ms;
    }
    bool connected() const { return connected_.load(); }

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    void drop();
    net::Endpoint remote_{};
    std::uint32_t retry_ms_   = 2000;
    std::uint64_t next_try_ms_ = 0;
    net::TcpConnection conn_;
    osal::Mutex mtx_;
    std::atomic<bool> connected_{false};
};

// UDP: mỗi datagram nhận được là một khối byte. Gửi tới `remote` cố định nếu có, ngược lại trả lời
// địa chỉ đã gửi gần nhất (kiểu "server"). Dùng cho MAVLink UDP, Modbus RTU-over-UDP...
class UdpStream final : public IByteStream, private core::NonCopyable {
public:
    static constexpr std::size_t kMaxDatagram = 512;

    core::Status open(std::uint16_t local_port, bool loopback_only = false);
    void set_remote(const net::Endpoint& remote);   // đích cố định (vd. trạm mặt đất 127.0.0.1:14550)
    std::uint16_t local_port() const { return sock_.local_port(); }

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    net::UdpSocket sock_;
    osal::Mutex mtx_;           // bảo vệ peer_/has_peer_
    net::Endpoint peer_{};
    bool has_peer_    = false;
    bool fixed_remote_ = false;
    std::uint8_t rx_[kMaxDatagram]{};
    std::size_t rx_len_ = 0;
    std::size_t rx_pos_ = 0;
};

}  // namespace comm
