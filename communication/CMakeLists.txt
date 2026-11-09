# Communication: ngăn xếp truyền thông, nằm TRÊN services và DƯỚI app.
#   link/    khung chung: IByteStream, IProtocol, LinkManager (kênh, chọn/đổi giao thức)
#   net/     stream TCP/UDP cho kênh           (ep_comm_net, chỉ khi target có mạng)
#   cli/     giao thức dòng lệnh văn bản      (ep_proto_cli)
#   modbus/  giao thức Modbus RTU slave       (ep_proto_modbus)
#   mavlink/ giao thức MAVLink v1             (ep_proto_mavlink)
#   mavlink2/ giao thức MAVLink v2 + dialect   (ep_proto_mavlink2, dùng bởi hw_bringup)
# Mỗi giao thức là một thư viện riêng: sản phẩm không link thì không tốn Flash.
add_subdirectory(link)
add_subdirectory(net)
add_subdirectory(cli)
add_subdirectory(modbus)
add_subdirectory(mavlink)
add_subdirectory(mavlink2)
