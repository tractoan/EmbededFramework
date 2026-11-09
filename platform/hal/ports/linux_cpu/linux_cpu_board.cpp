// Board port: CPU nhúng chạy Linux (x86/ARM SBC, COM Express...) – phần cứng THẬT qua driver kernel.
// Ánh xạ tài nguyên ở board_config.hpp, ghi đè được lúc chạy (EP_BOARD_CONF / biến môi trường).
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

#include "board_config.hpp"
#include "linux/linux_hal.hpp"
#include "linux_peripherals.hpp"

namespace hal {

namespace {

namespace cfg = linux_cpu::config;

class Overrides {
public:
    Overrides() {
        const char* path = std::getenv("EP_BOARD_CONF");
        if (path == nullptr) return;
        FILE* f = std::fopen(path, "r");
        if (f == nullptr) {
            std::fprintf(stderr, "[HAL] cannot read EP_BOARD_CONF=%s\n", path);
            return;
        }
        char line[256];
        while (std::fgets(line, sizeof(line), f) != nullptr) {
            std::string s(line);
            const std::size_t hash = s.find('#');
            if (hash != std::string::npos) s.resize(hash);
            const std::size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            values_[trim(s.substr(0, eq))] = trim(s.substr(eq + 1));
        }
        std::fclose(f);
    }

    // key "serial3" -> env EP_SERIAL3 > file > mặc định
    std::string get(const char* key, int index, const char* def) const {
        char k[32];
        std::snprintf(k, sizeof(k), index >= 0 ? "%s%d" : "%s", key, index);
        std::string env = "EP_";
        for (const char* p = k; *p; ++p) env += static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
        if (const char* v = std::getenv(env.c_str())) return v;
        const auto it = values_.find(k);
        return it != values_.end() ? it->second : (def ? def : "");
    }

private:
    static std::string trim(const std::string& s) {
        const std::size_t b = s.find_first_not_of(" \t\r\n");
        const std::size_t e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }
    std::map<std::string, std::string> values_;
};

bool present(const std::string& spec) { return !spec.empty() && spec != "none"; }

class LinuxCpuBoard final : public IBoard {
public:
    LinuxCpuBoard() {
        const Overrides o;
        for (int i = 0; i < cfg::kSerialCount; ++i) {
            const std::string s = o.get("serial", i, cfg::kSerial[i]);
            if (present(s)) serial_[i].reset(new linux_cpu::LinuxSerial(s));
        }
        for (int i = 0; i < cfg::kDigitalInputCount; ++i) {
            const std::string s = o.get("di", i, cfg::kDigitalInput[i]);
            if (present(s)) di_[i].reset(new linux_cpu::LinuxGpio(s, false));
        }
        for (int i = 0; i < cfg::kDigitalOutputCount; ++i) {
            const std::string s = o.get("do", i, cfg::kDigitalOutput[i]);
            if (present(s)) do_[i].reset(new linux_cpu::LinuxGpio(s, true));
        }
        for (int i = 0; i < cfg::kHdlcCount; ++i) {
            const std::string s = o.get("hdlc", i, cfg::kHdlc[i]);
            if (present(s)) hdlc_[i].reset(new linux_cpu::LinuxHdlc(s));
        }
        nvram_path_ = o.get("nvram", -1, cfg::kNvram);
        storage_.reset(new host::FileStorage(nvram_path_.c_str(), 4096));
    }

    core::Status init() override {
        // Chân lỗi (không có chip/line) chỉ cảnh báo: bring-up phải báo được phần còn lại.
        for (auto& p : di_) {
            if (p && !core::is_ok(p->configure(PinMode::Input))) p.reset();
        }
        for (auto& p : do_) {
            if (p && !core::is_ok(p->configure(PinMode::Output))) p.reset();
        }
        return console_.open(UartConfig{});
    }

    const char* name() const override { return "linux-cpu"; }
    IGpio* gpio(GpioId) override { return nullptr; }   // LED/nút trạng thái: không dùng trên CPU
    IUart* uart(UartId id) override { return id == UartId::Console ? &console_ : nullptr; }
    II2c* i2c(I2cId) override { return nullptr; }
    INvStorage* storage() override { return storage_.get(); }

    std::size_t serial_count() const override { return cfg::kSerialCount; }
    IUart* serial(std::size_t i) override { return i < cfg::kSerialCount ? serial_[i].get() : nullptr; }
    std::size_t digital_input_count() const override { return cfg::kDigitalInputCount; }
    IGpio* digital_input(std::size_t i) override { return i < cfg::kDigitalInputCount ? di_[i].get() : nullptr; }
    std::size_t digital_output_count() const override { return cfg::kDigitalOutputCount; }
    IGpio* digital_output(std::size_t i) override { return i < cfg::kDigitalOutputCount ? do_[i].get() : nullptr; }
    std::size_t hdlc_count() const override { return cfg::kHdlcCount; }
    IHdlcPort* hdlc(std::size_t i) override { return i < cfg::kHdlcCount ? hdlc_[i].get() : nullptr; }

    [[noreturn]] void reset() override {
        std::fputs("[HAL] reset requested -> exit(0) (service manager restarts the process)\n", stdout);
        std::fflush(stdout);
        std::_Exit(0);
    }

private:
    host::StdioUart console_;
    std::unique_ptr<linux_cpu::LinuxSerial> serial_[cfg::kSerialCount];
    std::unique_ptr<linux_cpu::LinuxGpio> di_[cfg::kDigitalInputCount];
    std::unique_ptr<linux_cpu::LinuxGpio> do_[cfg::kDigitalOutputCount];
    std::unique_ptr<linux_cpu::LinuxHdlc> hdlc_[cfg::kHdlcCount];
    std::string nvram_path_;   // FileStorage giữ con trỏ -> chuỗi phải sống cùng board
    std::unique_ptr<host::FileStorage> storage_;
};

}  // namespace

IBoard& board() {
    static LinuxCpuBoard instance;
    return instance;
}

}  // namespace hal
