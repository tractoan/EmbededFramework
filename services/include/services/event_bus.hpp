#pragma once
#include <array>

#include "core/status.hpp"
#include "osal/mutex.hpp"
#include "services/event.hpp"

namespace svc {

// Publish/Subscribe đồng bộ, không cấp phát động.
// publish() gọi on_event() của listener ngay trên thread của publisher, ngoài vùng khóa.
// => Listener PHẢI không chặn (thường chỉ đẩy event vào queue riêng – mô hình Active Object).
class EventBus : core::NonCopyable {
public:
    core::Status subscribe(EventId id, IEventListener* listener);
    core::Status unsubscribe(EventId id, IEventListener* listener);
    void publish(Event e);
    std::size_t subscriber_count() const;

private:
    struct Subscription {
        EventId id                = 0;
        IEventListener* listener  = nullptr;
    };
    std::array<Subscription, core::cfg::kMaxEventSubscribers> subs_{};
    std::size_t count_ = 0;
    mutable osal::Mutex mtx_;
};

}  // namespace svc
