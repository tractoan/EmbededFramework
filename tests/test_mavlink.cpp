// MAVLink v1: codec (khung, CRC, tái đồng bộ) và giao thức (heartbeat, param, command, telemetry).
#include <cstring>
#include <vector>

#include "mavlink/mavlink_protocol.hpp"
#include "mocks.hpp"
#include "test_framework.hpp"

namespace {
using namespace mavlink;

std::vector<std::uint8_t> frame(std::uint8_t id, const std::vector<std::uint8_t>& payload, std::uint8_t sysid = 255,
                                std::uint8_t compid = 190, std::uint8_t seq = 0) {
    std::uint8_t out[kMaxFrame];
    const std::size_t n = encode(out, seq, sysid, compid, id, payload.data(), payload.size());
    return std::vector<std::uint8_t>(out, out + n);
}

struct Decoded {
    std::uint8_t sysid, compid, msgid;
    std::vector<std::uint8_t> payload;
};
std::vector<Decoded> decode_all(const std::vector<std::uint8_t>& bytes) {
    std::vector<Decoded> out;
    Parser p;
    p.feed(bytes.data(), bytes.size(),
           [](void* ctx, const Frame& f) {
               static_cast<std::vector<Decoded>*>(ctx)->push_back(
                   Decoded{f.sysid, f.compid, f.msgid, std::vector<std::uint8_t>(f.payload, f.payload + f.len)});
           },
           &out);
    return out;
}
std::vector<Decoded> only(const std::vector<Decoded>& v, std::uint8_t id) {
    std::vector<Decoded> r;
    for (const auto& d : v) {
        if (d.msgid == id) r.push_back(d);
    }
    return r;
}
std::vector<std::uint8_t> param_id(const char* s) {
    std::vector<std::uint8_t> v(16, 0);
    for (std::size_t i = 0; s[i] && i < 16; ++i) v[i] = static_cast<std::uint8_t>(s[i]);
    return v;
}
std::vector<std::uint8_t> f32(float f) {
    std::vector<std::uint8_t> v(4);
    put_f32(v.data(), f);
    return v;
}
std::vector<std::uint8_t> cat(std::initializer_list<std::vector<std::uint8_t>> parts) {
    std::vector<std::uint8_t> r;
    for (const auto& p : parts) r.insert(r.end(), p.begin(), p.end());
    return r;
}

// ---- bản đồ thử nghiệm
constexpr msg::EventId kTempEvt = 0x3400, kStateEvt = 0x3401, kAckEvt = 0x3402;
bool temp_value(const msg::Event& e, float& v) {
    std::int32_t d = 0;
    if (!e.get(d)) return false;
    v = d / 10.0f;
    return true;
}
bool state_text(const msg::Event& e, std::uint8_t& sev, char* text, std::size_t n) {
    std::uint8_t s = 0;
    if (!e.get(s)) return false;
    sev = s ? mav::kSeverityCritical : mav::kSeverityInfo;
    std::snprintf(text, n, "state %u", static_cast<unsigned>(s));
    return true;
}
bool state_mav(const msg::Event& e, std::uint8_t& out) {
    std::uint8_t s = 0;
    if (!e.get(s)) return false;
    out = s ? mav::kStateCritical : mav::kStateActive;
    return true;
}
std::uint8_t ack_cmd(comm::Services& s, const float (&p)[7]) {
    if (p[0] != 1.0f) return mav::kResultDenied;
    s.bus.publish(msg::Event::make(kAckEvt));
    return mav::kResultAccepted;
}
const Telemetry kTm[]   = {{kTempEvt, "temp", &temp_value}};
const StatusText kSt[]  = {{kStateEvt, &state_text}};
const SystemState kSs[] = {{kStateEvt, &state_mav}};
const Command kCmd[]    = {{mav::kCmdUser1, &ack_cmd}};
const Map kMap{7, 191, mav::kTypeOnboardController, 1000, kTm, 1, kSt, 1, kSs, 1, kCmd, 1};

const svc::ParamDef kDefs[] = {
    {0x3501, "alarm.high", 750, -400, 1500, ""},
    {0x3502, "temp.period_ms", 500, 100, 60000, ""},
    {0x3503, "a_long_name_16ch", 1, 0, 9, ""},   // đúng 16 ký tự: không có '\0' trong khung
};

struct Fixture {
    msg::EventBus bus;
    svc::ParamStore params;
    svc::CommandService commands;
    comm::Services services{bus, params, commands};
    mock::ScriptedStream io;
    mock::Recorder rec;
    MavlinkV1Protocol mav{kMap};
    Fixture() {
        params.init(nullptr, &bus);
        params.register_params(kDefs, 3);
        bus.subscribe(kAckEvt, &rec);
        mav.attach(io, services);
    }
    std::vector<Decoded> exchange(const std::vector<std::uint8_t>& in) {
        io.clear_tx();
        mav.on_rx(in.data(), in.size());
        return decode_all(io.tx());
    }
};
}  // namespace

