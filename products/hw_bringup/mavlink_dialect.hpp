#pragma once
// "Cột MAVLink" của sản phẩm hw_bringup: ánh xạ Event bring-up <-> bản tin (giống modbus_map.cpp).
#include "mavlink2/mavlink_protocol.hpp"

namespace product {

class BringupDialect final : public mavlink2::IDialect {
public:
    bool crc_extra(std::uint32_t msgid, std::uint8_t& out) const override;
    void on_event(const msg::Event& e, mavlink2::Sender& tx) override;
    void on_message(const mavlink2::Message& m, comm::Services& s, mavlink2::Sender& tx) override;
    std::uint8_t on_command(const mavlink2::CommandLong& c, comm::Services& s, mavlink2::Sender& tx) override;
};

}  // namespace product
