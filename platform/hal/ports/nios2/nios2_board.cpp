// Board port: Intel (Altera) Nios II trên FPGA, BSP sinh bằng Nios II SBT (loại "UCOSII" hoặc "HAL").
// Thiết bị lấy từ system.h của BSP; tên/địa chỉ phụ thuộc thiết kế Platform Designer nên mọi thứ ghi đè
// được bằng macro lúc build (-DEP_NIOS2_DEFINES="..." – xem cmake/targets/nios2_ucos2.cmake).
//
//   Console    : EP_NIOS2_CONSOLE_DEV  (mặc định JTAG_UART_NAME, vd. "/dev/jtag_uart")   – HAL file descriptor
//   Aux UART   : EP_NIOS2_AUX_DEV      (mặc định tắt; vd. "/dev/uart_0")
//   LED        : EP_NIOS2_LED_PIO_BASE (mặc định LED_BASE / LEDS_BASE / LED_PIO_BASE),
//                bit EP_NIOS2_LED_STATUS_BIT (0), EP_NIOS2_LED_ALARM_BIT (1), EP_NIOS2_LED_ACTIVE_LOW (0)
//   Nút        : EP_NIOS2_BUTTON_PIO_BASE (mặc định KEY_BASE / BUTTON_BASE / BUTTON_PIO_BASE), bit 0, tích cực mức thấp
//   I2C        : EP_NIOS2_I2C_AVALON=1    -> Intel Avalon I2C (altera_avalon_i2c), EP_NIOS2_I2C_NAME
//                EP_NIOS2_I2C_OPENCORES=1 -> OpenCores I2C (i2c_opencores), EP_NIOS2_I2C_BASE, EP_NIOS2_I2C_CLK_HZ
//                (không đặt: board không có bus cảm biến)
//   NV storage : EP_NIOS2_FLASH_DEV (vd. EPCQ, on-chip flash MAX 10, CFI), EP_NIOS2_FLASH_OFFSET, EP_NIOS2_FLASH_SIZE
//                – vùng phải căn biên block xóa; không đặt: không lưu tham số
extern "C" {
#include "io.h"
#include "sys/alt_flash.h"
#include "sys/alt_irq.h"
#include "system.h"
#include <unistd.h>   /* usleep (HAL) */
#if defined(EP_NIOS2_I2C_AVALON) && EP_NIOS2_I2C_AVALON
#include "altera_avalon_i2c.h"
#elif defined(EP_NIOS2_I2C_OPENCORES) && EP_NIOS2_I2C_OPENCORES
#include "i2c_opencores.h"
#endif
}

#include <atomic>

#include "fdio/fd_uart.hpp"
#include "flash/flash_region_storage.hpp"
#include "hal/board.hpp"
#include "hal/wait.hpp"
#include "nios2_pio.hpp"

// ------------------------------------------------------------ mặc định từ system.h
#ifndef EP_NIOS2_CONSOLE_DEV
#if defined(JTAG_UART_NAME)
#define EP_NIOS2_CONSOLE_DEV JTAG_UART_NAME
#else
#define EP_NIOS2_CONSOLE_DEV "/dev/jtag_uart"
#endif
#endif
#ifndef EP_NIOS2_AUX_DEV
#define EP_NIOS2_AUX_DEV ""
#endif
#ifndef EP_NIOS2_LED_PIO_BASE
#if defined(LED_BASE)
#define EP_NIOS2_LED_PIO_BASE LED_BASE
#elif defined(LEDS_BASE)
#define EP_NIOS2_LED_PIO_BASE LEDS_BASE
#elif defined(LED_PIO_BASE)
#define EP_NIOS2_LED_PIO_BASE LED_PIO_BASE
#else
#define EP_NIOS2_LED_PIO_BASE 0
#endif
#endif
#ifndef EP_NIOS2_LED_STATUS_BIT
#define EP_NIOS2_LED_STATUS_BIT 0
#endif
#ifndef EP_NIOS2_LED_ALARM_BIT
#define EP_NIOS2_LED_ALARM_BIT 1
#endif
#ifndef EP_NIOS2_LED_ACTIVE_LOW
#define EP_NIOS2_LED_ACTIVE_LOW 0
#endif
#ifndef EP_NIOS2_LED_SET_CLEAR
#define EP_NIOS2_LED_SET_CLEAR 0   // PIO có OUTSET/OUTCLEAR
#endif
#ifndef EP_NIOS2_BUTTON_PIO_BASE
#if defined(KEY_BASE)
#define EP_NIOS2_BUTTON_PIO_BASE KEY_BASE
#elif defined(BUTTON_BASE)
#define EP_NIOS2_BUTTON_PIO_BASE BUTTON_BASE
#elif defined(BUTTON_PIO_BASE)
#define EP_NIOS2_BUTTON_PIO_BASE BUTTON_PIO_BASE
#else
#define EP_NIOS2_BUTTON_PIO_BASE 0
#endif
#endif
#ifndef EP_NIOS2_BUTTON_BIT
#define EP_NIOS2_BUTTON_BIT 0
#endif
#ifndef EP_NIOS2_BUTTON_ACTIVE_LOW
#define EP_NIOS2_BUTTON_ACTIVE_LOW 1   // nút trên board Terasic kéo lên, nhấn = 0
#endif
#ifndef EP_NIOS2_I2C_SPEED_HZ
#define EP_NIOS2_I2C_SPEED_HZ 100000u
#endif
#ifndef EP_NIOS2_I2C_CLK_HZ
#define EP_NIOS2_I2C_CLK_HZ ALT_CPU_FREQ   // xung clock cấp cho core I2C (thường = clock hệ thống)
#endif
#ifndef EP_NIOS2_FLASH_OFFSET
#define EP_NIOS2_FLASH_OFFSET 0u
#endif
#ifndef EP_NIOS2_FLASH_SIZE
#define EP_NIOS2_FLASH_SIZE 4096u
#endif

