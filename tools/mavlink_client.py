#!/usr/bin/env python3
"""Trạm mặt đất MAVLink v1 tối giản (chỉ thư viện chuẩn) để thử firmware qua TCP hoặc UDP.

Định dạng & CRC_EXTRA của từng message lấy từ pymavlink (dialects/v10/common.py); hiện thực
mã hóa/giải mã độc lập với code C++ của firmware để kiểm tra chéo.

Ví dụ (firmware mô phỏng đang chạy):
  tools/mavlink_client.py --tcp 127.0.0.1:5760 watch 3          # in mọi message nhận được trong 3 s
  tools/mavlink_client.py --tcp 127.0.0.1:5760 params           # đọc toàn bộ tham số
  tools/mavlink_client.py --tcp 127.0.0.1:5760 set alarm.high 700
  tools/mavlink_client.py --tcp 127.0.0.1:5760 cmd 31010 1      # MAV_CMD_USER_1: xác nhận cảnh báo
  tools/mavlink_client.py --tcp 127.0.0.1:5760 cmd 245 1        # MAV_CMD_PREFLIGHT_STORAGE: lưu tham số
  tools/mavlink_client.py --udp 14550 watch 3                   # lắng nghe như QGroundControl
"""
import argparse
import socket
import struct
import sys
import time

STX = 0xFE
# msgid: (tên, struct format của payload theo thứ tự trên dây, CRC_EXTRA)
MESSAGES = {
    0: ("HEARTBEAT", "<IBBBBB", 50),
    20: ("PARAM_REQUEST_READ", "<hBB16s", 214),
    21: ("PARAM_REQUEST_LIST", "<BB", 159),
    22: ("PARAM_VALUE", "<fHH16sB", 220),
    23: ("PARAM_SET", "<fBB16sB", 168),
    76: ("COMMAND_LONG", "<7fHBBB", 152),
    77: ("COMMAND_ACK", "<HB", 143),
    251: ("NAMED_VALUE_FLOAT", "<If10s", 170),
    253: ("STATUSTEXT", "<B50s", 83),
}
RESULTS = {0: "ACCEPTED", 1: "TEMPORARILY_REJECTED", 2: "DENIED", 3: "UNSUPPORTED", 4: "FAILED"}
GCS_SYSID, GCS_COMPID = 255, 190


def crc_mcrf4xx(data, crc=0xFFFF):
    for b in data:
        tmp = b ^ (crc & 0xFF)
        tmp = (tmp ^ (tmp << 4)) & 0xFF
        crc = ((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4)) & 0xFFFF
    return crc


class Link:
    def __init__(self, args):
        self.seq = 0
        self.rx = b""
        if args.tcp:
            host, port = args.tcp.rsplit(":", 1)
            self.sock = socket.create_connection((host, int(port)), timeout=2)
            self.peer = None
        else:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind(("127.0.0.1", args.udp))
            self.peer = None   # trả lời địa chỉ đã gửi tới (như GCS thật)
        self.sock.settimeout(0.1)
        self.last_hb = 0.0

    def send(self, msgid, *fields):
        _, fmt, extra = MESSAGES[msgid]
        payload = struct.pack(fmt, *fields)
        hdr = bytes([len(payload), self.seq & 0xFF, GCS_SYSID, GCS_COMPID, msgid])
        crc = crc_mcrf4xx(bytes([extra]), crc_mcrf4xx(hdr + payload))
        frame = bytes([STX]) + hdr + payload + struct.pack("<H", crc)
        self.seq += 1
        if self.peer is None and not isinstance(self.sock.type, int) and self.sock.type == socket.SOCK_DGRAM:
            return False
        if self.sock.type == socket.SOCK_DGRAM:
            self.sock.sendto(frame, self.peer)
        else:
            self.sock.sendall(frame)
        return True

    def heartbeat(self):
        if time.time() - self.last_hb >= 1.0 and (self.sock.type != socket.SOCK_DGRAM or self.peer):
            self.send(0, 0, 6, 8, 0, 4, 3)   # MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, ACTIVE
            self.last_hb = time.time()

    def messages(self, duration):
        """Sinh (msgid, sysid, compid, fields) trong `duration` giây; tự gửi heartbeat GCS."""
        end = time.time() + duration
        while time.time() < end:
            self.heartbeat()
            try:
                if self.sock.type == socket.SOCK_DGRAM:
                    data, self.peer = self.sock.recvfrom(1024)
                else:
                    data = self.sock.recv(1024)
                    if not data:
                        sys.exit("connection closed by device")
                self.rx += data
            except socket.timeout:
                pass
            while True:
                i = self.rx.find(bytes([STX]))
                if i < 0:
                    self.rx = b""
                    break
                self.rx = self.rx[i:]
                if len(self.rx) < 8 or len(self.rx) < 8 + self.rx[1]:
                    break
                n = self.rx[1]
                frame, msgid = self.rx[: 8 + n], self.rx[5]
                if msgid not in MESSAGES:
                    self.rx = self.rx[1:]
                    continue
                _, fmt, extra = MESSAGES[msgid]
                crc = crc_mcrf4xx(bytes([extra]), crc_mcrf4xx(frame[1 : 6 + n]))
                if struct.calcsize(fmt) != n or struct.unpack("<H", frame[6 + n :])[0] != crc:
                    print(f"!! bad frame msgid={msgid}", file=sys.stderr)
                    self.rx = self.rx[1:]
                    continue
                self.rx = self.rx[8 + n :]
                yield msgid, frame[3], frame[4], struct.unpack(fmt, frame[6 : 6 + n])


