#pragma once
#include <array>
#include <atomic>

#include "modbus/modbus_map.hpp"

namespace modbus {

// Modbus RTU slave.
// Hỗ trợ: FC03 Read Holding, FC04 Read Input, FC05 Write Single Coil,
//         FC06 Write Single Register, FC16 Write Multiple Registers.
// Đóng khung theo độ dài suy ra từ function code (không phụ thuộc đo khoảng lặng 3.5 ký tự),
// nên chạy được trên UART, RS485 lẫn RTU-over-UDP/TCP.
class ModbusRtuProtocol final : public comm::IProtocol, public svc::IEventListener {
public:
    static constexpr std::size_t kMaxInputs        = 32;
    static constexpr std::uint32_t kFrameTimeoutMs = 50;   // khung dở dang quá lâu -> bỏ

    explicit ModbusRtuProtocol(const Map& map) : map_(map) {}

    comm::ProtocolId id() const override { return comm::proto::kModbusRtu; }
    const char* name() const override { return "modbus"; }

    core::Status attach(comm::IByteStream& io, comm::Services& services) override;
    void detach() override;
    void on_rx(const std::uint8_t* data, std::size_t len) override;
    void poll(std::uint64_t now_ms) override;
    bool probe(const std::uint8_t* data, std::size_t len) const override;

    void on_event(const svc::Event& e) override;   // cập nhật bộ đệm input register

    std::uint32_t frames_ok() const { return frames_ok_.load(); }
    std::uint32_t crc_errors() const { return crc_errors_.load(); }

private:
    static constexpr std::size_t kUnsupported = static_cast<std::size_t>(-1);
    static std::size_t expected_length(const std::uint8_t* f, std::size_t len);
    bool addressed_to_me(std::uint8_t addr) const { return addr == map_.unit_id || addr == 0; }

    void process();
    std::size_t find_frame(std::size_t from) const;   // vị trí khung hợp lệ đầu tiên >= from, 0 nếu không có
    void consume(std::size_t n);
    void handle_frame(const std::uint8_t* f, std::size_t len);
    void reply(std::uint8_t* frame, std::size_t len_without_crc);
    void exception(std::uint8_t fc, std::uint8_t code);

    bool read_input(std::uint16_t addr, std::uint16_t& out) const;
    bool read_holding(std::uint16_t addr, std::uint16_t& out) const;
    const HoldingRegister* find_holding(std::uint16_t addr) const;
    std::uint8_t write_holding(std::uint16_t addr, std::uint16_t raw);   // 0 = OK, khác = mã exception

    const Map& map_;
    comm::IByteStream* io_     = nullptr;
    comm::Services* services_  = nullptr;
    std::uint8_t rx_[256]{};
    std::size_t rx_len_        = 0;
    std::uint64_t last_rx_ms_  = 0;
    bool broadcast_            = false;
    std::array<std::atomic<std::int32_t>, kMaxInputs> inputs_{};
    std::atomic<std::uint32_t> frames_ok_{0};
    std::atomic<std::uint32_t> crc_errors_{0};
};

}  // namespace modbus
