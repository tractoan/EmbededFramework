// Phần HAL riêng QNX (common/qnx): chạy trên board QNX – nhánh thiết bị vắng mặt / chưa cấu hình.
#include "qnx/qnx_hal.hpp"
#include "test_framework.hpp"

TEST(qnx_missing_i2c_fails_cleanly) {
    std::uint8_t b[4] = {};
    hal::host::QnxI2c i2c("/dev/ep-no-such-i2c");
    CHECK(i2c.open() == core::Status::NotFound);
    CHECK(i2c.write(0x48, b, 1, 10) == core::Status::NotSupported);
    CHECK(i2c.read(0x48, b, 2, 10) == core::Status::NotSupported);
    CHECK(i2c.write_read(0x48, b, 1, b, 2, 10) == core::Status::NotSupported);
}

TEST(qnx_mmio_gpio_unconfigured_or_invalid) {
    hal::host::MmioGpio none("");
    CHECK(none.configure(hal::PinMode::Output) == core::Status::NotSupported);
    CHECK(none.write(true) == core::Status::NotSupported);
    CHECK(!none.read());
    hal::host::MmioGpio bad("base=0x1000,out=0x3,bit=1,dir=0x4");   // offset không căn 4
    CHECK(bad.configure(hal::PinMode::Output) == core::Status::InvalidArg);
}
