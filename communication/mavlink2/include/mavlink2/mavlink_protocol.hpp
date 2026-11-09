#pragma once
#include <atomic>

#include "comm/protocol.hpp"
#include "mavlink2/common.hpp"

namespace mavlink2 {

// Kênh gửi bản tin – an toàn khi gọi từ nhiều thread (mỗi khung ghi nguyên khối).
class Sender {
public:
    virtual ~Sender() = default;
    virtual bool send_raw(std::uint32_t msgid, std::uint8_t crc_extra, const std::uint8_t* payload, std::size_t len) = 0;

    template <typename M>
    bool send(const M& m) {
        std::uint8_t buf[M::kLen]{};
        m.pack(buf);
        return send_raw(M::kId, M::kCrcExtra, buf, M::kLen);
    }
};

// Ánh xạ của SẢN PHẨM giữa mô hình dữ liệu chuẩn (Event / Param) và bản tin MAVLink
// (tương tự modbus::Map). Giao thức tự lo: khung, heartbeat, giao thức tham số
// (PARAM_* <-> ParamStore), COMMAND_LONG/ACK và MAV_CMD_PREFLIGHT_STORAGE.
class IDialect {
public:
    virtual ~IDialect() = default;
    // CRC_EXTRA của bản tin RIÊNG mà sản phẩm nhận (bản tin chuẩn giao thức đã biết).
    virtual bool crc_extra(std::uint32_t /*msgid*/, std::uint8_t& /*out*/) const { return false; }
    // Event miền -> bản tin. Gọi trên thread của bên publish: không chặn lâu, chỉ dùng tx.
    virtual void on_event(const msg::Event& /*e*/, Sender& /*tx*/) {}
    // Bản tin nhận được (không phải bản tin chuẩn đã xử lý) -> Event/Param. Thread của kênh.
    virtual void on_message(const Message& /*m*/, comm::Services& /*s*/, Sender& /*tx*/) {}
    // COMMAND_LONG chưa được giao thức xử lý. Trả MAV_RESULT_*.
    virtual std::uint8_t on_command(const CommandLong& /*c*/, comm::Services& /*s*/, Sender& /*tx*/) {
        return MAV_RESULT_UNSUPPORTED;
    }
};

class Mavlink2Protocol final : public comm::IProtocol, public msg::IEventListener, private Sender {
public:
    struct Config {
        std::uint8_t sysid        = 1;
        std::uint8_t compid       = 1;
        std::uint8_t mav_type     = MAV_TYPE_ONBOARD_CONTROLLER;
        std::uint32_t heartbeat_ms = 1000;
        std::uint32_t gcs_timeout_ms = 3000;   // không thấy heartbeat GCS quá lâu -> coi như mất kết nối
        std::uint8_t params_per_poll = 4;      // tốc độ trả danh sách tham số
    };

    explicit Mavlink2Protocol(IDialect& dialect) : Mavlink2Protocol(dialect, Config{}) {}
    Mavlink2Protocol(IDialect& dialect, const Config& cfg) : dialect_(dialect), cfg_(cfg), parser_(&lookup, this) {}

    comm::ProtocolId id() const override { return comm::proto::kMavlink; }
    const char* name() const override { return "mavlink"; }

    core::Status attach(comm::IByteStream& io, comm::Services& services) override;
    void detach() override;
    void on_rx(const std::uint8_t* data, std::size_t len) override;
    void poll(std::uint64_t now_ms) override;
    bool probe(const std::uint8_t* data, std::size_t len) const override;

    void on_event(const msg::Event& e) override;

    Sender& sender() { return *this; }
    bool gcs_connected() const { return gcs_connected_.load(); }
    std::uint32_t rx_messages() const { return rx_msgs_.load(); }
    std::uint32_t tx_messages() const { return tx_msgs_.load(); }
    std::uint32_t crc_errors() const { return parser_.crc_errors(); }

    // Thống nhất CRC_EXTRA cho bản tin chuẩn mà giao thức nhận.
    static bool standard_crc_extra(std::uint32_t msgid, std::uint8_t& out);

private:
    static bool lookup(std::uint32_t msgid, std::uint8_t& crc_extra, const void* self);
    bool send_raw(std::uint32_t msgid, std::uint8_t crc_extra, const std::uint8_t* payload, std::size_t len) override;
    void handle(const Message& m);
    void handle_command(const CommandLong& c, std::uint8_t from_sys, std::uint8_t from_comp);
    void send_param(std::size_t index);
    bool for_me(std::uint8_t sys, std::uint8_t comp) const {
        return (sys == 0 || sys == cfg_.sysid) && (comp == 0 || comp == cfg_.compid);
    }

    IDialect& dialect_;
    Config cfg_;
    Parser parser_;
    std::atomic<comm::IByteStream*> io_{nullptr};
    comm::Services* services_ = nullptr;
    std::atomic<std::uint8_t> seq_{0};
    std::atomic<std::uint32_t> rx_msgs_{0}, tx_msgs_{0};
    std::atomic<bool> gcs_connected_{false};
    std::uint64_t last_hb_tx_ = 0;
    std::uint64_t last_gcs_hb_ = 0;
    int param_stream_ = -1;   // >=0: đang gửi danh sách tham số từ chỉ số này
};

}  // namespace mavlink2
