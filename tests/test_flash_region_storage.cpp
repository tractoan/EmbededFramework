// INvStorage trên vùng flash NOR (Nios II HAL flash API, ...): flash NOR mô phỏng – chạy trên mọi host.
#include <cstdint>
#include <cstring>
#include <vector>

#include "flash/flash_region_storage.hpp"
#include "services/param_store.hpp"
#include "test_framework.hpp"

namespace {
// NOR: 2 vùng – 4 block 4 KB (0..16 KB) rồi 2 block 64 KB bắt đầu ở 16 KB (KHÔNG căn theo bội 64 KB tuyệt đối,
// như flash CFI có boot block). Xóa = 0xFF cả block; lập trình chỉ kéo bit 1 -> 0 (AND).
class FakeNor {
public:
    FakeNor() : mem_(4 * 4096 + 2 * 65536, 0x00) {}   // trạng thái ban đầu: CHƯA xóa
    bool open() { return opened_ = present; }
    bool block_at(std::uint32_t off, std::uint32_t& start, std::uint32_t& size) {
        constexpr std::uint32_t kSmallEnd = 4 * 4096u;
        if (off < kSmallEnd) {
            size  = 4096;
            start = off / 4096 * 4096;
        } else if (off < mem_.size()) {
            size  = 65536;
            start = kSmallEnd + (off - kSmallEnd) / 65536 * 65536;
        } else {
            return false;
        }
        return true;
    }
    bool read(std::uint32_t off, void* dst, std::uint32_t len) {
        if (off + len > mem_.size()) return false;
        std::memcpy(dst, &mem_[off], len);
        return true;
    }
    bool erase_block(std::uint32_t block, std::uint32_t size) {
        ++erases;
        std::memset(&mem_[block], 0xFF, size);
        return true;
    }
    bool program(std::uint32_t block, std::uint32_t off, const void* src, std::uint32_t len) {
        std::uint32_t start = 0, bs = 0;
        block_at(off, start, bs);
        if (block != start || off + len > block + bs) return false;   // đúng block, không vượt block
        const auto* s = static_cast<const std::uint8_t*>(src);
        for (std::uint32_t i = 0; i < len; ++i) mem_[off + i] &= s[i];
        return true;
    }
    bool present = true;
    bool opened_ = false;
    int erases   = 0;
    std::vector<std::uint8_t> mem_;
};
using Storage = hal::flash::FlashRegionStorage<FakeNor>;
}  // namespace

TEST(flash_region_validates_alignment_and_device) {
    FakeNor nor;
    CHECK(Storage(nor, 4096, 4096).init() == core::Status::Ok);
    CHECK(Storage(nor, 2048, 4096).init() == core::Status::InvalidArg);    // không căn block
    CHECK(Storage(nor, 0, 6000).init() == core::Status::InvalidArg);       // kết thúc giữa block
    CHECK(Storage(nor, 3 * 4096, 4096 + 65536).init() == core::Status::Ok);   // qua hai vùng khác cỡ block
    CHECK(Storage(nor, 65536, 65536).init() == core::Status::InvalidArg);     // bội 64 KB tuyệt đối nhưng giữa block
    CHECK(Storage(nor, 4 * 4096 + 2 * 65536, 4096).init() == core::Status::OutOfRange);
    CHECK(Storage(nor, 0, 0).init() == core::Status::InvalidArg);
    FakeNor absent;
    absent.present = false;
    Storage s(absent, 0, 4096);
    CHECK(s.init() == core::Status::NotFound);
    std::uint8_t b = 0;
    CHECK(s.read(0, &b, 1) == core::Status::NotSupported);
}

TEST(flash_region_nor_semantics_and_cross_block_write) {
    FakeNor nor;
    Storage s(nor, 4096, 8192);   // block 1 và 2
    CHECK(core::is_ok(s.init()));
    CHECK(core::is_ok(s.erase()));
    CHECK_EQ(nor.erases, 2);
    std::uint8_t buf[8];
    CHECK(core::is_ok(s.read(0, buf, sizeof(buf))));
    for (std::uint8_t v : buf) CHECK_EQ(v, 0xFF);
    CHECK_EQ(nor.mem_[0], 0x00);                        // ngoài vùng: không bị xóa

    std::uint8_t data[100];
    for (int i = 0; i < 100; ++i) data[i] = static_cast<std::uint8_t>(i * 7);
    CHECK(core::is_ok(s.write(4096 - 50, data, sizeof(data))));   // vắt qua biên block
    std::uint8_t back[100] = {};
    CHECK(core::is_ok(s.read(4096 - 50, back, sizeof(back))));
    CHECK(std::memcmp(back, data, sizeof(data)) == 0);

    const std::uint8_t ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};   // đổi 0 -> 1 không được nếu không xóa
    CHECK(s.write(4096 - 50, ones, sizeof(ones)) == core::Status::Error);
    CHECK(s.write(8190, data, 4) == core::Status::OutOfRange);
}

TEST(flash_region_spanning_regions_erases_right_blocks) {
    FakeNor nor;
    Storage s(nor, 3 * 4096, 4096 + 65536);   // block 4 KB cuối của vùng 1 + block 64 KB đầu của vùng 2
    CHECK(core::is_ok(s.init()));
    CHECK(core::is_ok(s.erase()));
    CHECK_EQ(nor.erases, 2);
    CHECK_EQ(nor.mem_[3 * 4096 - 1], 0x00);                      // trước vùng: không xóa
    CHECK_EQ(nor.mem_[3 * 4096], 0xFF);
    CHECK_EQ(nor.mem_[4 * 4096 + 65535], 0xFF);
    CHECK_EQ(nor.mem_[4 * 4096 + 65536], 0x00);                  // block 64 KB thứ hai: không xóa
    std::uint8_t data[64];
    std::memset(data, 0x5A, sizeof(data));
    CHECK(core::is_ok(s.write(4096 - 32, data, sizeof(data))));  // vắt qua biên hai vùng
    std::uint8_t back[64] = {};
    CHECK(core::is_ok(s.read(4096 - 32, back, sizeof(back))) && std::memcmp(back, data, sizeof(data)) == 0);
}

TEST(flash_region_backs_param_store_across_restart) {
    static const svc::ParamDef defs[] = {{0x4001, "f.a", 1, 0, 1000, ""}, {0x4002, "f.b", 2, 0, 1000, ""}};
    FakeNor nor;
    {
        Storage s(nor, 0, 4096);
        CHECK(core::is_ok(s.init()));
        svc::ParamStore ps;
        ps.init(&s, nullptr);
        ps.register_params(defs, 2);
        CHECK(ps.load() != core::Status::Ok);   // flash chưa xóa / chưa có dữ liệu
        ps.set(0x4001, 123);
        ps.set(0x4002, 456);
        CHECK(core::is_ok(ps.save()));
        ps.set(0x4001, 124);
        CHECK(core::is_ok(ps.save()));          // lưu lần hai: phải xóa trước (NOR)
    }
    {   // "khởi động lại"
        Storage s(nor, 0, 4096);
        CHECK(core::is_ok(s.init()));
        svc::ParamStore ps;
        ps.init(&s, nullptr);
        ps.register_params(defs, 2);
        CHECK(core::is_ok(ps.load()));
        CHECK_EQ(ps.get_or(0x4001, -1), 124);
        CHECK_EQ(ps.get_or(0x4002, -1), 456);
    }
}
