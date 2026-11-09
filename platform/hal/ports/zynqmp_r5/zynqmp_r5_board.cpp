// Board port: Zynq UltraScale+ MPSoC – RPU (Cortex-R5F) với Xilinx standalone BSP (Vitis), thường
// chạy FreeRTOS. Dùng driver PS của Xilinx: XGpioPs, XUartPs, XIicPs (polled).
//
// Mặc định theo ZCU102 khi RPU chạy cạnh Linux trên APU (APU giữ UART0) – kiểm tra với design Vivado
// (xparameters.h) và schematic của board, rồi ghi đè bằng -D lúc build nếu cần:
//   EP_ZYNQMP_R5_CONSOLE_UART  PS UART1          LED trạng thái  : MIO23
//   EP_ZYNQMP_R5_AUX_UART      (không có)        LED cảnh báo    : EMIO0 = pin 78 (design PL nối tới LED)
//   EP_ZYNQMP_R5_SENSOR_I2C    PS I2C1           Nút nhấn        : MIO22
//   EEPROM tham số             M24C08 @0x54 trên I2C1 sau mux TCA9548 @0x74 kênh 0
//
// Hai luồng cấu hình của embeddedsw đều được hỗ trợ: xparameters cổ điển (XPAR_*_DEVICE_ID) và
// System Device Tree (SDT, XPAR_*_BASEADDR).
// Chờ trong driver (UART chờ dữ liệu, EEPROM chờ chu kỳ ghi) dùng hal::wait_ms() -> target cài
// osal::sleep_ms nên task nhường CPU; chưa cài thì quay chờ bằng usleep() của BSP.
extern "C" {
#include "sleep.h"
#include "xgpiops.h"
#include "xiicps.h"
#include "xil_io.h"
#include "xparameters.h"
#include "xstatus.h"
#include "xuartps.h"
}

#include <atomic>
#include <cstring>

#include "hal/board.hpp"
#include "hal/wait.hpp"

#ifdef SDT
#define EP_ZYNQMP_R5_KEY(dev, n) XPAR_##dev##_##n##_BASEADDR
#else
#define EP_ZYNQMP_R5_KEY(dev, n) XPAR_##dev##_##n##_DEVICE_ID
#endif

#ifndef EP_ZYNQMP_R5_GPIO
#define EP_ZYNQMP_R5_GPIO EP_ZYNQMP_R5_KEY(XGPIOPS, 0)
#endif
#ifndef EP_ZYNQMP_R5_CONSOLE_UART
#define EP_ZYNQMP_R5_CONSOLE_UART EP_ZYNQMP_R5_KEY(XUARTPS, 1)
#endif
#ifndef EP_ZYNQMP_R5_SENSOR_I2C
#define EP_ZYNQMP_R5_SENSOR_I2C EP_ZYNQMP_R5_KEY(XIICPS, 1)
#endif
#ifndef EP_ZYNQMP_R5_I2C_SCLK_HZ
#define EP_ZYNQMP_R5_I2C_SCLK_HZ 100000u
#endif
#ifndef EP_ZYNQMP_R5_LED_STATUS
#define EP_ZYNQMP_R5_LED_STATUS 23u
#endif
#ifndef EP_ZYNQMP_R5_LED_ALARM
#define EP_ZYNQMP_R5_LED_ALARM 78u
#endif
#ifndef EP_ZYNQMP_R5_BUTTON
#define EP_ZYNQMP_R5_BUTTON 22u
#endif
// EEPROM lưu tham số (đặt EP_ZYNQMP_R5_EEPROM_ADDR = 0 để tắt)
#ifndef EP_ZYNQMP_R5_EEPROM_ADDR
#define EP_ZYNQMP_R5_EEPROM_ADDR 0x54u
#endif
#ifndef EP_ZYNQMP_R5_EEPROM_SIZE
#define EP_ZYNQMP_R5_EEPROM_SIZE 1024u       // M24C08
#endif
#ifndef EP_ZYNQMP_R5_EEPROM_PAGE
#define EP_ZYNQMP_R5_EEPROM_PAGE 16u
#endif
#ifndef EP_ZYNQMP_R5_EEPROM_ADDR_BYTES
#define EP_ZYNQMP_R5_EEPROM_ADDR_BYTES 1u    // 1: 24C01..24C16 (bit khối trong địa chỉ thiết bị); 2: 24C32+
#endif
#ifndef EP_ZYNQMP_R5_I2C_MUX_ADDR
#define EP_ZYNQMP_R5_I2C_MUX_ADDR 0x74u      // 0 = không có mux
#endif
#ifndef EP_ZYNQMP_R5_EEPROM_MUX_MASK
#define EP_ZYNQMP_R5_EEPROM_MUX_MASK 0x01u   // kênh 0
#endif

