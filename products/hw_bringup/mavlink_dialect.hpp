#pragma once
// "Cột MAVLink" của sản phẩm hw_bringup: ánh xạ Event bring-up <-> bản tin (giống modbus_map.cpp).
#include "mavlink/mavlink_protocol.hpp"

namespace product {

class BringupDialect final : public mavlink::IDialect {
public:
    bool crc_extra(std::uint32_t msgid, std::uint8_t& out) const override;
    void on_event(const svc::Event& e, mavlink::Sender& tx) override;
    void on_message(const mavlink::Message& m, comm::Services& s, mavlink::Sender& tx) override;
    std::uint8_t on_command(const mavlink::CommandLong& c, comm::Services& s, mavlink::Sender& tx) override;
};

}  // namespace product