def text(b):
    return b.split(b"\0", 1)[0].decode(errors="replace")


def show(msgid, sysid, compid, f):
    name = MESSAGES[msgid][0]
    if msgid == 0:
        detail = f"type={f[1]} autopilot={f[2]} system_status={f[4]} mavlink_version={f[5]}"
    elif msgid == 22:
        detail = f"{text(f[3]):16s} = {f[0]:g}  [{f[2]}/{f[1]}]"
    elif msgid == 251:
        detail = f"{text(f[2])} = {f[1]:.2f}"
    elif msgid == 253:
        detail = f"severity={f[0]} '{text(f[1])}'"
    elif msgid == 77:
        detail = f"command={f[0]} result={RESULTS.get(f[1], f[1])}"
    else:
        detail = str(f)
    print(f"[{sysid}:{compid}] {name:18s} {detail}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--tcp", help="host:port của firmware (vd. 127.0.0.1:5760)")
    g.add_argument("--udp", type=int, help="cổng UDP lắng nghe như trạm mặt đất (vd. 14550)")
    ap.add_argument("--target", type=int, default=1, help="sysid của thiết bị")
    ap.add_argument("--compid", type=int, default=191, help="compid của thiết bị")
    ap.add_argument("cmd", choices=["watch", "params", "set", "cmd"])
    ap.add_argument("args", nargs="*")
    a = ap.parse_args()
    link = Link(a)

    if a.cmd == "watch":
        for m in link.messages(float(a.args[0]) if a.args else 3):
            show(*m)
        return
    # Với UDP: chờ nhận một khung để biết địa chỉ thiết bị trước khi gửi
    if a.udp:
        for _ in link.messages(2):
            break
    if a.cmd == "params":
        link.send(21, a.target, a.compid)
        got = {}
        for msgid, s, c, f in link.messages(3):
            if msgid == 22:
                got[f[2]] = (text(f[3]), f[0], f[1])
                if len(got) == f[1]:
                    break
        for i in sorted(got):
            print(f"[{i:2d}] {got[i][0]:16s} = {got[i][1]:g}")
        print(f"{len(got)}/{next(iter(got.values()))[2] if got else '?'} parameters")
    elif a.cmd == "set":
        name, value = a.args[0], float(a.args[1])
        link.send(23, value, a.target, a.compid, name.encode(), 9)
        for msgid, s, c, f in link.messages(2):
            if msgid == 22 and text(f[3]) == name:
                print(f"{name} = {f[0]:g}")
                break
        else:
            sys.exit("no PARAM_VALUE reply")
    elif a.cmd == "cmd":
        command = int(a.args[0])
        p = [float(x) for x in a.args[1:]] + [0.0] * (7 - len(a.args[1:]))
        link.send(76, *p[:7], command, a.target, a.compid, 0)
        for msgid, s, c, f in link.messages(2):
            if msgid == 77 and f[0] == command:
                print(f"COMMAND_ACK command={f[0]} result={RESULTS.get(f[1], f[1])}")
                break
        else:
            sys.exit("no COMMAND_ACK")


if __name__ == "__main__":
    main()
