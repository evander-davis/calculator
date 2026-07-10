import argparse
from collections import deque
from enum import IntEnum
import struct
import threading
import time
import tkinter as tk

try:
    import serial
    from serial.tools import list_ports
except ModuleNotFoundError:
    serial = None
    list_ports = None


WIDTH = 320
HEIGHT = 240
FRAME_MAGIC = b"CFRM"
DIRTY_MAGIC = b"CDRT"
BATCH_MAGIC = b"CBAT"
END_MAGIC = b"CEND"
PACKET_MAGICS = (FRAME_MAGIC, DIRTY_MAGIC, BATCH_MAGIC, END_MAGIC)
RGB565_BYTES = WIDTH * HEIGHT * 2
RGB888_BYTES = WIDTH * HEIGHT * 3
PPM_HEADER = b"P6\n320 240\n255\n"


class CalcKey(IntEnum):
    None_ = 0
    Digit0 = 1
    Digit1 = 2
    Digit2 = 3
    Digit3 = 4
    Digit4 = 5
    Digit5 = 6
    Digit6 = 7
    Digit7 = 8
    Digit8 = 9
    Digit9 = 10
    Dot = 11
    Add = 12
    Subtract = 13
    Multiply = 14
    Divide = 15
    Power = 16
    LParen = 17
    RParen = 18
    Equal = 19
    Enter = 20
    Clear = 21
    Delete = 22
    Back = 23
    Left = 24
    Right = 25
    Up = 26
    Down = 27
    Home = 28
    Graph = 29
    YEquals = 30
    Window = 31
    Settings = 32
    About = 33
    Table = 34
    Vars = 35
    Sin = 36
    Cos = 37
    Tan = 38
    ASin = 39
    ACos = 40
    ATan = 41
    Sqrt = 42
    Log = 43
    Ln = 44
    Ans = 45
    Pi = 46
    ConstE = 47
    X = 48
    LetterA = 49
    LetterB = 50
    LetterC = 51
    LetterD = 52
    LetterE = 53
    LetterF = 54
    LetterG = 55
    LetterH = 56
    LetterI = 57
    LetterJ = 58
    LetterK = 59
    LetterL = 60
    LetterM = 61
    LetterN = 62
    LetterO = 63
    LetterP = 64
    LetterQ = 65
    LetterR = 66
    LetterS = 67
    LetterT = 68
    LetterU = 69
    LetterV = 70
    LetterW = 71
    LetterX = 72
    LetterY = 73
    LetterZ = 74
    Second = 75
    Alpha = 76
    Mode = 77
    Zoom = 78
    Trace = 79
    Stat = 80
    Math = 81
    Apps = 82
    Program = 83
    NthRoot = 84
    FracDecimal = 85
    Square = 86
    Comma = 87
    Store = 88
    On = 89
    Negate = 90
    Fraction = 91
    Imaginary = 92
    TenPower = 93
    ExpPower = 94


KEYS = {
    "0": CalcKey.Digit0,
    "1": CalcKey.Digit1,
    "2": CalcKey.Digit2,
    "3": CalcKey.Digit3,
    "4": CalcKey.Digit4,
    "5": CalcKey.Digit5,
    "6": CalcKey.Digit6,
    "7": CalcKey.Digit7,
    "8": CalcKey.Digit8,
    "9": CalcKey.Digit9,
    ".": CalcKey.Dot,
    "+": CalcKey.Add,
    "-": CalcKey.Subtract,
    "*": CalcKey.Multiply,
    "/": CalcKey.Divide,
    "^": CalcKey.Power,
    "(": CalcKey.LParen,
    ")": CalcKey.RParen,
    "=": CalcKey.Equal,
    "Return": CalcKey.Enter,
    "Escape": CalcKey.Clear,
    "Delete": CalcKey.Delete,
    "BackSpace": CalcKey.Back,
    "Left": CalcKey.Left,
    "Right": CalcKey.Right,
    "Up": CalcKey.Up,
    "Down": CalcKey.Down,
    "h": CalcKey.Home,
    "g": CalcKey.Graph,
    "y": CalcKey.YEquals,
    "w": CalcKey.Window,
    "m": CalcKey.Settings,
    "a": CalcKey.LetterA,
    "b": CalcKey.LetterB,
    "c": CalcKey.Clear,
    "d": CalcKey.LetterD,
    "e": CalcKey.LetterE,
    "f": CalcKey.LetterF,
    "i": CalcKey.LetterI,
    "j": CalcKey.LetterJ,
    "k": CalcKey.LetterK,
    "l": CalcKey.Ln,
    "n": CalcKey.LetterN,
    "o": CalcKey.LetterO,
    "p": CalcKey.Pi,
    "q": CalcKey.LetterQ,
    "r": CalcKey.LetterR,
    "s": CalcKey.Sin,
    "t": CalcKey.Tan,
    "u": CalcKey.LetterU,
    "v": CalcKey.LetterV,
    "x": CalcKey.X,
    "z": CalcKey.LetterZ,
}

