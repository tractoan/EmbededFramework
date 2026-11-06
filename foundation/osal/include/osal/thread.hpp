#pragma once
#include "core/noncopyable.hpp"
#include "core/status.hpp"
#include "osal/types.hpp"
#include "osal_port_types.hpp"

namespace osal {

struct ThreadConfig {
    const char* name     = "ep";
    Priority priority    = Priority::Normal;
    std::size_t stack_bytes = 2048;
    void* stack_mem      = nullptr;   // != nullptr -> cấp phát tĩnh (khuyến nghị trên MCU)
};

class Thread : core::NonCopyable {
public:
    using Entry = void (*)(void* arg);

    Thread() = default;
    ~Thread();

    core::Status start(const ThreadConfig& cfg, Entry entry, void* arg);
    void join();

    // Tiện ích: chạy method T::Method() làm thân thread.
    //   thread.start(cfg, &osal::Thread::invoke<Foo, &Foo::run>, this);
    template <typename T, void (T::*Method)()>
    static void invoke(void* self) {
        (static_cast<T*>(self)->*Method)();
    }

private:
    static void trampoline(void* self);

    port::ThreadHandle h_{};
    Entry entry_  = nullptr;
    void* arg_    = nullptr;
    bool started_ = false;
};

}  // namespace osal