TEST(mavlink_codec_roundtrip_and_layout) {
    const std::vector<std::uint8_t> hb = {0, 0, 0, 0, 18, 8, 0, 4, 3};
    const auto f = frame(msgid::kHeartbeat, hb, 1, 191, 42);
    CHECK_EQ(f.size(), 6u + 9u + 2u);
    CHECK(f[0] == 0xFE && f[1] == 9 && f[2] == 42 && f[3] == 1 && f[4] == 191 && f[5] == 0);
    CHECK(is_valid_frame(f.data(), f.size()));
    auto bad = f;
    bad[8] ^= 0x01;   // 1 bit trong payload
    CHECK(!is_valid_frame(bad.data(), bad.size()));

    std::uint8_t out[kMaxFrame];
    CHECK_EQ(encode(out, 0, 1, 1, 99, hb.data(), hb.size()), 0u);                       // msgid không có trong bảng
    CHECK_EQ(encode(out, 0, 1, 1, msgid::kHeartbeat, hb.data(), 8), 0u);                 // độ dài sai
    CHECK(find_message(msgid::kParamValue)->length == 25 && find_message(msgid::kCommandLong)->crc_extra == 152);

    const auto d = decode_all(f);
    CHECK(d.size() == 1 && d[0].msgid == 0 && d[0].payload == hb);
}

TEST(mavlink_parser_resyncs_through_garbage) {
    const auto a = frame(msgid::kParamRequestList, {7, 0});
    const auto b = frame(msgid::kCommandAck, {1, 2, 3});
    auto corrupt = frame(msgid::kParamRequestList, {7, 0});
    corrupt.back() ^= 0xFF;
    std::vector<std::uint8_t> stream = {0x00, 0xFE, 0x02};   // rác + STX giả
    stream.insert(stream.end(), a.begin(), a.end());
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    stream.insert(stream.end(), b.begin(), b.end());

    std::vector<Decoded> got;
    Parser p;
    for (std::uint8_t byte : stream) {   // đưa từng byte: khung bị chia nhỏ tối đa
        p.feed(&byte, 1, [](void* ctx, const Frame& f) { static_cast<std::vector<Decoded>*>(ctx)->push_back({f.sysid, f.compid, f.msgid, {}}); }, &got);
    }
    CHECK_EQ(got.size(), 2u);
    CHECK(got.size() == 2 && got[0].msgid == msgid::kParamRequestList && got[1].msgid == msgid::kCommandAck);
    CHECK(p.rejected() >= 1);
}

TEST(mavlink_heartbeat_and_system_state) {
    Fixture f;
    f.mav.poll(1000);
    auto hb = only(decode_all(f.io.tx()), msgid::kHeartbeat);
    CHECK(hb.size() == 1 && hb[0].sysid == 7 && hb[0].compid == 191);
    CHECK(hb.size() == 1 && hb[0].payload[4] == mav::kTypeOnboardController && hb[0].payload[7] == mav::kStateActive &&
          hb[0].payload[8] == 3);

    f.io.clear_tx();
    f.bus.publish(msg::Event::make(kStateEvt, std::uint8_t{1}));   // trạng thái nghiêm trọng
    f.mav.poll(1500);                                              // chưa tới chu kỳ
    CHECK(only(decode_all(f.io.tx()), msgid::kHeartbeat).empty());
    f.mav.poll(2000);
    hb = only(decode_all(f.io.tx()), msgid::kHeartbeat);
    CHECK(hb.size() == 1 && hb[0].payload[7] == mav::kStateCritical);
    auto txt = only(decode_all(f.io.tx()), msgid::kStatustext);
    CHECK(txt.size() == 1 && txt[0].payload[0] == mav::kSeverityCritical &&
          std::memcmp(txt[0].payload.data() + 1, "state 1", 7) == 0);
}

