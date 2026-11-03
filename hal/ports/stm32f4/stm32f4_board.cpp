// Board port: STM32F4-Discovery (STM32F407VG) trên STM32Cube HAL.
// Handle peripheral (huart2, hi2c1) do CubeMX sinh ra trong usart.c / i2c.c.
// Chỉ được biên dịch khi EP_PLATFORM=stm32f4_freertos.
#include <cstring>

#include "hal/board.hpp"
#include "stm32f4xx_hal.h"

extern "C" {
extern UART_HandleTypeDef huart2;
extern I2C_HandleTypeDef hi2c1;
}

namespace hal {

namespace {

using core::Status;

Status from_hal(HAL_StatusTypeDef s) {
    switch (s) {
        case HAL_OK:      return Status::Ok;
        case HAL_BUSY:    return Status::Busy;
        case HAL_TIMEOUT: return Status::Timeout;
        default:          return Status::Error;
    }
}

// ---------------------------------------------------------------- GPIO
class Stm32Gpio final : public IGpio {
public:
    Stm32Gpio(GPIO_TypeDef* port, std::uint16_t pin) : port_(port), pin_(pin) {}

    Status configure(PinMode mode) override {
        GPIO_InitTypeDef init{};
        init.Pin   = pin_;
        init.Speed = GPIO_SPEED_FREQ_LOW;
        switch (mode) {
            case PinMode::Input:           init.Mode = GPIO_MODE_INPUT;     init.Pull = GPIO_NOPULL;   break;
            case PinMode::InputPullUp:     init.Mode = GPIO_MODE_INPUT;     init.Pull = GPIO_PULLUP;   break;
            case PinMode::InputPullDown:   init.Mode = GPIO_MODE_INPUT;     init.Pull = GPIO_PULLDOWN; break;
            case PinMode::Output:          init.Mode = GPIO_MODE_OUTPUT_PP; init.Pull = GPIO_NOPULL;   break;
            case PinMode::OutputOpenDrain: init.Mode = GPIO_MODE_OUTPUT_OD; init.Pull = GPIO_NOPULL;   break;
        }
        HAL_GPIO_Init(port_, &init);
        return Status::Ok;
    }
    Status write(bool level) override {
        HAL_GPIO_WritePin(port_, pin_, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
        return Status::Ok;
    }
    bool read() const override { return HAL_GPIO_ReadPin(port_, pin_) == GPIO_PIN_SET; }
    Status toggle() override {
        HAL_GPIO_TogglePin(port_, pin_);
        return Status::Ok;
    }

private:
    GPIO_TypeDef* port_;
    std::uint16_t pin_;
};

// ---------------------------------------------------------------- UART
// Bản polling đơn giản. Sản phẩm thật: dùng DMA/IT + ring buffer + osal::Semaphore::give_from_isr.
class Stm32Uart final : public IUart {
public:
    explicit Stm32Uart(UART_HandleTypeDef* h) : h_(h) {}

    Status open(const UartConfig& cfg) override {
        h_->Init.BaudRate   = cfg.baudrate;
        h_->Init.WordLength = UART_WORDLENGTH_8B;
        h_->Init.StopBits   = cfg.stop_bits == 2 ? UART_STOPBITS_2 : UART_STOPBITS_1;
        h_->Init.Parity     = cfg.parity == UartConfig::Parity::Even ? UART_PARITY_EVEN
                            : cfg.parity == UartConfig::Parity::Odd  ? UART_PARITY_ODD
                                                                     : UART_PARITY_NONE;
        h_->Init.Mode       = UART_MODE_TX_RX;
        return from_hal(HAL_UART_Init(h_));
    }
    void close() override { HAL_UART_DeInit(h_); }

    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return HAL_UART_Transmit(h_, const_cast<std::uint8_t*>(data), static_cast<std::uint16_t>(len), timeout_ms) == HAL_OK
                   ? len : 0;
    }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        std::size_t n = 0;
        while (n < len && HAL_UART_Receive(h_, &data[n], 1, n == 0 ? timeout_ms : 0) == HAL_OK) ++n;
        return n;
    }

private:
    UART_HandleTypeDef* h_;
};

// ----------------------------------------------------------------- I2C
class Stm32I2c final : public II2c {
public:
    explicit Stm32I2c(I2C_HandleTypeDef* h) : h_(h) {}

    Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return from_hal(HAL_I2C_Master_Transmit(h_, addr << 1, const_cast<std::uint8_t*>(data),
                                                static_cast<std::uint16_t>(len), timeout_ms));
    }
    Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return from_hal(HAL_I2C_Master_Receive(h_, addr << 1, data, static_cast<std::uint16_t>(len), timeout_ms));
    }
    Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                      std::size_t rx_len, std::uint32_t timeout_ms) override {
        if (tx_len == 1) {   // repeated-start đọc thanh ghi
            return from_hal(HAL_I2C_Mem_Read(h_, addr << 1, tx[0], I2C_MEMADD_SIZE_8BIT, rx,
                                             static_cast<std::uint16_t>(rx_len), timeout_ms));
        }
        const Status st = write(addr, tx, tx_len, timeout_ms);
        return core::is_ok(st) ? read(addr, rx, rx_len, timeout_ms) : st;
    }

