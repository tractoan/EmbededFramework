# Target: phần khởi động của nền tảng đích được chọn bằng EP_TARGET.
# Interface chung: include/target/target.hpp; hiện thực: <EP_TARGET>/*.cpp|*.c
# Chỉ phụ thuộc trong platform (osal, hal) + SDK vendor – KHÔNG include app/.
file(GLOB EP_TARGET_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/${EP_TARGET}/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/${EP_TARGET}/*.c)
if(NOT EP_TARGET_SOURCES)
  message(FATAL_ERROR "No startup sources in platform/target/${EP_TARGET}/")
endif()
add_library(ep_target STATIC ${EP_TARGET_SOURCES})
target_include_directories(ep_target PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_target PUBLIC ep_hal ep_osal PRIVATE ${EP_TARGET_LIBS})
