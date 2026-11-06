#include "services/param_store.hpp"

#include <cstring>

#include "utils/crc.hpp"

namespace svc {

namespace {
constexpr std::uint32_t kMagic   = 0x45505052u;   // "EPPR"
constexpr std::uint16_t kVersion = 1;

struct Header {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t count;
};
struct Record {
    std::uint16_t id;
    std::uint16_t reserved;
    std::int32_t value;
};
static_assert(sizeof(Header) == 8 && sizeof(Record) == 8, "NV layout must be packed");
}  // namespace

void ParamStore::init(hal::INvStorage* storage, msg::EventBus* bus) {
    storage_ = storage;
    bus_     = bus;
}

int ParamStore::index_of(ParamId id) const {
    for (std::size_t i = 0; i < count_; ++i) {
        if (entries_[i].def->id == id) return static_cast<int>(i);
    }
    return -1;
}

core::Status ParamStore::register_params(const ParamDef* defs, std::size_t count) {
    if (defs == nullptr) return core::Status::InvalidArg;
    osal::LockGuard g(mtx_);
    if (count_ + count > entries_.size()) return core::Status::NoMemory;
    for (std::size_t i = 0; i < count; ++i) {
        const ParamDef& d = defs[i];
        if (index_of(d.id) >= 0 || d.min > d.max || d.default_value < d.min || d.default_value > d.max) {
            return core::Status::InvalidArg;
        }
    }
    for (std::size_t i = 0; i < count; ++i) entries_[count_++] = Entry{&defs[i], defs[i].default_value};
    return core::Status::Ok;
}

core::Status ParamStore::get(ParamId id, std::int32_t& out) const {
    osal::LockGuard g(mtx_);
    const int i = index_of(id);
    if (i < 0) return core::Status::NotFound;
    out = entries_[static_cast<std::size_t>(i)].value;
    return core::Status::Ok;
}

std::int32_t ParamStore::get_or(ParamId id, std::int32_t fallback) const {
    std::int32_t v = fallback;
    return core::is_ok(get(id, v)) ? v : fallback;
}

core::Status ParamStore::set(ParamId id, std::int32_t value) {
    {
        osal::LockGuard g(mtx_);
        const int i = index_of(id);
        if (i < 0) return core::Status::NotFound;
        Entry& e = entries_[static_cast<std::size_t>(i)];
        if (value < e.def->min || value > e.def->max) return core::Status::OutOfRange;
        if (e.value == value) return core::Status::Ok;
        e.value = value;
    }
    if (bus_ != nullptr) bus_->publish(msg::Event::make(evt::kParamChanged, ParamChanged{id, value}));
    return core::Status::Ok;
}

core::Status ParamStore::reset_defaults() {
    for (std::size_t i = 0; i < count(); ++i) {
        const ParamDef* d = at(i);
        if (d != nullptr) set(d->id, d->default_value);
    }
    return core::Status::Ok;
}

const ParamDef* ParamStore::find(const char* name) const {
    osal::LockGuard g(mtx_);
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::strcmp(entries_[i].def->name, name) == 0) return entries_[i].def;
    }
    return nullptr;
}

std::size_t ParamStore::count() const {
    osal::LockGuard g(mtx_);
    return count_;
}

const ParamDef* ParamStore::at(std::size_t index, std::int32_t* value) const {
    osal::LockGuard g(mtx_);
    if (index >= count_) return nullptr;
    if (value != nullptr) *value = entries_[index].value;
    return entries_[index].def;
}

core::Status ParamStore::save() {
    if (storage_ == nullptr) return core::Status::NotSupported;

    // Chụp snapshot trong khóa, ghi Flash (chậm) ngoài khóa.
    std::array<Record, core::cfg::kMaxParams> recs{};
    Header hdr{kMagic, kVersion, 0};
    {
        osal::LockGuard g(mtx_);
        for (std::size_t i = 0; i < count_; ++i) recs[i] = Record{entries_[i].def->id, 0, entries_[i].value};
        hdr.count = static_cast<std::uint16_t>(count_);
    }
    const std::size_t body = hdr.count * sizeof(Record);
    if (sizeof(Header) + body + sizeof(std::uint32_t) > storage_->size()) return core::Status::NoMemory;

    const std::uint32_t crc = utils::crc32(recs.data(), body, utils::crc32(&hdr, sizeof(hdr)));
    core::Status st = storage_->erase();
    if (core::is_ok(st)) st = storage_->write(0, &hdr, sizeof(hdr));
    if (core::is_ok(st)) st = storage_->write(sizeof(hdr), recs.data(), body);
    if (core::is_ok(st)) st = storage_->write(sizeof(hdr) + body, &crc, sizeof(crc));
    return st;
}

core::Status ParamStore::load() {
    if (storage_ == nullptr) return core::Status::NotSupported;

    Header hdr{};
    core::Status st = storage_->read(0, &hdr, sizeof(hdr));
    if (!core::is_ok(st)) return st;
    if (hdr.magic != kMagic || hdr.version != kVersion) return core::Status::NotFound;
    if (hdr.count > core::cfg::kMaxParams) return core::Status::Corrupted;

    std::array<Record, core::cfg::kMaxParams> recs{};
    const std::size_t body = hdr.count * sizeof(Record);
    std::uint32_t stored_crc = 0;
    st = storage_->read(sizeof(hdr), recs.data(), body);
    if (core::is_ok(st)) st = storage_->read(sizeof(hdr) + body, &stored_crc, sizeof(stored_crc));
    if (!core::is_ok(st)) return st;
    if (utils::crc32(recs.data(), body, utils::crc32(&hdr, sizeof(hdr))) != stored_crc) return core::Status::Corrupted;

    osal::LockGuard g(mtx_);
    for (std::size_t r = 0; r < hdr.count; ++r) {
        const int i = index_of(recs[r].id);
        if (i < 0) continue;   // tham số của firmware cũ đã bị bỏ
        Entry& e = entries_[static_cast<std::size_t>(i)];
        if (recs[r].value >= e.def->min && recs[r].value <= e.def->max) e.value = recs[r].value;
    }
    return core::Status::Ok;
}

}  // namespace svc