SPECIAL = {
    "F1": CalcKey.Home,
    "F2": CalcKey.YEquals,
    "F3": CalcKey.Graph,
    "F4": CalcKey.Window,
    "F5": CalcKey.Settings,
}

KEYPAD_ROWS = (
    (("Y=", "STATPLT", "F1", CalcKey.YEquals), ("WINDOW", "TBLSET", "F2", CalcKey.Window), ("ZOOM", "FORMAT", "F3", CalcKey.Zoom), ("TRACE", "CALC", "F4", CalcKey.Trace), ("GRAPH", "TABLE", "F5", CalcKey.Graph)),
    (("2ND", "", "", CalcKey.Second), ("n/d", "MODE", "", CalcKey.Fraction), None, None, ("DEL", "", "", CalcKey.Delete)),
    (("ALPHA", "A-LOCK", "", CalcKey.Alpha), ("X,T,t,n", "LINK", "", CalcKey.X), None, None, ("STAT", "LIST", "", CalcKey.Stat)),
    (("MATH", "TEST", "A", CalcKey.Math), ("APPS", "ANGLE", "B", CalcKey.Apps), ("PRGM", "DRAW", "C", CalcKey.Program), ("VARS", "DISTR", "", CalcKey.Vars), ("CLEAR", "", "", CalcKey.Clear)),
    (("^", "ROOT", "D", CalcKey.Power), ("SIN", "SIN^-1", "E", CalcKey.Sin), ("COS", "COS^-1", "F", CalcKey.Cos), ("TAN", "TAN^-1", "G", CalcKey.Tan), ("<>", "PI", "H", CalcKey.FracDecimal)),
    (("x^2", "SQRT", "I", CalcKey.Square), (",", "EE", "J", CalcKey.Comma), ("(", "{", "K", CalcKey.LParen), (")", "}", "L", CalcKey.RParen), ("/", "e", "M", CalcKey.Divide)),
    (("LOG", "10^x", "N", CalcKey.Log), ("7", "u", "O", CalcKey.Digit7), ("8", "v", "P", CalcKey.Digit8), ("9", "w", "Q", CalcKey.Digit9), ("*", "[", "R", CalcKey.Multiply)),
    (("LN", "e^x", "S", CalcKey.Ln), ("4", "L4", "T", CalcKey.Digit4), ("5", "L5", "U", CalcKey.Digit5), ("6", "L6", "V", CalcKey.Digit6), ("-", "]", "W", CalcKey.Subtract)),
    (("STO>", "RCL", "X", CalcKey.Store), ("1", "L1", "Y", CalcKey.Digit1), ("2", "L2", "Z", CalcKey.Digit2), ("3", "L3", "t", CalcKey.Digit3), ("+", "MEM", '"', CalcKey.Add)),
    (("ON", "OFF", "", CalcKey.On), ("0", "CATALOG", "SPACE", CalcKey.Digit0), (".", "", "i", CalcKey.Dot), ("(-)", "ANS", "?", CalcKey.Negate), ("ENTER", "ENTRY", "SOLVE", CalcKey.Enter)),
)

ARROW_KEYS = (
    ("▲", CalcKey.Up, 0, 1),
    ("◀", CalcKey.Left, 1, 0),
    ("▶", CalcKey.Right, 1, 2),
    ("▼", CalcKey.Down, 2, 1),
)


def keypad_button_text(primary, second, alpha):
    legends = "  ".join(label for label in (second, alpha) if label)
    return f"{legends}\n{primary}" if legends else f"\n{primary}"


def find_default_port():
    ports = list(list_ports.comports())
    if len(ports) == 1:
        return ports[0].device
    for port in ports:
        text = f"{port.device} {port.description} {port.hwid}".lower()
        if "pico" in text or "rp2350" in text or "xiao" in text or "cdc" in text:
            return port.device
    return None


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if chunk:
            data.extend(chunk)
        else:
            time.sleep(0.001)
    return bytes(data)


def sync_to_magic(stream):
    window = bytearray()
    while True:
        byte = stream.read(1)
        if not byte:
            continue
        window += byte
        if len(window) > 4:
            del window[0]
        candidate = bytes(window)
        if candidate in PACKET_MAGICS:
            return candidate


