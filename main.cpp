// Entry point DUY NHẤT của firmware – giống nhau cho mọi nền tảng và mọi sản phẩm.
//   - Nền tảng nào: chọn bằng EP_TARGET  (khởi động riêng ở platform/target/<EP_TARGET>/)
//   - Sản phẩm nào: chọn bằng EP_PRODUCT (product::name / product::setup ở products/<EP_PRODUCT>/)
#include "app/product.hpp"
#include "app/system.hpp"
#include "osal/time.hpp"
#include "target/target.hpp"

namespace {

int app_main() {
    static app::System system(hal::board());
    if (!core::is_ok(system.init(product::name(), &product::setup))) return 1;

    while (!target::stop_requested() && !system.shutdown_requested()) osal::sleep_ms(50);
    system.shutdown();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    target::init(argc, argv);
    return target::run(&app_main);
}
