#pragma once
#include "messaging/event.hpp"

namespace app {

// State machine dạng "state = member function" (kiểu QP/UML rút gọn):
//   - Entry/Exit action tự động khi transition().
//   - Không cấp phát động, không bảng chuyển trạng thái phức tạp, dễ đọc dễ mở rộng.
template <typename Owner>
class StateMachine {
public:
    enum class Signal : std::uint8_t { Entry, Exit, Event };
    using State = void (Owner::*)(Signal sig, const msg::Event* e);

    StateMachine(Owner& owner, State initial) : owner_(owner), state_(initial) {}

    void start() { (owner_.*state_)(Signal::Entry, nullptr); }
    void dispatch(const msg::Event& e) { (owner_.*state_)(Signal::Event, &e); }

    void transition(State next) {
        if (next == state_) return;
        (owner_.*state_)(Signal::Exit, nullptr);
        state_ = next;
        (owner_.*state_)(Signal::Entry, nullptr);
    }

    bool is_in(State s) const { return state_ == s; }

private:
    Owner& owner_;
    State state_;
};

}  // namespace app
