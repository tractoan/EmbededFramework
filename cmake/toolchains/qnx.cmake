# Toolchain QNX SDP 7.1 / 8.0 (qcc/q++). Trước khi chạy cmake: source <sdp>/qnxsdp-env.sh
# (đặt QNX_HOST, QNX_TARGET).   Kiến trúc: -DEP_QNX_ARCH=aarch64le (mặc định) | x86_64
if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
  message(FATAL_ERROR "QNX_HOST/QNX_TARGET not set: source qnxsdp-env.sh from your QNX SDP first")
endif()
set(EP_QNX_ARCH "aarch64le" CACHE STRING "QNX target architecture (aarch64le, x86_64)")

set(CMAKE_SYSTEM_NAME QNX)
set(CMAKE_SYSTEM_PROCESSOR ${EP_QNX_ARCH})
set(CMAKE_SYSROOT $ENV{QNX_TARGET})

set(CMAKE_C_COMPILER   $ENV{QNX_HOST}/usr/bin/qcc)
set(CMAKE_CXX_COMPILER $ENV{QNX_HOST}/usr/bin/q++)
set(CMAKE_C_COMPILER_TARGET   gcc_nto${EP_QNX_ARCH})
set(CMAKE_CXX_COMPILER_TARGET gcc_nto${EP_QNX_ARCH}_cxx)

set(CMAKE_FIND_ROOT_PATH $ENV{QNX_TARGET} $ENV{QNX_TARGET}/${EP_QNX_ARCH})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
