// GPIO trên core PIO (Nios II): logic thuần với "thanh ghi" giả – chạy trên mọi host.
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>

#include "nios2/nios2_pio.hpp"
#include "test_framework.hpp"

namespace {
using hal::nios2::PioKind;
using hal::nios2::PioSpec;

// Thanh ghi giả: (base, reg) -> giá trị; DATA của PIO chỉ-xuất đọc ra "rác" như phần cứng (không đọc lại được).
struct FakeIo {
    static std::map<std::pair<std::uintptr_t, unsigned>, std::uint32_t>& regs() {
        static std::map<std::pair<std::uintptr_t, unsigned>, std::uint32_t> r;
        return r;
    }
    static std::map<std::pair<std::uintptr_t, unsigned>, int>& writes() {
        static std::map<std::pair<std::uintptr_t, unsigned>, int> w;
        return w;
    }
    static std::uint32_t read(std::uintptr_t base, unsigned reg) { return regs()[{base, reg}]; }
    static void write(std::uintptr_t base, unsigned reg, std::uint32_t v) {
        regs()[{base, reg}] = v;
        ++writes()[{base, reg}];
    }
};
struct TestLock {
    TestLock() { m().lock(); }
    ~TestLock() { m().unlock(); }
    static std::recursive_mutex& m() {
        static std::recursive_mutex mtx;
        return mtx;
    }
};
using Pin = hal::nios2::PioPin<FakeIo, TestLock>;
std::uint32_t& R(std::uintptr_t base, unsigned reg) { return FakeIo::regs()[std::make_pair(base, reg)]; }
int W(std::uintptr_t base, unsigned reg) { return FakeIo::writes()[std::make_pair(base, reg)]; }
constexpr unsigned kData = 0, kDir = 1, kSet = 4, kClr = 5;
}  // namespace

TEST(nios2_pio_output_only_shares_shadow_between_pins) {
    constexpr std::uintptr_t led = 0x41070;
    R(led, kData) = 0xDEADBEEF;   // PIO chỉ-xuất: đọc DATA không phản ánh giá trị đang xuất
    Pin status(PioSpec{led, 0, PioKind::Output, false, false, 0});
    Pin alarm(PioSpec{led, 1, PioKind::Output, false, false, 0});
    CHECK(core::is_ok(status.configure(hal::PinMode::Output)));
    CHECK(core::is_ok(alarm.configure(hal::PinMode::Output)));
    CHECK_EQ(R(led, kData), 0u);          // mức an toàn, KHÔNG đọc-sửa-ghi từ DATA (rác)
    CHECK(core::is_ok(status.write(true)));
    CHECK(core::is_ok(alarm.write(true)));
    CHECK_EQ(R(led, kData), 0x3u);        // hai chân cùng PIO không ghi đè nhau
    CHECK(core::is_ok(status.write(false)));
    CHECK_EQ(R(led, kData), 0x2u);
    CHECK(alarm.read() && !status.read());               // đọc chân ra = bản sao, không phải DATA
    CHECK(alarm.configure(hal::PinMode::Input) == core::Status::NotSupported);
}

TEST(nios2_pio_active_low_set_clear_and_reset_value) {
    constexpr std::uintptr_t led = 0x50000;
    Pin pin(PioSpec{led, 4, PioKind::Output, true, true, 0xFF});   // OUTSET/OUTCLEAR, LED nối lên VCC
    CHECK(core::is_ok(pin.configure(hal::PinMode::Output)));
    CHECK_EQ(R(led, kSet), 1u << 4);      // tắt (an toàn) = chân lên 1 -> OUTSET
    CHECK_EQ(W(led, kData), 0);         // không chạm DATA
    CHECK(core::is_ok(pin.write(true)));                 // bật = chân xuống 0 -> OUTCLEAR
    CHECK_EQ(R(led, kClr), 1u << 4);
    CHECK(pin.read());
    CHECK(core::is_ok(pin.write(false)));
    CHECK(!pin.read());
}

TEST(nios2_pio_input_and_bidir_direction) {
    constexpr std::uintptr_t key = 0x41060, bidir = 0x42000;
    Pin button(PioSpec{key, 0, PioKind::Input, false, true, 0});   // nút kéo lên: nhấn = 0
    CHECK(core::is_ok(button.configure(hal::PinMode::Input)));
    CHECK(button.write(true) == core::Status::NotSupported);
    R(key, kData) = 0x1;
    CHECK(!button.read());
    R(key, kData) = 0x0;
    CHECK(button.read());
    CHECK(button.configure(hal::PinMode::Output) == core::Status::NotSupported);

    R(bidir, kDir) = 0x1;                 // bit khác đã là output: phải giữ nguyên
    Pin io(PioSpec{bidir, 3, PioKind::Bidir, false, false, 0});
    CHECK(core::is_ok(io.configure(hal::PinMode::Output)));
    CHECK_EQ(R(bidir, kDir), 0x9u);
    CHECK(core::is_ok(io.configure(hal::PinMode::Input)));
    CHECK_EQ(R(bidir, kDir), 0x1u);
    R(bidir, kData) = 1u << 3;
    CHECK(io.read());

    Pin unused;   // base = 0: chân không có trên board này
    CHECK(unused.configure(hal::PinMode::Output) == core::Status::NotSupported);
    CHECK(!unused.read());
}
