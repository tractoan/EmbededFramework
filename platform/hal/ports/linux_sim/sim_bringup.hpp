#pragma once
// Phần cứng mô phỏng cho sản phẩm bring-up (chỉ dùng bộ đệm cố định lúc chạy: framework cấm heap sau khởi động): 12 cổng serial, 20 DI / 20 DO, 2 cổng SDLC/HDLC có IMU.
//
// Hành vi từng cổng serial chọn bằng biến môi trường EP_SIM_SERIAL<i> (i = 0..11):
//   udp:<port>   cổng UDP 127.0.0.1:<port> (mặc định cổng 0 = udp:14555 cho phần mềm giao diện MAVLink)
//   gnss[:<ms>]  máy thu GNSS giả lập phát NMEA mỗi giây, bắt đầu sau <ms> (mặc định cổng 1 = gnss:0)
//   loop         có giắc loopback TX->RX (mặc định các cổng còn lại)
//   broken       loopback hỏng: đôi khi mất/sai byte
//   silent       không nối gì
// DI[i] nối vòng với DO[i] (jig test); EP_SIM_DI_STUCK=<mask hex> -> các DI đó kẹt mức 0.
// IMU: cổng HDLC 0 = IMU kiểu "ln200" 400 Hz, cổng 1 = IMU kiểu "hg1700" 100 Hz;
//      EP_SIM_IMU_OFF=<mask> -> tắt IMU tương ứng (mô phỏng mất kết nối).
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "hal/board.hpp"
#include "sim_peripherals.hpp"

namespace hal::sim {

// FIFO byte kích thước cố định, KHÔNG tự khóa (người dùng giữ mutex).
template <std::size_t N>
class ByteFifo {
public:
    bool push(std::uint8_t b) {
        if (count_ == N) return false;
        buf_[(head_ + count_++) % N] = b;
        return true;
    }
    std::size_t pop(std::uint8_t* out, std::size_t max) {
        std::size_t n = 0;
        while (n < max && count_ > 0) {
            out[n++] = buf_[head_];
            head_    = (head_ + 1) % N;
            --count_;
        }
        return n;
    }
    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    void clear() { head_ = count_ = 0; }

private:
    std::uint8_t buf_[N]{};
    std::size_t head_ = 0, count_ = 0;
};

// Cổng serial có giắc loopback (hoặc không nối gì / loopback hỏng).
class SimLoopUart final : public IUart {
public:
    enum class Mode : std::uint8_t { Loop, Broken, Silent };
    explicit SimLoopUart(Mode mode) : mode_(mode) {}
    core::Status open(const UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    Mode mode_;
    std::mutex m_;
    std::condition_variable cv_;
    ByteFifo<4096> buf_;
    std::uint32_t writes_ = 0;
};

// Máy thu GNSS giả lập: phát GGA + RMC mỗi giây; lệnh nhận được (kết thúc CR/LF) được in ra
// và trả lời bằng câu $PSIMACK.
class SimGnssUart final : public IUart {
public:
    explicit SimGnssUart(std::uint32_t start_delay_ms);
    core::Status open(const UartConfig& cfg) override;
    void close() override {}
    std::size_t write(const std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;
    std::size_t read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) override;

private:
    void queue_sentence(const char* body);   // thêm '$', checksum, CRLF
    void generate(std::chrono::steady_clock::time_point now);

    std::chrono::steady_clock::time_point start_;
    std::chrono::steady_clock::time_point next_fix_;
    std::uint32_t baud_ = 0;
    std::mutex m_;
    std::condition_variable cv_;
    ByteFifo<4096> out_;     // byte máy thu phát ra
    char cmd_[128]{};        // lệnh đang nhận (tới CR/LF)
    std::size_t cmd_len_ = 0;
    std::uint32_t epoch_ = 0;
};

// DI nối dây về DO tương ứng (jig loopback), có thể bị "kẹt" ở mức 0.
class SimWiredInput final : public IGpio {
public:
    SimWiredInput(const IGpio& source, bool stuck) : source_(source), stuck_(stuck) {}
    core::Status configure(PinMode) override { return core::Status::Ok; }
    core::Status write(bool) override { return core::Status::NotSupported; }
    bool read() const override { return stuck_ ? false : source_.read(); }

private:
    const IGpio& source_;
    bool stuck_;
};

// Cổng HDLC có IMU giả lập. kind: 0 = "ln200" (13 word 16-bit, 400 Hz), 1 = "hg1700" (30 byte, 100 Hz).
class SimHdlcImu final : public IHdlcPort {
public:
    SimHdlcImu(std::uint8_t kind, bool present) : kind_(kind), present_(present) {}
    core::Status open(const HdlcConfig& cfg) override;
    void close() override { open_ = false; }
    std::size_t read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t timeout_ms, HdlcFrameStatus* st) override;
    core::Status write_frame(const std::uint8_t*, std::size_t, std::uint32_t) override { return core::Status::Ok; }

private:
    std::size_t build(std::uint8_t* buf, std::size_t cap);
    std::uint8_t kind_;
    bool present_;
    bool open_ = false;
    std::uint32_t seq_ = 0;
    std::chrono::steady_clock::time_point next_;
};

// Tập tài nguyên bring-up của board mô phỏng.
class SimBringupHw {
public:
    static constexpr std::size_t kSerials = 12;
    static constexpr std::size_t kDigital = 20;
    static constexpr std::size_t kHdlc    = 2;

    SimBringupHw();
    void init();

    IUart* serial(std::size_t i) { return i < kSerials ? serials_[i].get() : nullptr; }
    IGpio* input(std::size_t i) { return i < kDigital ? inputs_[i].get() : nullptr; }
    IGpio* output(std::size_t i) { return i < kDigital ? outputs_[i].get() : nullptr; }
    IHdlcPort* hdlc(std::size_t i) { return i < kHdlc ? hdlc_[i].get() : nullptr; }

private:
    std::unique_ptr<IUart> serials_[kSerials];
    std::unique_ptr<SimGpio> outputs_[kDigital];
    std::unique_ptr<SimWiredInput> inputs_[kDigital];
    std::unique_ptr<SimHdlcImu> hdlc_[kHdlc];
};

}  // namespace hal::sim