namespace hal {

namespace {

// ---- truy cập thanh ghi: ldwio/stwio (bỏ qua data cache) + vùng găng = tắt ngắt (lõi đơn)
struct NiosIo {
    static std::uint32_t read(std::uintptr_t base, unsigned reg) { return static_cast<std::uint32_t>(IORD(base, reg)); }
    static void write(std::uintptr_t base, unsigned reg, std::uint32_t v) { IOWR(base, reg, v); }
};
struct IrqLock {
    IrqLock() : ctx_(alt_irq_disable_all()) {}
    ~IrqLock() { alt_irq_enable_all(ctx_); }
    alt_irq_context ctx_;
};
using Pio = nios2::PioPin<NiosIo, IrqLock>;

void wait_1ms() {
    if (!hal::wait_ms(1)) usleep(1000);
}

// Bus I2C dùng chung (cảm biến, EEPROM...): driver HAL không an toàn đa luồng -> khóa bận + chờ có nhường CPU.
class BusLock {
public:
    bool acquire(std::uint32_t timeout_ms) {
        for (std::uint32_t waited = 0; busy_.test_and_set(std::memory_order_acquire); ++waited) {
            if (waited >= timeout_ms) return false;
            wait_1ms();
        }
        return true;
    }
    void release() { busy_.clear(std::memory_order_release); }

private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
};

#if defined(EP_NIOS2_I2C_AVALON) && EP_NIOS2_I2C_AVALON
// ---- Intel Avalon I2C (altera_avalon_i2c): API HAL, polled (use_interrupts = 0), có repeated start
class NiosI2c final : public II2c {
public:
    core::Status open() {
        dev_ = alt_avalon_i2c_open(EP_NIOS2_I2C_NAME);
        return dev_ != nullptr ? core::Status::Ok : core::Status::NotFound;
    }
    core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return xfer(addr, timeout_ms, [&] { return alt_avalon_i2c_master_tx(dev_, data, len, 0); });
    }
    core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        return xfer(addr, timeout_ms, [&] { return alt_avalon_i2c_master_rx(dev_, data, len, 0); });
    }
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                            std::size_t rx_len, std::uint32_t timeout_ms) override {
        return xfer(addr, timeout_ms, [&] { return alt_avalon_i2c_master_tx_rx(dev_, tx, tx_len, rx, rx_len, 0); });
    }

