#!/usr/bin/env python3
"""Kiểm tra firmware bằng pymavlink (thư viện MAVLink chính thức) – đối chiếu độc lập với codec C++.

  python3 -m pip install pymavlink
  ./build/firmware &                      # bản mô phỏng (thermo_pro)
  tools/check_mavlink_pymavlink.py tcp:127.0.0.1:5760
  tools/check_mavlink_pymavlink.py udpin:127.0.0.1:14550

In PASS/FAIL cho: heartbeat, param list, param set, COMMAND_LONG/ACK, NAMED_VALUE_FLOAT.
"""
import os
import sys

os.environ.pop("MAVLINK20", None)   # dùng MAVLink v1
from pymavlink import mavutil  # noqa: E402


def check(name, ok):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")
    return ok


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else "tcp:127.0.0.1:5760"
    m = mavutil.mavlink_connection(url, source_system=255, source_component=190, dialect="common")
    results = []

    hb = m.wait_heartbeat(timeout=5)
    results.append(check("HEARTBEAT (v1, sysid 1)", hb is not None and m.target_system == 1 and hb.get_srcComponent() == 191))
    m.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS, mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 4)

    m.mav.param_request_list_send(1, 191)
    params, count = {}, None
    while True:
        p = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=3)
        if p is None:
            break
        params[p.param_id] = p.param_value
        count = p.param_count
        if len(params) == count:
            break
    results.append(check(f"PARAM_REQUEST_LIST ({len(params)}/{count})", count and len(params) == count))

    m.mav.param_set_send(1, 191, b"alarm.high", 710, mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
    p = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=3)
    results.append(check("PARAM_SET alarm.high = 710", p is not None and p.param_id == "alarm.high" and p.param_value == 710))

    m.mav.command_long_send(1, 191, mavutil.mavlink.MAV_CMD_USER_1, 0, 1, 0, 0, 0, 0, 0, 0)
    a = m.recv_match(type="COMMAND_ACK", blocking=True, timeout=3)
    results.append(check("COMMAND_LONG MAV_CMD_USER_1 -> ACCEPTED",
                         a is not None and a.result == mavutil.mavlink.MAV_RESULT_ACCEPTED))

    t = m.recv_match(type="NAMED_VALUE_FLOAT", blocking=True, timeout=3)
    results.append(check("NAMED_VALUE_FLOAT temp", t is not None and t.name == "temp"))

    print("ALL PASS" if all(results) else "SOME CHECKS FAILED")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