namespace hal {

namespace {

using core::Status;
#ifdef SDT
using Key = UINTPTR;
#else
using Key = u16;
#endif

// Chờ ~1 ms: nhường CPU nếu target đã cài hàm chờ, ngược lại quay chờ bằng BSP.
void wait_1ms() {
    if (!wait_ms(1)) usleep(1000);
}

// ---------------------------------------------------------------- GPIO
class PsGpio final : public IGpio {
public:
    PsGpio(XGpioPs& dev, const bool& ready, u32 pin) : dev_(dev), ready_(ready), pin_(pin) {}

    Status configure(PinMode mode) override {
        if (!ready_) return Status::NotSupported;
        output_ = mode == PinMode::Output || mode == PinMode::OutputOpenDrain;
        // Pull-up/pull-down của MIO do psu_init (FSBL) cấu hình, không qua driver GPIO.
        XGpioPs_SetDirectionPin(&dev_, pin_, output_ ? 1u : 0u);
        XGpioPs_SetOutputEnablePin(&dev_, pin_, output_ ? 1u : 0u);
        return Status::Ok;
    }
    Status write(bool level) override {
        if (!ready_ || !output_) return Status::NotSupported;
        XGpioPs_WritePin(&dev_, pin_, level ? 1u : 0u);
        return Status::Ok;
    }
    bool read() const override { return ready_ && XGpioPs_ReadPin(&dev_, pin_) != 0; }

private:
    XGpioPs& dev_;
    const bool& ready_;
    u32 pin_;
    bool output_ = false;
};

// ---------------------------------------------------------------- UART
class PsUart final : public IUart {
public:
    explicit PsUart(Key key) : key_(key) {}

    Status open(const UartConfig& cfg) override {
        XUartPs_Config* c = XUartPs_LookupConfig(key_);
        if (c == nullptr || XUartPs_CfgInitialize(&dev_, c, c->BaseAddress) != XST_SUCCESS) return Status::NotFound;
        XUartPsFormat f{};
        f.BaudRate = cfg.baudrate;
        f.DataBits = cfg.data_bits == 7 ? XUARTPS_FORMAT_7_BITS : cfg.data_bits == 6 ? XUARTPS_FORMAT_6_BITS
                                                                                     : XUARTPS_FORMAT_8_BITS;
        f.Parity   = cfg.parity == UartConfig::Parity::Even  ? XUARTPS_FORMAT_EVEN_PARITY
                   : cfg.parity == UartConfig::Parity::Odd   ? XUARTPS_FORMAT_ODD_PARITY
                                                             : XUARTPS_FORMAT_NO_PARITY;
        f.StopBits = cfg.stop_bits == 2 ? XUARTPS_FORMAT_2_STOP_BIT : XUARTPS_FORMAT_1_STOP_BIT;
        if (XUartPs_SetDataFormat(&dev_, &f) != XST_SUCCESS) return Status::InvalidArg;
        XUartPs_SetOperMode(&dev_, XUARTPS_OPER_MODE_NORMAL);
        ready_ = true;
        return Status::Ok;
    }
    void close() override { ready_ = false; }