private:
    template <typename F>
    core::Status xfer(std::uint8_t addr, std::uint32_t timeout_ms, F op) {
        if (dev_ == nullptr) return core::Status::NotSupported;
        if (!lock_.acquire(timeout_ms)) return core::Status::Busy;
        alt_avalon_i2c_master_target_set(dev_, addr);
        // Kiểu trạng thái là alt_u32 nhưng mã lỗi là hằng ÂM: chuyển về có dấu trước khi so (C++ cấm
        // nhãn case thu hẹp kiểu -> so trực tiếp với alt_u32 không biên dịch được).
        const auto st = static_cast<alt_32>(op());
        lock_.release();
        switch (st) {
            case ALT_AVALON_I2C_SUCCESS: return core::Status::Ok;
            case ALT_AVALON_I2C_TIMEOUT: return core::Status::Timeout;
            case ALT_AVALON_I2C_BUSY:    return core::Status::Busy;
            default:                     return core::Status::Error;   // NACK, mất arbitration...
        }
    }
    ALT_AVALON_I2C_DEV_t* dev_ = nullptr;
    BusLock lock_;
};
#define EP_NIOS2_HAS_I2C 1
#elif defined(EP_NIOS2_I2C_OPENCORES) && EP_NIOS2_I2C_OPENCORES
// ---- OpenCores I2C (i2c_opencores): driver polled, CHỜ KHÔNG TIMEOUT – bus kẹt (SDA bị giữ) sẽ treo task gọi.
class NiosI2c final : public II2c {
public:
    core::Status open() {
        I2C_init(EP_NIOS2_I2C_BASE, EP_NIOS2_I2C_CLK_HZ, EP_NIOS2_I2C_SPEED_HZ);
        open_ = true;
        return core::Status::Ok;
    }
    core::Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (!begin(timeout_ms)) return busy_or_closed();
        core::Status st = start(addr, false);
        for (std::size_t i = 0; core::is_ok(st) && i < len; ++i) st = send(data[i], i + 1 == len);
        if (core::is_ok(st) && len == 0) I2C_write(EP_NIOS2_I2C_BASE, 0, 1);   // không dữ liệu: chỉ STOP
        return end(st);
    }
    core::Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (len == 0) return core::Status::InvalidArg;
        if (!begin(timeout_ms)) return busy_or_closed();
        const core::Status st = start(addr, true);
        if (core::is_ok(st)) receive(data, len);
        return end(st);
    }
    core::Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                            std::size_t rx_len, std::uint32_t timeout_ms) override {
        if (rx_len == 0) return core::Status::InvalidArg;
        if (!begin(timeout_ms)) return busy_or_closed();
        core::Status st = start(addr, false);
        for (std::size_t i = 0; core::is_ok(st) && i < tx_len; ++i) st = send(tx[i], false);   // không STOP
        if (core::is_ok(st)) st = start(addr, true);                                            // repeated start
        if (core::is_ok(st)) receive(rx, rx_len);
        return end(st);
    }

private:
    bool begin(std::uint32_t timeout_ms) { return open_ && lock_.acquire(timeout_ms); }
    core::Status busy_or_closed() const { return open_ ? core::Status::Busy : core::Status::NotSupported; }
    core::Status end(core::Status st) {
        lock_.release();
        return st;
    }
    static core::Status start(std::uint8_t addr, bool rd) {
        return I2C_start(EP_NIOS2_I2C_BASE, addr, rd ? 1u : 0u) == I2C_ACK ? core::Status::Ok : core::Status::Error;
    }
    static core::Status send(std::uint8_t b, bool last) {
        return I2C_write(EP_NIOS2_I2C_BASE, b, last ? 1u : 0u) == I2C_ACK ? core::Status::Ok : core::Status::Error;
    }
    static void receive(std::uint8_t* d, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) d[i] = static_cast<std::uint8_t>(I2C_read(EP_NIOS2_I2C_BASE, i + 1 == n ? 1u : 0u));
    }
    bool open_ = false;
    BusLock lock_;
};
#define EP_NIOS2_HAS_I2C 1
#endif

#if defined(EP_NIOS2_FLASH_DEV)
// ---- HAL flash API (EPCQ/EPCS, on-chip flash, CFI): hàm trả 0 khi thành công
class NiosFlashApi {
public:
    bool open() {
        fd_ = alt_flash_open_dev(EP_NIOS2_FLASH_DEV);
        return fd_ != nullptr;
    }
    bool block_at(std::uint32_t offset, std::uint32_t& start, std::uint32_t& size) {
        flash_region* regions = nullptr;
        int n                 = 0;
        if (fd_ == nullptr || alt_get_flash_info(fd_, &regions, &n) != 0) return false;
        for (int i = 0; i < n; ++i) {
            const auto first = static_cast<std::uint32_t>(regions[i].offset);
            const auto end   = first + static_cast<std::uint32_t>(regions[i].region_size);
            const auto bs    = static_cast<std::uint32_t>(regions[i].block_size);
            if (offset >= first && offset < end && bs != 0) {
                start = first + (offset - first) / bs * bs;   // block đếm từ đầu vùng
                size  = bs;
                return true;
            }
        }
        return false;
    }
    bool read(std::uint32_t offset, void* dst, std::uint32_t len) {
        return alt_read_flash(fd_, static_cast<int>(offset), dst, static_cast<int>(len)) == 0;
    }
    bool erase_block(std::uint32_t block, std::uint32_t size) {
        return alt_erase_flash_block(fd_, static_cast<int>(block), static_cast<int>(size)) == 0;
    }
    bool program(std::uint32_t block, std::uint32_t offset, const void* src, std::uint32_t len) {
        return alt_write_flash_block(fd_, static_cast<int>(block), static_cast<int>(offset), src, static_cast<int>(len)) == 0;
    }

private:
    alt_flash_fd* fd_ = nullptr;
};
#endif

