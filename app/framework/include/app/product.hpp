#pragma once
#include "app/module_manager.hpp"

// Mỗi sản phẩm (products/<name>/product.cpp) hiện thực 2 hàm này:
// chọn module nghiệp vụ nào được lắp vào firmware và cấu hình của chúng.
namespace product {
const char* name();
core::Status register_modules(app::ModuleManager& modules);
}  // namespace product
