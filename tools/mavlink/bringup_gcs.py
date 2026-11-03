#!/usr/bin/env python3
"""Phần mềm giao diện tối giản (dòng lệnh) cho firmware hw_bringup qua MAVLink.
Dùng để thử firmware và làm mẫu cho phần mềm giao diện thật. Chỉ cần Python 3 (+ pyserial nếu dùng cổng COM).

  Mô phỏng (firmware ./build-bringup/firmware, cổng serial 0 = UDP 127.0.0.1:14555):
    tools/mavlink/bringup_gcs.py monitor
  Bo mạch thật:
    tools/mavlink/bringup_gcs.py --serial /dev/ttyUSB0 --baud 115200 monitor

Lệnh:
  monitor [--seconds N]        hiển thị trạng thái loopback, input/output, IMU, GNSS, STATUSTEXT
  output <kênh> <0|1>          đặt một output (OUTPUT_SET)
  outputs <mask hex>           đặt toàn bộ output
  relay <kênh> <0|1>           đặt output bằng COMMAND_LONG(MAV_CMD_DO_SET_RELAY) chuẩn
  gnss "<lệnh>" [--baud B]     gửi lệnh cấu hình tới máy thu GNSS (SERIAL_CONTROL, tự thêm CR LF)
  params                       liệt kê tham số
  param-set <tên> <giá trị>    đổi tham số (PARAM_SET)
  save                         lưu tham số (MAV_CMD_PREFLIGHT_STORAGE)
"""
import argparse
import math
import os
import socket
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mavlink_dialect import Codec, bringup_defs  # noqa: E402

ROLE = {0: 'loop', 1: 'GCS', 2: 'GNSS', 3: 'off'}
RESULT = {0: '-', 1: 'PASS', 2: 'TIMEOUT', 3: 'MISMATCH', 4: 'OPEN-ERR'}
GNSS = {0: 'DETECTING', 1: 'CONNECTED', 2: 'ABSENT -> loopback'}
SEV = ['EMERG', 'ALERT', 'CRIT', 'ERROR', 'WARN', 'NOTICE', 'INFO', 'DEBUG']


class UdpLink:
    def __init__(self, target):
        host, port = target.rsplit(':', 1)
        self.addr = (host, int(port))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.1)

    def write(self, data):
        self.sock.sendto(data, self.addr)

    def read(self):
        try:
            return self.sock.recv(4096)
        except socket.timeout:
            return b''


class SerialLink:
    def __init__(self, port, baud):
        import serial  # pyserial
        self.ser = serial.Serial(port, baud, timeout=0.1)

    def write(self, data):
        self.ser.write(data)

    def read(self):
        return self.ser.read(4096)


