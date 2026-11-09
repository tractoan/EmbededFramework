#include "mavlink/mavlink_v1.hpp"

#include "utils/crc.hpp"

namespace mavlink {

namespace {
// id, độ dài payload, CRC_EXTRA (common.xml)
constexpr MessageInfo kMessages[] = {
    {msgid::kHeartbeat, 9, 50},          {msgid::kParamRequestRead, 20, 214}, {msgid::kParamRequestList, 2, 159},
    {msgid::kParamValue, 25, 220},       {msgid::kParamSet, 23, 168},         {msgid::kCommandLong, 33, 152},
    {msgid::kCommandAck, 3, 143},        {msgid::kNamedValueFloat, 18, 170},  {msgid::kStatustext, 51, 83},
};

std::uint16_t frame_crc(const std::uint8_t* frame, std::size_t payload_len, std::uint8_t crc_extra) {
    // CRC trên LEN..PAYLOAD (bỏ STX), rồi CRC_EXTRA
    const std::uint16_t crc = utils::crc16_mcrf4xx(frame + 1, kHeaderLen - 1 + payload_len);
    return utils::crc16_mcrf4xx(&crc_extra, 1, crc);
}
}  // namespace

const MessageInfo* find_message(std::uint8_t id) {
    for (const MessageInfo& m : kMessages) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

std::size_t encode(std::uint8_t* out, std::uint8_t seq, std::uint8_t sysid, std::uint8_t compid, std::uint8_t msgid,
                   const std::uint8_t* payload, std::size_t len) {
    const MessageInfo* info = find_message(msgid);
    if (info == nullptr || len != info->length) return 0;
    out[0] = kStx;
    out[1] = static_cast<std::uint8_t>(len);
    out[2] = seq;
    out[3] = sysid;
    out[4] = compid;
    out[5] = msgid;
    std::memcpy(out + kHeaderLen, payload, len);
    const std::uint16_t crc = frame_crc(out, len, info->crc_extra);
    put_u16(out + kHeaderLen + len, crc);
    return kHeaderLen + len + 2;
}

bool is_valid_frame(const std::uint8_t* data, std::size_t len, std::size_t* frame_len) {
    if (len < kHeaderLen + 2 || data[0] != kStx) return false;
    const std::size_t need = kHeaderLen + data[1] + 2;
    if (len < need) return false;
    const MessageInfo* info = find_message(data[5]);
    if (info == nullptr || data[1] != info->length) return false;
    if (get_u16(data + kHeaderLen + data[1]) != frame_crc(data, data[1], info->crc_extra)) return false;
    if (frame_len != nullptr) *frame_len = need;
    return true;
}

void Parser::consume(std::size_t k) {
    std::memmove(buf_, buf_ + k, n_ - k);
    n_ -= k;
}

void Parser::feed(const std::uint8_t* data, std::size_t len, Callback cb, void* ctx) {
    for (std::size_t i = 0; i < len; ++i) {
        if (n_ == sizeof(buf_)) consume(1);   // tràn (chỉ khi toàn rác): bỏ byte cũ nhất
        buf_[n_++] = data[i];

        for (;;) {
            std::size_t skip = 0;   // bỏ rác cho tới STX
            while (skip < n_ && buf_[skip] != kStx) ++skip;
            if (skip > 0) consume(skip);
            if (n_ < 2) break;
            const std::size_t need = kHeaderLen + buf_[1] + 2;
            if (n_ < need) break;   // chờ thêm byte

            std::size_t flen = 0;
            if (is_valid_frame(buf_, n_, &flen)) {
                ++frames_ok_;
                const Frame f{buf_[2], buf_[3], buf_[4], buf_[5], buf_[1], buf_ + kHeaderLen};
                if (cb != nullptr) cb(ctx, f);
                consume(flen);
            } else {
                ++rejected_;   // STX giả / CRC sai / message lạ: trượt 1 byte để tái đồng bộ
                consume(1);
            }
        }
    }
}

}  // namespace mavlink
