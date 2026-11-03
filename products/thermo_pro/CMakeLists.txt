add_library(ep_product STATIC product.cpp)
target_include_directories(ep_product PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ep_product PUBLIC ep_modules)
