#include "sim_bringup.hpp"

#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace hal::sim {

using core::Status;
using Clock = std::chrono::steady_clock;

namespace {

void sim_print(const char* text) {
    const std::size_t n = std::strlen(text);
    (void)!::write(STDOUT_FILENO, text, n);
}

std::uint32_t env_mask(const char* name) {
    const char* v = std::getenv(name);
    return v ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 16)) : 0u;
}

void put_le16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put_le32(std::uint8_t* p, std::int32_t s) {
    const auto v = static_cast<std::uint32_t>(s);
    for (int k = 0; k < 4; ++k) p[k] = static_cast<std::uint8_t>(v >> (8 * k));
}
std::uint16_t sum16_le(const std::uint8_t* p, std::size_t bytes) {
    std::uint32_t s = 0;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) s += static_cast<std::uint32_t>(p[i] | (p[i + 1] << 8));
    return static_cast<std::uint16_t>(s & 0xFFFF);
}

}  // namespace

// ============================================================ SimLoopUart
Status SimLoopUart::open(const UartConfig& /*cfg*/) {
    std::lock_guard<std::mutex> g(m_);
    buf_.clear();
    return Status::Ok;
}

std::size_t SimLoopUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    if (mode_ == Mode::Silent) return len;   // byte đi ra dây, không quay về
    {
        std::lock_guard<std::mutex> g(m_);
        ++writes_;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t b = data[i];
            if (mode_ == Mode::Broken && (writes_ % 3) == 0 && i == len / 2) b ^= 0x10;   // nhiễu: lật bit
            if (mode_ == Mode::Broken && (writes_ % 5) == 0 && i == 1) continue;           // mất byte
            buf_.push(b);   // đầy: tràn FIFO phần cứng, mất byte
        }
    }
    cv_.notify_all();
    return len;
}

std::size_t SimLoopUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return !buf_.empty(); });
    return buf_.pop(data, len);
}

// ============================================================ SimGnssUart
SimGnssUart::SimGnssUart(std::uint32_t start_delay_ms)
    : start_(Clock::now() + std::chrono::milliseconds(start_delay_ms)), next_fix_(start_) {}

Status SimGnssUart::open(const UartConfig& cfg) {
    std::lock_guard<std::mutex> g(m_);
    if (baud_ != 0 && baud_ != cfg.baudrate) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "\033[35m[SIM] GNSS: baudrate %lu -> %lu\033[0m\r\n",
                      static_cast<unsigned long>(baud_), static_cast<unsigned long>(cfg.baudrate));
        sim_print(msg);
    }
    baud_ = cfg.baudrate;
    return Status::Ok;
}

void SimGnssUart::queue_sentence(const char* body) {
    std::uint8_t cs = 0;
    for (const char* p = body; *p; ++p) cs ^= static_cast<std::uint8_t>(*p);
    char line[160];
    const int n = std::snprintf(line, sizeof(line), "$%s*%02X\r\n", body, cs);
    for (int i = 0; i > -1 && i < n && i < static_cast<int>(sizeof(line)) - 1; ++i) out_.push(static_cast<std::uint8_t>(line[i]));
}

void SimGnssUart::generate(Clock::time_point now) {
    if (now < start_) return;
    while (now >= next_fix_) {
        next_fix_ += std::chrono::seconds(1);
        ++epoch_;
        const unsigned s = epoch_ % 60, m = (epoch_ / 60) % 60, h = (epoch_ / 3600 + 3) % 24;
        const double jitter = 0.00001 * std::sin(epoch_ * 0.3);
        char body[128];
        std::snprintf(body, sizeof(body), "GPGGA,%02u%02u%02u.00,2101.71%03d,N,10551.25%03d,E,1,12,0.8,15.2,M,-22.0,M,,",
                      h, m, s, static_cast<int>(500 + jitter * 1e7), static_cast<int>(200 - jitter * 1e7));
        queue_sentence(body);
        std::snprintf(body, sizeof(body), "GPRMC,%02u%02u%02u.00,A,2101.71500,N,10551.25200,E,0.02,,031126,,,A", h, m, s);
        queue_sentence(body);
    }
}

