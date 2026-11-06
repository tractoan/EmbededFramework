#pragma once
#include "app/module_manager.hpp"
#include "comm/link_manager.hpp"
#include "hal/board.hpp"

namespace app {

// Những gì một sản phẩm được phép "lắp ráp" khi khởi động.
struct ProductSetup {
    ModuleManager& modules;      // module nghiệp vụ
    comm::LinkManager& links;    // kênh truyền thông + giao thức cho phép trên từng kênh
    hal::IBoard& board;          // để lấy thêm cổng (vd. UartId::Aux)
    comm::IByteStream& console;  // cổng console đã sẵn sàng
};

}  // namespace app

// Mỗi sản phẩm (products/<name>/product.cpp) hiện thực 2 hàm này.
namespace product {
const char* name();
core::Status setup(app::ProductSetup& s);
}  // namespace product
