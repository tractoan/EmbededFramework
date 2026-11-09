// TCP/IP: lớp net (socket) và các stream của LinkManager, chạy qua loopback.
#include <cstring>
#include <string>

#include "comm/net_streams.hpp"
#include "mocks.hpp"
#include "net/net.hpp"
#include "osal/time.hpp"
#include "test_framework.hpp"

namespace {
std::string receive_text(net::TcpConnection& c, std::size_t want, std::uint32_t timeout_ms = 1000) {
    std::string out;
    std::uint8_t buf[64];
    const std::uint64_t until = osal::now_ms() + timeout_ms;
    while (out.size() < want && osal::now_ms() < until) {
        std::size_t got = 0;
        if (core::is_ok(c.receive(buf, sizeof(buf), got, 20))) out.append(reinterpret_cast<char*>(buf), got);
    }
    return out;
}

// Đọc stream (như thread RX của kênh) cho tới khi có đủ dữ liệu.
std::string read_stream(comm::IByteStream& s, std::size_t want, int rounds = 100) {
    std::string out;
    std::uint8_t buf[64];
    for (int i = 0; i < rounds && out.size() < want; ++i) {
        const std::size_t n = s.read(buf, sizeof(buf), 10);
        out.append(reinterpret_cast<char*>(buf), n);
    }
    return out;
}
}  // namespace

TEST(net_endpoint_parse) {
    net::Endpoint e{};
    CHECK(net::Endpoint::parse("192.168.1.20", 5760, e));
    CHECK(e.address == 0xC0A80114u && e.port == 5760);
    CHECK(!net::Endpoint::parse("300.1.1.1", 1, e));
    CHECK(!net::Endpoint::parse("abc", 1, e));
    CHECK(net::Endpoint::loopback(7).address == 0x7F000001u);
}

TEST(net_tcp_loopback_send_receive_close) {
    net::TcpListener listener;
    CHECK(core::is_ok(listener.open(0, true)));   // cổng trống bất kỳ, chỉ loopback
    CHECK(listener.local_port() != 0);

    net::TcpConnection server, client;
    CHECK(listener.accept(server, 20) == core::Status::Timeout);
    CHECK(core::is_ok(client.connect(net::Endpoint::loopback(listener.local_port()), 1000)));
    CHECK(core::is_ok(listener.accept(server, 1000)));

    const char hello[] = "hello";
    CHECK_EQ(client.send(reinterpret_cast<const std::uint8_t*>(hello), 5, 500), 5u);
    CHECK(receive_text(server, 5) == "hello");
    CHECK_EQ(server.send(reinterpret_cast<const std::uint8_t*>("ok"), 2, 500), 2u);
    CHECK(receive_text(client, 2) == "ok");

    std::uint8_t b[8];
    std::size_t got = 0;
    CHECK(server.receive(b, sizeof(b), got, 20) == core::Status::Timeout);
    client.close();
    CHECK(server.receive(b, sizeof(b), got, 500) == core::Status::Closed);   // phía kia đóng -> Closed

    net::TcpConnection refused;   // không ai lắng nghe
    listener.close();
    CHECK(!core::is_ok(refused.connect(net::Endpoint::loopback(1), 200)));
}

TEST(net_udp_loopback) {
    net::UdpSocket a, b;
    CHECK(core::is_ok(a.open(0, true)));
    CHECK(core::is_ok(b.open(0, true)));
    CHECK(core::is_ok(a.send_to(net::Endpoint::loopback(b.local_port()), reinterpret_cast<const std::uint8_t*>("ping"), 4)));
    std::uint8_t buf[16];
    std::size_t got = 0;
    net::Endpoint from{};
    CHECK(core::is_ok(b.receive_from(buf, sizeof(buf), got, from, 1000)));
    CHECK(got == 4 && std::memcmp(buf, "ping", 4) == 0);
    CHECK(from.port == a.local_port());
    CHECK(b.receive_from(buf, sizeof(buf), got, from, 20) == core::Status::Timeout);
}

