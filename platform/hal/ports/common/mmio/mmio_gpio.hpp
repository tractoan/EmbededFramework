#pragma once
// GPIO qua thanh ghi memory-mapped – dùng khi OS không có API GPIO chuẩn (QNX, bare-metal có MMU...).
// Header-only, không phụ thuộc OS: phần ánh xạ địa chỉ vật lý do port OS làm (vd. mmap_device_memory
// trên QNX), lớp này chỉ đọc/ghi thanh ghi qua con trỏ -> unit test được trên host với mảng thường.
//
// Mô hình thanh ghi (đủ cho phần lớn khối GPIO: Zynq/ZynqMP, i.MX, TI AM3/AM6, STM32MP...):
//   out   : thanh ghi dữ liệu ra (đọc-sửa-ghi), hoặc set/clr nếu khối có thanh ghi W1S/W1C (atomic)
//   in    : thanh ghi trạng thái chân (mặc định = out)
//   dir   : hướng, bit = 1 -> output        | dir_n : hướng, bit = 0 -> output (vd. TI GPIO_OE)
//   oen   : (tùy chọn) output enable, bit = 1 -> bật driver (Zynq OEN)
// Pull-up/down KHÔNG cấu hình ở đây (nằm ở khối pinmux/IOMUX của SoC – đặt trong device tree / BSP).
//
// Chuỗi cấu hình (biến môi trường), các khóa phân cách bằng dấu phẩy, số theo cú pháp C (0x.., thập phân):
//   "base=0xFF0A0000,out=0x40,in=0x60,dir=0x204,oen=0x208,bit=23"          (ZynqMP MIO23)
//   "base=0x30200000,out=0x0,in=0x8,dir=0x4,bit=13,active_low=1"           (i.MX8M GPIO1_IO13)
//   "base=0x44E07000,out=0x13C,in=0x138,dir_n=0x134,set=0x194,clr=0x190,bit=21"  (AM335x GPIO0_21)
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "hal/gpio.hpp"

namespace hal::mmio {

struct GpioSpec {
    std::uint64_t base = 0;            // địa chỉ vật lý của khối GPIO
    std::uint32_t out  = 0;            // offset (byte) các thanh ghi tính từ base
    std::uint32_t in   = 0;
    std::uint32_t dir  = 0;
    std::uint32_t oen  = 0;
    std::uint32_t set  = 0;
    std::uint32_t clr  = 0;
    std::uint8_t bit   = 0;
    bool has_in = false, has_dir = false, dir_inverted = false, has_oen = false, has_set_clr = false;
    bool active_low = false;           // LED nối lên VCC: mức logic 1 = chân xuống 0

