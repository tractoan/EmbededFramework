#pragma once
#include <cstddef>

#include "core/status.hpp"

namespace hal {

// Bộ nhớ không bay hơi (Flash nội / EEPROM / file mô phỏng).
// Ngữ nghĩa giống Flash: phải erase() trước khi write() lại cùng vùng.
class INvStorage {
public:
    virtual ~INvStorage() = default;
    virtual std::size_t size() const                                               = 0;
    virtual core::Status read(std::size_t offset, void* data, std::size_t len)        = 0;
    virtual core::Status write(std::size_t offset, const void* data, std::size_t len) = 0;
    virtual core::Status erase()                                                   = 0;
};

}  // namespace hal
