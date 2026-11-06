#pragma once
#include <cstddef>
#include <cstdint>

#include "hal/gpio.hpp"
#include "hal/hdlc.hpp"
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

    // ---- Tài nguyên dạng mảng (board có nhiều cổng cùng loại: bring-up, I/O công nghiệp...)
    // Chỉ số 0..count-1 là thứ tự logic của board (vd. đánh số trên vỏ thiết bị).
    // Mặc định: không có -> board cũ không phải sửa.
    virtual std::size_t serial_count() const { return 0; }
    virtual IUart* serial(std::size_t /*index*/) { return nullptr; }   // RS232/RS485 (chế độ do board cấu hình)
    virtual std::size_t digital_input_count() const { return 0; }
    virtual IGpio* digital_input(std::size_t /*index*/) { return nullptr; }   // đã cấu hình Input
    virtual std::size_t digital_output_count() const { return 0; }
    virtual IGpio* digital_output(std::size_t /*index*/) { return nullptr; }  // đã cấu hình Output
    virtual std::size_t hdlc_count() const { return 0; }
    virtual IHdlcPort* hdlc(std::size_t /*index*/) { return nullptr; }       // SDLC/HDLC đồng bộ
};

// Do board port được chọn lúc build cung cấp.
IBoard& board();

}  // namespace hal