    // Vùng cần ánh xạ: [base, base + span()) – lớn nhất trong các offset + 4 byte.
    std::uint32_t span() const {
        std::uint32_t m = out;
        for (std::uint32_t o : {in, dir, oen, set, clr}) m = o > m ? o : m;
        return m + 4u;
    }
};

// Phân tích chuỗi cấu hình. Trả về false nếu thiếu base/out/hướng, khóa lạ, số sai, offset không căn 4,
// bit >= 32, hoặc chỉ có một trong hai set/clr.
inline bool parse_gpio_spec(const char* text, GpioSpec& out_spec) {
    if (text == nullptr || text[0] == '\0') return false;
    GpioSpec s;
    bool have_base = false, have_out = false, have_bit = false, have_set = false, have_clr = false;
    const char* p = text;
    while (*p != '\0') {
        const char* eq = std::strchr(p, '=');
        if (eq == nullptr) return false;
        const std::size_t klen = static_cast<std::size_t>(eq - p);
        char* end = nullptr;
        const unsigned long long v = std::strtoull(eq + 1, &end, 0);
        if (end == eq + 1 || (*end != ',' && *end != '\0')) return false;
        auto key = [&](const char* k) { return std::strlen(k) == klen && std::strncmp(p, k, klen) == 0; };
        auto reg = [&](std::uint32_t& dst) {
            if (v > 0xFFFFu || (v & 3u) != 0) return false;   // offset trong khối, căn 32 bit
            dst = static_cast<std::uint32_t>(v);
            return true;
        };
        bool ok = true;
        if (key("base"))            { s.base = v; have_base = true; }
        else if (key("out"))        { ok = reg(s.out); have_out = true; }
        else if (key("in"))         { ok = reg(s.in); s.has_in = true; }
        else if (key("dir"))        { ok = reg(s.dir); s.has_dir = true; s.dir_inverted = false; }
        else if (key("dir_n"))      { ok = reg(s.dir); s.has_dir = true; s.dir_inverted = true; }
        else if (key("oen"))        { ok = reg(s.oen); s.has_oen = true; }
        else if (key("set"))        { ok = reg(s.set); have_set = true; }
        else if (key("clr"))        { ok = reg(s.clr); have_clr = true; }
        else if (key("bit"))        { ok = v < 32; s.bit = static_cast<std::uint8_t>(v); have_bit = true; }
        else if (key("active_low")) { ok = v <= 1; s.active_low = v == 1; }
        else return false;
        if (!ok) return false;
        p = *end == ',' ? end + 1 : end;
    }
    if (!have_base || !have_out || !have_bit || !s.has_dir || have_set != have_clr) return false;
    s.has_set_clr = have_set;
    if (!s.has_in) s.in = s.out;
    out_spec = s;
    return true;
}

// Một chân GPIO trên vùng thanh ghi đã ánh xạ. `regs` trỏ tới địa chỉ ảo tương ứng với spec.base.
class GpioPin final : public IGpio {
public:
    GpioPin() = default;
    void attach(volatile std::uint32_t* regs, const GpioSpec& spec) {
        regs_ = regs;
        spec_ = spec;
    }
    bool attached() const { return regs_ != nullptr; }

    core::Status configure(PinMode mode) override {
        if (regs_ == nullptr) return core::Status::NotSupported;
        output_ = mode == PinMode::Output || mode == PinMode::OutputOpenDrain;
        if (output_) write(false);   // mức an toàn (tắt) TRƯỚC khi bật driver ra
        const bool dir_bit = spec_.dir_inverted ? !output_ : output_;
        modify(spec_.dir, dir_bit);
        if (spec_.has_oen) modify(spec_.oen, output_);
        return core::Status::Ok;
    }

    core::Status write(bool level) override {
        if (regs_ == nullptr) return core::Status::NotSupported;
        const bool phys = level != spec_.active_low;
        if (spec_.has_set_clr) {
            reg(phys ? spec_.set : spec_.clr) = mask();   // W1S/W1C: atomic, không cần khóa
        } else {
            modify(spec_.out, phys);
        }
        return core::Status::Ok;
    }

    bool read() const override {
        if (regs_ == nullptr) return false;
        const bool phys = (reg(spec_.in) & mask()) != 0;
        return phys != spec_.active_low;
    }

private:
    std::uint32_t mask() const { return 1u << spec_.bit; }
    volatile std::uint32_t& reg(std::uint32_t offset) const { return regs_[offset / 4u]; }

    // Đọc-sửa-ghi: nhiều chân dùng chung một thanh ghi -> khóa chung trong tiến trình.
    // (Driver khác ngoài tiến trình dùng cùng bank thì không bảo vệ được – xem ghi chú tài liệu.)
    void modify(std::uint32_t offset, bool on) const {
        while (lock().test_and_set(std::memory_order_acquire)) {
        }
        const std::uint32_t v = reg(offset);
        reg(offset)           = on ? (v | mask()) : (v & ~mask());
        lock().clear(std::memory_order_release);
    }
    static std::atomic_flag& lock() {
        static std::atomic_flag f = ATOMIC_FLAG_INIT;
        return f;
    }

    volatile std::uint32_t* regs_ = nullptr;
    GpioSpec spec_{};
    bool output_ = false;
};

}  // namespace hal::mmio
