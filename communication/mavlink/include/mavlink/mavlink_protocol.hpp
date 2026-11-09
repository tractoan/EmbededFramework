#pragma once
#include <atomic>

#include "mavlink/mavlink_map.hpp"
#include "mavlink/mavlink_v1.hpp"
#include "osal/mutex.hpp"

namespace mavlink {

// MAVLink v1 cho thiết bị (component) trong hệ thống MAVLink:
//  - HEARTBEAT theo chu kỳ; system_status lấy từ Event qua Map::states
//  - Param protocol nối thẳng ParamStore (giá trị gửi dạng REAL32; PARAM_SET làm tròn về số nguyên)
//  - COMMAND_LONG -> Map::commands -> COMMAND_ACK
//  - Event -> NAMED_VALUE_FLOAT / STATUSTEXT
//  - Theo dõi HEARTBEAT của trạm mặt đất (GCS): log khi kết nối / mất kết nối
// Chạy trên mọi IByteStream: UART, TCP, UDP.
class MavlinkV1Protocol final : public comm::IProtocol, public msg::IEventListener {
public:
    static constexpr std::uint32_t kGcsTimeoutMs = 3000;
    static constexpr std::size_t kParamsPerPoll  = 4;   // giãn PARAM_VALUE khi gửi danh sách

    explicit MavlinkV1Protocol(const Map& map) : map_(map) {}

    comm::ProtocolId id() const override { return comm::proto::kMavlink; }
    const char* name() const override { return "mavlink"; }

    core::Status attach(comm::IByteStream& io, comm::Services& services) override;
    void detach() override;
    void on_rx(const std::uint8_t* data, std::size_t len) override;
    void poll(std::uint64_t now_ms) override;
    bool probe(const std::uint8_t* data, std::size_t len) const override { return is_valid_frame(data, len); }

    void on_event(const msg::Event& e) override;   // telemetry / statustext / trạng thái (mọi thread)

    bool gcs_connected() const { return gcs_connected_.load(); }
    std::uint32_t frames_rx() const { return parser_.frames_ok(); }
    std::uint32_t frames_rejected() const { return parser_.rejected(); }

private:
    static void on_frame(void* self, const Frame& f);
    void handle(const Frame& f);
    void send(std::uint8_t msgid, const std::uint8_t* payload, std::size_t len);
    void send_heartbeat();
    void send_param(std::size_t index);
    void send_ack(std::uint16_t command, std::uint8_t result);
    bool for_me(std::uint8_t target_system, std::uint8_t target_component) const;
    int find_param(const std::uint8_t* id16) const;

    const Map& map_;
    comm::Services* services_ = nullptr;
    Parser parser_;

    osal::Mutex tx_mtx_;                 // seq + ghi khung nguyên khối (gửi từ nhiều thread)
    comm::IByteStream* io_ = nullptr;    // chỉ đổi dưới tx_mtx_
    std::uint8_t seq_      = 0;

    std::atomic<std::uint8_t> mav_state_{mav::kStateActive};
    std::atomic<bool> gcs_connected_{false};
    std::uint64_t last_heartbeat_ms_ = 0;   // các trường dưới: chỉ thread RX của kênh
    std::uint64_t last_gcs_ms_       = 0;
    std::uint64_t now_ms_            = 0;
    long param_cursor_               = -1;  // >= 0: đang gửi danh sách tham số
};

}  // namespace mavlink
