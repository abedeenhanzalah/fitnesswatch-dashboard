"""Record the FitnessWatch calibration firmware over USB serial."""

import argparse
from binascii import crc_hqx
from collections import Counter
import csv
from datetime import datetime
from pathlib import Path
import time

from serial import Serial, SerialException
from serial.tools.list_ports import comports


FIELDS = (
    "phase", "record", "sequence", "uptime_ms", "red", "ir",
    "ax_mm_s2", "ay_mm_s2", "az_mm_s2", "gx_mrad_s", "gy_mrad_s", "gz_mrad_s",
    "device", "error", "reference_bpm", "reference_spo2_percent", "notes",
)
PHASES = (
    ("no_contact", 8, "Leave the sensor uncovered on the bench."),
    ("fingertip_still", 20, "Rest a fingertip lightly over the optical window."),
    ("wrist_still", 60, "Fit the watch snugly and rest your forearm on the table."),
    ("wrist_motion", 20, "While seated, slowly rotate and relax your wrist a few times."),
)


def parse_frame(line):
    payload, delimiter, checksum = line.rstrip(b"\r\n").rpartition(b"*")
    if not delimiter or len(checksum) != 4:
        return None
    try:
        if crc_hqx(payload, 0xFFFF) != int(checksum, 16):
            return None
        record, *fields = payload.decode("ascii").split(",")
        if record == "E" and len(fields) == 3 and fields[1] in ("PPG", "IMU"):
            return record, int(fields[0]), fields[1], int(fields[2])
        if (record, len(fields)) not in (("P", 4), ("M", 8)):
            return None
        values = tuple(map(int, fields))
        if min(values[:2]) < 0:
            return None
        if record == "P" and not all(0 <= value <= 262143 for value in values[2:]):
            return None
        return record, *values
    except (UnicodeDecodeError, ValueError):
        return None


def capture_phase(connection, writer, phase, seconds):
    connection.reset_input_buffer()
    connection.write(b"S")
    pending = bytearray()
    counts = Counter()
    gaps = Counter()
    previous = {}
    deadline = time.monotonic() + seconds
    last_countdown = None
    while time.monotonic() < deadline:
        remaining = max(0, round(deadline - time.monotonic()))
        if remaining % 5 == 0 and remaining != last_countdown:
            print(f"  {remaining:2} s left  |  PPG {counts['P']}  |  IMU {counts['M']}", flush=True)
            last_countdown = remaining
        pending.extend(connection.read(connection.in_waiting or 1))
        rows = pending.split(b"\n")
        pending = rows.pop()
        for line in rows:
            frame = parse_frame(line)
            if frame is None:
                counts["invalid"] += 1
                continue
            kind, *values = frame
            counts[kind] += 1
            if kind == "E":
                timestamp, device, error = values
                writer.writerow(dict(phase=phase, record="error", uptime_ms=timestamp,
                                     device=device, error=error))
                continue
            sequence, timestamp, *readings = values
            if kind in previous:
                jump = sequence - previous[kind] - 1
                if jump > 0:
                    gaps[kind] += jump
                elif jump < 0:
                    counts["resets"] += 1
            previous[kind] = sequence
            names = FIELDS[4:6] if kind == "P" else FIELDS[6:12]
            writer.writerow(dict(phase=phase, record="ppg" if kind == "P" else "imu",
                                 sequence=sequence, uptime_ms=timestamp,
                                 **dict(zip(names, readings))))
    connection.write(b"X")
    print(f"  PPG {counts['P']}, IMU {counts['M']}, missing sequence numbers "
          f"{gaps['P']} PPG / {gaps['M']} IMU, damaged lines {counts['invalid']}, "
          f"sensor errors {counts['E']}, sequence resets {counts['resets']}.")
    if counts["P"] < seconds * 45 or counts["M"] < seconds * 22:
        print("  Sample rate was below target. Keep this CSV for diagnosis.")
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="USB serial port, for example COM7")
    args = parser.parse_args()
    port = args.port
    if not port:
        candidates = [p.device for p in comports() if (p.vid, p.pid) == (0x2FE3, 0x0004)]
        if len(candidates) != 1:
            parser.exit(1, "Connect the flashed watch over USB, or pass --port COMx.\n")
        port = candidates[0]

    timestamp = datetime.now().astimezone()
    output = Path(__file__).resolve().parent / f"wrist-calibration-{timestamp:%Y%m%d-%H%M%S-%f}.csv"
    try:
        with Serial(port, 115200, timeout=0.1) as connection, output.open("x", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=FIELDS)
            writer.writeheader()
            writer.writerow(dict(record="session", notes=f"{timestamp.isoformat()}; {port}; target 50 Hz PPG / 25 Hz IMU"))
            print(f"Recording from {port}. Close any other serial viewer before starting.")
            print("For the seated wrist phase, have a reference pulse reading ready if available.")
            for phase, seconds, instruction in PHASES:
                input(f"\n{instruction} Press Enter when ready for {seconds} seconds. ")
                counts = capture_phase(connection, writer, phase, seconds)
                file.flush()
                if counts["P"] == counts["M"] == 0:
                    print("No calibration frames received. Check the UF2 and USB cable.")
                    break
                if phase == "wrist_still":
                    bpm = input("Reference BPM, if available (Enter to skip): ").strip()
                    spo2 = input("Reference SpO2 %, if available (Enter to skip): ").strip()
                    note = input("Reference device and watch position (Enter to skip): ").strip()
                    writer.writerow(dict(phase=phase, record="reference", reference_bpm=bpm,
                                         reference_spo2_percent=spo2, notes=note))
    except SerialException as error:
        print(f"Cannot read {port}: {error}")
        return 1
    except (KeyboardInterrupt, EOFError):
        print("\nCapture stopped.")
    finally:
        if output.exists():
            print(f"CSV saved at {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
