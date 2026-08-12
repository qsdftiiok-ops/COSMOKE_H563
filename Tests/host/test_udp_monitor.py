from __future__ import annotations

import importlib.util
import pathlib
import struct
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "cosmoke_udp_monitor", ROOT / "Tools" / "cosmoke_udp_monitor.py"
)
assert SPEC and SPEC.loader
MONITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MONITOR)


class UdpMonitorTests(unittest.TestCase):
    def make_packet(self) -> bytes:
        values = [
            MONITOR.SYNC, 1, 1, MONITOR.PACKET_SIZE, 0x0001,
            42, 1234, 0x0022, 3,
            1, 2, 3, 4,
            100, 200, 300, 400,
            32768, 32769, 32770, 32771,
            5, 0,
        ]
        packet = bytearray(struct.pack(MONITOR.PACKET_FORMAT, *values))
        struct.pack_into("<H", packet, len(packet) - 2, MONITOR.crc16_ccitt(packet[:-2]))
        return bytes(packet)

    def test_binary_decode(self) -> None:
        decoded = MONITOR.decode_binary(self.make_packet())
        self.assertEqual(decoded["sequence"], 42)
        self.assertEqual(decoded["scaled"], (100, 200, 300, 400))
        self.assertEqual(decoded["dropped"], 5)

    def test_crc_error_is_rejected(self) -> None:
        packet = bytearray(self.make_packet())
        packet[20] ^= 0x01
        with self.assertRaisesRegex(ValueError, "CRC"):
            MONITOR.decode_binary(bytes(packet))

    def test_flag_names(self) -> None:
        self.assertEqual(MONITOR.flag_text(0x0101), "VALID|LAN_LINK_DOWN")


if __name__ == "__main__":
    unittest.main()

