#include <cstring>

#include "test_framework.hpp"
#include "utils/crc.hpp"
#include "utils/fixed_point.hpp"
#include "utils/ring_buffer.hpp"

TEST(utils_crc_known_vectors) {
    const char* s = "123456789";   // check value chuẩn của các thuật toán CRC
    CHECK_EQ(utils::crc32(s, 9), 0xCBF43926u);
    CHECK_EQ(utils::crc16_ccitt(s, 9), 0x29B1u);
    // Tính nối tiếp nhiều khối phải bằng tính một lần
    CHECK_EQ(utils::crc32(s + 4, 5, utils::crc32(s, 4)), 0xCBF43926u);
    CHECK_EQ(utils::crc16_ccitt(s + 4, 5, utils::crc16_ccitt(s, 4)), 0x29B1u);
}

TEST(utils_format_fixed) {
    CHECK(std::strcmp(utils::Fixed(-123, 1).c_str(), "-12.3") == 0);
    CHECK(std::strcmp(utils::Fixed(5, 1).c_str(), "0.5") == 0);
    CHECK(std::strcmp(utils::Fixed(-5, 1).c_str(), "-0.5") == 0);
    CHECK(std::strcmp(utils::Fixed(3305, 3).c_str(), "3.305") == 0);
    CHECK(std::strcmp(utils::Fixed(7, 2).c_str(), "0.07") == 0);
    CHECK(std::strcmp(utils::Fixed(42, 0).c_str(), "42") == 0);
    CHECK(std::strcmp(utils::Fixed(INT32_MIN, 1).c_str(), "-214748364.8") == 0);
    char small[4];
    CHECK_EQ(utils::format_fixed(small, sizeof(small), 12345, 1), 3u);   // cắt an toàn
    CHECK(std::strcmp(small, "123") == 0);
}

TEST(utils_ring_buffer_spsc) {
    utils::RingBuffer<int, 4> rb;   // dung lượng 3
    CHECK(rb.empty());
    CHECK(rb.push(1) && rb.push(2) && rb.push(3));
    CHECK(!rb.push(4));
    CHECK_EQ(rb.size(), 3u);
    int v = 0;
    CHECK(rb.pop(v) && v == 1);
    CHECK(rb.push(4));   // quay vòng
    int out[8] = {};
    CHECK_EQ(rb.pop(out, 8), 3u);
    CHECK(out[0] == 2 && out[1] == 3 && out[2] == 4);
    CHECK(!rb.pop(v));
}
