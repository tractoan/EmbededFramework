# Messaging: lõi giao tiếp của framework, KHÔNG phụ thuộc nền tảng – cùng một code trên mọi RTOS/board.
#   Event          : message kích thước cố định (payload inline + Buffer tùy chọn)
#   EventBus       : publish/subscribe theo EventId, quy ước chia dải ID theo lớp
#   Buffer + pool  : payload lớn đếm tham chiếu trên bộ nhớ tĩnh (BlockPool, PoolAllocator, ChainAllocator)
# Đứng trên platform (dùng osal::Mutex, CriticalSection, now_ms); platform không được include messaging.
add_library(ep_messaging STATIC
  src/event_bus.cpp
  src/buffer.cpp)
target_include_directories(ep_messaging PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_messaging PUBLIC ep_osal)
