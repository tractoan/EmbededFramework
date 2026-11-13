#pragma once
// INvStorage trên một VÙNG của flash NOR (xóa theo block, ghi chỉ kéo bit 1 -> 0). Header-only, không phụ
// thuộc OS/SDK: thao tác flash qua policy `Api` -> test được trên host với flash NOR mô phỏng.
// Dùng cho Nios II (HAL flash API: EPCQ/EPCS, on-chip flash MAX 10, CFI) và các SoC có API tương tự.
//
// Api:
//   bool open();                                                    // mở thiết bị
//   bool block_at(std::uint32_t offset, std::uint32_t& start, std::uint32_t& size);  // block chứa offset
//        (block đếm từ đầu VÙNG của flash – vùng có cỡ block khác nhau không nhất thiết căn theo địa chỉ tuyệt đối)
//   bool read(std::uint32_t offset, void* dst, std::uint32_t len);
//   bool erase_block(std::uint32_t block_offset, std::uint32_t block_size);
//   bool program(std::uint32_t block_offset, std::uint32_t offset, const void* src, std::uint32_t len); // không xóa
//
// Ngữ nghĩa INvStorage: vùng đã xóa đọc ra 0xFF; write() chỉ LẬP TRÌNH (không xóa) và đọc lại để kiểm tra –
// ghi đè lên vùng chưa xóa (cần đổi bit 0 -> 1) trả Error. ParamStore luôn erase() trước khi ghi.
// Vùng [offset, offset + size) phải nằm gọn theo biên block (kiểm tra ở init()).
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "hal/storage.hpp"

namespace hal::flash {

template <typename Api>
class FlashRegionStorage final : public INvStorage {
public:
    FlashRegionStorage(Api& api, std::uint32_t offset, std::uint32_t size) : api_(api), offset_(offset), size_(size) {}

    // Mở thiết bị và kiểm tra vùng: InvalidArg nếu không căn biên block / rỗng; NotFound nếu không mở được.
    core::Status init() {
        if (size_ == 0) return core::Status::InvalidArg;
        if (!api_.open()) return core::Status::NotFound;
        std::uint32_t pos = offset_;
        while (pos < offset_ + size_) {
            std::uint32_t start = 0, bs = 0;
            if (!api_.block_at(pos, start, bs) || bs == 0) return core::Status::OutOfRange;   // ngoài flash
            if (start != pos) return core::Status::InvalidArg;                                 // không căn block
            pos += bs;
        }
        if (pos != offset_ + size_) return core::Status::InvalidArg;   // vùng kết thúc giữa block
        ready_ = true;
        return core::Status::Ok;
    }
    bool ready() const { return ready_; }

    std::size_t size() const override { return size_; }

    core::Status read(std::size_t offset, void* data, std::size_t len) override {
        if (!ready_) return core::Status::NotSupported;
        if (offset + len > size_) return core::Status::OutOfRange;
        return api_.read(offset_ + static_cast<std::uint32_t>(offset), data, static_cast<std::uint32_t>(len))
                   ? core::Status::Ok
                   : core::Status::Error;
    }

    core::Status write(std::size_t offset, const void* data, std::size_t len) override {
        if (!ready_) return core::Status::NotSupported;
        if (offset + len > size_) return core::Status::OutOfRange;
        // Chia theo block (API lập trình theo block), rồi đọc lại để chắc dữ liệu thật sự nằm trên flash.
        const auto* src     = static_cast<const std::uint8_t*>(data);
        std::uint32_t pos   = offset_ + static_cast<std::uint32_t>(offset);
        std::uint32_t left  = static_cast<std::uint32_t>(len);
        while (left > 0) {
            std::uint32_t block = 0, bs = 0;
            if (!api_.block_at(pos, block, bs) || bs == 0) return core::Status::Error;
            const std::uint32_t chunk = (block + bs - pos) < left ? (block + bs - pos) : left;
            if (!api_.program(block, pos, src, chunk)) return core::Status::Error;
            if (!verify(pos, src, chunk)) return core::Status::Error;   // vd. ghi lên vùng chưa xóa
            pos += chunk;
            src += chunk;
            left -= chunk;
        }
        return core::Status::Ok;
    }

    core::Status erase() override {
        if (!ready_) return core::Status::NotSupported;
        std::uint32_t pos = offset_;
        while (pos < offset_ + size_) {
            std::uint32_t start = 0, bs = 0;
            if (!api_.block_at(pos, start, bs) || bs == 0) return core::Status::Error;
            if (!api_.erase_block(start, bs)) return core::Status::Error;
            pos = start + bs;
        }
        return core::Status::Ok;
    }

private:
    bool verify(std::uint32_t pos, const std::uint8_t* src, std::uint32_t len) {
        std::uint8_t buf[32];
        while (len > 0) {
            const std::uint32_t n = len < sizeof(buf) ? len : static_cast<std::uint32_t>(sizeof(buf));
            if (!api_.read(pos, buf, n) || std::memcmp(buf, src, n) != 0) return false;
            pos += n;
            src += n;
            len -= n;
        }
        return true;
    }

    Api& api_;
    std::uint32_t offset_;
    std::uint32_t size_;
    bool ready_ = false;
};

}  // namespace hal::flash
