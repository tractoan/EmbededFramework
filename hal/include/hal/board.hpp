#pragma once
#include <cstdint>

#include "hal/gpio.hpp"
#include "hal/i2c.hpp"
#include "hal/storage.hpp"
#include "hal/uart.hpp"

namespace hal {

// ID LOGIC của tài nguyên phần cứng. Lớp trên chỉ biết ID logic;
// mỗi board port tự ánh xạ sang chân/peripheral vật lý.
enum class GpioId : std::uint8_t { StatusLed, AlarmLed, UserButton, Count };
enum class UartId : std::uint8_t { Console, Aux, Count };   // Aux: cổng thứ 2 (RS485, telemetry...)
enum class I2cId : std::uint8_t { Sensor, Count };

// Board Support: điểm truy cập duy nhất tới phần cứng (Abstract Factory).
class IBoard {
public:
    virtual ~IBoard() = default;
    virtual core::Status init()          = 0;
    virtual const char* name() const     = 0;
    virtual IGpio* gpio(GpioId id)       = 0;   // nullptr nếu board không có
    virtual IUart* uart(UartId id)       = 0;
    virtual II2c* i2c(I2cId id)          = 0;
    virtual INvStorage* storage()        = 0;
    [[noreturn]] virtual void reset()    = 0;
};

// Do board port được chọn lúc build cung cấp.
IBoard& board();

}  // namespace hal