    // Polled: XUartPs_Send/Recv chỉ đẩy/lấy từ FIFO rồi trả về ngay.
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (!ready_) return 0;
        std::size_t sent = 0;
        for (std::uint32_t waited = 0; sent < len;) {
            sent += XUartPs_Send(&dev_, const_cast<u8*>(data + sent), static_cast<u32>(len - sent));
            if (sent >= len || waited++ >= timeout_ms) break;
            wait_1ms();   // FIFO TX đầy: chờ
        }
        return sent;
    }
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        if (!ready_) {
            for (std::uint32_t i = 0; i < timeout_ms; ++i) wait_1ms();
            return 0;
        }
        for (std::uint32_t waited = 0;; ++waited) {
            const u32 n = XUartPs_Recv(&dev_, data, static_cast<u32>(len));
            if (n > 0 || waited >= timeout_ms) return n;
            wait_1ms();
        }
    }

private:
    Key key_;
    XUartPs dev_{};
    bool ready_ = false;
};

// ----------------------------------------------------------------- I2C
// Dùng chung được giữa nhiều thread (vd. cảm biến ở thread nghiệp vụ + EEPROM ở thread CLI):
// khóa giữ trọn một giao dịch (kể cả bước chọn kênh mux) – spin rồi nhường CPU qua hal::wait_ms.
class PsI2c final : public II2c {
public:
    class Guard {
    public:
        explicit Guard(PsI2c& bus) : bus_(bus) {
            while (bus_.busy_.test_and_set(std::memory_order_acquire)) wait_1ms();
        }
        ~Guard() { bus_.busy_.clear(std::memory_order_release); }
        Guard(const Guard&)            = delete;
        Guard& operator=(const Guard&) = delete;

    private:
        PsI2c& bus_;
    };

    explicit PsI2c(Key key) : key_(key) {}

    Status open(std::uint32_t sclk_hz) {
        XIicPs_Config* c = XIicPs_LookupConfig(key_);
        if (c == nullptr || XIicPs_CfgInitialize(&dev_, c, c->BaseAddress) != XST_SUCCESS) return Status::NotFound;
        if (XIicPs_SetSClk(&dev_, sclk_hz) != XST_SUCCESS) return Status::InvalidArg;
        ready_ = true;
        return Status::Ok;
    }

    Status write(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        Guard g(*this);
        return send(addr, data, len, timeout_ms);
    }
    Status read(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override {
        Guard g(*this);
        return recv(addr, data, len, timeout_ms);
    }
    Status write_read(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                      std::size_t rx_len, std::uint32_t timeout_ms) override {
        Guard g(*this);
        return send_recv(addr, tx, tx_len, rx, rx_len, timeout_ms);
    }

    // Thao tác thô – người gọi phải giữ Guard (dùng bởi Eeprom24Storage)
    Status send(std::uint8_t addr, const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
        if (!ready_) return Status::NotSupported;
        if (!wait_idle(timeout_ms)) return Status::Timeout;
        return XIicPs_MasterSendPolled(&dev_, const_cast<u8*>(data), static_cast<s32>(len), addr) == XST_SUCCESS
                   ? Status::Ok : Status::Error;   // NACK / mất trọng tài
    }
    Status recv(std::uint8_t addr, std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
        if (!ready_) return Status::NotSupported;
        if (!wait_idle(timeout_ms)) return Status::Timeout;
        return XIicPs_MasterRecvPolled(&dev_, data, static_cast<s32>(len), addr) == XST_SUCCESS ? Status::Ok
                                                                                             : Status::Error;
    }
    Status send_recv(std::uint8_t addr, const std::uint8_t* tx, std::size_t tx_len, std::uint8_t* rx,
                     std::size_t rx_len, std::uint32_t timeout_ms) {
        if (!ready_) return Status::NotSupported;
        if (!wait_idle(timeout_ms)) return Status::Timeout;
        // Repeated start giữa phần ghi địa chỉ thanh ghi và phần đọc (theo cách dùng của driver Xilinx)
        XIicPs_SetOptions(&dev_, XIICPS_REP_START_OPTION);
        const s32 st = XIicPs_MasterSendPolled(&dev_, const_cast<u8*>(tx), static_cast<s32>(tx_len), addr);
        XIicPs_ClearOptions(&dev_, XIICPS_REP_START_OPTION);
        if (st != XST_SUCCESS) return Status::Error;
        return XIicPs_MasterRecvPolled(&dev_, rx, static_cast<s32>(rx_len), addr) == XST_SUCCESS ? Status::Ok
                                                                                                : Status::Error;
    }

private:
    bool wait_idle(std::uint32_t timeout_ms) {
        for (std::uint32_t waited = 0; XIicPs_BusIsBusy(&dev_); ++waited) {
            if (waited >= timeout_ms) return false;
            wait_1ms();
        }
        return true;
    }

    Key key_;
    XIicPs dev_{};
    bool ready_ = false;
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
};

// ------------------------------------------------------- EEPROM 24Cxx
class Eeprom24Storage final : public INvStorage {
public:
    Eeprom24Storage(PsI2c& bus, std::uint8_t addr, std::size_t size, std::size_t page, std::uint8_t addr_bytes,
                    std::uint8_t mux_addr, std::uint8_t mux_mask)
        : bus_(bus), addr_(addr), size_(size), page_(page), addr_bytes_(addr_bytes), mux_addr_(mux_addr),
          mux_mask_(mux_mask) {}

