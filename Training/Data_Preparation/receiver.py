#!/usr/bin/env python3
"""
STM32 Gesture Recorder
Usage: python receiver.py <COM_PORT>
Example: python receiver.py COM3
"""

import sys
import os
import csv
import time
import struct
import threading
import msvcrt
import serial

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
CALIB_DURATION = 0.5  # seconds holding still for bias estimation
RECORD_DURATION = 2.0  # seconds of gesture capture
BYTES_PER_SAMPLE = 6  # 3 × int16_t little-endian (Gx, Gy, Gz)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.dirname(SCRIPT_DIR)
CSV_DIR = os.path.join(PARENT_DIR, "CSVs")

# ---------------------------------------------------------------------------
# Shared state (reader thread writes buffers; main thread drives the FSM)
# ---------------------------------------------------------------------------
STATE_IDLE = "IDLE"
STATE_CALIBRATING = "CALIBRATING"
STATE_READY = "READY"
STATE_RECORDING = "RECORDING"

state = STATE_IDLE
state_lock = threading.Lock()
phase_start = 0.0

calib_buffer = []  # raw (gx, gy, gz) during calibration
record_buffer = []  # bias-corrected (gx, gy, gz) during recording
bias = (0.0, 0.0, 0.0)


# ---------------------------------------------------------------------------
# Serial reader thread
# ---------------------------------------------------------------------------
def reader_thread_fn(ser: serial.Serial) -> None:
    global calib_buffer, record_buffer

    while True:
        raw = ser.read(BYTES_PER_SAMPLE)
        if len(raw) < BYTES_PER_SAMPLE:
            continue

        gx, gy, gz = struct.unpack("<hhh", raw)

        with state_lock:
            current = state

        if current == STATE_CALIBRATING:
            calib_buffer.append((gx, gy, gz))
        elif current == STATE_RECORDING:
            bx, by, bz = bias
            record_buffer.append((gx - bx, gy - by, gz - bz))
        # IDLE → silently discard


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def next_filename(gesture_name: str) -> str:
    """Returns Training/CSVs/<gesture_name>_NNN.csv with auto-increment."""
    os.makedirs(CSV_DIR, exist_ok=True)
    n = 1
    while True:
        path = os.path.join(CSV_DIR, f"{gesture_name}_{n:03d}.csv")
        if not os.path.exists(path):
            return path
        n += 1


def save_csv(samples: list, gesture_name: str) -> None:
    path = next_filename(gesture_name)
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["Index", "Gx", "Gy", "Gz"])
        for i, (gx, gy, gz) in enumerate(samples):
            writer.writerow([i, round(gx), round(gy), round(gz)])
    print(f"[SAVED]  {path}  ({len(samples)} samples)")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def main() -> None:
    global state, phase_start, calib_buffer, record_buffer, bias

    if len(sys.argv) < 2:
        print("Usage: python receiver.py <COM_PORT>")
        sys.exit(1)

    try:
        ser = serial.Serial(sys.argv[1], baudrate=115200, timeout=1)
    except serial.SerialException as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    print(f"[INFO]   Connected to {sys.argv[1]}")
    print("Press R to record a gesture.  Ctrl-C to quit.\n")

    reader = threading.Thread(target=reader_thread_fn, args=(ser,), daemon=True)
    reader.start()

    try:
        while True:
            # --- keyboard input (non-blocking) ---
            if msvcrt.kbhit():
                key = msvcrt.getch().lower()

                with state_lock:
                    current = state

                if key == b"r" and current == STATE_IDLE:
                    calib_buffer.clear()
                    phase_start = time.time()
                    with state_lock:
                        state = STATE_CALIBRATING
                    print("[CALIBRATING]  Hold still...")

                elif current == STATE_READY:
                    record_buffer.clear()
                    phase_start = time.time()
                    with state_lock:
                        state = STATE_RECORDING
                    print("[RECORDING]  Perform gesture now...")

            # --- FSM transitions driven by elapsed time ---
            now = time.time()

            with state_lock:
                current = state

            if current == STATE_CALIBRATING and (now - phase_start) >= CALIB_DURATION:
                if not calib_buffer:
                    print(
                        "[WARNING]  No calibration samples received — check connection."
                    )
                    with state_lock:
                        state = STATE_IDLE
                    print("Press R to try again.\n")
                else:
                    n = len(calib_buffer)
                    bx = sum(s[0] for s in calib_buffer) / n
                    by = sum(s[1] for s in calib_buffer) / n
                    bz = sum(s[2] for s in calib_buffer) / n
                    bias = (bx, by, bz)

                    with state_lock:
                        state = STATE_READY
                    print(
                        "[READY]  Press any key to start the 2-second recording window..."
                    )

            elif current == STATE_RECORDING and (now - phase_start) >= RECORD_DURATION:
                with state_lock:
                    state = STATE_IDLE
                samples = list(record_buffer)

                print(f"[DONE]  {len(samples)} samples captured.")
                name = input("Gesture name: ").strip().lower()
                name = name.replace(" ", "_")
                if name:
                    save_csv(samples, name)
                else:
                    print("[DISCARDED]  No name given.")
                print("\nPress R to record another gesture.\n")

            time.sleep(0.005)

    except KeyboardInterrupt:
        print("\n[EXIT]")
        ser.close()


if __name__ == "__main__":
    main()
