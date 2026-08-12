#!/usr/bin/env python3
"""COSMOKE telemetry monitor for CSV or 64-byte binary packets."""

from __future__ import annotations

import argparse
import struct
import sys

try:
    import serial
except ImportError:
    print("pyserial is required: py -m pip install pyserial", file=sys.stderr)
    raise SystemExit(2)


PACKET_FORMAT = "<HBBHHIIHH4i4i4HHH"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)
SYNC = b"\x5A\xA5"


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def read_exact(port: serial.Serial, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = port.read(size - len(data))
        if not chunk:
            raise TimeoutError("serial timeout")
        data.extend(chunk)
    return bytes(data)


def find_sync(port: serial.Serial) -> None:
    previous = b""
    while True:
        current = read_exact(port, 1)
        if previous + current == SYNC:
            return
        previous = current


def monitor_binary(port: serial.Serial) -> None:
    print("seq,ms,flags,adc_status,i1_mA,v1_mV,i2_mA,v2_mV,dropped,crc_errors")
    while True:
        find_sync(port)
        packet = SYNC + read_exact(port, PACKET_SIZE - 2)
        fields = struct.unpack(PACKET_FORMAT, packet)
        received_crc = fields[-1]
        if fields[4] != PACKET_SIZE or crc16_ccitt(packet[:-2]) != received_crc:
            print("packet CRC/length error", file=sys.stderr)
            continue

        _, version, packet_type, _, flags, sequence, milliseconds, adc_status, crc_errors, *payload = fields
        raw = payload[0:4]
        scaled = payload[4:8]
        dac = payload[8:12]
        dropped = payload[12]
        print(
            f"{sequence},{milliseconds},0x{flags:04X},0x{adc_status:04X},"
            f"{scaled[0]},{scaled[1]},{scaled[2]},{scaled[3]},{dropped},{crc_errors}"
        )
        if version != 1 or packet_type != 1:
            print(f"unexpected packet version/type: {version}/{packet_type}", file=sys.stderr)
        _ = raw, dac


def monitor_csv(port: serial.Serial) -> None:
    while True:
        line = port.readline()
        if line:
            print(line.decode("ascii", errors="replace").rstrip())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="Virtual COM port, for example COM7")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--binary", action="store_true", help="Decode 64-byte binary packets")
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=2.0) as port:
        if args.binary:
            monitor_binary(port)
        else:
            monitor_csv(port)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
