#pragma once
// DigitalOutputs: một nhóm cổng số ra (relay, DO công nghiệp...) dưới dạng MỘT svc::Output loại Actuator.
// Nhiều kênh cùng một output (DD-06): lệnh {mask, value} ghi đồng thời các bit được chọn.
//   - Chế độ TRỰC TIẾP: command()/set() ghi phần cứng ngay trên thread người gọi (không thread riêng).
//   - An toàn = tắt MỌI kênh: lúc khởi động (sau product::setup), khi disarm / failsafe (OutputHub::disarm_all),
//     khi deadman hết hạn (Options::command_timeout_ms != 0), khi tắt máy.
//   - Chưa arm -> lệnh bị từ chối (Busy). Ai arm (nghiệp vụ / lệnh từ giao diện) là chính sách của sản phẩm.
#include <atomic>
#include <cstdint>

#include "device/digital_io.hpp"
#include "services/output.hpp"

namespace svc {

struct DigitalOutCommand {
    std::uint32_t mask;    // chỉ các bit này được ghi
    std::uint32_t value;
};

class DigitalOutputs final : public Output<DigitalOutCommand> {
public:
    struct Options {
        std::uint32_t command_timeout_ms = 0;   // deadman; 0 = tắt (giữ mức đã đặt cho tới lệnh kế tiếp)
        OutputClock clock                = nullptr;
    };

    DigitalOutputs(const char* name, device::IDigitalOutputs& bank, const Options& opt)
        : Output(name, make_config(opt)), bank_(bank) {}
    DigitalOutputs(const char* name, device::IDigitalOutputs& bank) : DigitalOutputs(name, bank, Options{}) {}

    std::size_t count() const { return bank_.count(); }
    std::uint32_t all_mask() const {
        return bank_.count() >= 32 ? 0xFFFFFFFFu : ((1u << bank_.count()) - 1u);
    }
    std::uint32_t state() const { return bank_.state(); }   // giá trị đang xuất (bit i = kênh i)

    // Kiểm tra kênh rồi gửi lệnh. InvalidArg: mask rỗng / có kênh không tồn tại; Busy: chưa arm;
    // lỗi ghi phần cứng: mã lỗi của bank. Mã lỗi ghi chỉ chính xác khi một thread gọi set() (vd. thread
    // ứng dụng); nhiều nguồn cùng lúc vẫn an toàn, chỉ có thể nhận nhầm mã lỗi của lần ghi kia.
    core::Status set(std::uint32_t mask, std::uint32_t value) {
        if (mask == 0 || (mask & ~all_mask()) != 0) return core::Status::InvalidArg;
        write_status_ = core::Status::Ok;
        const core::Status st = command({mask, value & mask});
        if (!core::is_ok(st)) return st;
        return write_status_.load();
    }
    std::uint32_t write_errors() const { return write_errors_.load(); }

private:
    static Config make_config(const Options& o) {
        Config c;
        c.update_period_ms   = 0;   // trực tiếp
        c.command_timeout_ms = o.command_timeout_ms;
        c.clock              = o.clock;
        c.cls                = OutputClass::Actuator;
        return c;
    }
    void on_command(const DigitalOutCommand& c, std::uint64_t /*now*/) override {
        const core::Status st = bank_.write(c.mask, c.value);
        if (!core::is_ok(st)) {
            write_errors_.fetch_add(1);
            write_status_ = st;
        }
    }
    void write_safe() override { bank_.write(all_mask(), 0); }

    device::IDigitalOutputs& bank_;
    std::atomic<std::uint32_t> write_errors_{0};
    std::atomic<core::Status> write_status_{core::Status::Ok};
};

}  // namespace svc
