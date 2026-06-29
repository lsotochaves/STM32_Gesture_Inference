#!/usr/bin/env python3
"""
Gesture Sound Player
Usage: python pc_sound_player.py <SERIAL_PORT>
Example: python pc_sound_player.py /dev/ttyACM0   (Linux)
         python pc_sound_player.py COM5            (Windows)

Reads gesture predictions from the STM32 over USB-serial and plays
the corresponding sound. A new prediction cuts the current playback
and starts the new sound immediately.

Dependencies: pyserial, pygame
  pip install pyserial pygame
"""

import sys
import os
import serial
import pygame

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOUND_DIR  = os.path.join(os.path.dirname(SCRIPT_DIR), "sounds")

SOUND_MAP = {
    "rest":             os.path.join(SOUND_DIR, "rest.mp3"),
    "horizontal_shake": os.path.join(SOUND_DIR, "roll.mp3"),
    "vertical_shake":   os.path.join(SOUND_DIR, "jump.mp3"),
}


def main():
    if len(sys.argv) < 2:
        print("Usage: python pc_sound_player.py <SERIAL_PORT>")
        sys.exit(1)

    for name, path in SOUND_MAP.items():
        if not os.path.isfile(path):
            print(f"[ERROR] Sound file not found: {path}")
            sys.exit(1)

    pygame.mixer.init()

    try:
        ser = serial.Serial(sys.argv[1], baudrate=115200, timeout=1)
    except serial.SerialException as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    print(f"[INFO]  Connected to {sys.argv[1]}")
    print(f"[INFO]  Listening for predictions...\n")

    last_gesture = None

    try:
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            if line not in SOUND_MAP:
                print(f"[WARN]  Unknown prediction: {line!r}")
                continue

            print(f"[GESTURE]  {line}")

            if line != last_gesture:
                pygame.mixer.music.stop()
                pygame.mixer.music.load(SOUND_MAP[line])
                pygame.mixer.music.play()
            elif not pygame.mixer.music.get_busy():
                pygame.mixer.music.play()

            last_gesture = line

    except KeyboardInterrupt:
        print("\n[EXIT]")
        pygame.mixer.music.stop()
        pygame.mixer.quit()
        ser.close()


if __name__ == "__main__":
    main()
