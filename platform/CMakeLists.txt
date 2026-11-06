# Platform: tầng nền, mọi thứ phía trên đứng trên nó.
#   core/   kiểu nền tảng (Status, config, NonCopyable, macro compiler)
#   osal/   trừu tượng hóa hệ điều hành   (port chọn lúc build: posix, freertos...)
#   hal/    trừu tượng hóa ngoại vi TRÊN chip (port theo board: linux_sim, stm32f4...)
#   device/ driver thiết bị NGOÀI chip trên board (TMP102...), chỉ dùng bus HAL -> chạy mọi board
#   target/ khởi động của nền tảng đích (target::init/run/stop_requested), chọn qua EP_TARGET
#
# Phụ thuộc trong tầng: core <- osal, hal <- device, target. Không thư viện nào phụ thuộc services trở lên
# (main.cpp ở thư mục gốc mới là nơi nối platform với app).
add_subdirectory(core)
add_subdirectory(osal)
add_subdirectory(hal)
add_subdirectory(device)
add_subdirectory(target)