    std::size_t size() const override { return size_; }

    Status read(std::size_t offset, void* data, std::size_t len) override {
        if (offset + len > size_) return Status::OutOfRange;
        PsI2c::Guard g(bus_);
        Status st = select_mux();
        auto* out = static_cast<std::uint8_t*>(data);
        while (core::is_ok(st) && len > 0) {
            // 1 byte địa chỉ: không đọc vượt khối 256 byte (bit khối nằm trong địa chỉ thiết bị)
            const std::size_t chunk = addr_bytes_ == 1 ? min(len, 256u - (offset & 0xFFu)) : len;
            std::uint8_t a[2];
            st = bus_.send_recv(device(offset), a, encode(offset, a), out, chunk, 50);
            offset += chunk;
            out += chunk;
            len -= chunk;
        }
        return st;
    }

    Status write(std::size_t offset, const void* data, std::size_t len) override {
        if (offset + len > size_) return Status::OutOfRange;
        PsI2c::Guard g(bus_);
        Status st     = select_mux();
        const auto* p = static_cast<const std::uint8_t*>(data);
        while (core::is_ok(st) && len > 0) {
            const std::size_t chunk = min(len, page_ - (offset % page_));   // không ghi vượt ranh giới trang
            std::uint8_t buf[2 + 64];
            const std::size_t a = encode(offset, buf);
            std::memcpy(buf + a, p, chunk);
            st = bus_.send(device(offset), buf, a + chunk, 50);
            if (core::is_ok(st)) st = wait_write_cycle(offset);
            offset += chunk;
            p += chunk;
            len -= chunk;
        }
        return st;
    }

    Status erase() override {
        std::uint8_t blank[16];
        std::memset(blank, 0xFF, sizeof(blank));
        for (std::size_t off = 0; off < size_; off += sizeof(blank)) {
            const Status st = write(off, blank, min(sizeof(blank), size_ - off));
            if (!core::is_ok(st)) return st;
        }
        return Status::Ok;
    }

private:
    static std::size_t min(std::size_t a, std::size_t b) { return a < b ? a : b; }

    std::uint8_t device(std::size_t offset) const {
        return addr_bytes_ == 1 ? static_cast<std::uint8_t>(addr_ | ((offset >> 8) & 0x07u)) : addr_;
    }
    std::size_t encode(std::size_t offset, std::uint8_t* out) const {
        if (addr_bytes_ == 1) {
            out[0] = static_cast<std::uint8_t>(offset & 0xFFu);
            return 1;
        }
        out[0] = static_cast<std::uint8_t>((offset >> 8) & 0xFFu);
        out[1] = static_cast<std::uint8_t>(offset & 0xFFu);
        return 2;
    }
    Status select_mux() {
        if (mux_addr_ == 0) return Status::Ok;
        return bus_.send(mux_addr_, &mux_mask_, 1, 50);
    }
    // Sau khi ghi, EEPROM không ACK cho tới khi xong chu kỳ ghi nội bộ (~5 ms): thăm dò ACK tối đa 20 ms.
    Status wait_write_cycle(std::size_t offset) {
        std::uint8_t a[2];
        const std::size_t n = encode(offset, a);
        for (int i = 0; i < 20; ++i) {
            if (core::is_ok(bus_.send(device(offset), a, n, 5))) return Status::Ok;
            wait_1ms();
        }
        return Status::Timeout;
    }

