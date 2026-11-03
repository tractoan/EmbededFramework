#include "mavlink/codec.hpp"

#include "utils/crc.hpp"

namespace mavlink {

std::size_t encode(std::uint8_t* out, std::size_t cap, std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid,
                   std::uint32_t msgid, std::uint8_t crc_extra, const std::uint8_t* payload, std::size_t len) {
    if (len > kMaxPayload) return 0;
    while (len > 1 && payload[len - 1] == 0) --len;   // cắt byte 0 cuối (MAVLink v2)
    const std::size_t total = kHeaderV2 + len + 2;
    if (cap < total) return 0;
    out[0] = kStxV2;
    out[1] = static_cast<std::uint8_t>(len);
    out[2] = 0;   // incompat_flags
    out[3] = 0;   // compat_flags
    out[4] = seq;
    out[5] = sysid;
    out[6] = compid;
    out[7] = static_cast<std::uint8_t>(msgid);
    out[8] = static_cast<std::uint8_t>(msgid >> 8);
    out[9] = static_cast<std::uint8_t>(msgid >> 16);
    if (len > 0) std::memcpy(out + kHeaderV2, payload, len);
    std::uint16_t crc = utils::crc16_mcrf4xx(out + 1, kHeaderV2 - 1 + len);
    crc               = utils::crc16_mcrf4xx(&crc_extra, 1, crc);
    out[kHeaderV2 + len]     = static_cast<std::uint8_t>(crc);
    out[kHeaderV2 + len + 1] = static_cast<std::uint8_t>(crc >> 8);
    return total;
}

void Parser::drop(std::size_t n) {
    std::memmove(buf_, buf_ + n, len_ - n);
    len_ -= n;
}

bool Parser::push(std::uint8_t b) {
    if (len_ == sizeof(buf_)) drop(1);
    buf_[len_++] = b;
    return next();
}

bool Parser::next() {
    while (len_ > 0) {
        if (buf_[0] != kStxV2 && buf_[0] != kStxV1) {
            std::size_t k = 1;   // bỏ nhanh mọi byte không phải STX
            while (k < len_ && buf_[k] != kStxV2 && buf_[k] != kStxV1) ++k;
            drop(k);
            continue;
        }
        const bool v2          = buf_[0] == kStxV2;
        const std::size_t hlen = v2 ? kHeaderV2 : 6;
        if (len_ < hlen) return false;
        if (v2 && (buf_[2] & ~kIncompatSigned) != 0) {   // cờ không hỗ trợ / STX giả
            drop(1);
            continue;
        }
        const std::size_t plen  = buf_[1];
        const std::size_t total = hlen + plen + 2 + (v2 && (buf_[2] & kIncompatSigned) ? kSignatureLen : 0);
        if (len_ < total) return false;

        const std::uint32_t msgid = v2 ? (buf_[7] | (buf_[8] << 8) | (static_cast<std::uint32_t>(buf_[9]) << 16)) : buf_[5];
        std::uint8_t extra = 0;
        if (!lookup_(msgid, extra, ctx_)) {
            // Không kiểm được CRC: có thể là khung thật của bản tin lạ, hoặc STX giả. Quét tiếp từ byte sau.
            ++unknown_;
            drop(1);
            continue;
        }
        std::uint16_t crc = utils::crc16_mcrf4xx(buf_ + 1, hlen - 1 + plen);
        crc               = utils::crc16_mcrf4xx(&extra, 1, crc);
        if (buf_[hlen + plen] != static_cast<std::uint8_t>(crc) || buf_[hlen + plen + 1] != static_cast<std::uint8_t>(crc >> 8)) {
            ++crc_errors_;
            drop(1);
            continue;
        }
        msg_         = Message{};
        msg_.version = v2 ? 2 : 1;
        msg_.len     = static_cast<std::uint8_t>(plen);
        msg_.msgid   = msgid;
        msg_.seq     = v2 ? buf_[4] : buf_[2];
        msg_.sysid   = v2 ? buf_[5] : buf_[3];
        msg_.compid  = v2 ? buf_[6] : buf_[4];
        std::memcpy(msg_.payload, buf_ + hlen, plen);   // phần còn lại = 0 (mở rộng theo chuẩn v2)
        drop(total);
        return true;
    }
    return false;
}

bool starts_with_valid_frame(const std::uint8_t* data, std::size_t len, CrcLookup lookup, const void* ctx) {
    if (len < 1 || (data[0] != kStxV1 && data[0] != kStxV2)) return false;
    Parser p(lookup, ctx);
    for (std::size_t i = 0; i < len && i < kMaxFrame; ++i) {
        if (p.push(data[i])) return true;
        if (i > 0 && p.buffered() == 0) return false;   // khung đầu không hợp lệ (đã bị bỏ)
    }
    return false;
}

}  // namespace mavlink
