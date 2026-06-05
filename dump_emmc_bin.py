#!/usr/bin/env python3

import argparse
import binascii
import csv
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required: pip install pyserial") from exc

try:
    from openpyxl import Workbook
except ImportError:  # pragma: no cover
    Workbook = None


HEADER_MAGIC = b"HFDB"
FOOTER_MAGIC = b"HFDE"
HEADER_STRUCT = struct.Struct("<4sHHIIHHI")
RECORD_STRUCT = struct.Struct("<I5H7hBbbbBBII")
FOOTER_STRUCT = struct.Struct("<4sIII")

RECORD_FIELDS = [
    "timestamp_ms",
    "pack_voltage_deci_v",
    "pack_summed_voltage_deci_v",
    "lowest_cell_voltage_100uv",
    "average_cell_voltage_100uv",
    "highest_cell_voltage_100uv",
    "pack_current_deci_a",
    "lowest_pack_current_deci_a",
    "average_pack_current_deci_a",
    "highest_pack_current_deci_a",
    "lowest_pack_power_kw",
    "average_pack_power_kw",
    "highest_pack_power_kw",
    "pack_soc",
    "highest_temp_c",
    "average_temp_c",
    "lowest_temp_c",
    "fault_flags",
    "record_flags",
    "stored_record_crc32",
    "dump_record_crc32",
]


def crc32_bytes(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Request and parse the hard-fs-dashboard eMMC binary dump."
    )
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbmodemXXXX")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--output-prefix",
        default="emmc_dump",
        help="Output file prefix without extension",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="Seconds to wait for the binary dump header",
    )
    return parser.parse_args()


def read_exact(ser: serial.Serial, size: int) -> bytes:
    data = ser.read(size)
    if len(data) != size:
        raise RuntimeError(f"Serial timeout while reading {size} bytes, got {len(data)}")
    return data


def wait_for_header(ser: serial.Serial, timeout_s: float) -> bytes:
    start = time.monotonic()
    window = bytearray()

    while (time.monotonic() - start) < timeout_s:
        chunk = ser.read(1)
        if not chunk:
            continue

        window += chunk
        if len(window) > len(HEADER_MAGIC):
            window = window[-len(HEADER_MAGIC):]

        if bytes(window) == HEADER_MAGIC:
            return HEADER_MAGIC + read_exact(ser, HEADER_STRUCT.size - len(HEADER_MAGIC))

    raise RuntimeError("Timed out waiting for binary dump header")


def unpack_header(header_bytes: bytes) -> dict:
    magic, version, header_size, session_id, record_count, record_size, flags, header_crc = (
        HEADER_STRUCT.unpack(header_bytes)
    )
    if magic != HEADER_MAGIC:
        raise RuntimeError(f"Unexpected header magic: {magic!r}")
    if header_size != HEADER_STRUCT.size:
        raise RuntimeError(f"Unexpected header size: {header_size}")
    if record_size != RECORD_STRUCT.size:
        raise RuntimeError(f"Unexpected record size: {record_size}")

    calculated_crc = crc32_bytes(header_bytes[:-4])
    if calculated_crc != header_crc:
        raise RuntimeError(
            f"Header CRC mismatch: expected 0x{header_crc:08X}, got 0x{calculated_crc:08X}"
        )

    return {
        "version": version,
        "session_id": session_id,
        "record_count": record_count,
        "flags": flags,
    }


def unpack_record(record_bytes: bytes) -> dict:
    values = RECORD_STRUCT.unpack(record_bytes)
    row = dict(zip(RECORD_FIELDS, values))
    payload_crc = crc32_bytes(record_bytes[:-4])
    row["dump_crc_valid"] = payload_crc == row["dump_record_crc32"]
    row["stored_crc_valid"] = bool(row["record_flags"] & 0x01)
    return row


def unpack_footer(footer_bytes: bytes) -> dict:
    magic, record_count, stream_crc, footer_crc = FOOTER_STRUCT.unpack(footer_bytes)
    if magic != FOOTER_MAGIC:
        raise RuntimeError(f"Unexpected footer magic: {magic!r}")

    calculated_crc = crc32_bytes(footer_bytes[:-4])
    if calculated_crc != footer_crc:
        raise RuntimeError(
            f"Footer CRC mismatch: expected 0x{footer_crc:08X}, got 0x{calculated_crc:08X}"
        )

    return {
        "record_count": record_count,
        "stream_crc": stream_crc,
    }


def write_csv(path: Path, rows: list[dict]) -> None:
    fieldnames = RECORD_FIELDS + ["stored_crc_valid", "dump_crc_valid"]
    with path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_xlsx(path: Path, rows: list[dict]) -> bool:
    if Workbook is None:
        return False

    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "eMMC Dump"
    headers = RECORD_FIELDS + ["stored_crc_valid", "dump_crc_valid"]
    sheet.append(headers)
    for row in rows:
        sheet.append([row[name] for name in headers])
    workbook.save(path)
    return True


def main() -> int:
    args = parse_args()
    output_prefix = Path(args.output_prefix)
    raw_path = output_prefix.with_suffix(".bin")
    csv_path = output_prefix.with_suffix(".csv")
    xlsx_path = output_prefix.with_suffix(".xlsx")

    with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        time.sleep(0.2)
        ser.write(b"b")
        ser.flush()

        header_bytes = wait_for_header(ser, args.timeout)
        header = unpack_header(header_bytes)

        records = []
        stream_crc = 0xFFFFFFFF

        with raw_path.open("wb") as raw_file:
            raw_file.write(header_bytes)

            for _ in range(header["record_count"]):
                record_bytes = read_exact(ser, RECORD_STRUCT.size)
                raw_file.write(record_bytes)
                stream_crc = binascii.crc32(record_bytes, stream_crc) & 0xFFFFFFFF
                records.append(unpack_record(record_bytes))

            footer_bytes = read_exact(ser, FOOTER_STRUCT.size)
            raw_file.write(footer_bytes)

        footer = unpack_footer(footer_bytes)
        stream_crc ^= 0xFFFFFFFF

        if footer["record_count"] != header["record_count"]:
            raise RuntimeError(
                f"Footer record count mismatch: expected {header['record_count']}, "
                f"got {footer['record_count']}"
            )

        if footer["stream_crc"] != stream_crc:
            raise RuntimeError(
                f"Stream CRC mismatch: expected 0x{footer['stream_crc']:08X}, "
                f"got 0x{stream_crc:08X}"
            )

    write_csv(csv_path, records)
    wrote_xlsx = write_xlsx(xlsx_path, records)

    print(f"Session ID: {header['session_id']}")
    print(f"Records:    {header['record_count']}")
    print(f"Raw dump:   {raw_path}")
    print(f"CSV:        {csv_path}")
    if wrote_xlsx:
        print(f"Excel:      {xlsx_path}")
    else:
        print("Excel:      skipped (install openpyxl to generate .xlsx)")

    invalid_stored_crc = sum(1 for row in records if not row["stored_crc_valid"])
    invalid_dump_crc = sum(1 for row in records if not row["dump_crc_valid"])
    print(f"Stored CRC invalid rows: {invalid_stored_crc}")
    print(f"Dump CRC invalid rows:   {invalid_dump_crc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
