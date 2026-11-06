add_library(ep_product STATIC product.cpp)
target_link_libraries(ep_product PUBLIC ep_business ep_proto_cli)
