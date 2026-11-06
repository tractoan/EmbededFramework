#pragma once
#include <array>
#include <atomic>
#include <initializer_list>

#include "comm/protocol.hpp"
#include "core/config.hpp"
#include "osal/thread.hpp"

namespace comm {

// Quản lý các kênh truyền thông. Mỗi kênh = 1 stream + danh sách giao thức được phép
// + 1 tham số "comm.<tên>" chọn giao thức đang chạy:
//     0 = off, 1 = auto (tự nhận dạng), hoặc ProtocolId của giao thức.
// Đổi tham số (CLI "link set", Modbus, MAVLink...) -> kênh chuyển giao thức NGAY lúc chạy,
// "param save" -> giữ lựa chọn sau khi khởi động lại.
class LinkManager final : public svc::IEventListener, public svc::ICommandHandler, private core::NonCopyable {
public:
    static constexpr svc::ParamId kParamBase    = 0x0200;   // comm.<kênh thứ i> = 0x0200 + i
    static constexpr std::uint32_t kAutoIdleMs  = 10000;    // auto: im lặng lâu -> nhận dạng lại
    static constexpr std::uint32_t kSniffIdleMs = 500;      // auto: xóa bộ đệm nhận dạng dở dang

    core::Status init(Services& services);   // đăng ký lệnh "link", theo dõi kParamChanged

    // Gọi trước ParamStore::load() (trong product::setup) để tham số kênh được nạp từ Flash.
    // priority/stack_bytes: thread RX của kênh (stack lấy từ vùng stack tĩnh khi start()).
    core::Status add_channel(const char* name, IByteStream* stream, std::initializer_list<IProtocol*> protocols,
                             ProtocolId default_protocol, osal::Priority priority = osal::Priority::Normal,
                             std::size_t stack_bytes = 4096);

    core::Status start();
    void stop();

    // Truy vấn trạng thái (thread-safe)
    std::size_t channel_count() const { return count_; }
    const char* channel_name(std::size_t ch) const;
    const char* active_protocol(std::size_t ch) const;   // "none" nếu chưa có
    ProtocolId setting(std::size_t ch) const;

    void on_event(const svc::Event& e) override;
    int on_command(int argc, char** argv, svc::CommandOutput& out) override;   // "link"

private:
    class Channel : core::NonCopyable {
    public:
        void run();
        IProtocol* find(ProtocolId id) const;
        const char* label(ProtocolId id) const;

        LinkManager* owner = nullptr;
        const char* name   = nullptr;
        IByteStream* stream = nullptr;
        SerializedStream tx;
        std::array<IProtocol*, core::cfg::kCommMaxProtocols> protocols{};
        std::size_t n_protocols = 0;
        svc::ParamDef def{};
        char param_name[24]{};
        osal::Priority priority{osal::Priority::Normal};
        std::size_t stack_bytes = 4096;

        std::atomic<std::int32_t> requested{proto::kOff};
        std::atomic<IProtocol*> active{nullptr};
        std::atomic<std::uint32_t> rx_bytes{0};
        osal::Thread thread;
        std::atomic<bool> running{false};

    private:
        static constexpr ProtocolId kUnset = 0xFF;
        void apply(ProtocolId target);
        void sniff(const std::uint8_t* data, std::size_t len);
        void deactivate();

        ProtocolId setting_ = kUnset;
        bool autodetect_    = false;
        std::uint8_t sniff_buf_[64]{};
        std::size_t sniff_len_ = 0;
        std::uint64_t last_rx_ = 0;
    };

    Channel* find_channel(const char* name);

    std::array<Channel, core::cfg::kCommMaxChannels> channels_;
    std::size_t count_  = 0;
    Services* services_ = nullptr;
};

}  // namespace comm
