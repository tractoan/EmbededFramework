// Phần HAL riêng Linux (common/linux): I2C /dev/i2c-N, GPIO chardev – nhánh thiết bị vắng mặt.
#include "linux/linux_hal.hpp"
#include "test_framework.hpp"

TEST(linux_missing_i2c_and_gpio_fail_cleanly) {
    std::uint8_t b[4] = {};
    hal::host::I2cDev i2c("/dev/ep-no-such-i2c");
    CHECK(i2c.open() == core::Status::NotFound);
    CHECK(!core::is_ok(i2c.write(0x48, b, 1, 10)));
    CHECK(!core::is_ok(i2c.write_read(0x48, b, 1, b, 2, 10)));

    hal::host::GpioLine by_label("ep-no-such-chip", 3, "ep-test");
    CHECK(!core::is_ok(by_label.configure(hal::PinMode::Output)));
    CHECK(!by_label.is_open());
    CHECK(!core::is_ok(by_label.write(true)));
    CHECK(!by_label.read());
    hal::host::GpioLine by_path("/dev/ep-no-such-gpiochip", 3, "ep-test");
    CHECK(!core::is_ok(by_path.configure(hal::PinMode::Input)));
}
