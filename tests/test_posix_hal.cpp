// HAL POSIX dùng chung (platform/hal/ports/common/posix): chạy trên mọi target có "posix" trong
// EP_HAL_COMMON (linux_sim, zynqmp_a53_linux qua qemu-aarch64, qnx trên board QNX).
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "posix/posix_hal.hpp"
#include "services/param_store.hpp"
#include "test_framework.hpp"

namespace {
std::string temp_path(const char* tag) {
    const char* dir = std::getenv("TMPDIR");
    return std::string(dir != nullptr && dir[0] != '\0' ? dir : "/tmp") + "/ep_test_" + tag + "_" + std::to_string(::getpid()) + ".bin";
}

std::size_t read_all(int fd, std::uint8_t* buf, std::size_t want) {
    std::size_t got = 0;
    for (int i = 0; i < 100 && got < want; ++i) {
        const ssize_t n = ::read(fd, buf + got, want - got);
        if (n > 0) got += static_cast<std::size_t>(n);
        else std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return got;
}
// Storage chậm như Flash thật + phát hiện hai chuỗi erase..write chồng lấn nhau.
class SlowCheckedStorage final : public hal::INvStorage {
public:
    explicit SlowCheckedStorage(hal::INvStorage& inner) : inner_(inner) {}
    std::size_t size() const override { return inner_.size(); }
    core::Status read(std::size_t o, void* d, std::size_t n) override { return slow([&] { return inner_.read(o, d, n); }); }
    core::Status write(std::size_t o, const void* d, std::size_t n) override {
        const bool last = o + n >= 8 + 2 * 8 + 4;   // header + 2 bản ghi + CRC: kết thúc một lần save
        const core::Status st = slow([&] { return inner_.write(o, d, n); });
        if (last) writers_.fetch_sub(1);
        return st;
    }
    core::Status erase() override {
        if (writers_.fetch_add(1) != 0) overlaps_.fetch_add(1);   // save khác đang dở dang
        return slow([&] { return inner_.erase(); });
    }
    int overlaps() const { return overlaps_.load(); }

private:
    template <typename F>
    core::Status slow(F f) {
        std::this_thread::sleep_for(std::chrono::microseconds(300));
        return f();
    }
    hal::INvStorage& inner_;
    std::atomic<int> writers_{0};
    std::atomic<int> overlaps_{0};
};
}  // namespace

TEST(posix_termios_uart_over_pty) {
    // Cặp pseudo-terminal theo POSIX (posix_openpt) – có trên cả Linux và QNX.
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    if (master < 0) return;
    CHECK(::grantpt(master) == 0 && ::unlockpt(master) == 0);
    char name[128] = {};
    const char* pts = ::ptsname(master);
    CHECK(pts != nullptr);
    if (pts == nullptr) return;
    std::strncpy(name, pts, sizeof(name) - 1);
    const int slave = ::open(name, O_RDWR | O_NOCTTY);   // giữ một đầu slave mở: tránh EIO ở master

    hal::host::TermiosUart uart(name);
    CHECK(uart.open(hal::UartConfig{}) == core::Status::Ok);   // 115200 8N1, raw
    CHECK(uart.is_open());

    // master -> uart: byte nhị phân đi qua nguyên vẹn (raw mode: không dịch CR/LF, không echo)
    const std::uint8_t tx[] = {0x01, 0x03, 0x00, 0x0D, 0x0A, 0xFF, 0x11, 0x13};
    CHECK_EQ(::write(master, tx, sizeof(tx)), static_cast<ssize_t>(sizeof(tx)));
    std::uint8_t rx[16] = {};
    std::size_t got = 0;
    for (int i = 0; i < 20 && got < sizeof(tx); ++i) got += uart.read(rx + got, sizeof(rx) - got, 50);
    CHECK_EQ(got, sizeof(tx));
    CHECK(std::memcmp(rx, tx, sizeof(tx)) == 0);

    // uart -> master
    const std::uint8_t out[] = {'o', 'k', '\n', 0x00, 0x80};
    CHECK_EQ(uart.write(out, sizeof(out), 100), sizeof(out));
    std::uint8_t back[8] = {};
    CHECK_EQ(read_all(master, back, sizeof(out)), sizeof(out));
    CHECK(std::memcmp(back, out, sizeof(out)) == 0);

    // không có dữ liệu: read trả về 0 sau khoảng timeout (không treo, không trả sớm bất thường)
    const auto t0 = std::chrono::steady_clock::now();
    CHECK_EQ(uart.read(rx, sizeof(rx), 30), 0u);
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    CHECK(dt.count() >= 25 && dt.count() < 500);

    hal::UartConfig bad{};
    bad.baudrate = 12345;
    CHECK(uart.open(bad) == core::Status::InvalidArg);
    uart.close();
    CHECK(!uart.is_open());
    ::close(slave);
    ::close(master);
}

TEST(posix_termios_uart_applies_line_settings) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    if (master < 0) return;
    CHECK(::grantpt(master) == 0 && ::unlockpt(master) == 0);
    const char* pts = ::ptsname(master);
    if (pts == nullptr) return;
    char name[128] = {};
    std::strncpy(name, pts, sizeof(name) - 1);
    const int probe = ::open(name, O_RDWR | O_NOCTTY);   // đọc lại cấu hình mà TermiosUart đã đặt
    CHECK(probe >= 0);