TEST(mavlink_param_protocol_bridges_param_store) {
    Fixture f;
    // PARAM_REQUEST_LIST -> PARAM_VALUE cho mọi tham số, gửi dần trong poll()
    f.exchange(frame(msgid::kParamRequestList, {7, 191}));
    f.io.clear_tx();
    for (int i = 0; i < 3; ++i) f.mav.poll(10);
    auto values = only(decode_all(f.io.tx()), msgid::kParamValue);
    CHECK_EQ(values.size(), 3u);
    if (values.size() == 3) {
        CHECK(get_f32(values[0].payload.data()) == 750.0f);
        CHECK(get_u16(values[0].payload.data() + 4) == 3 && get_u16(values[2].payload.data() + 6) == 2);
        CHECK(str_equals(values[0].payload.data() + 8, 16, "alarm.high"));
        CHECK(str_equals(values[2].payload.data() + 8, 16, "a_long_name_16ch"));
        CHECK(values[0].payload[24] == mav::kParamTypeReal32);
    }

    // PARAM_REQUEST_READ theo tên (index = -1) và theo chỉ số
    auto r = only(f.exchange(frame(msgid::kParamRequestRead, cat({{0xFF, 0xFF, 7, 191}, param_id("temp.period_ms")}))), msgid::kParamValue);
    CHECK(r.size() == 1 && get_f32(r[0].payload.data()) == 500.0f);
    r = only(f.exchange(frame(msgid::kParamRequestRead, cat({{2, 0, 7, 191}, param_id("")}))), msgid::kParamValue);
    CHECK(r.size() == 1 && get_u16(r[0].payload.data() + 6) == 2);

    // PARAM_SET: hợp lệ -> ParamStore đổi; ngoài miền -> giữ nguyên; cả hai đều trả giá trị hiện tại
    r = only(f.exchange(frame(msgid::kParamSet, cat({f32(700.4f), {7, 191}, param_id("alarm.high"), {9}}))), msgid::kParamValue);
    CHECK_EQ(f.params.get_or(0x3501, 0), 700);
    CHECK(r.size() == 1 && get_f32(r[0].payload.data()) == 700.0f);
    r = only(f.exchange(frame(msgid::kParamSet, cat({f32(99999.0f), {7, 191}, param_id("alarm.high"), {9}}))), msgid::kParamValue);
    CHECK_EQ(f.params.get_or(0x3501, 0), 700);
    CHECK(r.size() == 1 && get_f32(r[0].payload.data()) == 700.0f);

    // Gửi cho hệ thống khác / tham số không tồn tại -> không phản hồi
    CHECK(f.exchange(frame(msgid::kParamSet, cat({f32(1.0f), {9, 191}, param_id("alarm.high"), {9}}))).empty());
    CHECK(f.exchange(frame(msgid::kParamSet, cat({f32(1.0f), {7, 191}, param_id("nope"), {9}}))).empty());
}

TEST(mavlink_command_long_and_telemetry) {
    Fixture f;
    auto cmd = [](std::uint16_t c, float p1) {
        std::vector<std::uint8_t> v = f32(p1);
        for (int i = 1; i < 7; ++i) {
            auto z = f32(0);
            v.insert(v.end(), z.begin(), z.end());
        }
        v.push_back(static_cast<std::uint8_t>(c));
        v.push_back(static_cast<std::uint8_t>(c >> 8));
        v.insert(v.end(), {7, 191, 0});
        return frame(msgid::kCommandLong, v);
    };
    auto ack = only(f.exchange(cmd(mav::kCmdUser1, 1.0f)), msgid::kCommandAck);
    CHECK(ack.size() == 1 && get_u16(ack[0].payload.data()) == mav::kCmdUser1 && ack[0].payload[2] == mav::kResultAccepted);
    CHECK_EQ(f.rec.count(kAckEvt), 1u);
    ack = only(f.exchange(cmd(mav::kCmdUser1, 0.0f)), msgid::kCommandAck);
    CHECK(ack.size() == 1 && ack[0].payload[2] == mav::kResultDenied);
    ack = only(f.exchange(cmd(400, 0.0f)), msgid::kCommandAck);   // lệnh không có trong bảng
    CHECK(ack.size() == 1 && ack[0].payload[2] == mav::kResultUnsupported);

    f.io.clear_tx();
    f.bus.publish(msg::Event::make(kTempEvt, std::int32_t{-123}));
    auto tm = only(decode_all(f.io.tx()), msgid::kNamedValueFloat);
    CHECK(tm.size() == 1 && get_f32(tm[0].payload.data() + 4) == -12.3f && str_equals(tm[0].payload.data() + 8, 10, "temp"));

    f.mav.detach();   // sau detach: không gửi gì nữa
    f.io.clear_tx();
    f.bus.publish(msg::Event::make(kTempEvt, std::int32_t{1}));
    CHECK(f.io.tx().empty());
}

TEST(mavlink_gcs_link_supervision_and_probe) {
    Fixture f;
    CHECK(!f.mav.gcs_connected());
    f.mav.poll(100);
    f.exchange(frame(msgid::kHeartbeat, {0, 0, 0, 0, 6, 8, 0, 4, 3}, 255, 190));   // HEARTBEAT của GCS
    CHECK(f.mav.gcs_connected());
    f.mav.poll(100 + MavlinkV1Protocol::kGcsTimeoutMs + 1000);
    CHECK(!f.mav.gcs_connected());

    const auto good = frame(msgid::kHeartbeat, {0, 0, 0, 0, 6, 8, 0, 4, 3});
    CHECK(f.mav.probe(good.data(), good.size()));
    CHECK(!f.mav.probe(good.data(), good.size() - 1));
    const std::uint8_t modbus[] = {1, 3, 0, 0, 0, 1, 0x84, 0x0A};
    CHECK(!f.mav.probe(modbus, sizeof(modbus)));
}
