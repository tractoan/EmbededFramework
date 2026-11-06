# OSAL: trừu tượng hóa hệ điều hành. Port được chọn lúc build (link-time polymorphism,
# không tốn chi phí virtual). Interface trong include/, hiện thực trong ports/<os>/.
add_library(ep_osal STATIC ports/${EP_OSAL_PORT}/osal_${EP_OSAL_PORT}.cpp)
target_include_directories(ep_osal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_OSAL_PORT}/include)   # osal_port_types.hpp
target_link_libraries(ep_osal PUBLIC ep_core ep_utils ${EP_OSAL_PORT_LIBS})