    // Pty của Linux luôn ép CS8 + không parity (drivers/tty/pty.c); UART thật và một số OS thì giữ.
    // Dò trước: chỉ kiểm tra data bits/parity khi thiết bị giữ được giá trị đã đặt.
    bool keeps_framing = false;
    {
        termios t{};
        if (probe >= 0 && ::tcgetattr(probe, &t) == 0) {
            t.c_cflag = (t.c_cflag & ~static_cast<tcflag_t>(CSIZE | PARODD)) | CS7 | PARENB;
            ::tcsetattr(probe, TCSANOW, &t);
            keeps_framing = ::tcgetattr(probe, &t) == 0 && (t.c_cflag & CSIZE) == CS7 && (t.c_cflag & PARENB) != 0;
        }
    }

    hal::host::TermiosUart uart(name);
    hal::UartConfig cfg{};
    cfg.baudrate  = 9600;
    cfg.data_bits = 7;
    cfg.parity    = hal::UartConfig::Parity::Even;
    cfg.stop_bits = 2;
    CHECK(core::is_ok(uart.open(cfg)));
    termios t{};
    CHECK(::tcgetattr(probe, &t) == 0);
    CHECK(::cfgetospeed(&t) == B9600 && ::cfgetispeed(&t) == B9600);
    if (keeps_framing) {
        CHECK((t.c_cflag & CSIZE) == CS7);
        CHECK((t.c_cflag & PARENB) != 0 && (t.c_cflag & PARODD) == 0);
    }
    CHECK((t.c_cflag & CSTOPB) != 0);
    CHECK((t.c_cflag & (CLOCAL | CREAD)) == (CLOCAL | CREAD));
    // raw mode: không canonical/echo/tín hiệu, không dịch CR/LF, không xử lý đầu ra, không XON/XOFF
    CHECK((t.c_lflag & (ICANON | ECHO | ISIG | IEXTEN)) == 0);
    CHECK((t.c_iflag & (ICRNL | INLCR | IGNCR | IXON | ISTRIP)) == 0);
    CHECK((t.c_oflag & OPOST) == 0);

    cfg.baudrate  = 115200;
    cfg.data_bits = 8;
    cfg.parity    = hal::UartConfig::Parity::Odd;
    cfg.stop_bits = 1;
    CHECK(core::is_ok(uart.open(cfg)));   // mở lại: cấu hình mới thay hoàn toàn cấu hình cũ
    CHECK(::tcgetattr(probe, &t) == 0);
    CHECK(::cfgetospeed(&t) == B115200);
    CHECK((t.c_cflag & CSIZE) == CS8);
    if (keeps_framing) CHECK((t.c_cflag & (PARENB | PARODD)) == (PARENB | PARODD));
    CHECK((t.c_cflag & CSTOPB) == 0);