class Nios2Board final : public IBoard {
public:
    core::Status init() override {
        // Thiết bị thiếu không làm hỏng khởi động: ngoại vi đó trả lỗi khi dùng.
        (void)status_led_.configure(PinMode::Output);
        (void)alarm_led_.configure(PinMode::Output);
        (void)button_.configure(PinMode::Input);
#if defined(EP_NIOS2_HAS_I2C)
        i2c_ok_ = core::is_ok(sensor_i2c_.open());
#endif
#if defined(EP_NIOS2_FLASH_DEV)
        storage_ok_ = core::is_ok(storage_.init());
#endif
        aux_ok_ = core::is_ok(aux_uart_.open(UartConfig{}));
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "nios2"; }

    IGpio* gpio(GpioId id) override {
        switch (id) {
            case GpioId::StatusLed:  return &status_led_;
            case GpioId::AlarmLed:   return &alarm_led_;
            case GpioId::UserButton: return &button_;
            default:                 return nullptr;
        }
    }
    IUart* uart(UartId id) override {
        if (id == UartId::Console) return &console_;
        if (id == UartId::Aux && aux_ok_) return &aux_uart_;
        return nullptr;
    }
    II2c* i2c(I2cId id) override {
#if defined(EP_NIOS2_HAS_I2C)
        if (id == I2cId::Sensor && i2c_ok_) return &sensor_i2c_;
#endif
        (void)id;
        return nullptr;
    }
    INvStorage* storage() override {
#if defined(EP_NIOS2_FLASH_DEV)
        if (storage_ok_) return &storage_;
#endif
        return nullptr;
    }

    [[noreturn]] void reset() override {
        // Nhảy về vector reset: CPU khởi động lại phần mềm (ngoại vi KHÔNG được reset). Reset phần cứng thật:
        // dùng watchdog (altera_avalon_timer cấu hình watchdog) hoặc chân reset của hệ thống.
        alt_irq_disable_all();
        reinterpret_cast<void (*)()>(NIOS2_RESET_ADDR)();
        for (;;) {}
    }

private:
    Pio status_led_{nios2::PioSpec{EP_NIOS2_LED_PIO_BASE, EP_NIOS2_LED_STATUS_BIT, nios2::PioKind::Output,
                                   EP_NIOS2_LED_SET_CLEAR != 0, EP_NIOS2_LED_ACTIVE_LOW != 0, 0}};
    Pio alarm_led_{nios2::PioSpec{EP_NIOS2_LED_PIO_BASE, EP_NIOS2_LED_ALARM_BIT, nios2::PioKind::Output,
                                  EP_NIOS2_LED_SET_CLEAR != 0, EP_NIOS2_LED_ACTIVE_LOW != 0, 0}};
    Pio button_{nios2::PioSpec{EP_NIOS2_BUTTON_PIO_BASE, EP_NIOS2_BUTTON_BIT, nios2::PioKind::Input, false,
                               EP_NIOS2_BUTTON_ACTIVE_LOW != 0, 0}};
    fdio::FdUart console_{EP_NIOS2_CONSOLE_DEV};
    fdio::FdUart aux_uart_{EP_NIOS2_AUX_DEV};
    bool aux_ok_ = false;
#if defined(EP_NIOS2_HAS_I2C)
    NiosI2c sensor_i2c_;
    bool i2c_ok_ = false;
#endif
#if defined(EP_NIOS2_FLASH_DEV)
    NiosFlashApi flash_api_;
    flash::FlashRegionStorage<NiosFlashApi> storage_{flash_api_, EP_NIOS2_FLASH_OFFSET, EP_NIOS2_FLASH_SIZE};
    bool storage_ok_ = false;
#endif
};

}  // namespace

IBoard& board() {
    static Nios2Board instance;
    return instance;
}

}  // namespace hal
