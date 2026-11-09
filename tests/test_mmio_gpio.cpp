// GPIO thanh ghi (common/mmio): logic thuần, chạy trên mọi host với "thanh ghi" là mảng thường.
#include <cstdint>

#include "mmio/mmio_gpio.hpp"
#include "test_framework.hpp"

namespace {
using hal::mmio::GpioSpec;
using hal::mmio::parse_gpio_spec;
}  // namespace

TEST(mmio_gpio_spec_parsing) {
    GpioSpec s;
    CHECK(parse_gpio_spec("base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=23", s));
    CHECK(s.base == 0xFF0A0000u && s.out == 0x40 && s.in == 0x60 && s.dir == 0x204 && s.oen == 0x208);
    CHECK(s.bit == 23 && s.has_in && s.has_dir && !s.dir_inverted && s.has_oen && !s.has_set_clr && !s.active_low);
    CHECK_EQ(s.span(), 0x20Cu);

    CHECK(parse_gpio_spec("base=0x30200000,out=0,dir=4,bit=13,active_low=1", s));
    CHECK(s.in == s.out && s.active_low && !s.has_oen);   // in mặc định = out

    CHECK(parse_gpio_spec("base=0x44E07000,out=0x13C,in=0x138,dir_n=0x134,set=0x194,clr=0x190,bit=21", s));
    CHECK(s.dir_inverted && s.has_set_clr && s.set == 0x194 && s.clr == 0x190);

    CHECK(!parse_gpio_spec("", s));
    CHECK(!parse_gpio_spec(nullptr, s));
    CHECK(!parse_gpio_spec("out=0x0,dir=0x4,bit=1", s));                    // thiếu base
    CHECK(!parse_gpio_spec("base=0x1000,dir=0x4,bit=1", s));                // thiếu out
    CHECK(!parse_gpio_spec("base=0x1000,out=0x0,bit=1", s));                // thiếu hướng
    CHECK(!parse_gpio_spec("base=0x1000,out=0x0,dir=0x4", s));              // thiếu bit
    CHECK(!parse_gpio_spec("base=0x1000,out=0x0,dir=0x4,bit=32", s));       // bit ngoài thanh ghi
    CHECK(!parse_gpio_spec("base=0x1000,out=0x2,dir=0x4,bit=1", s));        // không căn 4
    CHECK(!parse_gpio_spec("base=0x1000,out=0x0,dir=0x4,bit=1,set=0x8", s)); // set thiếu clr
    CHECK(!parse_gpio_spec("base=0x1000,out=0x0,dir=0x4,bit=1,foo=1", s));  // khóa lạ
    CHECK(!parse_gpio_spec("base=0x1000,out=zz,dir=0x4,bit=1", s));         // số sai
    CHECK(parse_gpio_spec("base=0x1000,out=0x0,dir=0x4,bit=1,", s));        // dấu phẩy cuối: chấp nhận
}

TEST(mmio_gpio_rmw_dir_oen_and_shared_register) {
    // Bố cục kiểu Zynq: out 0x40, in 0x60, dir 0x204, oen 0x208 (offset byte; mảng 32 bit)
    static volatile std::uint32_t regs[0x210 / 4] = {};
    regs[0x40 / 4]  = 0xA5000000u;   // bit khác trong bank phải được giữ nguyên
    regs[0x204 / 4] = 0x00000001u;
    GpioSpec led_spec, btn_spec;
    CHECK(parse_gpio_spec("base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=23", led_spec));
    CHECK(parse_gpio_spec("base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=22", btn_spec));
    hal::mmio::GpioPin led, btn;
    CHECK(led.configure(hal::PinMode::Output) == core::Status::NotSupported);   // chưa attach
    led.attach(regs, led_spec);
    btn.attach(regs, btn_spec);

    CHECK(core::is_ok(led.configure(hal::PinMode::Output)));
    CHECK(core::is_ok(btn.configure(hal::PinMode::InputPullUp)));
    CHECK_EQ(regs[0x204 / 4], 0x00800001u);   // chỉ bit 23 là output
    CHECK_EQ(regs[0x208 / 4], 0x00800000u);   // OEN bật cho LED, không cho nút
    CHECK_EQ(regs[0x40 / 4], 0xA5000000u);    // configure output đặt mức an toàn 0 trước

    CHECK(core::is_ok(led.write(true)));
    CHECK_EQ(regs[0x40 / 4], 0xA5800000u);
    regs[0x60 / 4] = 0x00400000u;             // phần cứng: nút đang ở mức 1, LED đọc về 0
    CHECK(btn.read());
    CHECK(!led.read());
    CHECK(core::is_ok(led.write(false)));
    CHECK_EQ(regs[0x40 / 4] & 0x00800000u, 0u);
    CHECK_EQ(regs[0x40 / 4] & 0xFF000000u, 0xA5000000u);
}

TEST(mmio_gpio_inverted_dir_set_clr_active_low) {
    // Bố cục kiểu TI AM335x: OE (0 = output), DATAIN, SET/CLEAR (W1S/W1C)
    static volatile std::uint32_t regs[0x198 / 4] = {};
    regs[0x134 / 4] = 0xFFFFFFFFu;   // reset: tất cả input
    GpioSpec spec;
    CHECK(parse_gpio_spec("base=0x44E07000,out=0x13C,in=0x138,dir_n=0x134,set=0x194,clr=0x190,bit=21,active_low=1",
                          spec));
    hal::mmio::GpioPin pin;
    pin.attach(regs, spec);
    CHECK(core::is_ok(pin.configure(hal::PinMode::Output)));
    CHECK_EQ(regs[0x134 / 4], 0xFFDFFFFFu);    // bit 21 = 0 -> output
    CHECK_EQ(regs[0x194 / 4], 1u << 21);       // active_low: mức an toàn "tắt" = chân lên 1 (SET)

    regs[0x190 / 4] = 0;
    regs[0x194 / 4] = 0;
    CHECK(core::is_ok(pin.write(true)));       // bật = kéo chân xuống: ghi CLEAR, không đụng SET
    CHECK_EQ(regs[0x190 / 4], 1u << 21);
    CHECK_EQ(regs[0x194 / 4], 0u);
    CHECK_EQ(regs[0x13C / 4], 0u);             // không đọc-sửa-ghi thanh ghi out
    regs[0x138 / 4] = 0;                       // chân đang ở 0 -> logic 1 (active low)
    CHECK(pin.read());
    CHECK(core::is_ok(pin.configure(hal::PinMode::Input)));
    CHECK_EQ(regs[0x134 / 4], 0xFFFFFFFFu);
}
