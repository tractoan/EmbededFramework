#include "comm/link_manager.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "osal/time.hpp"
#include "services/logger.hpp"

namespace comm {

namespace {
constexpr const char* kTag = "link";
}

// ============================================================== LinkManager
core::Status LinkManager::init(Services& services) {
    services_ = &services;
    core::Status st = services.bus.subscribe(svc::evt::kParamChanged, this);
    if (!core::is_ok(st)) return st;
    return services.commands.register_command({"link", "list | set <channel> <off|auto|protocol>", this});
}

core::Status LinkManager::add_channel(const char* name, IByteStream* stream, std::initializer_list<IProtocol*> protocols,
                                      ProtocolId default_protocol, osal::Priority priority, std::size_t stack_bytes) {
    if (services_ == nullptr || name == nullptr || stream == nullptr) return core::Status::InvalidArg;
    if (count_ >= channels_.size() || protocols.size() > core::cfg::kCommMaxProtocols) return core::Status::NoMemory;
    if (find_channel(name) != nullptr) return core::Status::Busy;

    Channel& ch = channels_[count_];
    ch.owner  = this;
    ch.name   = name;
    ch.stream = stream;
    ch.tx.bind(stream);
    ch.priority    = priority;
    ch.stack_bytes = stack_bytes;
    ch.n_protocols = 0;
    for (IProtocol* p : protocols) {
        if (p == nullptr) return core::Status::InvalidArg;
        ch.protocols[ch.n_protocols++] = p;
    }
    if (default_protocol > proto::kAuto && ch.find(default_protocol) == nullptr) return core::Status::InvalidArg;

    std::snprintf(ch.param_name, sizeof(ch.param_name), "comm.%s", name);
    ch.def = svc::ParamDef{static_cast<svc::ParamId>(kParamBase + count_), ch.param_name, default_protocol, 0, 255,
                           "0=off 1=auto"};
    const core::Status st = services_->params.register_params(&ch.def, 1);
    if (!core::is_ok(st)) return st;
    ++count_;
    return core::Status::Ok;
}

core::Status LinkManager::start() {
    for (std::size_t i = 0; i < count_; ++i) {
        Channel& ch = channels_[i];
        ch.requested = services_->params.get_or(ch.def.id, ch.def.default_value);
        ch.running   = true;
        const core::Status st = ch.thread.start(osal::ThreadConfig{ch.name, ch.priority, ch.stack_bytes, nullptr},
                                                &osal::Thread::invoke<Channel, &Channel::run>, &ch);
        if (!core::is_ok(st)) return st;
    }
    return core::Status::Ok;
}

void LinkManager::stop() {
    for (std::size_t i = 0; i < count_; ++i) channels_[i].running = false;
    for (std::size_t i = 0; i < count_; ++i) channels_[i].thread.join();
}

void LinkManager::on_event(const msg::Event& e) {
    svc::ParamChanged pc{};
    if (e.id != svc::evt::kParamChanged || !e.get(pc)) return;
    if (pc.id >= kParamBase && pc.id < kParamBase + count_) {
        channels_[pc.id - kParamBase].requested = pc.value;   // thread của kênh sẽ tự chuyển
    }
}

LinkManager::Channel* LinkManager::find_channel(const char* name) {
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::strcmp(channels_[i].name, name) == 0) return &channels_[i];
    }
    return nullptr;
}

const char* LinkManager::channel_name(std::size_t ch) const { return ch < count_ ? channels_[ch].name : nullptr; }

const char* LinkManager::active_protocol(std::size_t ch) const {
    if (ch >= count_) return "none";
    const IProtocol* p = channels_[ch].active.load();
    return p ? p->name() : "none";
}

ProtocolId LinkManager::setting(std::size_t ch) const {
    return ch < count_ ? static_cast<ProtocolId>(channels_[ch].requested.load()) : proto::kOff;
}

int LinkManager::on_command(int argc, char** argv, svc::CommandOutput& out) {
    const char* sub = argc >= 2 ? argv[1] : "list";
    if (std::strcmp(sub, "list") == 0) {
        for (std::size_t i = 0; i < count_; ++i) {
            const Channel& ch = channels_[i];
            out.print("  %-8s setting=%-7s active=%-7s rx=%lu  available:", ch.name,
                      ch.label(static_cast<ProtocolId>(ch.requested.load())), active_protocol(i),
                      static_cast<unsigned long>(ch.rx_bytes.load()));
            for (std::size_t k = 0; k < ch.n_protocols; ++k) out.print(" %s", ch.protocols[k]->name());
            out.print("\r\n");
        }
        return 0;
    }
    if (std::strcmp(sub, "set") == 0 && argc >= 4) {
        Channel* ch = find_channel(argv[2]);
        if (ch == nullptr) {
            out.print("unknown channel '%s'\r\n", argv[2]);
            return -1;
        }
        int value = -1;
        if (std::strcmp(argv[3], "off") == 0) {
            value = proto::kOff;
        } else if (std::strcmp(argv[3], "auto") == 0) {
            value = proto::kAuto;
        } else {
            for (std::size_t k = 0; k < ch->n_protocols; ++k) {
                if (std::strcmp(ch->protocols[k]->name(), argv[3]) == 0) value = ch->protocols[k]->id();
            }
        }
        if (value < 0) {
            out.print("protocol '%s' not available on '%s'\r\n", argv[3], ch->name);
            return -1;
        }
        out.print("%s -> %s (use 'param save' to persist)\r\n", ch->name, argv[3]);
        return core::is_ok(services_->params.set(ch->def.id, value)) ? 0 : -1;
    }
    out.print("usage: link list | set <channel> <off|auto|protocol>\r\n");
    return -1;
}

