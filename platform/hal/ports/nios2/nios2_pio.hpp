#pragma once
// GPIO trên core PIO của Intel (altera_avalon_pio) cho Nios II. Header-only, KHÔNG include header của Intel:
// truy cập thanh ghi qua policy `Io`, vùng găng qua policy `Lock` -> test được trên host.
//
// Trên Nios II:
//   - Io  = IORD/IOWR (lệnh ldwio/stwio): BỎ QUA data cache. Không dùng con trỏ volatile thường (có thể bị cache).
//   - Lock = tắt/bật ngắt (alt_irq_disable_all) – lõi đơn, không spinlock (spinlock + priority có thể treo).
//
// Thanh ghi PIO (offset theo word): DATA 0, DIRECTION 1, IRQ MASK 2, EDGE CAPTURE 3, OUTSET 4, OUTCLEAR 5
// (OUTSET/OUTCLEAR chỉ có khi bật "Enable individual bit setting/clearing" trong Platform Designer).
//
// PIO chỉ-xuất (Output) không đọc lại được giá trị đang xuất -> giữ BẢN SAO (shadow) dùng chung cho mọi chân
// trên cùng một PIO; ghi DATA = bản sao. Các bit của PIO mà framework không quản lý được ghi theo bản sao
// (khởi tạo = reset_value) – dành trọn PIO cho framework, hoặc dùng OUTSET/OUTCLEAR (set_clear = true).
#include <cstddef>
#include <cstdint>

#include "hal/gpio.hpp"

namespace hal::nios2 {

enum class PioKind : std::uint8_t {
    Output,   // chỉ xuất
    Input,    // chỉ vào
    Bidir,    // hai chiều, có thanh ghi DIRECTION
    InOut,    // cổng vào và cổng ra riêng (DATA đọc = vào, ghi = ra), không có DIRECTION
};

struct PioSpec {
    std::uintptr_t base     = 0;      // *_BASE trong system.h; 0 = chân không dùng
    std::uint8_t bit        = 0;
    PioKind kind            = PioKind::Output;
    bool set_clear          = false;  // có thanh ghi OUTSET/OUTCLEAR
    bool active_low         = false;  // mức logic 1 = chân 0 (vd. LED nối lên VCC, nút kéo lên)
    std::uint32_t reset_value = 0;    // giá trị ra lúc reset của PIO (cấu hình trong Platform Designer)
};

namespace pio_reg {
inline constexpr unsigned kData      = 0;
inline constexpr unsigned kDirection = 1;
inline constexpr unsigned kOutSet    = 4;
inline constexpr unsigned kOutClear  = 5;
}  // namespace pio_reg

// Io: static std::uint32_t read(std::uintptr_t base, unsigned reg); static void write(std::uintptr_t base, unsigned reg, std::uint32_t v);
// Lock: kiểu RAII – vùng găng trong suốt thời gian tồn tại.
template <typename Io, typename Lock, std::size_t MaxPio = 8>
class PioPin final : public IGpio {
public:
    PioPin() = default;
    explicit PioPin(const PioSpec& spec) : spec_(spec) {}

    core::Status configure(PinMode mode) override {
        if (spec_.base == 0) return core::Status::NotSupported;   // chân không được cấu hình cho board này
        if (spec_.bit >= 32) return core::Status::InvalidArg;
        const bool want_out = mode == PinMode::Output || mode == PinMode::OutputOpenDrain;
        if (want_out && spec_.kind == PioKind::Input) return core::Status::NotSupported;
        if (!want_out && spec_.kind == PioKind::Output) return core::Status::NotSupported;
        Shadow* sh = shadow();
        if (sh == nullptr) return core::Status::NoMemory;   // quá MaxPio PIO khác nhau
        output_ = want_out;
        if (output_) write(false);   // mức an toàn (tắt) TRƯỚC khi bật driver ra
        if (spec_.kind == PioKind::Bidir) {
            Lock l;
            std::uint32_t dir = Io::read(spec_.base, pio_reg::kDirection);
            dir               = output_ ? (dir | mask()) : (dir & ~mask());
            Io::write(spec_.base, pio_reg::kDirection, dir);
        }
        configured_ = true;
        return core::Status::Ok;
    }

    core::Status write(bool level) override {
        if (spec_.base == 0 || !output_) return core::Status::NotSupported;
        const bool phys = level != spec_.active_low;
        Shadow* sh      = shadow();
        if (sh == nullptr) return core::Status::NoMemory;
        Lock l;
        sh->value = phys ? (sh->value | mask()) : (sh->value & ~mask());
        if (spec_.set_clear) {
            Io::write(spec_.base, phys ? pio_reg::kOutSet : pio_reg::kOutClear, mask());   // chỉ bit này
        } else {
            Io::write(spec_.base, pio_reg::kData, sh->value);
        }
        return core::Status::Ok;
    }

    bool read() const override {
        if (spec_.base == 0 || !configured_) return false;
        bool phys;
        if (spec_.kind == PioKind::Output) {   // không đọc được chân: trả giá trị đang xuất
            const Shadow* sh = shadow();
            phys             = sh != nullptr && (sh->value & mask()) != 0;
        } else {
            phys = (Io::read(spec_.base, pio_reg::kData) & mask()) != 0;
        }
        return phys != spec_.active_low;
    }

    const PioSpec& spec() const { return spec_; }

private:
    struct Shadow {
        std::uintptr_t base = 0;
        std::uint32_t value = 0;
    };
    std::uint32_t mask() const { return 1u << spec_.bit; }

    // Bản sao dùng chung theo địa chỉ PIO (mọi chân trên cùng PIO thấy cùng một giá trị).
    Shadow* shadow() const {
        static Shadow table[MaxPio];
        Lock l;
        for (Shadow& s : table) {
            if (s.base == spec_.base) return &s;
        }
        for (Shadow& s : table) {
            if (s.base == 0) {
                s.base  = spec_.base;
                s.value = spec_.reset_value;
                return &s;
            }
        }
        return nullptr;
    }

    PioSpec spec_{};
    bool output_     = false;
    bool configured_ = false;
};

}  // namespace hal::nios2
