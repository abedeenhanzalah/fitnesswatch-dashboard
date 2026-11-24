import argparse
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description="Read XIAO sensor diagnostics over USB.")
    parser.add_argument("port", nargs="?", help="XIAO serial port, such as COM7")
    args = parser.parse_args()
    ports = [port for port in list_ports.comports() if port.vid is not None]
    port = args.port
    if not port:
        if len(ports) == 1:
            port = ports[0].device
        else:
            for candidate in ports:
                print(f"{candidate.device}: {candidate.description}")
            if not ports:
                parser.exit(1, "No USB serial port found. Check the cable and let the XIAO leave bootloader mode.\n")
            port = input("Enter the XIAO COM port: ").strip()

    try:
        with serial.Serial(port, 115200, timeout=1) as connection:
            print(f"Listening on {port}. Press Ctrl+C to stop.", flush=True)
            last_report = time.monotonic()
            while True:
                line = connection.readline().decode("utf-8", errors="replace")
                _, marker, reading = line.partition("WATCH_TEST ")
                if marker:
                    print(reading.strip(), flush=True)
                    last_report = time.monotonic()
                elif time.monotonic() - last_report >= 6:
                    print("No readings yet. Check the COM port and that the USB test UF2 was flashed.", flush=True)
                    last_report = time.monotonic()
    except KeyboardInterrupt:
        pass
    except serial.SerialException as error:
        parser.exit(1, f"Cannot read {port}: {error}\n")


if __name__ == "__main__":
    main()
