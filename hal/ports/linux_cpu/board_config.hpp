#pragma once
// Ánh xạ chỉ số LOGIC -> thiết bị Linux của bo mạch bring-up. Sửa bảng này theo sơ đồ bo mạch,
// hoặc ghi đè lúc chạy (không cần build lại) bằng file cấu hình / biến môi trường:
//   EP_BOARD_CONF=/etc/ep_board.conf   mỗi dòng "key=value", '#' là chú thích
//   key: serial<i>, di<i>, do<i>, hdlc<i>, nvram  (biến môi trường EP_SERIAL<i>, EP_DI<i>... ưu tiên hơn file)
// Giá trị rỗng hoặc "none" = không có trên bo mạch.
namespace hal::linux_cpu::config {

inline constexpr int kSerialCount = 12;
inline constexpr int kDigitalInputCount = 20;
inline constexpr int kDigitalOutputCount = 20;
inline constexpr int kHdlcCount = 2;

// serial<i>: "/dev/ttyXXX[:rs485|:rs485-echo]"
inline constexpr const char* kSerial[kSerialCount] = {
    "/dev/ttyS0", "/dev/ttyS1", "/dev/ttyS2", "/dev/ttyS3", "/dev/ttyS4", "/dev/ttyS5",
    "/dev/ttyS6", "/dev/ttyS7", "/dev/ttyS8", "/dev/ttyS9", "/dev/ttyS10", "/dev/ttyS11",
};

// di<i> / do<i>: "gpiochipN:line[:low]"
inline constexpr const char* kDigitalInput[kDigitalInputCount] = {
    "gpiochip0:0",  "gpiochip0:1",  "gpiochip0:2",  "gpiochip0:3",  "gpiochip0:4",
    "gpiochip0:5",  "gpiochip0:6",  "gpiochip0:7",  "gpiochip0:8",  "gpiochip0:9",
    "gpiochip0:10", "gpiochip0:11", "gpiochip0:12", "gpiochip0:13", "gpiochip0:14",
    "gpiochip0:15", "gpiochip0:16", "gpiochip0:17", "gpiochip0:18", "gpiochip0:19",
};
inline constexpr const char* kDigitalOutput[kDigitalOutputCount] = {
    "gpiochip1:0",  "gpiochip1:1",  "gpiochip1:2",  "gpiochip1:3",  "gpiochip1:4",
    "gpiochip1:5",  "gpiochip1:6",  "gpiochip1:7",  "gpiochip1:8",  "gpiochip1:9",
    "gpiochip1:10", "gpiochip1:11", "gpiochip1:12", "gpiochip1:13", "gpiochip1:14",
    "gpiochip1:15", "gpiochip1:16", "gpiochip1:17", "gpiochip1:18", "gpiochip1:19",
};

// hdlc<i>: "synclink:/dev/ttySLGx" | "nhdlc:/dev/ttyX" | "chardev:/dev/x" | "packet:hdlcX"
inline constexpr const char* kHdlc[kHdlcCount] = {"synclink:/dev/ttySLG0", "synclink:/dev/ttySLG1"};

inline constexpr const char* kNvram = "ep_nvram.bin";   // tương đối: thư mục làm việc của tiến trình

}  // namespace hal::linux_cpu::config
