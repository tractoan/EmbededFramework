#include "modbus/modbus_rtu.hpp"

#include <cstring>

#include "osal/time.hpp"
#include "utils/crc.hpp"

namespace modbus {

namespace {
// Mã exception chuẩn
constexpr std::uint8_t kIllegalFunction = 0x01;
constexpr std::uint8_t kIllegalAddress  = 0x02;
constexpr std::uint8_t kIllegalValue    = 0x03;
constexpr std::uint8_t kDeviceFailure   = 0x04;

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
void put_be16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v & 0xFF);
}
bool crc_ok(const std::uint8_t* f, std::size_t n) {
    const std::uint16_t crc = utils::crc16_modbus(f, n - 2);
    return f[n - 2] == (crc & 0xFF) && f[n - 1] == (crc >> 8);
}
std::uint16_t to_reg(std::int32_t v, bool is_signed) {
    if (is_signed) {
        if (v < -32768) v = -32768;
        if (v > 32767) v = 32767;
        return static_cast<std::uint16_t>(static_cast<std::int16_t>(v));
    }
    if (v < 0) v = 0;
    if (v > 0xFFFF) v = 0xFFFF;
    return static_cast<std::uint16_t>(v);
}
}  // namespace

// ------------------------------------------------------------- vòng đời
core::Status ModbusRtuProtocol::attach(comm::IByteStream& io, comm::Services& services) {
    if (map_.input_count > kMaxInputs) return core::Status::NoMemory;
    io_       = &io;
    services_ = &services;
    rx_len_   = 0;
    return services.bus.subscribe(msg::evt::kAny, this);
}

void ModbusRtuProtocol::detach() {
    if (services_ != nullptr) services_->bus.unsubscribe(msg::evt::kAny, this);
    io_       = nullptr;
    services_ = nullptr;
    rx_len_   = 0;
}

void ModbusRtuProtocol::on_event(const msg::Event& e) {
    for (std::size_t i = 0; i < map_.input_count; ++i) {
        std::int32_t v = 0;
        if (map_.inputs[i].event == e.id && map_.inputs[i].extract(e, v)) inputs_[i] = v;
    }
}

// ------------------------------------------------------------ đóng khung
std::size_t ModbusRtuProtocol::expected_length(const std::uint8_t* f, std::size_t len) {
    if (len < 2) return 0;
    switch (f[1]) {
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06: return 8;
        case 0x10: return len < 7 ? 0 : 9u + f[6];
        default:   return kUnsupported;
    }
}

bool ModbusRtuProtocol::probe(const std::uint8_t* data, std::size_t len) const {
    const std::size_t need = expected_length(data, len);
    if (need == 0 || need == kUnsupported || need > len) return false;
    return addressed_to_me(data[0]) && crc_ok(data, need);
}

void ModbusRtuProtocol::on_rx(const std::uint8_t* data, std::size_t len) {
    last_rx_ms_ = osal::now_ms();
    for (std::size_t i = 0; i < len; ++i) {
        if (rx_len_ == sizeof(rx_)) rx_len_ = 0;   // tràn: bỏ rác
        rx_[rx_len_++] = data[i];
    }
    process();
}

void ModbusRtuProtocol::poll(std::uint64_t now_ms) {
    // last_rx_ms_ (on_rx) có thể mới hơn now_ms của vòng lặp kênh -> so sánh dạng cộng, không tràn số
    if (rx_len_ == 0 || now_ms < last_rx_ms_ + kFrameTimeoutMs) return;
    // Khoảng lặng: coi toàn bộ dữ liệu còn lại là một khung (vd. function code không hỗ trợ)
    if (rx_len_ >= 4 && crc_ok(rx_, rx_len_) && addressed_to_me(rx_[0])) {
        frames_ok_.fetch_add(1);
        broadcast_ = rx_[0] == 0;
        exception(rx_[1], kIllegalFunction);
    }
    rx_len_ = 0;
}

void ModbusRtuProtocol::consume(std::size_t n) {
    std::memmove(rx_, rx_ + n, rx_len_ - n);
    rx_len_ -= n;
}

std::size_t ModbusRtuProtocol::find_frame(std::size_t from) const {
    for (std::size_t k = from; k + 4 <= rx_len_; ++k) {
        const std::size_t n = expected_length(rx_ + k, rx_len_ - k);
        if (n != 0 && n != kUnsupported && n <= rx_len_ - k && crc_ok(rx_ + k, n)) return k;
    }
    return 0;
}

void ModbusRtuProtocol::process() {
    while (rx_len_ >= 2) {
        const std::size_t need = expected_length(rx_, rx_len_);
        const bool known       = need != 0 && need != kUnsupported && need <= sizeof(rx_);

        if (known && rx_len_ >= need && crc_ok(rx_, need)) {
            if (addressed_to_me(rx_[0])) {
                frames_ok_.fetch_add(1);
                handle_frame(rx_, need);
            }
            consume(need);
            continue;
        }

        // Đầu bộ đệm không phải khung hợp lệ (rác, mất đồng bộ) hoặc chưa đủ dữ liệu:
        // nếu phía sau đã có một khung hoàn chỉnh hợp lệ thì bỏ phần rác phía trước.
        const bool waiting = need == 0 || (known && rx_len_ < need);
        if (const std::size_t k = find_frame(1)) {
            if (!waiting) crc_errors_.fetch_add(1);
            consume(k);
            continue;
        }
        if (waiting || need == kUnsupported) return;   // chờ thêm byte / chờ khoảng lặng (poll)
        crc_errors_.fetch_add(1);                       // sai CRC: trượt 1 byte
        consume(1);
    }
}

