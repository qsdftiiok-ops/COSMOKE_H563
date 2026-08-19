#!/usr/bin/env python3
"""Receive and decode COSMOKE CSV or binary telemetry over UDP."""

from __future__ import annotations

import argparse
import socket
import struct
import sys
from typing import Any


PACKET_FORMAT = "<HBBHHIIHH4i4i4HHH"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)
SYNC = 0xA55A
FLAG_NAMES = {
    0: "VALID",
    1: "AFE_FAULT",
    2: "ADC_CRC_ERROR",
    3: "ADC_FRAME_DROP",
    4: "ADC_SPI_ERROR",
    5: "ADC_OFFLINE",
    6: "DAC_ERROR",
    7: "OUTPUT_CLIPPED",
    8: "LAN_LINK_DOWN",
    9: "LAN_NO_ADDRESS",
    10: "LAN_TX_ERROR",
    11: "LAN_FRAME_DROP",
    12: "MUTED",
    13: "WARMUP",
}


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = (
                ((crc << 1) ^ 0x1021) & 0xFFFF
                if crc & 0x8000
                else (crc << 1) & 0xFFFF
            )
    return crc


def decode_binary(packet: bytes) -> dict[str, Any]:
    if len(packet) != PACKET_SIZE:
        raise ValueError(f"unexpected packet size {len(packet)} (expected {PACKET_SIZE})")

    fields = struct.unpack(PACKET_FORMAT, packet)
    sync, version, packet_type, length, flags = fields[:5]
    received_crc = fields[-1]
    if sync != SYNC:
        raise ValueError(f"bad sync 0x{sync:04X}")
    if length != PACKET_SIZE:
        raise ValueError(f"bad embedded length {length}")
    if crc16_ccitt(packet[:-2]) != received_crc:
        raise ValueError("CRC mismatch")

    sequence, milliseconds, adc_status, crc_errors = fields[5:9]
    payload = fields[9:]
    return {
        "version": version,
        "type": packet_type,
        "flags": flags,
        "sequence": sequence,
        "milliseconds": milliseconds,
        "adc_status": adc_status,
        "crc_errors": crc_errors,
        "raw": payload[0:4],
        "scaled": payload[4:8],
        "dac": payload[8:12],
        "dropped": payload[12],
    }


def flag_text(flags: int) -> str:
    names = [name for bit, name in FLAG_NAMES.items() if flags & (1 << bit)]
    unknown = flags & ~sum(1 << bit for bit in FLAG_NAMES)
    if unknown:
        names.append(f"UNKNOWN_0x{unknown:04X}")
    return "|".join(names) if names else "NONE"


def print_binary(packet: bytes, peer: tuple[str, int], show_flags: bool) -> bool:
    try:
        decoded = decode_binary(packet)
    except ValueError as error:
        print(f"{peer[0]}:{peer[1]}: {error}", file=sys.stderr)
        return False

    scaled = decoded["scaled"]
    print(
        f"{peer[0]},{decoded['sequence']},{decoded['milliseconds']},"
        f"0x{decoded['flags']:04X},0x{decoded['adc_status']:04X},"
        f"{scaled[0]},{scaled[1]},{scaled[2]},{scaled[3]},"
        f"{decoded['dropped']},{decoded['crc_errors']}"
    )
    if show_flags:
        print(f"  flags: {flag_text(decoded['flags'])}", file=sys.stderr)
    if decoded["version"] != 1 or decoded["type"] != 1:
        print(
            f"{peer[0]}: unexpected version/type "
            f"{decoded['version']}/{decoded['type']}",
            file=sys.stderr,
        )
    return True


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Listen for COSMOKE telemetry (default: auto-detect CSV/binary)."
    )
    parser.add_argument("--bind", default="0.0.0.0", help="local address to bind")
    parser.add_argument("--port", type=int, default=5000, help="UDP destination port")
    parser.add_argument(
        "--format", choices=("auto", "csv", "binary"), default="auto"
    )
    parser.add_argument(
        "--binary", action="store_true", help="legacy alias for --format binary"
    )
    parser.add_argument("--show-flags", action="store_true", help="decode flag names")
    parser.add_argument("--once", action="store_true", help="exit after one datagram")
    parser.add_argument(
        "--timeout", type=float, default=0.0, help="receive timeout in seconds (0=wait)"
    )
    args = parser.parse_args()
    selected_format = "binary" if args.binary else args.format
    printed_binary_header = False

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        receiver.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        receiver.bind((args.bind, args.port))
        if args.timeout > 0:
            receiver.settimeout(args.timeout)
        print(
            f"Listening on {args.bind}:{args.port} ({selected_format})",
            file=sys.stderr,
        )

        while True:
            try:
                packet, peer = receiver.recvfrom(2048)
            except socket.timeout:
                print("Receive timeout: no COSMOKE datagram arrived.", file=sys.stderr)
                return 1

            packet_format = selected_format
            if packet_format == "auto":
                packet_format = (
                    "binary"
                    if len(packet) == PACKET_SIZE and packet[:2] == b"\x5A\xA5"
                    else "csv"
                )

            if packet_format == "binary":
                if not printed_binary_header:
                    print(
                        "source,seq,ms,flags,adc_status,i1_mA,v1_mV,"
                        "i2_mA,v2_mV,dropped,crc_errors"
                    )
                    printed_binary_header = True
                print_binary(packet, peer, args.show_flags)
            else:
                text = packet.decode("ascii", errors="replace").rstrip("\r\n")
                print(f"{peer[0]},{text}")

            if args.once:
                return 0


if __name__ == "__main__":
    raise SystemExit(main())