    cfg.data_bits = 9;
    CHECK(uart.open(cfg) == core::Status::InvalidArg);
    uart.close();
    if (probe >= 0) ::close(probe);
    ::close(master);
}

TEST(posix_missing_uart_fails_cleanly) {
    hal::host::TermiosUart uart("/dev/ep-no-such-tty");
    CHECK(!core::is_ok(uart.open(hal::UartConfig{})));
    std::uint8_t b[4];
    CHECK_EQ(uart.read(b, sizeof(b), 1), 0u);
    CHECK_EQ(uart.write(b, sizeof(b), 1), 0u);
}

TEST(posix_file_storage_erased_semantics) {
    const std::string path = temp_path("nv");
    std::remove(path.c_str());
    hal::host::FileStorage nv(path.c_str(), 64);
    CHECK_EQ(nv.size(), 64u);

    std::uint8_t buf[8] = {};
    CHECK(core::is_ok(nv.read(10, buf, sizeof(buf))));   // chưa có file = Flash trắng
    for (std::uint8_t v : buf) CHECK_EQ(v, 0xFF);

    const std::uint8_t data[] = {1, 2, 3, 4};
    CHECK(core::is_ok(nv.write(20, data, sizeof(data))));
    std::uint8_t rd[6] = {};
    CHECK(core::is_ok(nv.read(19, rd, sizeof(rd))));
    const std::uint8_t expect[] = {0xFF, 1, 2, 3, 4, 0xFF};
    CHECK(std::memcmp(rd, expect, sizeof(expect)) == 0);

    CHECK(nv.write(62, data, sizeof(data)) == core::Status::OutOfRange);
    CHECK(nv.read(61, rd, sizeof(rd)) == core::Status::OutOfRange);

    CHECK(core::is_ok(nv.erase()));
    CHECK(core::is_ok(nv.read(19, rd, sizeof(rd))));
    for (std::uint8_t v : rd) CHECK_EQ(v, 0xFF);

    hal::host::FileStorage bad("/nonexistent-dir/ep/nv.bin", 64);
    CHECK(!core::is_ok(bad.write(0, data, sizeof(data))));
    CHECK(!core::is_ok(bad.erase()));
    std::remove(path.c_str());
}

TEST(param_store_concurrent_saves_keep_nv_valid) {
    static const svc::ParamDef defs[] = {
        {0x3001, "c.a", 0, 0, 100000, ""},
        {0x3002, "c.b", 0, 0, 100000, ""},
    };
    const std::string path = temp_path("ps");
    std::remove(path.c_str());
    hal::host::FileStorage file(path.c_str(), 256);
    SlowCheckedStorage nv(file);
    svc::ParamStore ps;
    ps.init(&nv, nullptr);
    CHECK(core::is_ok(ps.register_params(defs, 2)));

    // Hai luồng (vd. CLI "param save" và Modbus) cùng set + save liên tục.
    bool ok_a = true, ok_b = true;
    std::thread a([&] {
        for (int i = 1; i <= 50; ++i) {
            ps.set(0x3001, i);
            ok_a &= core::is_ok(ps.save());
        }
    });
    std::thread b([&] {
        for (int i = 1; i <= 50; ++i) {
            ps.set(0x3002, i * 2);
            ok_b &= core::is_ok(ps.save());
        }
    });
    a.join();
    b.join();
    CHECK(ok_a && ok_b);
    CHECK_EQ(nv.overlaps(), 0);                 // không có hai save xen kẽ

    svc::ParamStore fresh;
    fresh.init(&nv, nullptr);
    fresh.register_params(defs, 2);
    CHECK(fresh.load() == core::Status::Ok);    // bản ghi cuối hợp lệ (CRC đúng)
    CHECK_EQ(fresh.get_or(0x3001, -1), 50);     // save cuối cùng chứa giá trị mới nhất của cả hai
    CHECK_EQ(fresh.get_or(0x3002, -1), 100);
    std::remove(path.c_str());
}
