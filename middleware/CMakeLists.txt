# Middleware: ngăn xếp truyền thông, nằm TRÊN services và DƯỚI app.
#   comm/    : khung chung (IByteStream, IProtocol, LinkManager) + CliProtocol
#   modbus/  : giao thức Modbus RTU (tùy chọn)
#   mavlink/ : giao thức MAVLink v2 (tùy chọn)
# Mỗi giao thức là một thư viện riêng: sản phẩm không link thì không tốn Flash.
add_subdirectory(comm)
add_subdirectory(modbus)
add_subdirectory(mavlink)
