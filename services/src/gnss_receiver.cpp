#include "services/gnss_receiver.hpp"

#include "osal/time.hpp"

namespace svc {

core::Status GnssReceiver::init(const Config& cfg) {
    if (cfg.ports == nullptr || cfg.port >= cfg.ports->count()) return core::Status::InvalidArg;
    if (running_) return core::Status::Busy;
    cfg_ = cfg;
    return core::Status::Ok;
}

core::Status GnssReceiver::start(std::uint32_t baudrate) {
    if (cfg_.ports == nullptr) return core::Status::NotSupported;
    if (running_) return core::Status::Ok;
    const std::uint32_t baud = baudrate != 0 ? baudrate : cfg_.baudrate;
    core::Status st = cfg_.ports->set_baudrate(cfg_.port, baud);
    if (core::is_ok(st)) st = cfg_.ports->set_mode(cfg_.port, SerialMode::Stream);
    if (!core::is_ok(st)) return st;
    baud_ = baud;
    rx_bytes_ = 0;
    last_rx_ms_ = 0;
    running_ = true;
    st = thread_.start(cfg_.task, &osal::Thread::invoke<GnssReceiver, &GnssReceiver::run>, this);
    if (!core::is_ok(st)) running_ = false;
    return st;
}

void GnssReceiver::stop() {
    if (!running_.exchange(false)) return;
    thread_.join();
    TxJob job;   // bỏ lệnh còn chờ (nhả Buffer về pool)
    while (tx_.receive(job, osal::kNoWait)) {}
}

bool GnssReceiver::latest_fix(device::GnssFix& out, std::uint32_t& seq) const {
    osal::LockGuard g(fix_mtx_);
    out = fix_;
    seq = fix_seq_;
    return fix_seq_ != 0;
}

core::Status GnssReceiver::send(msg::Buffer data, std::uint32_t baudrate) {
    if (!data && baudrate == 0) return core::Status::InvalidArg;
    TxJob job;
    job.data     = std::move(data);
    job.baudrate = baudrate;
    if (!tx_.send(job, osal::kNoWait)) {
        tx_dropped_.fetch_add(1);
        return core::Status::Busy;
    }
    return core::Status::Ok;
}

GnssStats GnssReceiver::stats() const {
    GnssStats s;
    s.rx_bytes        = rx_bytes_.load();
    s.tx_bytes        = tx_bytes_.load();
    s.sentences       = sentences_.load();
    s.checksum_errors = checksum_errors_.load();
    s.raw_dropped     = raw_dropped_.load();
    s.tx_dropped      = tx_dropped_.load();
    s.tx_errors       = tx_errors_.load();
    s.baudrate        = baud_.load();
    s.last_rx_ms      = last_rx_ms_.load();
    s.running         = running_.load();
    {
        osal::LockGuard g(fix_mtx_);
        s.fixes = fix_seq_;
    }
    return s;
}

void GnssReceiver::on_sentence(const device::GnssFix& fix, device::NmeaParser::Result kind, std::uint64_t now_ms) {
    if (kind == device::NmeaParser::Result::Gga) {
        last_gga_ms_ = now_ms;
    } else if (last_gga_ms_ != 0 && now_ms < last_gga_ms_ + kRmcOnlyMs) {
        return;   // có GGA đều đặn: RMC chỉ cập nhật ngày/tốc độ, fix gom theo GGA
    }
    osal::LockGuard g(fix_mtx_);
    fix_ = fix;
    ++fix_seq_;
}

void GnssReceiver::process(std::uint32_t wait_ms, std::uint64_t now_ms) {
    TxJob job;
    if (tx_.receive(job, wait_ms)) {   // chờ lệnh = nhịp nghỉ của vòng lặp
        if (job.baudrate != 0 && core::is_ok(cfg_.ports->set_baudrate(cfg_.port, job.baudrate))) baud_ = job.baudrate;
        const std::size_t n = job.data.size();
        if (n > 0) {
            const std::size_t w = cfg_.ports->write(cfg_.port, job.data.data(), n);
            tx_bytes_.fetch_add(static_cast<std::uint32_t>(w));
            if (w != n) tx_errors_.fetch_add(1);
        }
    }
    std::uint8_t buf[256];
    for (int k = 0; k < 16; ++k) {   // tối đa 4 KB mỗi vòng
        const std::size_t n = cfg_.ports->read(cfg_.port, buf, sizeof(buf));
        if (n == 0) break;
        rx_bytes_.fetch_add(static_cast<std::uint32_t>(n));
        last_rx_ms_ = now_ms;
        parser_.feed(buf, n, [&](const device::GnssFix& f, device::NmeaParser::Result r) { on_sentence(f, r, now_ms); });
        if (tap_) {
            for (std::size_t i = 0; i < n; ++i) {
                if (!raw_.push(buf[i])) {
                    raw_dropped_.fetch_add(static_cast<std::uint32_t>(n - i));
                    break;
                }
            }
        }
    }
    sentences_       = parser_.sentences();
    checksum_errors_ = parser_.checksum_errors();
}

void GnssReceiver::run() {
    while (running_) process(10, osal::now_ms());
}

}  // namespace svc
