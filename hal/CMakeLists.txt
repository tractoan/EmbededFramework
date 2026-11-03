# HAL: trừu tượng hóa phần cứng. Interface (runtime polymorphism) trong include/hal,
# hiện thực theo board trong ports/<board>/. HAL KHÔNG phụ thuộc OSAL hay lớp trên.
file(GLOB EP_HAL_PORT_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_HAL_PORT}/*.cpp)
add_library(ep_hal STATIC ${EP_HAL_PORT_SOURCES})
target_include_directories(ep_hal
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ports/${EP_HAL_PORT})
target_link_libraries(ep_hal PUBLIC ep_core PRIVATE ${EP_HAL_PORT_LIBS})
