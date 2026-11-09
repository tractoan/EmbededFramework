#pragma once
#include <cstdint>

#include "comm/byte_stream.hpp"
#include "core/status.hpp"
#include "services/command_service.hpp"
#include "messaging/event_bus.hpp"
#include "services/param_store.hpp"

namespace comm {

// ID ổn định của giao thức – là GIÁ TRỊ tham số "comm.<kênh>" lưu trong Flash,
// nên không được đánh số lại khi thêm/bớt giao thức.
using ProtocolId = std::uint8_t;
namespace proto {
inline constexpr ProtocolId kOff       = 0;
inline constexpr ProtocolId kAuto      = 1;    // tự nhận dạng qua IProtocol::probe()
inline constexpr ProtocolId kCli       = 2;
inline constexpr ProtocolId kModbusRtu = 3;
inline constexpr ProtocolId kMavlink   = 4;    // dành sẵn
inline constexpr ProtocolId kUserBase  = 32;   // giao thức riêng của sản phẩm
}  // namespace proto

// Những dịch vụ một giao thức được phép dùng: nói chuyện với nghiệp vụ CHỈ qua
// EventBus (mô hình dữ liệu chuẩn), ParamStore và bảng lệnh.
struct Services {
    msg::EventBus& bus;
    svc::ParamStore& params;
    svc::CommandService& commands;
};

// Một giao thức = codec (byte <-> message) + phiên làm việc + ánh xạ message <-> Event miền.
// Mỗi instance gắn với TỐI ĐA một kênh tại một thời điểm (có trạng thái riêng).
//
// Luồng gọi:
//  - attach/detach/on_rx/poll: trên thread RX của kênh (tuần tự, không cần khóa nội bộ).
//  - io.write(): an toàn từ mọi thread (kênh đã tuần tự hóa việc ghi).
class IProtocol {
public:
    virtual ~IProtocol() = default;
    virtual ProtocolId id() const    = 0;
    virtual const char* name() const = 0;

    virtual core::Status attach(IByteStream& io, Services& services) = 0;
    virtual void detach()                                             = 0;

    virtual void on_rx(const std::uint8_t* data, std::size_t len) = 0;
    virtual void poll(std::uint64_t /*now_ms*/) {}   // timeout khung, heartbeat, retry...

    // Chế độ auto: dữ liệu đầu tiên trên kênh có phải của giao thức này không?
    // data bắt đầu tại vị trí ứng viên đầu khung. Phải nghiêm ngặt (vd. kiểm tra CRC).
    virtual bool probe(const std::uint8_t* /*data*/, std::size_t /*len*/) const { return false; }
};

}  // namespace comm