std::size_t SimGnssUart::write(const std::uint8_t* data, std::size_t len, std::uint32_t /*timeout_ms*/) {
    std::lock_guard<std::mutex> g(m_);
    if (Clock::now() < start_) return len;   // máy thu chưa bật
    for (std::size_t i = 0; i < len; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == '\r' || c == '\n') {
            if (cmd_len_ == 0) continue;
            cmd_[cmd_len_] = '\0';
            char msg[200];
            std::snprintf(msg, sizeof(msg), "\033[35m[SIM] GNSS rx command: %s\033[0m\r\n", cmd_);
            sim_print(msg);
            char body[80];
            std::snprintf(body, sizeof(body), "PSIMACK,%.60s", cmd_);
            for (char* p = body; *p; ++p) *p = (*p == '*' || *p == '$') ? '_' : *p;
            queue_sentence(body);
            cmd_len_ = 0;
        } else if (cmd_len_ < sizeof(cmd_) - 1) {
            cmd_[cmd_len_++] = c;
        }
    }
    cv_.notify_all();
    return len;
}

std::size_t SimGnssUart::read(std::uint8_t* data, std::size_t len, std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lk(m_);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        generate(Clock::now());
        if (!out_.empty()) break;
        const auto wake = next_fix_ < deadline ? (next_fix_ > Clock::now() ? next_fix_ : Clock::now()) : deadline;
        if (Clock::now() >= deadline) return 0;
        cv_.wait_until(lk, wake);
    }
    return out_.pop(data, len);
}

// ============================================================= SimHdlcImu
Status SimHdlcImu::open(const HdlcConfig& /*cfg*/) {
    open_ = true;
    next_ = Clock::now();
    return Status::Ok;
}

std::size_t SimHdlcImu::build(std::uint8_t* buf, std::size_t cap) {
    const double rate = kind_ == 0 ? 400.0 : 100.0;
    const double dt   = 1.0 / rate;
    const double t    = seq_ * dt;
    // Chuyển động giả lập: quay quanh Z 10°/s + lắc nhẹ quanh X, gia tốc trọng trường trên -Z.
    const double wx = 0.05 * std::sin(2 * M_PI * 0.5 * t), wy = 0.0, wz = 10.0 * M_PI / 180.0;
    const double ax = 0.2 * std::sin(2 * M_PI * 0.2 * t), ay = 0.0, az = -9.80665;
    ++seq_;

    if (kind_ == 0) {   // "ln200": 13 word 16-bit LE: dV xyz (2^-14 m/s), dθ xyz (2^-19 rad), status, mux id, mux, raw x3, sum
        if (cap < 26) return 0;
        const double sv = std::ldexp(1.0, 14), sa = std::ldexp(1.0, 19);
        const std::int16_t w[12] = {
            static_cast<std::int16_t>(std::lround(ax * dt * sv)), static_cast<std::int16_t>(std::lround(ay * dt * sv)),
            static_cast<std::int16_t>(std::lround(az * dt * sv)), static_cast<std::int16_t>(std::lround(wx * dt * sa)),
            static_cast<std::int16_t>(std::lround(wy * dt * sa)), static_cast<std::int16_t>(std::lround(wz * dt * sa)),
            0, static_cast<std::int16_t>(seq_ % 32), 0x0123, static_cast<std::int16_t>(seq_), 0, 0};
        for (int i = 0; i < 12; ++i) put_le16(buf + 2 * i, static_cast<std::uint16_t>(w[i]));
        put_le16(buf + 24, sum16_le(buf, 24));
        return 26;
    }
    // "hg1700": [0]=0x0E địa chỉ, [1]=0x01 mã bản tin, dθ xyz int32 (2^-33 rad), dV xyz int32 (2^-27 ft/s),
    //           status u16, checksum u16 = tổng word 16-bit từ byte 2..27
    if (cap < 30) return 0;
    const double sa = std::ldexp(1.0, 33), sv = std::ldexp(1.0, 27) / 0.3048;
    buf[0] = 0x0E;
    buf[1] = 0x01;
    put_le32(buf + 2, static_cast<std::int32_t>(std::lround(wx * dt * sa)));
    put_le32(buf + 6, static_cast<std::int32_t>(std::lround(wy * dt * sa)));
    put_le32(buf + 10, static_cast<std::int32_t>(std::lround(wz * dt * sa)));
    put_le32(buf + 14, static_cast<std::int32_t>(std::lround(ax * dt * sv)));
    put_le32(buf + 18, static_cast<std::int32_t>(std::lround(ay * dt * sv)));
    put_le32(buf + 22, static_cast<std::int32_t>(std::lround(az * dt * sv)));
    put_le16(buf + 26, 0x0000);
    put_le16(buf + 28, sum16_le(buf + 2, 26));
    return 30;
}