def apply_rgb565_region(rgb, x, y, width, height, payload):
    validate_dirty_region(x, y, width, height)
    if len(payload) != width * height * 2:
        raise ValueError(f"Invalid dirty payload length {len(payload)}")

    payload_offset = 0
    for row in range(height):
        rgb_offset = ((y + row) * WIDTH + x) * 3
        for _ in range(width):
            value = payload[payload_offset] | (payload[payload_offset + 1] << 8)
            rgb[rgb_offset] = ((value >> 11) & 0x1F) * 255 // 31
            rgb[rgb_offset + 1] = ((value >> 5) & 0x3F) * 255 // 63
            rgb[rgb_offset + 2] = (value & 0x1F) * 255 // 31
            payload_offset += 2
            rgb_offset += 3


def validate_dirty_region(x, y, width, height):
    if (
        width <= 0
        or height <= 0
        or x < 0
        or y < 0
        or x + width > WIDTH
        or y + height > HEIGHT
    ):
        raise ValueError(f"Invalid dirty region {x},{y} {width}x{height}")


def rgb565_frame_to_rgb(payload):
    rgb = bytearray(RGB888_BYTES)
    apply_rgb565_region(rgb, 0, 0, WIDTH, HEIGHT, payload)
    return rgb


def read_batch_regions(stream, rgb, region_count):
    payload_bytes = 0
    wire_bytes = 0
    for _ in range(region_count):
        header = read_exact(stream, 8)
        x, y, width, height = struct.unpack("<HHHH", header)
        validate_dirty_region(x, y, width, height)
        if width > 16 or height > 16:
            raise ValueError(f"Dirty tile exceeds 16x16: {width}x{height}")
        payload = read_exact(stream, width * height * 2)
        apply_rgb565_region(rgb, x, y, width, height, payload)
        payload_bytes += len(payload)
        wire_bytes += len(header) + len(payload)
    return payload_bytes, wire_bytes


def run_protocol_self_test():
    rgb = bytearray(RGB888_BYTES)
    payload = struct.pack("<HHHH", 0xF800, 0x07E0, 0x001F, 0xFFFF)
    apply_rgb565_region(rgb, 3, 4, 2, 2, payload)

    def pixel(x, y):
        offset = (y * WIDTH + x) * 3
        return tuple(rgb[offset : offset + 3])

    assert pixel(3, 4) == (255, 0, 0)
    assert pixel(4, 4) == (0, 255, 0)
    assert pixel(3, 5) == (0, 0, 255)
    assert pixel(4, 5) == (255, 255, 255)

    class MemoryStream:
        def __init__(self, data):
            self.data = bytearray(data)

        def read(self, size):
            result = bytes(self.data[:size])
            del self.data[:size]
            return result

    assert sync_to_magic(MemoryStream(b"noise" + DIRTY_MAGIC)) == DIRTY_MAGIC
    batch_rgb = bytearray(RGB888_BYTES)
    batch_data = struct.pack("<HHHHH", 7, 8, 1, 1, 0xF800)
    batch_payload, batch_wire = read_batch_regions(MemoryStream(batch_data), batch_rgb, 1)
    assert batch_payload == 2 and batch_wire == 10
    assert tuple(batch_rgb[((8 * WIDTH + 7) * 3) : ((8 * WIDTH + 7) * 3 + 3)]) == (255, 0, 0)
    layout_keys = [entry[3] for row in KEYPAD_ROWS for entry in row if entry is not None]
    layout_keys.extend(key for _, key, _, _ in ARROW_KEYS)
    assert len(layout_keys) == 50
    assert KEYS["Left"] == CalcKey.Left
    assert KEYS["BackSpace"] == CalcKey.Back
    assert SPECIAL["F1"] == CalcKey.Home
    assert SPECIAL["F3"] == CalcKey.Graph
    assert keypad_button_text("SIN", "SIN^-1", "E") == "SIN^-1  E\nSIN"
    try:
        apply_rgb565_region(rgb, WIDTH, 0, 1, 1, b"\0\0")
        raise AssertionError("out-of-bounds dirty region accepted")
    except ValueError:
        pass
    print("tether protocol self-test passed")


