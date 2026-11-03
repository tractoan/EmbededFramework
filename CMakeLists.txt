cmake_minimum_required(VERSION 3.16)
project(EmbeddedPlatform VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Lựa chọn build:
#   EP_PLATFORM : nền tảng đích (cmake/platforms/<name>.cmake) -> chọn port OSAL + HAL
#   EP_PRODUCT  : biến thể sản phẩm (products/<name>)            -> chọn nghiệp vụ
# ---------------------------------------------------------------------------
set(EP_PLATFORM "linux_sim"  CACHE STRING "Target platform (cmake/platforms/<name>.cmake)")
set(EP_PRODUCT  "thermo_pro" CACHE STRING "Product variant (products/<name>)")
option(EP_BUILD_TESTS "Build host unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(EP_PRODUCT_DIR ${PROJECT_SOURCE_DIR}/products/${EP_PRODUCT})
if(NOT EXISTS ${EP_PRODUCT_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "Unknown product '${EP_PRODUCT}'")
endif()

# Platform file phải định nghĩa: EP_OSAL_PORT, EP_HAL_PORT, EP_MAIN_SOURCE
# và có thể định nghĩa: EP_OSAL_PORT_LIBS, EP_HAL_PORT_LIBS, EP_PLATFORM_LIBS
include(${PROJECT_SOURCE_DIR}/cmake/platforms/${EP_PLATFORM}.cmake)
message(STATUS "EP: platform=${EP_PLATFORM} osal=${EP_OSAL_PORT} hal=${EP_HAL_PORT} product=${EP_PRODUCT}")

# Cờ biên dịch chung cho mọi lớp (phong cách embedded: không exception, không RTTI)
add_library(ep_options INTERFACE)
target_compile_options(ep_options INTERFACE
  -Wall -Wextra
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)

# Thứ tự = thứ tự phụ thuộc. Quy tắc phụ thuộc được ép bởi target_link_libraries:
#   core <- utils <- osal, hal <- services <- app(framework, modules) <- product <- firmware
add_subdirectory(core)
add_subdirectory(utils)
add_subdirectory(osal)
add_subdirectory(hal)
add_subdirectory(services)
add_subdirectory(app)
add_subdirectory(${EP_PRODUCT_DIR} product)

add_executable(firmware ${EP_MAIN_SOURCE})
target_link_libraries(firmware PRIVATE ep_product ${EP_PLATFORM_LIBS})

if(EP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