std::size_t SimHdlcImu::read_frame(std::uint8_t* buf, std::size_t cap, std::uint32_t timeout_ms, HdlcFrameStatus* st) {
    if (st != nullptr) *st = HdlcFrameStatus{};
    if (!open_ || !present_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
        return 0;
    }
    const auto period = std::chrono::microseconds(kind_ == 0 ? 2500 : 10000);
    auto now = Clock::now();
    if (now - next_ > std::chrono::milliseconds(200)) next_ = now;   // tụt quá xa (máy bận): bỏ qua
    if (next_ > now) {
        if (next_ - now > std::chrono::milliseconds(timeout_ms)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
            return 0;
        }
        std::this_thread::sleep_until(next_);
    }
    next_ += period;
    return build(buf, cap);
}

// ============================================================ SimBringupHw
SimBringupHw::SimBringupHw() {
    const std::uint32_t stuck  = env_mask("EP_SIM_DI_STUCK");
    const std::uint32_t imuoff = env_mask("EP_SIM_IMU_OFF");
    for (std::size_t i = 0; i < kDigital; ++i) {
        outputs_[i].reset(new SimGpio("DO", false));
        inputs_[i].reset(new SimWiredInput(*outputs_[i], (stuck >> i) & 1u));
    }
    for (std::size_t i = 0; i < kHdlc; ++i) {
        hdlc_[i].reset(new SimHdlcImu(static_cast<std::uint8_t>(i), ((imuoff >> i) & 1u) == 0));
    }
    for (std::size_t i = 0; i < kSerials; ++i) {
        char var[24];
        std::snprintf(var, sizeof(var), "EP_SIM_SERIAL%u", static_cast<unsigned>(i));
        std::string spec = i == 0 ? "udp:14555" : (i == 1 ? "gnss:0" : "loop");
        if (const char* v = std::getenv(var)) spec = v;

        if (spec.rfind("udp", 0) == 0) {
            const std::size_t c = spec.find(':');
            const auto port = static_cast<std::uint16_t>(c == std::string::npos ? 14555 : std::atoi(spec.c_str() + c + 1));
            serials_[i].reset(new SimUdpUart(port));
        } else if (spec.rfind("gnss", 0) == 0) {
            const std::size_t c = spec.find(':');
            serials_[i].reset(new SimGnssUart(c == std::string::npos ? 0u : static_cast<std::uint32_t>(std::atoi(spec.c_str() + c + 1))));
        } else if (spec == "broken") {
            serials_[i].reset(new SimLoopUart(SimLoopUart::Mode::Broken));
        } else if (spec == "silent") {
            serials_[i].reset(new SimLoopUart(SimLoopUart::Mode::Silent));
        } else {
            serials_[i].reset(new SimLoopUart(SimLoopUart::Mode::Loop));
        }
    }
}

void SimBringupHw::init() {
    for (auto& o : outputs_) {
        o->configure(PinMode::Output);
        o->write(false);
    }
}

}  // namespace hal::sim
