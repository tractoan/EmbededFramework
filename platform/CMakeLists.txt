# Platform: tầng nền, mọi thứ phía trên đứng trên nó.
#   core/   kiểu nền tảng (Status, config, NonCopyable, macro compiler)
#   osal/   trừu tượng hóa hệ điều hành   (port chọn lúc build: posix, freertos...)
#   hal/    trừu tượng hóa ngoại vi TRÊN chip (port theo board: linux_sim, stm32f4...)
#   device/ driver thiết bị NGOÀI chip trên board (TMP102...), chỉ dùng bus HAL -> chạy mọi board
#   target/ entry point main() cho từng nền tảng đích (chọn qua EP_TARGET, xem cmake/targets/)
#
# Phụ thuộc trong tầng: core <- osal, hal <- device. Các thư viện này không phụ thuộc services trở lên.
# Ngoại lệ duy nhất: target/*/main.cpp KHÔNG thuộc thư viện nào – nó được biên dịch thẳng vào
# executable firmware (EP_MAIN_SOURCE) và là nơi khởi động app::System, nên được include app/.
add_subdirectory(core)
add_subdirectory(osal)
add_subdirectory(hal)
add_subdirectory(device)
