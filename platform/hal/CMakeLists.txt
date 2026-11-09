# HAL: trừu tượng hóa phần cứng. Interface (runtime polymorphism) trong include/hal,
# hiện thực theo board trong ports/<board>/. HAL KHÔNG phụ thuộc OSAL hay lớp trên.
#   EP_HAL_PORT   : thư mục port của board (ports/<EP_HAL_PORT>/*.cpp)
#   EP_HAL_COMMON : (tùy chọn) danh sách mã dùng chung giữa nhiều board -> ports/common/<tên>/*.cpp
#                   vd. "posix;linux" (Linux), "posix;qnx" (QNX). mmio/ là header-only, luôn include được.
file(GLOB EP_HAL_PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_HAL_PORT}/*.cpp)
set(EP_HAL_COMMON_SOURCES "")
foreach(_common ${EP_HAL_COMMON})
  if(NOT IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ports/common/${_common})
    message(FATAL_ERROR "EP_HAL_COMMON: no ports/common/${_common}/")
  endif()
  file(GLOB _common_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ports/common/${_common}/*.cpp)
  list(APPEND EP_HAL_COMMON_SOURCES ${_common_sources})
endforeach()
add_library(ep_hal STATIC src/wait.cpp ${EP_HAL_PORT_SOURCES} ${EP_HAL_COMMON_SOURCES})
target_include_directories(ep_hal
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_HAL_PORT}
          ${CMAKE_CURRENT_SOURCE_DIR}/ports/common)
target_link_libraries(ep_hal PUBLIC ep_core ep_utils PRIVATE ${EP_HAL_PORT_LIBS})