    PsI2c& bus_;
    std::uint8_t addr_;
    std::size_t size_;
    std::size_t page_;
    std::uint8_t addr_bytes_;
    std::uint8_t mux_addr_;
    std::uint8_t mux_mask_;
};

// --------------------------------------------------------------- Board
class ZynqMpR5Board final : public IBoard {
public:
    Status init() override {
        XGpioPs_Config* gc = XGpioPs_LookupConfig(EP_ZYNQMP_R5_GPIO);
        gpio_ready_        = gc != nullptr && XGpioPs_CfgInitialize(&gpio_, gc, gc->BaseAddr) == XST_SUCCESS;
        status_led_.configure(PinMode::Output);
        alarm_led_.configure(PinMode::Output);
        button_.configure(PinMode::InputPullUp);
        sensor_ok_ = core::is_ok(sensor_i2c_.open(EP_ZYNQMP_R5_I2C_SCLK_HZ));
#ifdef EP_ZYNQMP_R5_AUX_UART
        aux_ok_ = core::is_ok(aux_.open(UartConfig{}));
#endif
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "zynqmp-r5"; }

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
#ifdef EP_ZYNQMP_R5_AUX_UART
        if (id == UartId::Aux && aux_ok_) return &aux_;
#endif
        return nullptr;
    }
    II2c* i2c(I2cId id) override { return id == I2cId::Sensor && sensor_ok_ ? &sensor_i2c_ : nullptr; }
    INvStorage* storage() override { return EP_ZYNQMP_R5_EEPROM_ADDR != 0 && sensor_ok_ ? &eeprom_ : nullptr; }

    [[noreturn]] void reset() override {
        // PS soft reset: CRL_APB.RESET_CTRL (0xFF5E0218) bit SOFT_RESET. Cần quyền ghi (XPPU) từ RPU;
        // hệ thống dùng PMU firmware có thể thay bằng XPm_ResetAssert/XPm_SystemShutdown (xilpm).
        Xil_Out32(0xFF5E0218u, 0x10u);
        for (;;) {}
    }

private:
    XGpioPs gpio_{};
    bool gpio_ready_ = false;
    PsGpio status_led_{gpio_, gpio_ready_, EP_ZYNQMP_R5_LED_STATUS};
    PsGpio alarm_led_{gpio_, gpio_ready_, EP_ZYNQMP_R5_LED_ALARM};
    PsGpio button_{gpio_, gpio_ready_, EP_ZYNQMP_R5_BUTTON};
    PsUart console_{static_cast<Key>(EP_ZYNQMP_R5_CONSOLE_UART)};
#ifdef EP_ZYNQMP_R5_AUX_UART
    PsUart aux_{static_cast<Key>(EP_ZYNQMP_R5_AUX_UART)};
    bool aux_ok_ = false;
#endif
    PsI2c sensor_i2c_{static_cast<Key>(EP_ZYNQMP_R5_SENSOR_I2C)};
    bool sensor_ok_ = false;
    Eeprom24Storage eeprom_{sensor_i2c_,
                            EP_ZYNQMP_R5_EEPROM_ADDR,
                            EP_ZYNQMP_R5_EEPROM_SIZE,
                            EP_ZYNQMP_R5_EEPROM_PAGE,
                            EP_ZYNQMP_R5_EEPROM_ADDR_BYTES,
                            EP_ZYNQMP_R5_I2C_MUX_ADDR,
                            EP_ZYNQMP_R5_EEPROM_MUX_MASK};
};

}  // namespace

IBoard& board() {
    static ZynqMpR5Board instance;
    return instance;
}

}  // namespace hal
