#!/usr/bin/env python3
"""Modbus RTU-over-UDP client tối giản (chỉ dùng thư viện chuẩn) để thử firmware mô phỏng.

Ví dụ:
  tools/modbus_udp_client.py read-input   0 2          # nhiệt độ, mức cảnh báo
  tools/modbus_udp_client.py read-holding 0 8          # tham số
  tools/modbus_udp_client.py write        1 700        # alarm.high = 70.0 °C
  tools/modbus_udp_client.py coil         0 on         # xác nhận cảnh báo
  tools/modbus_udp_client.py write        7 2          # comm.aux = CLI  (đổi giao thức từ xa)
  tools/modbus_udp_client.py text "link"               # gửi một dòng văn bản (khi kênh đang là CLI)
"""
import argparse
import socket
import struct
import sys

EXCEPTIONS = {1: "illegal function", 2: "illegal data address", 3: "illegal data value", 4: "device failure"}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(pdu: bytes) -> bytes:
    return pdu + struct.pack("<H", crc16_modbus(pdu))


def transact(sock, addr, pdu: bytes) -> bytes:
    sock.sendto(frame(pdu), addr)
    resp, _ = sock.recvfrom(512)
    if len(resp) < 4 or crc16_modbus(resp[:-2]) != struct.unpack("<H", resp[-2:])[0]:
        sys.exit(f"bad response: {resp.hex(' ')}")
    if resp[1] & 0x80:
        sys.exit(f"exception {resp[2]}: {EXCEPTIONS.get(resp[2], '?')}")
    return resp[:-2]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=15020)
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("cmd", choices=["read-input", "read-holding", "write", "coil", "text"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    dst = (a.host, a.port)
    try:
        if a.cmd in ("read-input", "read-holding"):
            start, qty = int(a.args[0]), int(a.args[1]) if len(a.args) > 1 else 1
            fc = 0x04 if a.cmd == "read-input" else 0x03
            r = transact(sock, dst, struct.pack(">BBHH", a.unit, fc, start, qty))
            for i, (v,) in enumerate(struct.iter_unpack(">h", r[3:])):
                print(f"[{start + i}] = {v}")
        elif a.cmd == "write":
            reg, val = int(a.args[0]), int(a.args[1])
            transact(sock, dst, struct.pack(">BBHH", a.unit, 0x06, reg, val & 0xFFFF))
            print(f"[{reg}] <- {val} OK")
        elif a.cmd == "coil":
            coil, on = int(a.args[0]), a.args[1].lower() in ("1", "on", "true")
            transact(sock, dst, struct.pack(">BBHH", a.unit, 0x05, coil, 0xFF00 if on else 0))
            print(f"coil {coil} <- {'ON' if on else 'OFF'} OK")
        else:
            sock.sendto((" ".join(a.args) + "\n").encode(), dst)
            out = b""
            try:
                while True:
                    out += sock.recvfrom(4096)[0]
            except socket.timeout:
                pass
            print(out.decode(errors="replace"), end="")
    except socket.timeout:
        sys.exit("timeout: no response (wrong protocol on channel or firmware not running?)")


if __name__ == "__main__":
    main()
