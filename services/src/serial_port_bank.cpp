#include "services/serial_port_bank.hpp"

#include <cstring>

#include "osal/time.hpp"
#include "utils/crc.hpp"

namespace svc {

const char* to_string(SerialMode m) {
    switch (m) {
        case SerialMode::Off:      return "off";
        case SerialMode::Loopback: return "loopback";
        case SerialMode::Stream:   return "stream";
        case SerialMode::External: return "external";
    }
    return "?";
}

const char* to_string(LoopResult r) {
    switch (r) {
        case LoopResult::None:      return "-";
        case LoopResult::Pass:      return "PASS";
        case LoopResult::Timeout:   return "TIMEOUT";
        case LoopResult::Mismatch:  return "MISMATCH";
        case LoopResult::OpenError: return "OPEN-ERR";
    }
    return "?";
}

void SerialPortBank::make_test_frame(std::uint8_t port, std::uint8_t seq, std::uint8_t* out) {
    out[0] = 0x55;
    out[1] = 0xAA;
    out[2] = port;
    out[3] = seq;
    // Mẫu dữ liệu thay đổi theo seq (LFSR 8-bit) để phát hiện cả dính bit lẫn lệch khung cũ.
    std::uint8_t x = static_cast<std::uint8_t>(seq ^ 0xA5 ^ (port << 4));
    for (std::size_t i = 4; i < kFrameLen - 2; ++i) {
        x = static_cast<std::uint8_t>((x << 1) | (((x >> 7) ^ (x >> 5) ^ (x >> 4) ^ (x >> 3)) & 1u));
        out[i] = x;
    }
    const std::uint16_t crc = utils::crc16_ccitt(out, kFrameLen - 2);
    out[kFrameLen - 2] = static_cast<std::uint8_t>(crc >> 8);
    out[kFrameLen - 1] = static_cast<std::uint8_t>(crc & 0xFF);
}

core::Status SerialPortBank::add(const PortConfig& cfg) {
    if (running_) return core::Status::Busy;
    if (count_ >= ports_.size()) return core::Status::NoMemory;
    Port& p  = ports_[count_];
    p.bank   = this;
    p.index  = static_cast<std::uint8_t>(count_);
    p.cfg    = cfg;
    p.mode   = cfg.mode;
    p.baud   = cfg.uart_cfg.baudrate;
    ++count_;
    return core::Status::Ok;
}

core::Status SerialPortBank::set_mode(std::size_t port, SerialMode mode) {
    if (port >= count_) return core::Status::OutOfRange;
    Port& p = ports_[port];
    if (mode == SerialMode::External || p.mode.load() == SerialMode::External) return core::Status::InvalidArg;
    p.mode = mode;
    return core::Status::Ok;
}

SerialPortStats SerialPortBank::stats(std::size_t port) const {
    SerialPortStats s;
    if (port >= count_) return s;
    const Port& p = ports_[port];
    s.mode       = p.mode.load();
    s.last       = p.last.load();
    s.pass       = p.pass.load();
    s.fail       = p.fail.load();
    s.tx_bytes   = p.tx_bytes.load();
    s.rx_bytes   = p.rx_bytes.load();
    s.overflow   = p.overflow.load();
    s.baudrate   = p.baud.load();
    s.last_rx_ms = p.last_rx_ms.load();
    s.peer       = p.cfg.peer;
    return s;
}

void SerialPortBank::reset_counters(std::size_t port) {
    if (port >= count_) return;
    Port& p = ports_[port];
    p.pass = p.fail = p.tx_bytes = p.rx_bytes = p.overflow = 0;
    p.last = LoopResult::None;
}

std::size_t SerialPortBank::write(std::size_t port, const std::uint8_t* data, std::size_t len) {
    if (port >= count_ || data == nullptr) return 0;
    Port& p = ports_[port];
    if (p.mode.load() != SerialMode::Stream || p.cfg.uart == nullptr) return 0;
    const std::size_t n = p.cfg.uart->write(data, len, 100);
    p.tx_bytes.fetch_add(static_cast<std::uint32_t>(n));
    return n;
}

std::size_t SerialPortBank::read(std::size_t port, std::uint8_t* data, std::size_t max) {
    if (port >= count_ || data == nullptr) return 0;
    return ports_[port].rx.pop(data, max);
}

core::Status SerialPortBank::set_baudrate(std::size_t port, std::uint32_t baud) {
    if (port >= count_) return core::Status::OutOfRange;
    if (baud < 300 || baud > 12000000) return core::Status::InvalidArg;
    if (ports_[port].mode.load() == SerialMode::External) return core::Status::InvalidArg;
    ports_[port].baud_request = baud;
    return core::Status::Ok;
}

core::Status SerialPortBank::start() {
    if (running_) return core::Status::Ok;
    running_ = true;
    for (std::size_t i = 0; i < count_; ++i) {
        Port& p = ports_[i];
        if (p.mode.load() == SerialMode::External) continue;
        const core::Status st = p.thread.start(osal::ThreadConfig{p.cfg.name, p.cfg.priority, p.cfg.stack_bytes, p.cfg.stack_mem},
                                               &osal::Thread::invoke<Port, &Port::run>, &p);
        if (!core::is_ok(st)) return st;
    }
    return core::Status::Ok;
}

void SerialPortBank::stop() {
    if (!running_) return;
    running_ = false;
    for (std::size_t i = 0; i < count_; ++i) ports_[i].thread.join();
}

// =================================================================== Port
bool SerialPortBank::Port::ensure_open() {
    if (const std::uint32_t b = baud_request.exchange(0)) {
        cfg.uart_cfg.baudrate = b;
        if (is_open && cfg.uart != nullptr) cfg.uart->close();
        is_open       = false;
        retry_open_ms = 0;
    }
    if (is_open) return true;
    const std::uint64_t now = osal::now_ms();
    if (now < retry_open_ms) return false;
    if (cfg.uart != nullptr && core::is_ok(cfg.uart->open(cfg.uart_cfg))) {
        is_open = true;
        baud    = cfg.uart_cfg.baudrate;
        return true;
    }
    retry_open_ms = now + 1000;
    return false;
}

void SerialPortBank::Port::loopback_once(Port& rx_side, Port& stats_side) {
    std::uint8_t frame[kFrameLen];
    std::uint8_t got[kFrameLen];
    make_test_frame(stats_side.index, stats_side.seq++, frame);

    std::uint8_t junk[64];   // bỏ byte cũ còn sót (phản hồi muộn của lần thử trước)
    for (int k = 0; k < 16 && rx_side.cfg.uart->read(junk, sizeof(junk), 0) > 0; ++k) {}

    tx_bytes.fetch_add(static_cast<std::uint32_t>(cfg.uart->write(frame, kFrameLen, 100)));

    const std::uint32_t baudrate = cfg.uart_cfg.baudrate ? cfg.uart_cfg.baudrate : 9600;
    const std::uint64_t deadline = osal::now_ms() + bank->timeout_ms_ + (kFrameLen * 10u * 1000u) / baudrate + 1;
    std::size_t n = 0;
    for (std::uint64_t now = osal::now_ms(); n < kFrameLen && now < deadline; now = osal::now_ms()) {
        n += rx_side.cfg.uart->read(got + n, kFrameLen - n, static_cast<std::uint32_t>(deadline - now));
    }
    rx_side.rx_bytes.fetch_add(static_cast<std::uint32_t>(n));
    if (n > 0) rx_side.last_rx_ms = osal::now_ms();

    const LoopResult r = n == 0 ? LoopResult::Timeout
                                : (n == kFrameLen && std::memcmp(got, frame, kFrameLen) == 0 ? LoopResult::Pass
                                                                                              : LoopResult::Mismatch);
    stats_side.last = r;
    (r == LoopResult::Pass ? stats_side.pass : stats_side.fail).fetch_add(1);
}

void SerialPortBank::Port::stream_once() {
    std::uint8_t buf[128];
    const std::size_t n = cfg.uart->read(buf, sizeof(buf), 20);
    if (n == 0) return;
    rx_bytes.fetch_add(static_cast<std::uint32_t>(n));
    last_rx_ms = osal::now_ms();
    for (std::size_t i = 0; i < n; ++i) {
        if (!rx.push(buf[i])) overflow.fetch_add(1);
    }
}

void SerialPortBank::Port::run() {
    while (bank->running_) {
        const SerialMode m = mode.load();
        if (m == SerialMode::Off || m == SerialMode::External) {
            osal::sleep_ms(50);
            continue;
        }

        Port* peer = nullptr;
        if (m == SerialMode::Loopback && cfg.peer >= 0 && static_cast<std::size_t>(cfg.peer) < bank->count_) {
            peer = &bank->ports_[static_cast<std::size_t>(cfg.peer)];
            if (peer->mode.load() != SerialMode::Loopback) peer = nullptr;   // cổng kia đang làm việc khác
            if (peer != nullptr && cfg.peer < index) {                     // cặp do cổng chỉ số nhỏ điều khiển
                osal::sleep_ms(50);
                continue;
            }
        }

        if (!ensure_open() || (peer != nullptr && !peer->ensure_open())) {
            if (m == SerialMode::Loopback) {
                Port& who = (peer != nullptr && is_open) ? *peer : *this;
                who.last = LoopResult::OpenError;
                who.fail.fetch_add(1);
                osal::sleep_ms(bank->period_ms_);
            } else {
                osal::sleep_ms(50);
            }
            continue;
        }

        if (m == SerialMode::Stream) {
            stream_once();
            continue;
        }
        if (peer == nullptr) {
            loopback_once(*this, *this);
        } else {
            pair_dir = !pair_dir;
            if (pair_dir) {
                loopback_once(*peer, *this);          // A -> B, kết quả ghi cho A
            } else {
                peer->loopback_once(*this, *peer);    // B -> A, kết quả ghi cho B
            }
        }
        osal::sleep_ms(bank->period_ms_);
    }
}

}  // namespace svc
