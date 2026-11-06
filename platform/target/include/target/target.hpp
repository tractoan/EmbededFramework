#pragma once
// Phần khởi động phụ thuộc nền tảng đích. main.cpp ở thư mục gốc chỉ gọi 3 hàm này nên
// GIỐNG NHAU cho mọi target; mỗi target (platform/target/<EP_TARGET>/) tự hiện thực chúng.

namespace target {

// Hàm ứng dụng chạy sau khi nền tảng sẵn sàng. Trả về mã thoát (0 = bình thường).
using AppMain = int (*)();

// Gọi đầu tiên trong main(): khởi tạo tối thiểu của nền tảng
// (Linux: đọc tham số dòng lệnh, bắt Ctrl+C; MCU: HAL vendor, clock, ngoại vi CubeMX).
void init(int argc, char** argv);

// Chạy app_main trong ngữ cảnh đúng của nền tảng:
//   Linux    : gọi trực tiếp trên main thread, trả về mã thoát của nó.
//   FreeRTOS : tạo task khởi động, start scheduler – không bao giờ trả về;
//              app_main trả về mã khác 0 -> reset board.
int run(AppMain app_main);

// Ứng dụng có nên dừng không (Ctrl+C, hết thời gian --run-for...). Trên MCU luôn false.
bool stop_requested();

}  // namespace target
