#pragma once
// Topic<T>: kênh "giá trị mới nhất" cho dữ liệu tần số cao (mẫu cảm biến, trạng thái ước lượng, setpoint)
// – KHÔNG đi qua EventBus/queue (không copy vào hàng đợi của mọi thread, không bị đầy, không trễ).
//
//   msg::Topic<ImuSample> imu;                        // product sở hữu (static), truyền tham chiếu cho hai phía
//   imu.publish(sample, osal::now_ms());              // 1 bên ghi (vd. thread lấy mẫu)
//   ImuSample s; msg::TopicStamp st;
//   if (core::is_ok(imu.read(s, &st)) && st.sequence != last_seq) { last_seq = st.sequence; ... }
//
// Hiện thực: seqlock trên các word std::atomic (không data race theo mô hình bộ nhớ C++, sạch dưới TSan),
// không khóa, không cấp phát, ghi O(sizeof T).
//  - MỘT bên ghi tại một thời điểm (nhiều bên ghi -> tự khóa bên ngoài). Nhiều bên đọc.
//  - Đọc KHÔNG bao giờ quay vô hạn: trên RTOS đơn lõi, bên đọc priority cao có thể ngắt bên ghi giữa
//    chừng -> sau kMaxReadRetries lần thấy dữ liệu đang ghi dở, read() trả Busy; bên đọc dùng giá trị
//    trước đó và đọc lại ở chu kỳ sau.
//  - Không gọi publish() từ ISR nếu thread có thể đang publish cùng topic (vi phạm "một bên ghi").
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "core/status.hpp"

namespace msg {

struct TopicStamp {
    std::uint32_t sequence = 0;       // đếm số lần publish (0 = chưa có dữ liệu); so sánh != để biết có mẫu mới
                                      // (quay vòng sau 2^31 lần, không bao giờ quay về 0)
    std::uint64_t timestamp_ms = 0;   // thời điểm do bên ghi cung cấp
};

template <typename T>
class Topic {
    static_assert(std::is_trivially_copyable<T>::value, "Topic payload must be trivially copyable");

public:
    static constexpr int kMaxReadRetries = 8;

    void publish(const T& value, std::uint64_t timestamp_ms) {
        std::uint32_t buf[kWords] = {};
        buf[0] = static_cast<std::uint32_t>(timestamp_ms);
        buf[1] = static_cast<std::uint32_t>(timestamp_ms >> 32);
        std::memcpy(&buf[kHeaderWords], &value, sizeof(T));

        const std::uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);          // lẻ: đang ghi
        std::atomic_thread_fence(std::memory_order_release);
        for (std::size_t i = 0; i < kWords; ++i) words_[i].store(buf[i], std::memory_order_relaxed);
        std::uint32_t done = s + 2;                             // chẵn: xong
        if (done == 0) done = 2;                                // quay vòng (~24 ngày ở 1 kHz): 0 dành cho "chưa có"
        seq_.store(done, std::memory_order_release);
    }

    // Ok: out = bản chụp nhất quán; NotFound: chưa từng publish; Busy: bên ghi đang ghi dở quá lâu.
    core::Status read(T& out, TopicStamp* stamp = nullptr) const {
        for (int attempt = 0; attempt < kMaxReadRetries; ++attempt) {
            const std::uint32_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 == 0) return core::Status::NotFound;
            if ((s1 & 1u) != 0) continue;                         // đang ghi
            std::uint32_t buf[kWords];
            for (std::size_t i = 0; i < kWords; ++i) buf[i] = words_[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) != s1) continue;   // bị ghi đè trong lúc đọc
            std::memcpy(&out, &buf[kHeaderWords], sizeof(T));
            if (stamp != nullptr) {
                stamp->sequence     = s1 / 2u;
                stamp->timestamp_ms = static_cast<std::uint64_t>(buf[0]) | (static_cast<std::uint64_t>(buf[1]) << 32);
            }
            return core::Status::Ok;
        }
        return core::Status::Busy;
    }

    // Số lần publish đến nay (đọc nhanh, không chép dữ liệu) – dùng để kiểm tra "có mẫu mới không".
    std::uint32_t sequence() const { return seq_.load(std::memory_order_acquire) / 2u; }

private:
    static constexpr std::size_t kHeaderWords = 2;   // timestamp 64 bit
    static constexpr std::size_t kWords       = kHeaderWords + (sizeof(T) + 3u) / 4u;

    std::atomic<std::uint32_t> seq_{0};
    std::array<std::atomic<std::uint32_t>, kWords> words_{};
};

}  // namespace msg
