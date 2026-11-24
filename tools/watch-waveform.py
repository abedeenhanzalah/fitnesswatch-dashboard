"""Plot the 50 Hz MAX30102 USB stream and estimate pulse rate."""
import argparse
from collections import deque
import time

import matplotlib.pyplot as plt
import numpy as np
import serial
from scipy.signal import butter, find_peaks, sosfiltfilt
from serial.tools import list_ports

RATE = 50
FILTER = butter(2, (0.65, 3.5), fs=RATE, btype="bandpass", output="sos")


def analyze(ir):
    waveform = sosfiltfilt(FILTER, ir)
    if len(ir) < 8 * RATE or np.median(ir) < 1000 or np.max(ir) >= 261000:
        return waveform, None
    amplitude = np.percentile(waveform, 95) - np.percentile(waveform, 5)
    if amplitude < 20:
        return waveform, None
    peaks, _ = find_peaks(waveform, distance=0.29 * RATE, prominence=0.35 * amplitude)
    peaks = peaks[(peaks >= RATE // 2) & (peaks < len(ir) - RATE // 2)]
    intervals = np.diff(peaks) / RATE
    if len(intervals) < 4:
        return waveform, None
    period = np.median(intervals)
    if not 0.30 <= period <= 1.5 or np.any(np.abs(intervals - period) > 0.20 * period):
        return waveform, None
    return waveform, round(60 / period)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="XIAO USB serial port, e.g. COM7")
    args = parser.parse_args()
    ports = [p for p in list_ports.comports() if p.vid is not None]
    port = args.port
    if not port:
        if not ports:
            parser.exit(1, "No USB serial port. Connect the XIAO and let it leave bootloader mode.\n")
        for candidate in ports:
            print(f"{candidate.device}: {candidate.description}")
        port = ports[0].device if len(ports) == 1 else input("XIAO COM port: ").strip()
    samples = deque(maxlen=12 * RATE)
    previous = None
    pending = bytearray()
    try:
        with serial.Serial(port, 115200, timeout=0.1) as connection:
            plt.ion()
            figure, axis = plt.subplots()
            line, = axis.plot([], [], color="tab:red")
            axis.set(xlabel="Seconds", ylabel="Filtered infrared signal (counts)", xlim=(-12, 0))
            axis.grid(alpha=0.3)
            last_sample = time.monotonic()
            while plt.fignum_exists(figure.number):
                pending.extend(connection.read(connection.in_waiting or 1))
                rows = pending.split(b"\n")
                pending = rows.pop()
                for row in rows:
                    _, marker, values = row.partition(b"WATCH_PPG ")
                    if marker:
                        try:
                            sequence, _, ir = map(int, values.split())
                        except ValueError:
                            continue
                        if previous is not None and sequence != previous + 1:
                            samples.clear()
                        samples.append(ir)
                        previous = sequence
                        last_sample = time.monotonic()
                    elif b"WATCH_TEST " in row:
                        print(row.split(b"WATCH_TEST ", 1)[1].decode(errors="replace").strip())
                title = "Collecting signal. Hold your fingertip still."
                if time.monotonic() - last_sample > 2:
                    samples.clear()
                    title = "No optical samples. Check firmware and terminal output."
                if len(samples) >= RATE:
                    waveform, bpm = analyze(np.asarray(samples))
                    line.set_data(np.arange(1 - len(waveform), 1) / RATE, waveform)
                    axis.relim()
                    axis.autoscale_view(scalex=False)
                    if bpm is not None:
                        title = f"Estimated pulse rate: {bpm} BPM"
                else:
                    line.set_data([], [])
                axis.set_title(title)
                plt.pause(0.05)
    except serial.SerialException as error:
        parser.exit(1, f"Cannot read {port}: {error}. Close the other serial viewer if open.\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