// --------------------------------------------------------------- xử lý
void ModbusRtuProtocol::reply(std::uint8_t* frame, std::size_t len) {
    if (broadcast_ || io_ == nullptr) return;   // broadcast: không phản hồi
    const std::uint16_t crc = utils::crc16_modbus(frame, len);
    frame[len]     = static_cast<std::uint8_t>(crc & 0xFF);
    frame[len + 1] = static_cast<std::uint8_t>(crc >> 8);
    io_->write(frame, len + 2, 100);
}

void ModbusRtuProtocol::exception(std::uint8_t fc, std::uint8_t code) {
    std::uint8_t f[5] = {map_.unit_id, static_cast<std::uint8_t>(fc | 0x80), code};
    reply(f, 3);
}

void ModbusRtuProtocol::handle_frame(const std::uint8_t* f, std::size_t /*len*/) {
    broadcast_             = f[0] == 0;
    const std::uint8_t fc  = f[1];
    const std::uint16_t a  = be16(f + 2);
    const std::uint16_t v  = be16(f + 4);   // số lượng (03/04/10) hoặc giá trị (05/06)

    switch (fc) {
        case 0x03:
        case 0x04: {
            if (v < 1 || v > 125) return exception(fc, kIllegalValue);
            std::uint8_t resp[3 + 250 + 2];
            resp[0] = map_.unit_id;
            resp[1] = fc;
            resp[2] = static_cast<std::uint8_t>(v * 2);
            for (std::uint16_t i = 0; i < v; ++i) {
                std::uint16_t reg = 0;
                const bool ok = fc == 0x03 ? read_holding(static_cast<std::uint16_t>(a + i), reg)
                                           : read_input(static_cast<std::uint16_t>(a + i), reg);
                if (!ok) return exception(fc, kIllegalAddress);
                put_be16(&resp[3 + 2 * i], reg);
            }
            return reply(resp, 3u + 2u * v);
        }
        case 0x05: {
            if (v != 0xFF00 && v != 0x0000) return exception(fc, kIllegalValue);
            for (std::size_t i = 0; i < map_.coil_count; ++i) {
                if (map_.coils[i].address != a) continue;
                if (!core::is_ok(map_.coils[i].action(*services_, v == 0xFF00))) return exception(fc, kDeviceFailure);
                std::uint8_t echo[8];
                std::memcpy(echo, f, 6);
                return reply(echo, 6);
            }
            return exception(fc, kIllegalAddress);
        }
        case 0x06: {
            const std::uint8_t code = write_holding(a, v);
            if (code != 0) return exception(fc, code);
            std::uint8_t echo[8];
            std::memcpy(echo, f, 6);
            return reply(echo, 6);
        }
        case 0x10: {
            if (v < 1 || v > 123 || f[6] != v * 2) return exception(fc, kIllegalValue);
            for (std::uint16_t i = 0; i < v; ++i) {   // kiểm tra địa chỉ trước khi ghi bất kỳ giá trị nào
                if (find_holding(static_cast<std::uint16_t>(a + i)) == nullptr) return exception(fc, kIllegalAddress);
            }
            for (std::uint16_t i = 0; i < v; ++i) {
                const std::uint8_t code = write_holding(static_cast<std::uint16_t>(a + i), be16(f + 7 + 2 * i));
                if (code != 0) return exception(fc, code);
            }
            std::uint8_t resp[8];
            std::memcpy(resp, f, 6);
            return reply(resp, 6);
        }
        default: return exception(fc, kIllegalFunction);
    }
}

bool ModbusRtuProtocol::read_input(std::uint16_t addr, std::uint16_t& out) const {
    for (std::size_t i = 0; i < map_.input_count; ++i) {
        if (map_.inputs[i].address == addr) {
            out = to_reg(inputs_[i].load(), map_.inputs[i].is_signed);
            return true;
        }
    }
    return false;
}

const HoldingRegister* ModbusRtuProtocol::find_holding(std::uint16_t addr) const {
    for (std::size_t i = 0; i < map_.holding_count; ++i) {
        if (map_.holdings[i].address == addr) return &map_.holdings[i];
    }
    return nullptr;
}

bool ModbusRtuProtocol::read_holding(std::uint16_t addr, std::uint16_t& out) const {
    const HoldingRegister* h = find_holding(addr);
    std::int32_t v = 0;
    if (h == nullptr || !core::is_ok(services_->params.get(h->param, v))) return false;
    out = to_reg(v, h->is_signed);
    return true;
}

std::uint8_t ModbusRtuProtocol::write_holding(std::uint16_t addr, std::uint16_t raw) {
    const HoldingRegister* h = find_holding(addr);
    if (h == nullptr) return kIllegalAddress;
    const std::int32_t value = h->is_signed ? static_cast<std::int16_t>(raw) : static_cast<std::int32_t>(raw);
    switch (services_->params.set(h->param, value)) {
        case core::Status::Ok:         return 0;
        case core::Status::OutOfRange: return kIllegalValue;
        case core::Status::NotFound:   return kIllegalAddress;
        default:                       return kDeviceFailure;
    }
}

}  // namespace modbus