class Gcs:
    def __init__(self, link, target_sys=1, target_comp=1):
        self.link, self.codec = link, Codec(bringup_defs())
        self.tsys, self.tcomp = target_sys, target_comp
        self.lock = threading.Lock()
        self.alive = True
        threading.Thread(target=self._heartbeat, daemon=True).start()

    def send(self, name, **fields):
        with self.lock:
            self.link.write(self.codec.encode(name, **fields))

    def _heartbeat(self):
        while self.alive:
            self.send('HEARTBEAT', type=6, autopilot=8, system_status=4, mavlink_version=3)   # MAV_TYPE_GCS
            time.sleep(1.0)

    def messages(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            for m in self.codec.feed(self.link.read()):
                yield m

    def command(self, cmd, *params):
        p = list(params) + [0.0] * (7 - len(params))
        self.send('COMMAND_LONG', target_system=self.tsys, target_component=self.tcomp, command=cmd,
                  **{'param%d' % (i + 1): float(p[i]) for i in range(7)})
        for name, m, _, _ in self.messages(2.0):
            if name == 'COMMAND_ACK' and m['command'] == cmd:
                return m['result']
        return None


def bits(mask, n):
    return ''.join('1' if (mask >> i) & 1 else '0' for i in range(n))


def monitor(gcs, seconds):
    gnss_bytes = 0
    last_print = 0
    state = {}
    for name, m, _, _ in gcs.messages(seconds):
        if name == 'STATUSTEXT':
            print('[%s] %s' % (SEV[m['severity']] if m['severity'] < 8 else m['severity'], m['text']))
        elif name == 'SERIAL_CONTROL' and m['flags'] & 1:
            gnss_bytes += m['count']
            data = bytes(m['data'][:m['count']]).decode(errors='replace')
            state['gnss_tail'] = (state.get('gnss_tail', '') + data)[-200:]
        else:
            state[name if name != 'IMU_DATA' else 'IMU%d' % m['imu_id']] = m
        if time.time() - last_print < 1.0:
            continue
        last_print = time.time()
        print('-' * 78)
        if 'BRINGUP_STATUS' in state:
            b = state['BRINGUP_STATUS']
            print('uptime %.1fs  GNSS port %d: %s  rx=%d tx=%d baud=%d  (forwarded %d bytes)' % (
                b['time_boot_ms'] / 1000, b['gnss_port'], GNSS.get(b['gnss_state'], '?'), b['gnss_rx_bytes'],
                b['gnss_tx_bytes'], b['gnss_baudrate'], gnss_bytes))
        if 'SERIAL_LOOPBACK_STATUS' in state:
            s = state['SERIAL_LOOPBACK_STATUS']
            for i in range(s['port_count']):
                print('  serial %2d %-4s %-8s pass=%-7d fail=%d' % (i, ROLE.get(s['role'][i], '?'),
                      RESULT.get(s['result'][i], '?') if s['role'][i] == 0 else '', s['pass_count'][i], s['fail_count'][i]))
        if 'IO_STATUS' in state:
            io = state['IO_STATUS']
            print('  DI %s   DO %s' % (bits(io['input_mask'], io['input_count']), bits(io['output_mask'], io['output_count'])))
        for k in ('IMU0', 'IMU1', 'IMU2', 'IMU3'):
            if k in state:
                d = state[k]
                fl = d['flags']
                print('  %s type=%d %3d Hz frames=%d err=%d %s gyro=[%s] accel=[%s]' % (
                    k, d['imu_type'], d['rate_hz'], d['frame_count'], d['error_count'],
                    'OK ' if fl & 1 else ('START-FAILED' if fl & 4 else 'NO-DATA'),
                    ' '.join('%+.4f' % v for v in d['gyro']), ' '.join('%+.3f' % v for v in d['accel'])))
        if 'gnss_tail' in state:
            print('  GNSS> ' + state['gnss_tail'].strip().splitlines()[-1][:72])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--udp', default='127.0.0.1:14555', help='địa chỉ UDP của firmware mô phỏng')
    ap.add_argument('--serial', help='cổng serial tới bo mạch (cần pyserial)')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('cmd', nargs='?', default='monitor')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--seconds', type=float, default=1e9, help='monitor: thời gian chạy')
    ap.add_argument('--gnss-baud', type=int, default=0, help='gnss: đổi baudrate cổng máy thu trước khi gửi')
    a = ap.parse_args()

    gcs = Gcs(SerialLink(a.serial, a.baud) if a.serial else UdpLink(a.udp))
    try:
        if a.cmd == 'monitor':
            monitor(gcs, a.seconds)
        elif a.cmd == 'output':
            bit = 1 << int(a.args[0])
            gcs.send('OUTPUT_SET', mask=bit, value=bit if int(a.args[1]) else 0, target_system=gcs.tsys,
                     target_component=gcs.tcomp)
            for name, m, _, _ in gcs.messages(1.0):
                if name == 'IO_STATUS':
                    print('DO %s' % bits(m['output_mask'], m['output_count']))
                    print('DI %s' % bits(m['input_mask'], m['input_count']))
                    break
        elif a.cmd == 'outputs':
            gcs.send('OUTPUT_SET', mask=0xFFFFFFFF >> (32 - 20), value=int(a.args[0], 16),
                     target_system=gcs.tsys, target_component=gcs.tcomp)
        elif a.cmd == 'relay':
            print('result', gcs.command(181, int(a.args[0]), int(a.args[1])))
        elif a.cmd == 'gnss':
            data = (' '.join(a.args) + '\r\n').encode() if a.args else b''
            for k in range(0, max(len(data), 1), 70):
                chunk = data[k:k + 70]
                gcs.send('SERIAL_CONTROL', device=1, flags=2 | 4, baudrate=a.gnss_baud if k == 0 else 0,
                         count=len(chunk), data=list(chunk), target_system=gcs.tsys, target_component=gcs.tcomp)
            reply = b''
            for name, m, _, _ in gcs.messages(2.0):
                if name == 'SERIAL_CONTROL' and m['flags'] & 1:
                    reply += bytes(m['data'][:m['count']])
                elif name == 'STATUSTEXT':
                    print('[%s] %s' % (SEV[m['severity']], m['text']))
            print(reply.decode(errors='replace'))
        elif a.cmd == 'params':
            gcs.send('PARAM_REQUEST_LIST', target_system=gcs.tsys, target_component=gcs.tcomp)
            for name, m, _, _ in gcs.messages(2.0):
                if name == 'PARAM_VALUE':
                    print('%3d/%d %-16s = %g' % (m['param_index'], m['param_count'], m['param_id'], m['param_value']))
        elif a.cmd == 'param-set':
            gcs.send('PARAM_SET', target_system=gcs.tsys, target_component=gcs.tcomp, param_id=a.args[0],
                     param_value=float(a.args[1]), param_type=6)
            for name, m, _, _ in gcs.messages(2.0):
                if name == 'PARAM_VALUE' and m['param_id'] == a.args[0]:
                    print('%s = %g' % (m['param_id'], m['param_value']))
                    break
        elif a.cmd == 'save':
            print('result', gcs.command(245, 1))
        else:
            ap.error('unknown command ' + a.cmd)
    except KeyboardInterrupt:
        pass
    finally:
        gcs.alive = False
    return 0


if __name__ == '__main__':
    sys.exit(main())
