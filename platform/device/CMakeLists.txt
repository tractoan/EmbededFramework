# Device: driver thiết bị ngoài chip + interface theo loại thiết bị.
# Được dùng: HAL (bus), OSAL (delay, khóa bus dùng chung), utils (CRC...).
# KHÔNG được dùng services (EventBus, ParamStore, Logger): báo lỗi bằng core::Status,
# lớp trên quyết định ghi log / phát event. Không tự tạo thread.
add_library(ep_device STATIC
  src/tmp102.cpp
  src/digital_io.cpp    # DI/DO trên các chân hal::IGpio
  src/imu_format.cpp)   # giải mã khung IMU theo bảng (hàm thuần)
target_include_directories(ep_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ep_device PUBLIC ep_hal ep_osal)
