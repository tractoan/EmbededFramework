cmake_minimum_required(VERSION 3.16)
project(EmbeddedPlatform VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Lựa chọn build:
#   EP_TARGET   : nền tảng đích (cmake/targets/<name>.cmake) -> chọn port OSAL + HAL
#   EP_PRODUCT  : biến thể sản phẩm (products/<name>)            -> chọn nghiệp vụ
# ---------------------------------------------------------------------------
if(DEFINED EP_PLATFORM)   # tương thích ngược: tên cũ của EP_TARGET
  message(WARNING "EP_PLATFORM đã đổi tên thành EP_TARGET – hãy dùng -DEP_TARGET=${EP_PLATFORM}")
  set(EP_TARGET ${EP_PLATFORM} CACHE STRING "Build target (cmake/targets/<name>.cmake)" FORCE)
  unset(EP_PLATFORM CACHE)
endif()
set(EP_TARGET "linux_sim"  CACHE STRING "Build target (cmake/targets/<name>.cmake)")
set(EP_PRODUCT  "thermo_pro" CACHE STRING "Product variant (products/<name>)")
option(EP_BUILD_TESTS "Build host unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EP_PRODUCT_DIR ${PROJECT_SOURCE_DIR}/products/${EP_PRODUCT})
if(NOT EXISTS ${EP_PRODUCT_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "Unknown product '${EP_PRODUCT}'")
endif()

# Target file phải định nghĩa: EP_OSAL_PORT, EP_HAL_PORT
# và có thể định nghĩa: EP_OSAL_PORT_LIBS, EP_HAL_PORT_LIBS, EP_TARGET_LIBS.
# Mã khởi động của target (target::init/run) nằm ở platform/target/<EP_TARGET>/.
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/cmake/targets/${EP_TARGET}.cmake)
  message(FATAL_ERROR "Unknown target '${EP_TARGET}' (see cmake/targets/)")
endif()
include(${PROJECT_SOURCE_DIR}/cmake/targets/${EP_TARGET}.cmake)
message(STATUS "EP: target=${EP_TARGET} osal=${EP_OSAL_PORT} hal=${EP_HAL_PORT} product=${EP_PRODUCT}")

# Cờ biên dịch chung cho mọi lớp (phong cách embedded: không exception, không RTTI)
add_library(ep_options INTERFACE)
target_compile_options(ep_options INTERFACE
  -Wall -Wextra
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

# Thứ tự = thứ tự phụ thuộc. Quy tắc phụ thuộc được ép bởi target_link_libraries:
#   utils (độc lập)
#   platform(core <- osal, hal <- device, target) <- services <- communication <- app(runtime, business) <- product <- firmware
add_subdirectory(utils)
add_subdirectory(platform)
add_subdirectory(services)
add_subdirectory(communication)
add_subdirectory(app)
add_subdirectory(${EP_PRODUCT_DIR} product)

# main.cpp ở thư mục gốc: một entry point duy nhất cho mọi target & sản phẩm.
add_executable(firmware main.cpp)
# ep_heap_guard: phát hiện mọi cấp phát heap sau khi khởi động xong (osal/memory.hpp).
target_link_libraries(firmware PRIVATE ep_heap_guard ep_product ep_target ${EP_TARGET_LIBS})

if(EP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
