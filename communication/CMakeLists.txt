# Communication: ngăn xếp truyền thông, nằm TRÊN services và DƯỚI app.
#   link/    khung chung: IByteStream, IProtocol, LinkManager (kênh, chọn/đổi giao thức)
#   cli/     giao thức dòng lệnh văn bản      (ep_proto_cli)
#   modbus/  giao thức Modbus RTU slave       (ep_proto_modbus)
# Mỗi giao thức là một thư viện riêng: sản phẩm không link thì không tốn Flash.
add_subdirectory(link)
add_subdirectory(cli)
add_subdirectory(modbus)
