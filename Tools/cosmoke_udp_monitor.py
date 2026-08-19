#!/usr/bin/env python3
r"""Receive, decode, and optionally capture COSMOKE UDP telemetry as CSV.

Example:
    python .\Tools\cosmoke_udp_monitor.py --csv-out .\logs\soak_001.csv
"""

from __future__ import annotations

import argparse
import csv
import socket
import struct
import sys
from datetime import datetime, timezone
from pathlib import Path
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

CSV_FIELDS = (
    "sequence", "milliseconds", "flags", "adc_status",
    "i1", "v1", "i2", "v2",
    "raw_i1", "raw_v1", "raw_i2", "raw_v2",
    "dac_i1", "dac_v1", "dac_i2", "dac_v2",
    "dropped", "crc_errors", "spi_errors", "dac_errors",
)

CAPTURE_COLUMNS = (
    "received_at", "source", "format", "sequence", "device_ms",
    "flags_hex", "flags_text", "adc_status_hex",
    "i1_mA", "v1_mV", "i2_mA", "v2_mV",
    "raw_i1", "raw_v1", "raw_i2", "raw_v2",
    "dac_i1", "dac_v1", "dac_i2", "dac_v2",
    "dropped", "crc_errors", "spi_errors", "dac_errors",
)


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


def decode_csv(packet: bytes) -> dict[str, Any]:
    """Decode the firmware's default 20-field CSV telemetry datagram."""
    text = packet.decode("ascii").strip()
    values = text.split(",")
    if len(values) != len(CSV_FIELDS):
        raise ValueError(f"CSV field count is {len(values)}, expected {len(CSV_FIELDS)}")
    return {name: int(value, 0) for name, value in zip(CSV_FIELDS, values)}


def flag_text(flags: int) -> str:
    names = [name for bit, name in FLAG_NAMES.items() if flags & (1 << bit)]
    unknown = flags & ~sum(1 << bit for bit in FLAG_NAMES)
    if unknown:
        names.append(f"UNKNOWN_0x{unknown:04X}")
    return "|".join(names) if names else "NONE"


def capture_row(
    decoded: dict[str, Any], peer: tuple[str, int], packet_format: str, received_at: datetime
) -> dict[str, Any]:
    """Normalize CSV and binary telemetry to one analysis-friendly capture row."""
    if packet_format == "binary":
        raw = decoded["raw"]
        scaled = decoded["scaled"]
        dac = decoded["dac"]
        sequence = decoded["sequence"]
        milliseconds = decoded["milliseconds"]
        flags = decoded["flags"]
        adc_status = decoded["adc_status"]
        crc_errors = decoded["crc_errors"]
        dropped = decoded["dropped"]
        spi_errors = None
        dac_errors = None
    else:
        raw = (decoded["raw_i1"], decoded["raw_v1"], decoded["raw_i2"], decoded["raw_v2"])
        scaled = (decoded["i1"], decoded["v1"], decoded["i2"], decoded["v2"])
        dac = (decoded["dac_i1"], decoded["dac_v1"], decoded["dac_i2"], decoded["dac_v2"])
        sequence = decoded["sequence"]
        milliseconds = decoded["milliseconds"]
        flags = decoded["flags"]
        adc_status = decoded["adc_status"]
        crc_errors = decoded["crc_errors"]
        dropped = decoded["dropped"]
        spi_errors = decoded["spi_errors"]
        dac_errors = decoded["dac_errors"]

    return {
        "received_at": received_at.isoformat(timespec="milliseconds"),
        "source": f"{peer[0]}:{peer[1]}",
        "format": packet_format.upper(),
        "sequence": sequence,
        "device_ms": milliseconds,
        "flags_hex": f"0x{flags:04X}",
        "flags_text": flag_text(flags),
        "adc_status_hex": f"0x{adc_status:04X}",
        "i1_mA": scaled[0], "v1_mV": scaled[1],
        "i2_mA": scaled[2], "v2_mV": scaled[3],
        "raw_i1": raw[0], "raw_v1": raw[1], "raw_i2": raw[2], "raw_v2": raw[3],
        "dac_i1": dac[0], "dac_v1": dac[1], "dac_i2": dac[2], "dac_v2": dac[3],
        "dropped": dropped,
        "crc_errors": crc_errors,
        "spi_errors": spi_errors,
        "dac_errors": dac_errors,
    }


