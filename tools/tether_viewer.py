import argparse
import struct
import threading
import time
import tkinter as tk

import serial
from serial.tools import list_ports


WIDTH = 320
HEIGHT = 240
MAGIC = b"CFRM"
PAYLOAD_SIZE = WIDTH * HEIGHT * 2

KEYS = {
    "0": 1,
    "1": 2,
    "2": 3,
    "3": 4,
    "4": 5,
    "5": 6,
    "6": 7,
    "7": 8,
    "8": 9,
    "9": 10,
    ".": 11,
    "+": 12,
    "-": 13,
    "*": 14,
    "/": 15,
    "^": 16,
    "(": 17,
    ")": 18,
    "=": 19,
    "Return": 20,
    "Escape": 21,
    "BackSpace": 24,
    "Left": 25,
    "Right": 26,
    "Up": 27,
    "Down": 28,
    "h": 29,
    "g": 30,
    "y": 31,
    "w": 32,
    "m": 33,
    "a": 49,
    "b": 50,
    "c": 51,
    "d": 52,
    "e": 53,
    "f": 54,
    "i": 57,
    "j": 58,
    "k": 59,
    "l": 60,
    "n": 62,
    "o": 63,
    "p": 64,
    "q": 65,
    "r": 66,
    "s": 67,
    "t": 68,
    "u": 69,
    "v": 70,
    "x": 72,
    "z": 74,
}

SPECIAL = {
    "F1": 29,
    "F2": 31,
    "F3": 30,
    "F4": 32,
    "F5": 33,
}


def find_default_port():
    ports = list(list_ports.comports())
    if len(ports) == 1:
        return ports[0].device
    for port in ports:
        text = f"{port.device} {port.description} {port.hwid}".lower()
        if "pico" in text or "rp2350" in text or "xiao" in text or "cdc" in text:
            return port.device
    return None


def read_exact(ser, size):
    data = bytearray()
    while len(data) < size:
        chunk = ser.read(size - len(data))
        if chunk:
            data.extend(chunk)
        else:
            time.sleep(0.001)
    return bytes(data)


def sync_to_magic(ser):
    window = bytearray()
    while True:
        byte = ser.read(1)
        if not byte:
            continue
        window += byte
        if len(window) > len(MAGIC):
            del window[0]
        if bytes(window) == MAGIC:
            return


def rgb565_to_ppm(payload):
    rgb = bytearray()
    for i in range(0, len(payload), 2):
        value = payload[i] | (payload[i + 1] << 8)
        r = ((value >> 11) & 0x1F) * 255 // 31
        g = ((value >> 5) & 0x3F) * 255 // 63
        b = (value & 0x1F) * 255 // 31
        rgb.extend((r, g, b))
    return b"P6\n320 240\n255\n" + bytes(rgb)


class Viewer:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=0.2)
        self.root = tk.Tk()
        self.root.title(f"RP2350 Calculator Tether - {port}")
        self.photo = tk.PhotoImage(width=WIDTH, height=HEIGHT)
        self.label = tk.Label(self.root, image=self.photo, bd=0)
        self.label.pack()
        self.status = tk.StringVar(value="Waiting for frames...")
        tk.Label(self.root, textvariable=self.status, anchor="w").pack(fill="x")
        self.root.bind("<KeyPress>", self.on_key)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.running = True
        self.frames = 0
        self.reader = threading.Thread(target=self.read_loop, daemon=True)
        self.reader.start()

    def on_key(self, event):
        key = SPECIAL.get(event.keysym)
        if key is None:
            key = KEYS.get(event.keysym)
        if key is not None:
            self.ser.write(bytes((ord("K"), key & 0xFF)))

    def read_loop(self):
        while self.running:
            try:
                sync_to_magic(self.ser)
                header = read_exact(self.ser, 8)
                seq, width, height = struct.unpack("<IHH", header)
                if width != WIDTH or height != HEIGHT:
                    self.status.set(f"Unexpected frame size {width}x{height}")
                    continue
                payload = read_exact(self.ser, PAYLOAD_SIZE)
                ppm = rgb565_to_ppm(payload)
                self.frames += 1
                self.root.after(0, self.update_image, ppm, seq)
            except Exception as exc:
                self.root.after(0, self.status.set, f"Serial error: {exc}")
                time.sleep(0.25)

    def update_image(self, ppm, seq):
        self.photo.configure(data=ppm, format="PPM")
        self.status.set(f"Frame {seq}  Keys: F1 Home, F2 Y=, F3 Graph, F4 Window, Enter, arrows, digits/operators")

    def close(self):
        self.running = False
        try:
            self.ser.close()
        finally:
            self.root.destroy()

    def run(self):
        self.root.mainloop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    port = args.port or find_default_port()
    if not port:
        print("No serial port found. Use --port COMx.")
        for item in list_ports.comports():
            print(f"{item.device}: {item.description}")
        raise SystemExit(2)

    Viewer(port, args.baud).run()


if __name__ == "__main__":
    main()
