#include "services/event_bus.hpp"

#include "osal/time.hpp"

namespace svc {

core::Status EventBus::subscribe(EventId id, IEventListener* listener) {
    if (listener == nullptr) return core::Status::InvalidArg;
    osal::LockGuard g(mtx_);
    for (std::size_t i = 0; i < count_; ++i) {
        if (subs_[i].id == id && subs_[i].listener == listener) return core::Status::Ok;
    }
    if (count_ >= subs_.size()) return core::Status::NoMemory;
    subs_[count_++] = Subscription{id, listener};
    return core::Status::Ok;
}

core::Status EventBus::unsubscribe(EventId id, IEventListener* listener) {
    osal::LockGuard g(mtx_);
    for (std::size_t i = 0; i < count_; ++i) {
        if (subs_[i].id == id && subs_[i].listener == listener) {
            subs_[i] = subs_[--count_];
            return core::Status::Ok;
        }
    }
    return core::Status::NotFound;
}

void EventBus::publish(Event e) {
    e.timestamp_ms = static_cast<std::uint32_t>(osal::now_ms());

    std::array<IEventListener*, core::cfg::kMaxEventSubscribers> targets{};
    std::size_t n = 0;
    {
        osal::LockGuard g(mtx_);
        for (std::size_t i = 0; i < count_; ++i) {
            if (subs_[i].id == e.id || subs_[i].id == evt::kAny) targets[n++] = subs_[i].listener;
        }
    }
    // Gọi ngoài khóa: listener có thể publish tiếp mà không deadlock.
    for (std::size_t i = 0; i < n; ++i) targets[i]->on_event(e);
}

std::size_t EventBus::subscriber_count() const {
    osal::LockGuard g(mtx_);
    return count_;
}

}  // namespace svc