class SequenceStats:
    """Track UDP sequence gaps without treating a device restart as packet loss."""

    def __init__(self) -> None:
        self.frames = 0
        self.lost = 0
        self.resets_or_reorders = 0
        self._last_sequence: int | None = None
        self._last_device_ms: int | None = None

    def add(self, sequence: int, device_ms: int) -> None:
        if self._last_sequence is not None:
            if sequence > self._last_sequence + 1:
                self.lost += sequence - self._last_sequence - 1
            elif sequence <= self._last_sequence:
                self.resets_or_reorders += 1
                if self._last_device_ms is not None and device_ms < self._last_device_ms:
                    print("Device timestamp reset detected.", file=sys.stderr)
        self.frames += 1
        self._last_sequence = sequence
        self._last_device_ms = device_ms


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
    parser.add_argument(
        "--csv-out", type=Path, metavar="PATH",
        help="save normalized telemetry with PC receive timestamps to PATH",
    )
    parser.add_argument(
        "--append", action="store_true",
        help="append to --csv-out instead of replacing the capture file",
    )
    parser.add_argument("--once", action="store_true", help="exit after one datagram")
    parser.add_argument(
        "--timeout", type=float, default=0.0, help="receive timeout in seconds (0=wait)"
    )
    args = parser.parse_args()
    selected_format = "binary" if args.binary else args.format
    printed_binary_header = False
    stats = SequenceStats()
    capture_file = None
    capture_writer = None

    if args.append and args.csv_out is None:
        parser.error("--append requires --csv-out")
    if args.csv_out is not None:
        args.csv_out.parent.mkdir(parents=True, exist_ok=True)
        mode = "a" if args.append else "w"
        capture_file = args.csv_out.open(mode, newline="", encoding="utf-8")
        capture_writer = csv.DictWriter(capture_file, fieldnames=CAPTURE_COLUMNS)
        if not args.append or args.csv_out.stat().st_size == 0:
            capture_writer.writeheader()
        print(f"Capturing normalized CSV to {args.csv_out}", file=sys.stderr)

    try:
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

                try:
                    decoded = (
                        decode_binary(packet)
                        if packet_format == "binary"
                        else decode_csv(packet)
                    )
                except (UnicodeDecodeError, ValueError) as error:
                    print(f"{peer[0]}:{peer[1]}: {error}", file=sys.stderr)
                    continue

                if packet_format == "binary":
                    if not printed_binary_header:
                        print(
                            "source,seq,ms,flags,adc_status,i1_mA,v1_mV,"
                            "i2_mA,v2_mV,dropped,crc_errors"
                        )
                        printed_binary_header = True
                    scaled = decoded["scaled"]
                    print(
                        f"{peer[0]},{decoded['sequence']},{decoded['milliseconds']},"
                        f"0x{decoded['flags']:04X},0x{decoded['adc_status']:04X},"
                        f"{scaled[0]},{scaled[1]},{scaled[2]},{scaled[3]},"
                        f"{decoded['dropped']},{decoded['crc_errors']}"
                    )
                    if args.show_flags:
                        print(f"  flags: {flag_text(decoded['flags'])}", file=sys.stderr)
                else:
                    print(f"{peer[0]},{packet.decode('ascii', errors='replace').rstrip(chr(13) + chr(10))}")

                row = capture_row(decoded, peer, packet_format, datetime.now(timezone.utc))
                stats.add(row["sequence"], row["device_ms"])
                if capture_writer is not None:
                    capture_writer.writerow(row)
                    capture_file.flush()

                if args.once:
                    return 0
    except KeyboardInterrupt:
        return 0
    finally:
        if capture_file is not None:
            capture_file.close()
            print(
                f"Capture summary: frames={stats.frames}, estimated_udp_loss={stats.lost}, "
                f"resets_or_reorders={stats.resets_or_reorders}",
                file=sys.stderr,
            )


if __name__ == "__main__":
    raise SystemExit(main())
