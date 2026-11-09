#!/usr/bin/env python3
"""MAVLink v2 tối giản, chỉ dùng thư viện chuẩn Python: nạp định nghĩa bản tin từ XML,
tính CRC_EXTRA, đóng/mở khung. Dùng cho công cụ thử nghiệm (bringup_gcs.py) và để kiểm tra chéo
hằng số CRC_EXTRA/độ dài trong mã C++ của firmware:

    python3 tools/mavlink/mavlink_dialect.py --check
"""
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

_TYPES = {  # kiểu -> (mã struct, kích thước)
    'char': ('s', 1), 'uint8_t': ('B', 1), 'int8_t': ('b', 1), 'uint16_t': ('H', 2), 'int16_t': ('h', 2),
    'uint32_t': ('I', 4), 'int32_t': ('i', 4), 'uint64_t': ('Q', 8), 'int64_t': ('q', 8),
    'float': ('f', 4), 'double': ('d', 8),
}


def x25(data, crc=0xFFFF):
    for b in data:
        t = (b ^ (crc & 0xFF)) & 0xFF
        t = (t ^ (t << 4)) & 0xFF
        crc = ((crc >> 8) ^ (t << 8) ^ (t << 3) ^ (t >> 4)) & 0xFFFF
    return crc


class Field:
    def __init__(self, name, typ, extension):
        self.name = name
        self.extension = extension
        m = re.match(r'(\w+?)(?:_mavlink_version)?(?:\[(\d+)\])?$', typ)
        self.base = m.group(1)
        self.array = int(m.group(2)) if m.group(2) else 0
        self.code, self.size = _TYPES[self.base]

    @property
    def wire_len(self):
        return self.size * max(self.array, 1)

    @property
    def fmt(self):
        if self.base == 'char':
            return '%ds' % max(self.array, 1)
        return ('%d' % self.array if self.array else '') + self.code


class MessageDef:
    def __init__(self, msgid, name, fields):
        self.id, self.name = msgid, name
        base = [f for f in fields if not f.extension]
        ext = [f for f in fields if f.extension]
        self.wire = sorted(base, key=lambda f: f.size, reverse=True) + ext   # sort ổn định
        crc = x25((name + ' ').encode())
        for f in sorted(base, key=lambda f: f.size, reverse=True):
            crc = x25((f.base + ' ').encode(), crc)
            crc = x25((f.name + ' ').encode(), crc)
            if f.array:
                crc = x25(bytes([f.array]), crc)
        self.crc_extra = (crc & 0xFF) ^ (crc >> 8)
        self.fmt = '<' + ''.join(f.fmt for f in self.wire)
        self.length = struct.calcsize(self.fmt)
        self.base_length = sum(f.wire_len for f in base)

    def pack(self, **values):
        args = []
        for f in self.wire:
            v = values.get(f.name, 0)
            if f.base == 'char':
                args.append(v.encode() if isinstance(v, str) else bytes(v))
            elif f.array:
                v = list(v) if v else []
                args.extend((v + [0] * f.array)[:f.array])
            else:
                args.append(v)
        return struct.pack(self.fmt, *args)

    def unpack(self, payload):
        payload = payload + bytes(self.length - len(payload)) if len(payload) < self.length else payload[:self.length]
        vals = list(struct.unpack(self.fmt, payload))
        out = {}
        for f in self.wire:
            if f.base == 'char':
                out[f.name] = vals.pop(0).split(b'\0', 1)[0].decode(errors='replace')
            elif f.array:
                out[f.name], vals = vals[:f.array], vals[f.array:]
            else:
                out[f.name] = vals.pop(0)
        return out


def load(*paths):
    defs = {}
    for path in paths:
        for msg in ET.parse(path).getroot().iter('message'):
            fields, ext = [], False
            for el in msg:
                if el.tag == 'extensions':
                    ext = True
                elif el.tag == 'field':
                    fields.append(Field(el.get('name'), el.get('type'), ext))
            d = MessageDef(int(msg.get('id')), msg.get('name'), fields)
            defs[d.id] = defs[d.name] = d
    return defs


def bringup_defs():
    return load(os.path.join(HERE, 'common_subset.xml'), os.path.join(HERE, 'bringup.xml'))


class Codec:
    def __init__(self, defs, sysid=255, compid=190):
        self.defs, self.sysid, self.compid, self.seq = defs, sysid, compid, 0
        self.buf = bytearray()
        self.crc_errors = 0

    def encode(self, name, **values):
        d = self.defs[name]
        payload = d.pack(**values).rstrip(b'\0') or b'\0'
        hdr = struct.pack('<BBBBBBBHB', 0xFD, len(payload), 0, 0, self.seq, self.sysid, self.compid,
                          d.id & 0xFFFF, d.id >> 16)
        self.seq = (self.seq + 1) & 0xFF
        crc = x25(bytes([d.crc_extra]), x25(hdr[1:] + payload))
        return hdr + payload + struct.pack('<H', crc)

    def feed(self, data):
        """Trả danh sách (tên, dict trường, sysid, compid) của các bản tin hợp lệ."""
        self.buf += data
        out = []
        while True:
            i = self.buf.find(b'\xfd')
            if i < 0:
                self.buf.clear()
                return out
            del self.buf[:i]
            if len(self.buf) < 12:
                return out
            plen, incompat = self.buf[1], self.buf[2]
            total = 10 + plen + 2 + (13 if incompat & 1 else 0)
            if len(self.buf) < total:
                return out
            msgid = self.buf[7] | (self.buf[8] << 8) | (self.buf[9] << 16)
            d = self.defs.get(msgid)
            frame = bytes(self.buf[:total])
            if d is not None:
                crc = x25(bytes([d.crc_extra]), x25(frame[1:10 + plen]))
                if struct.unpack_from('<H', frame, 10 + plen)[0] == crc:
                    out.append((d.name, d.unpack(frame[10:10 + plen]), frame[5], frame[6]))
                    del self.buf[:total]
                    continue
                self.crc_errors += 1
            del self.buf[:1]


def check():
    """So CRC_EXTRA / độ dài trong mã C++ với giá trị tính từ XML."""
    defs = bringup_defs()
    files = [os.path.join(ROOT, 'communication/mavlink2/include/mavlink2/common.hpp'),
             os.path.join(ROOT, 'products/hw_bringup/bringup_messages.hpp')]
    pat = re.compile(r'kId = (\d+);\s*static constexpr std::uint8_t kCrcExtra = (\d+);\s*'
                     r'static constexpr std::size_t kLen = (\d+);')
    errors, n = 0, 0
    for path in files:
        for msgid, crc, length in pat.findall(open(path, encoding='utf-8').read()):
            d = defs.get(int(msgid))
            n += 1
            if d is None:
                print('unknown message id %s in %s' % (msgid, path))
                errors += 1
            elif d.crc_extra != int(crc) or d.length != int(length):
                print('%s: C++ crc_extra=%s len=%s, XML crc_extra=%d len=%d' % (d.name, crc, length, d.crc_extra, d.length))
                errors += 1
    print('checked %d message definitions, %d mismatch(es)' % (n, errors))
    return 1 if errors or n == 0 else 0


if __name__ == '__main__':
    if '--check' in sys.argv:
        sys.exit(check())
    for k, d in sorted((k, v) for k, v in bringup_defs().items() if isinstance(k, int)):
        print('%6d %-24s crc_extra=%3d len=%3d' % (d.id, d.name, d.crc_extra, d.length))