// ================================================================== Channel
IProtocol* LinkManager::Channel::find(ProtocolId id) const {
    for (std::size_t k = 0; k < n_protocols; ++k) {
        if (protocols[k]->id() == id) return protocols[k];
    }
    return nullptr;
}

const char* LinkManager::Channel::label(ProtocolId id) const {
    if (id == proto::kOff) return "off";
    if (id == proto::kAuto) return "auto";
    const IProtocol* p = find(id);
    return p ? p->name() : "?";
}

void LinkManager::Channel::deactivate() {
    if (IProtocol* p = active.exchange(nullptr)) p->detach();
}

void LinkManager::Channel::apply(ProtocolId target) {
    IProtocol* next = nullptr;
    if (target > proto::kAuto) {
        next = find(target);
        if (next == nullptr) {
            // Giao thức không có trên kênh này: giữ nguyên, trả tham số về giá trị cũ.
            const ProtocolId keep = setting_ == kUnset ? static_cast<ProtocolId>(def.default_value) : setting_;
            LOGW(kTag, "%s: protocol #%u not available, keeping '%s'", name, static_cast<unsigned>(target), label(keep));
            requested = keep;
            owner->services_->params.set(def.id, keep);
            if (setting_ != kUnset) return;
            target = keep;
            next   = find(keep);
        }
    }

    deactivate();
    autodetect_ = target == proto::kAuto;
    sniff_len_  = 0;
    setting_    = target;

    if (next != nullptr) {
        const core::Status st = next->attach(tx, *owner->services_);
        if (!core::is_ok(st)) {
            LOGE(kTag, "%s: attach '%s' failed: %s", name, next->name(), core::to_string(st));
            return;
        }
        active = next;
    }
    LOGI(kTag, "%s: protocol -> %s", name, label(target));
}

void LinkManager::Channel::sniff(const std::uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
        if (sniff_len_ == sizeof(sniff_buf_)) {   // đầy: bỏ byte cũ nhất
            std::memmove(sniff_buf_, sniff_buf_ + 1, sizeof(sniff_buf_) - 1);
            --sniff_len_;
        }
        sniff_buf_[sniff_len_++] = data[i];
    }
    // Thử mọi vị trí bắt đầu (bỏ qua rác ở đầu) với từng giao thức theo thứ tự ưu tiên.
    for (std::size_t off = 0; off < sniff_len_; ++off) {
        for (std::size_t k = 0; k < n_protocols; ++k) {
            IProtocol* p = protocols[k];
            if (!p->probe(sniff_buf_ + off, sniff_len_ - off)) continue;
            if (!core::is_ok(p->attach(tx, *owner->services_))) return;
            active = p;
            LOGI(kTag, "%s: auto-detected %s", name, p->name());
            const std::size_t n = sniff_len_ - off;
            sniff_len_ = 0;
            p->on_rx(sniff_buf_ + off, n);   // chuyển luôn dữ liệu đã nhận cho giao thức
            return;
        }
    }
}

void LinkManager::Channel::run() {
    std::uint8_t buf[64];
    while (running) {
        const auto req = static_cast<ProtocolId>(requested.load());
        if (req != setting_) apply(req);

        const std::size_t n      = stream->read(buf, sizeof(buf), 20);
        const std::uint64_t now  = osal::now_ms();
        IProtocol* p             = active.load();

        if (n > 0) {
            rx_bytes.fetch_add(static_cast<std::uint32_t>(n));
            last_rx_ = now;
            if (p != nullptr) {
                p->on_rx(buf, n);
            } else if (autodetect_) {
                sniff(buf, n);
            }
        } else if (autodetect_) {
            if (p != nullptr && now - last_rx_ > kAutoIdleMs) {
                LOGI(kTag, "%s: idle, re-detecting protocol", name);
                deactivate();
            } else if (p == nullptr && sniff_len_ > 0 && now - last_rx_ > kSniffIdleMs) {
                sniff_len_ = 0;
            }
        }
        if ((p = active.load()) != nullptr) p->poll(now);
    }
    deactivate();
    setting_ = kUnset;
}

}  // namespace comm
