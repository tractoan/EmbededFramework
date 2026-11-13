// hal::fdio::FdUart (UART qua file descriptor không chặn – Nios II HAL) kiểm tra trên host bằng pty.
#include <fcntl.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "fdio/fd_uart.hpp"
#include "test_framework.hpp"

TEST(fd_uart_nonblocking_io_and_timeouts_over_pty) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0);
    if (master < 0) return;
    CHECK(::grantpt(master) == 0 && ::unlockpt(master) == 0);
    const char* pts = ::ptsname(master);
    if (pts == nullptr) return;
    char name[128] = {};
    std::strncpy(name, pts, sizeof(name) - 1);
    const int keep = ::open(name, O_RDWR | O_NOCTTY);   // giữ đầu slave mở
    termios t{};                                        // pty mặc định có echo/canonical: đặt raw như UART
    if (keep >= 0 && ::tcgetattr(keep, &t) == 0) {
        t.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | ISIG | IEXTEN);
        t.c_iflag &= ~static_cast<tcflag_t>(ICRNL | IXON);
        t.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        ::tcsetattr(keep, TCSANOW, &t);
    }

    hal::fdio::FdUart uart(name);
    CHECK(core::is_ok(uart.open(hal::UartConfig{})));
    const char msg[] = "ping\n";
    CHECK_EQ(::write(master, msg, 5), 5);
    std::uint8_t rx[16] = {};
    std::size_t got = 0;
    for (int i = 0; i < 20 && got < 5; ++i) got += uart.read(rx + got, sizeof(rx) - got, 20);
    CHECK_EQ(got, 5u);
    CHECK(std::memcmp(rx, "ping\n", 5) == 0);

    const auto t0 = std::chrono::steady_clock::now();
    CHECK_EQ(uart.read(rx, sizeof(rx), 30), 0u);         // không có dữ liệu: chờ ~timeout, không quay bận vô hạn
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    CHECK(dt.count() >= 25 && dt.count() < 500);

    CHECK_EQ(uart.write(reinterpret_cast<const std::uint8_t*>("ok\n"), 3, 50), 3u);
    char back[8] = {};
    long n = 0;
    for (int i = 0; i < 50 && n <= 0; ++i) {
        n = ::read(master, back, sizeof(back));
        if (n <= 0) ::usleep(2000);
    }
    CHECK(n >= 2 && back[0] == 'o' && back[1] == 'k');

    uart.close();
    CHECK(!uart.is_open());
    hal::fdio::FdUart missing("/dev/ep-no-such-jtag-uart");
    CHECK(missing.open(hal::UartConfig{}) == core::Status::NotFound);
    CHECK_EQ(missing.read(rx, 4, 5), 0u);
    hal::fdio::FdUart disabled("");                       // cổng tắt trong cấu hình
    CHECK(disabled.open(hal::UartConfig{}) == core::Status::NotFound);
    if (keep >= 0) ::close(keep);
    ::close(master);
}
