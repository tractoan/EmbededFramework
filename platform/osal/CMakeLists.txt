# OSAL: trừu tượng hóa hệ điều hành. Port được chọn lúc build (link-time polymorphism,
# không tốn chi phí virtual). Interface trong include/, hiện thực trong ports/<os>/.
# src/memory.cpp dùng chung mọi port: vùng stack tĩnh, giai đoạn khởi động, danh sách thread.
add_library(ep_osal STATIC
  src/memory.cpp
  ports/${EP_OSAL_PORT}/osal_${EP_OSAL_PORT}.cpp)
target_include_directories(ep_osal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_OSAL_PORT}/include)   # osal_port_types.hpp
target_link_libraries(ep_osal PUBLIC ep_core ep_utils ${EP_OSAL_PORT_LIBS})

# Bộ chặn heap (thay operator new/delete; target có thể bật thêm bọc malloc bằng
# EP_HEAP_GUARD_WRAP_MALLOC + -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc).
# OBJECT library: object được link trực tiếp vào firmware nên chắc chắn được dùng.
add_library(ep_heap_guard OBJECT src/heap_guard.cpp)
target_link_libraries(ep_heap_guard PUBLIC ep_osal)
target_compile_definitions(ep_heap_guard PRIVATE ${EP_HEAP_GUARD_DEFINES})
