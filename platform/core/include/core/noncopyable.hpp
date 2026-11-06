#pragma once

namespace core {

class NonCopyable {
protected:
    NonCopyable()  = default;
    ~NonCopyable() = default;

public:
    NonCopyable(const NonCopyable&)            = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

}  // namespace core