class Viewer:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=0.2)
        self.root = tk.Tk()
        self.root.title(f"RP2350 Calculator Tether - {port}")
        self.photo = tk.PhotoImage(width=WIDTH, height=HEIGHT)
        self.label = tk.Label(self.root, image=self.photo, bd=0)
        self.label.pack()
        self.status = tk.StringVar(value="Waiting for dirty-region keyframe...")
        tk.Label(self.root, textvariable=self.status, anchor="w").pack(fill="x")
        self.build_keypad()
        self.root.bind("<KeyPress>", self.on_key)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.rgb = bytearray(RGB888_BYTES)
        self.running = True
        self.frames = 0
        self.pending_wire_bytes = 0
        self.pending_tiles = 0
        self.pending_payload_bytes = 0
        self.pending_sequence = None
        self.pending_key_at = None
        self.frame_times = deque()
        self.wire_samples = deque()
        self.reader = threading.Thread(target=self.read_loop, daemon=True)
        self.reader.start()
        self.ser.write(b"R")

    def on_key(self, event):
        key = SPECIAL.get(event.keysym)
        if key is None:
            key = KEYS.get(event.keysym)
        if key is not None:
            self.send_key(key)

    def send_key(self, key):
        try:
            self.pending_key_at = time.monotonic()
            self.ser.write(bytes((ord("K"), int(key) & 0xFF)))
        except Exception as exc:
            self.status.set(f"Key send error: {exc}")

    def create_keypad_button(self, parent, entry, row, column, **grid_options):
        primary, second, alpha, key = entry
        button = tk.Button(
            parent,
            text=keypad_button_text(primary, second, alpha),
            command=lambda selected_key=key: self.send_key(selected_key),
            width=14,
            height=2,
            font=("Segoe UI", 7),
            bg="#e0e4ec",
            fg="#14161b",
            activebackground="#aad2ff",
            activeforeground="#14161b",
            relief="raised",
            bd=2,
            takefocus=False,
        )
        button.grid(row=row, column=column, padx=3, pady=3, sticky="nsew", **grid_options)

    def build_keypad(self):
        keypad = tk.Frame(self.root, bg="#2a2d34", padx=8, pady=8)
        keypad.pack(fill="both", expand=True)
        for column in range(5):
            keypad.grid_columnconfigure(column, weight=1, uniform="calculator-key")

        for row_index, row in enumerate(KEYPAD_ROWS):
            for column_index, entry in enumerate(row):
                if entry is not None:
                    self.create_keypad_button(keypad, entry, row_index, column_index)

        arrow_pad = tk.Frame(keypad, bg="#2a2d34")
        arrow_pad.grid(row=1, column=2, rowspan=2, columnspan=2, padx=3, pady=3, sticky="nsew")
        for index in range(3):
            arrow_pad.grid_rowconfigure(index, weight=1)
            arrow_pad.grid_columnconfigure(index, weight=1)
        for label, key, row, column in ARROW_KEYS:
            button = tk.Button(
                arrow_pad,
                text=label,
                command=lambda selected_key=key: self.send_key(selected_key),
                width=4,
                height=1,
                font=("Segoe UI Symbol", 9, "bold"),
                bg="#e0e4ec",
                activebackground="#aad2ff",
                relief="raised",
                bd=2,
                takefocus=False,
            )
            button.grid(row=row, column=column, padx=2, pady=1, sticky="nsew")

    def request_keyframe(self):
        self.ser.write(b"R")

    def record_frame_metrics(self, now, wire_bytes):
        self.frame_times.append(now)
        self.wire_samples.append((now, wire_bytes))
        cutoff = now - 1.0
        while self.frame_times and self.frame_times[0] < cutoff:
            self.frame_times.popleft()
        while self.wire_samples and self.wire_samples[0][0] < cutoff:
            self.wire_samples.popleft()

        if len(self.frame_times) >= 2:
            elapsed = self.frame_times[-1] - self.frame_times[0]
            display_fps = (len(self.frame_times) - 1) / elapsed if elapsed > 0 else 0.0
        else:
            display_fps = 0.0
        wire_kib = sum(sample[1] for sample in self.wire_samples) / 1024.0
        return display_fps, wire_kib

    def finish_frame(self, seq, tiles, flags, payload_bytes, render_us, stack_used=0, end_packet_bytes=24):
        now = time.monotonic()
        self.frames += 1
        wire_bytes = self.pending_wire_bytes + end_packet_bytes
        self.pending_wire_bytes = 0
        self.pending_tiles = 0
        self.pending_payload_bytes = 0
        self.pending_sequence = None
        display_fps, wire_kib = self.record_frame_metrics(now, wire_bytes)

        key_latency_ms = None
        if self.pending_key_at is not None:
            latency = now - self.pending_key_at
            if 0.0 <= latency <= 2.0:
                key_latency_ms = latency * 1000.0
            self.pending_key_at = None

        ppm = PPM_HEADER + bytes(self.rgb)
        details = (
            f"Frame {seq}  {display_fps:4.1f} display FPS  {wire_kib:6.1f} KiB/s  "
            f"{tiles} tiles/{payload_bytes / 1024.0:.1f} KiB  render {render_us / 1000.0:.2f} ms  "
            f"stack {stack_used / 1024.0:.1f} KiB"
        )
        if flags & 1:
            details += "  keyframe"
        if key_latency_ms is not None:
            details += f"  key->frame {key_latency_ms:.1f} ms"
        self.root.after(0, self.update_image, ppm, details)

    def read_loop(self):
        while self.running:
            try:
                magic = sync_to_magic(self.ser)
                if magic == FRAME_MAGIC:
                    header = read_exact(self.ser, 8)
                    seq, width, height = struct.unpack("<IHH", header)
                    if width != WIDTH or height != HEIGHT:
                        raise ValueError(f"Unexpected frame size {width}x{height}")
                    payload = read_exact(self.ser, RGB565_BYTES)
                    self.rgb = rgb565_frame_to_rgb(payload)
                    self.pending_wire_bytes = 4 + len(header) + len(payload)
                    self.finish_frame(seq, 1, 1, len(payload), 0, end_packet_bytes=0)
                elif magic == DIRTY_MAGIC:
                    header = read_exact(self.ser, 12)
                    seq, x, y, width, height = struct.unpack("<IHHHH", header)
                    validate_dirty_region(x, y, width, height)
                    if width > 16 or height > 16:
                        raise ValueError(f"Dirty tile exceeds 16x16: {width}x{height}")
                    payload = read_exact(self.ser, width * height * 2)
                    if self.pending_sequence is None:
                        self.pending_sequence = seq
                    elif self.pending_sequence != seq:
                        raise ValueError(f"Dirty sequence changed from {self.pending_sequence} to {seq}")
                    apply_rgb565_region(self.rgb, x, y, width, height, payload)
                    self.pending_wire_bytes += 4 + len(header) + len(payload)
                    self.pending_tiles += 1
                    self.pending_payload_bytes += len(payload)
                elif magic == BATCH_MAGIC:
                    header = read_exact(self.ser, 8)
                    seq, regions, _reserved = struct.unpack("<IHH", header)
                    if self.pending_sequence is None:
                        self.pending_sequence = seq
                    elif self.pending_sequence != seq:
                        raise ValueError(f"Dirty sequence changed from {self.pending_sequence} to {seq}")
                    payload_bytes, region_wire_bytes = read_batch_regions(self.ser, self.rgb, regions)
                    self.pending_wire_bytes += 4 + len(header) + region_wire_bytes
                    self.pending_tiles += regions
                    self.pending_payload_bytes += payload_bytes
                elif magic == END_MAGIC:
                    header = read_exact(self.ser, 20)
                    seq, tiles, flags, payload_bytes, render_us, stack_used = struct.unpack("<IHHIII", header)
                    if self.pending_sequence is None and tiles == 0 and payload_bytes == 0:
                        self.pending_sequence = seq
                    if self.pending_sequence != seq:
                        raise ValueError(f"Frame end sequence {seq} does not match {self.pending_sequence}")
                    if tiles != self.pending_tiles or payload_bytes != self.pending_payload_bytes:
                        raise ValueError(
                            f"Frame {seq} summary mismatch: {tiles}/{payload_bytes} received "
                            f"{self.pending_tiles}/{self.pending_payload_bytes}"
                        )
                    self.finish_frame(seq, tiles, flags, payload_bytes, render_us, stack_used)
            except Exception as exc:
                if self.running:
                    self.pending_wire_bytes = 0
                    self.pending_tiles = 0
                    self.pending_payload_bytes = 0
                    self.pending_sequence = None
                    self.root.after(0, self.status.set, f"Serial/protocol error: {exc}; requesting keyframe")
                    try:
                        self.request_keyframe()
                    except Exception:
                        pass
                    time.sleep(0.25)

    def update_image(self, ppm, details):
        self.photo.configure(data=ppm, format="PPM")
        self.status.set(details)

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
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        run_protocol_self_test()
        return

    if serial is None:
        print("pyserial is required. Install it with: py -m pip install pyserial")
        raise SystemExit(2)

    port = args.port or find_default_port()
    if not port:
        print("No serial port found. Use --port COMx.")
        for item in list_ports.comports():
            print(f"{item.device}: {item.description}")
        raise SystemExit(2)

    Viewer(port, args.baud).run()


if __name__ == "__main__":
    main()