TEST(net_tcp_server_stream_sessions) {
    comm::TcpServerStream s;
    CHECK(core::is_ok(s.open(0, true)));
    CHECK_EQ(s.write(reinterpret_cast<const std::uint8_t*>("x"), 1, 10), 0u);   // chưa có client: bỏ dữ liệu

    for (int session = 1; session <= 2; ++session) {   // client 1 ngắt -> client 2 được nhận
        net::TcpConnection client;
        CHECK(core::is_ok(client.connect(net::Endpoint::loopback(s.local_port()), 1000)));
        CHECK(mock::wait_until([&] {
            std::uint8_t b[4];
            s.read(b, sizeof(b), 10);
            return s.connected();
        }));
        CHECK_EQ(s.sessions(), static_cast<std::uint32_t>(session));
        CHECK_EQ(client.send(reinterpret_cast<const std::uint8_t*>("abc"), 3, 500), 3u);
        CHECK(read_stream(s, 3) == "abc");
        CHECK_EQ(s.write(reinterpret_cast<const std::uint8_t*>("reply"), 5, 500), 5u);
        CHECK(receive_text(client, 5) == "reply");
        client.close();
        CHECK(mock::wait_until([&] {
            std::uint8_t b[4];
            s.read(b, sizeof(b), 10);
            return !s.connected();
        }));
    }
}

TEST(net_tcp_client_stream_reconnects) {
    net::TcpListener listener;
    CHECK(core::is_ok(listener.open(0, true)));
    comm::TcpClientStream c;
    c.configure(net::Endpoint::loopback(listener.local_port()), 50);
    std::uint8_t b[8];
    c.read(b, sizeof(b), 50);   // kết nối
    CHECK(c.connected());
    net::TcpConnection server;
    CHECK(core::is_ok(listener.accept(server, 1000)));
    CHECK_EQ(c.write(reinterpret_cast<const std::uint8_t*>("hi"), 2, 500), 2u);
    CHECK(receive_text(server, 2) == "hi");

    server.close();   // server ngắt -> client phát hiện, tự kết nối lại sau retry_ms
    CHECK(mock::wait_until([&] {
        c.read(b, sizeof(b), 10);
        return !c.connected();
    }));
    CHECK(mock::wait_until([&] {
        c.read(b, sizeof(b), 10);
        return c.connected();
    }));
    CHECK(core::is_ok(listener.accept(server, 1000)));
}

TEST(net_udp_stream_remote_and_reply_to_sender) {
    net::UdpSocket gcs;
    CHECK(core::is_ok(gcs.open(0, true)));
    comm::UdpStream fixed;
    CHECK(core::is_ok(fixed.open(0, true)));
    fixed.set_remote(net::Endpoint::loopback(gcs.local_port()));
    CHECK_EQ(fixed.write(reinterpret_cast<const std::uint8_t*>("tm"), 2, 10), 2u);
    std::uint8_t buf[16];
    std::size_t got = 0;
    net::Endpoint from{};
    CHECK(core::is_ok(gcs.receive_from(buf, sizeof(buf), got, from, 1000)) && got == 2);

    comm::UdpStream server;   // không có remote cố định: trả lời người gửi gần nhất
    CHECK(core::is_ok(server.open(0, true)));
    CHECK_EQ(server.write(reinterpret_cast<const std::uint8_t*>("?"), 1, 10), 0u);
    gcs.send_to(net::Endpoint::loopback(server.local_port()), reinterpret_cast<const std::uint8_t*>("query"), 5);
    CHECK(read_stream(server, 5) == "query");
    CHECK_EQ(server.write(reinterpret_cast<const std::uint8_t*>("answer"), 6, 10), 6u);
    CHECK(core::is_ok(gcs.receive_from(buf, sizeof(buf), got, from, 1000)));
    CHECK(got == 6 && std::memcmp(buf, "answer", 6) == 0);
}
