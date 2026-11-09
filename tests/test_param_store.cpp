#include "mocks.hpp"
#include "services/param_store.hpp"
#include "test_framework.hpp"

namespace {
const svc::ParamDef kDefs[] = {
    {0x2001, "a.speed", 100, 0, 1000, "rpm"},
    {0x2002, "a.mode", 1, 0, 3, ""},
};
}

TEST(param_store_validation_and_events) {
    msg::EventBus bus;
    mock::Recorder rec;
    bus.subscribe(svc::evt::kParamChanged, &rec);
    svc::ParamStore ps;
    ps.init(nullptr, &bus);
    CHECK(core::is_ok(ps.register_params(kDefs, 2)));
    CHECK(ps.register_params(kDefs, 1) == core::Status::InvalidArg);   // trùng ID

    CHECK_EQ(ps.get_or(0x2001, -1), 100);
    CHECK(ps.set(0x2001, 5000) == core::Status::OutOfRange);
    CHECK(core::is_ok(ps.set(0x2001, 250)));
    CHECK(core::is_ok(ps.set(0x2001, 250)));   // không đổi -> không phát event
    CHECK_EQ(rec.events.size(), 1u);
    svc::ParamChanged pc{};
    CHECK(rec.events[0].get(pc) && pc.id == 0x2001 && pc.value == 250);
    CHECK(ps.find("a.mode") == &kDefs[1]);
    CHECK(ps.set(0x9999, 1) == core::Status::NotFound);
}

TEST(param_store_persistence_with_crc) {
    mock::Storage nv;
    {
        svc::ParamStore ps;
        ps.init(&nv, nullptr);
        ps.register_params(kDefs, 2);
        CHECK(ps.load() == core::Status::NotFound);   // flash trắng
        ps.set(0x2001, 777);
        ps.set(0x2002, 3);
        CHECK(core::is_ok(ps.save()));
    }
    {
        svc::ParamStore ps;   // "reboot"
        ps.init(&nv, nullptr);
        ps.register_params(kDefs, 2);
        CHECK(core::is_ok(ps.load()));
        CHECK_EQ(ps.get_or(0x2001, 0), 777);
        CHECK_EQ(ps.get_or(0x2002, 0), 3);
    }
    nv.mem[12] ^= 0x01;   // lỗi bit trong vùng dữ liệu
    {
        svc::ParamStore ps;
        ps.init(&nv, nullptr);
        ps.register_params(kDefs, 2);
        CHECK(ps.load() == core::Status::Corrupted);
        CHECK_EQ(ps.get_or(0x2001, 0), 100);   // giữ mặc định
    }
}