private:
    I2C_HandleTypeDef* h_;
};

// --------------------------------------------------- Flash (sector 11)
class Stm32FlashStorage final : public INvStorage {
public:
    static constexpr std::uint32_t kBase   = 0x080E0000u;   // Sector 11, 128 KB
    static constexpr std::uint32_t kSector = FLASH_SECTOR_11;
    static constexpr std::size_t kSize     = 4096;          // chỉ dùng 4 KB đầu

    std::size_t size() const override { return kSize; }

    Status read(std::size_t offset, void* data, std::size_t len) override {
        if (offset + len > kSize) return Status::OutOfRange;
        std::memcpy(data, reinterpret_cast<const void*>(kBase + offset), len);
        return Status::Ok;
    }
    Status write(std::size_t offset, const void* data, std::size_t len) override {
        if (offset + len > kSize) return Status::OutOfRange;
        HAL_FLASH_Unlock();
        const auto* p = static_cast<const std::uint8_t*>(data);
        HAL_StatusTypeDef s = HAL_OK;
        for (std::size_t i = 0; i < len && s == HAL_OK; ++i) {
            s = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, kBase + offset + i, p[i]);
        }
        HAL_FLASH_Lock();
        return from_hal(s);
    }
    Status erase() override {
        FLASH_EraseInitTypeDef e{};
        e.TypeErase    = FLASH_TYPEERASE_SECTORS;
        e.Sector       = kSector;
        e.NbSectors    = 1;
        e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        std::uint32_t err = 0;
        HAL_FLASH_Unlock();
        const HAL_StatusTypeDef s = HAL_FLASHEx_Erase(&e, &err);
        HAL_FLASH_Lock();
        return from_hal(s);
    }
};

// --------------------------------------------------------------- Board
class Stm32F4DiscoveryBoard final : public IBoard {
public:
    Status init() override {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        status_led_.configure(PinMode::Output);
        alarm_led_.configure(PinMode::Output);
        button_.configure(PinMode::InputPullDown);
        return console_.open(UartConfig{});
    }
    const char* name() const override { return "stm32f4-discovery"; }

    IGpio* gpio(GpioId id) override {
        switch (id) {
            case GpioId::StatusLed:  return &status_led_;   // LD4 xanh lá – PD12
            case GpioId::AlarmLed:   return &alarm_led_;    // LD5 đỏ      – PD14
            case GpioId::UserButton: return &button_;       // B1          – PA0
            default:                 return nullptr;
        }
    }
    IUart* uart(UartId id) override { return id == UartId::Console ? &console_ : nullptr; }
    II2c* i2c(I2cId id) override { return id == I2cId::Sensor ? &i2c_ : nullptr; }
    INvStorage* storage() override { return &flash_; }
    [[noreturn]] void reset() override {
        NVIC_SystemReset();
        for (;;) {}
    }

private:
    Stm32Gpio status_led_{GPIOD, GPIO_PIN_12};
    Stm32Gpio alarm_led_{GPIOD, GPIO_PIN_14};
    Stm32Gpio button_{GPIOA, GPIO_PIN_0};
    Stm32Uart console_{&huart2};
    Stm32I2c i2c_{&hi2c1};
    Stm32FlashStorage flash_;
};

}  // namespace

IBoard& board() {
    static Stm32F4DiscoveryBoard instance;
    return instance;
}

}  // namespace hal
