"""Load the FPGA game and connect keyboard, mouse and desktop audio."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time
import zlib

import serial
try:
    from usurper_export import ExportSession
    from usurper_pc_audio import PCAudioBridge
except ModuleNotFoundError:
    from Scripts.usurper_export import ExportSession
    from Scripts.usurper_pc_audio import PCAudioBridge


ROOT = Path(__file__).resolve().parents[1]
KEYS = set("wasdijklfgbtnvxcpreh012~[],.") | set("WASDIJKLFGBTNVXCPREH") | {"\r", "\n", " ", "\t"}
CONTROL_CANCEL = 0x40
POINTER_HIDDEN = 0x80
MOVE_KEYS = {"w": 1, "up": 1, "s": 2, "down": 2, "a": 4, "left": 4, "d": 8, "right": 8}
COMMAND_KEYS = {"return": b"\r", "kp_enter": b"\r", "space": b" ", "escape": b"\x1b", "tab": b"n",
                "asciitilde": b"~", "bracketleft": b"[", "bracketright": b"]", "comma": b",", "period": b"."}
LEGACY_FIRMWARE_LIMIT = 512 * 1024
FIRMWARE_LIMIT = 896 * 1024


def checked_boot_upgrade(firmware: Path, payload: bytes) -> bytes | None:
    if len(payload) <= LEGACY_FIRMWARE_LIMIT:
        return None
    metadata = json.loads((firmware.parent / "boot_upgrade.json").read_text(encoding="utf-8"))
    bridge = (firmware.parent / "boot_upgrade.bin").read_bytes()
    if (metadata.get("schema") != "usurper-volatile-boot-upgrade.v1"
            or metadata.get("max_firmware_bytes") != FIRMWARE_LIMIT
            or metadata.get("firmware_sha256") != hashlib.sha256(payload).hexdigest()
            or metadata.get("sha256") != hashlib.sha256(bridge).hexdigest()
            or metadata.get("bytes") != len(bridge)
            or not 0 < len(bridge) <= LEGACY_FIRMWARE_LIMIT):
        raise ValueError("Boot RAM upgrade metadata or image does not match this firmware")
    return bridge


def send_firmware(uart, payload: bytes) -> None:
    uart.write(b"TS2D" + struct.pack("<II", len(payload), zlib.crc32(payload)))
    for offset in range(0, len(payload), 64):
        uart.write(payload[offset:offset + 64])
    uart.flush()


def wait_uart(uart, log: bytearray, tokens: tuple[bytes, ...], seconds: int, error: str, *, buffered_prefix: bool=False) -> bytes:
    # Asset requests/footers may begin in the preceding read. Keep only a
    # bounded incomplete-marker suffix, never accept an old complete token.
    reply = bytearray(log[-(max(map(len,tokens))-1):]) if buffered_prefix else bytearray()
    deadline = time.monotonic() + seconds
    while not all(token in reply for token in tokens):
        data = uart.read(4096)
        reply.extend(data); log.extend(data)
        if data:
            print(data.decode("ascii", errors="replace"), end="", flush=True)
        if b"FAIL" in reply or b"TRAP" in reply:
            raise RuntimeError(error + ": board reported failure")
        if time.monotonic() > deadline:
            raise RuntimeError(error)
    return bytes(reply)


def begin_fast_assets(uart, log: bytearray):
    """Negotiate only after an explicit firmware capability advertisement.

    Reuse the tested export transport's physical FIFO drain, sync and ACK baud
    transitions. Payload direction is host-to-board; existing asset CRC and
    bounded destinations remain unchanged. Legacy firmware receives no new tag.
    """
    if b"ASSET_UPLOAD_BAUD=460800\r\n" not in log:
        return None
    def read_line(seconds):
        line = bytearray(); deadline = time.monotonic() + seconds
        while not line.endswith(b"\n"):
            data = uart.read(1)
            line.extend(data); log.extend(data)
            if time.monotonic() > deadline:
                raise TimeoutError("Asset baud handshake line timeout")
        print(line.decode("ascii", errors="replace"), end="", flush=True)
        return bytes(line)
    session = ExportSession(uart, {"export_baud": 460800}, read_line)
    session._write(b"ASFT")
    return session.begin()


def finish_fast_assets(uart, log: bytearray, session):
    if session is None:
        return
    if b"ASSET_SESSION_DONE\r\n" not in log:
        wait_uart(uart, log, (b"ASSET_SESSION_DONE\r\n",), 180,
                  "Asset session did not finish CRC/decode checks",buffered_prefix=True)
    if b"ASSET_LOAD=PASS\r\n" not in log or b"FAIL" in log or b"TRAP" in log:
        session.abort()
        raise RuntimeError("Asset validation failed; no success ACK")
    session.finish(verified=True)


def load_baked_extension(uart, log: bytearray, firmware: Path, reuse: bool) -> dict | None:
    """Optional separate immutable Warden payload; preserves the base asset cache."""
    preparation_path = firmware.parent / "preparation.json"
    if not preparation_path.exists():
        return None
    preparation = json.loads(preparation_path.read_text(encoding="utf-8"))
    metadata = preparation.get("warden_baked_assets")
    if not metadata:
        return None
    payload = (firmware.parent / metadata["compressed_file"]).read_bytes()
    if (not 0 < len(payload) <= 4 * 1024 * 1024
            or len(payload) != metadata["compressed_bytes"]
            or hashlib.sha256(payload).hexdigest() != metadata["compressed_sha256"]
            or zlib.crc32(payload) != metadata["compressed_crc"]):
        raise ValueError("Baked Warden payload does not match this firmware preparation")
    if b"WARDEN_ASSET_READY" not in log:
        wait_uart(uart, log, (b"WARDEN_ASSET_READY",), 30, "No optional Warden asset request",buffered_prefix=True)
    cached = False
    if reuse:
        uart.write(b"WBCK" + struct.pack("<II", len(payload), zlib.crc32(payload)))
        uart.flush()
        reply = bytearray(); deadline = time.monotonic() + 180
        while b"WARDEN_ASSET_CACHE=PASS" not in reply and b"WARDEN_ASSET_CACHE=MISS" not in reply:
            data = uart.read(4096); reply.extend(data); log.extend(data)
            if data:
                print(data.decode("ascii", errors="replace"), end="", flush=True)
            if b"FAIL" in reply or b"TRAP" in reply:
                raise RuntimeError("Warden asset cache validation failed")
            if time.monotonic() > deadline:
                raise TimeoutError("No Warden cache response")
        cached = b"WARDEN_ASSET_CACHE=PASS" in reply
    if not cached:
        uart.write(b"WBL4" + struct.pack("<II", len(payload), zlib.crc32(payload)))
        for offset in range(0, len(payload), 256):
            uart.write(payload[offset:offset+256])
            if offset % (256 * 1024) == 0:
                print(f"WARDEN_ASSET_TX_BYTES={offset}/{len(payload)}", flush=True)
        uart.flush()
    if b"WARDEN_ASSET_LOAD=PASS" not in log:
        wait_uart(uart, log, (b"WARDEN_ASSET_LOAD=PASS",), 180, "Warden resource decode/CRC/attachment failed",buffered_prefix=True)
    return dict(metadata, reused_after_crc=cached)


def control_packet(held: int, x: int, y: int, wheel: int, sequence: int) -> bytes:
    x = max(0, min(1023, x))
    y = max(0, min(575, y))
    angle = round(math.degrees(math.atan2(y - 288, x - 512))) % 360
    payload = struct.pack("<BBHHHb", sequence & 255, held & 255, angle, x, y, max(-1, min(1, wheel)))
    checksum = 0xA5
    for byte in payload:
        checksum ^= byte
    return b"\xfe" + payload + bytes([checksum])


def pointer_position(x: int, y: int, width: int, height: int) -> tuple[int, int, bool]:
    scale = min(max(width, 1) / 1024, max(height, 1) / 576)
    left, top = (width - 1024 * scale) / 2, (height - 576 * scale) / 2
    inside = left <= x < left + 1024 * scale and top <= y < top + 576 * scale
    return (max(0, min(1023, int((x - left) / scale))),
            max(0, min(575, int((y - top) / scale))), inside)


class BoardControls:
    def __init__(self) -> None:
        self.keys: set[str] = set()
        self.mouse_buttons = 0

    def key(self, keysym: str, pressed: bool) -> bytes:
        key = keysym.lower()
        if not pressed:
            self.keys.discard(key)
            return b""
        if key in self.keys:
            return b""
        self.keys.add(key)
        if key in MOVE_KEYS or key == "f":
            return b""
        return COMMAND_KEYS.get(key, key.encode("ascii") if len(key) == 1 and key in KEYS else b"")

    def button(self, bit: int, pressed: bool) -> None:
        self.mouse_buttons = self.mouse_buttons | bit if pressed else self.mouse_buttons & ~bit

    def held(self) -> int:
        held = self.mouse_buttons | (16 if "f" in self.keys else 0)
        for key in self.keys:
            held |= MOVE_KEYS.get(key, 0)
        return held

    def clear(self) -> None:
        self.keys.clear()
        self.mouse_buttons = 0


class BoardInputWindow:
    def __init__(self, uart) -> None:
        import tkinter as tk

        self.tk = tk
        self.uart = uart
        self.closed = False
        self.controls = BoardControls()
        self.focused = True
        self.pointer_x, self.pointer_y = 512, 288
        self.wheel = 0
        self.sequence = 0
        self.last_repeat = 0.0
        self.sounds = None
        self.connection_error = None
        self.root = tk.Tk()
        self.root.title("Usurper 板卡控制")
        self.root.minsize(640, 360)
        self.root.attributes("-topmost", True)
        width, height = 1024, 576
        screen_width = self.root.winfo_screenwidth()
        screen_height = self.root.winfo_screenheight()
        self.root.geometry(f"{width}x{height+66}+{(screen_width-width)//2}+{max(0,(screen_height-height-66)//2)}")
        from tkinter import ttk
        footer=tk.Frame(self.root,background='#101a24');footer.pack(side='bottom',fill='x')
        self.audio_status=tk.StringVar(value='音频初始化中…')
        tk.Label(footer,textvariable=self.audio_status,background='#101a24',foreground='#ecfaff',anchor='w').pack(fill='x')
        row=tk.Frame(footer,background='#101a24');row.pack(fill='x')
        self.audio_device=tk.StringVar(value='系统默认输出')
        self.audio_selector=ttk.Combobox(row,textvariable=self.audio_device,state='readonly',width=42,values=['系统默认输出'])
        self.audio_selector.pack(side='left');self.audio_selector.bind('<<ComboboxSelected>>',self._audio_select)
        ttk.Button(row,text='试听音乐 / 音效',command=self._audio_preview).pack(side='left')
        ttk.Button(row,text='重试音频',command=self._audio_select).pack(side='left')
        tk.Label(row,text='WASD 移动 · 鼠标操作 · Enter 确认 · P 暂停',background='#101a24',foreground='#a2becb').pack(side='left',padx=12)
        self.canvas = tk.Canvas(self.root, width=width, height=height, background="#080b0f", highlightthickness=0, takefocus=True)
        self.canvas.pack(fill="both", expand=True)
        self.viewport = self.canvas.create_rectangle(0, 0, width - 1, height - 1, fill="#101a24", outline="#35525d")
        self.pointer = self.canvas.create_polygon(0, 0, 0, 22, 7, 16, 12, 26, 17, 23, 12, 13, 21, 13,
                                                   fill="#ecfaff", outline="#39bdff", width=2)
        self.canvas.bind("<ButtonPress-1>", lambda _event: self._mouse_button(16, True))
        self.canvas.bind("<ButtonRelease-1>", lambda _event: self._mouse_button(16, False))
        self.canvas.bind("<ButtonPress-3>", lambda _event: self._mouse_button(32, True))
        self.canvas.bind("<ButtonRelease-3>", lambda _event: self._mouse_button(32, False))
        self.canvas.bind("<MouseWheel>", self._on_wheel)
        self.root.bind_all("<KeyPress>", self._on_key)
        self.root.bind_all("<KeyRelease>", self._on_release)
        self.root.bind("<FocusOut>", self._focus_lost)
        self.root.bind("<FocusIn>", self._focus_gained)
        self.canvas.bind("<Leave>", self._pointer_left)
        self.canvas.bind("<Configure>", self._layout)
        self.root.protocol("WM_DELETE_WINDOW", self.close)
        self.root.update_idletasks()
        self.root.focus_force()
        self.root.lift()
        self.canvas.focus_set()

    def _layout(self, _event=None) -> None:
        width, height = self.canvas.winfo_width(), self.canvas.winfo_height()
        scale = min(width / 1024, height / 576)
        left, top = (width - 1024 * scale) / 2, (height - 576 * scale) / 2
        self.canvas.coords(self.viewport, left, top, left + 1024 * scale - 1, top + 576 * scale - 1)

    def _send(self, command: bytes = b"", cancel: bool = False) -> None:
        if self.closed or self.uart is None:
            return
        x = self.canvas.winfo_pointerx() - self.canvas.winfo_rootx()
        y = self.canvas.winfo_pointery() - self.canvas.winfo_rooty()
        px, py, inside = pointer_position(x, y, self.canvas.winfo_width(), self.canvas.winfo_height())
        active = self.focused and not cancel
        if active:
            self.pointer_x, self.pointer_y = px, py
        held = self.controls.held() if active else CONTROL_CANCEL | POINTER_HIDDEN
        color = "#ffe078" if held & 16 else "#77dbff" if held & 32 else "#ecfaff"
        self.canvas.itemconfigure(self.pointer, fill=color, state="normal" if active else "hidden")
        self.canvas.coords(self.pointer, x, y, x, y + 22, x + 7, y + 16, x + 12, y + 26,
                           x + 17, y + 23, x + 12, y + 13, x + 21, y + 13)
        try:
            packet = control_packet(held, self.pointer_x, self.pointer_y, self.wheel if active else 0, self.sequence)
            self.uart.write(packet + command)
            self.sequence = (self.sequence + 1) & 255
            self.wheel = 0
            self.last_repeat = time.monotonic()
        except (OSError, serial.SerialException) as error:
            self.disconnect(error)

    def disconnect(self,error):
        self.connection_error=str(error);self.controls.clear()
        if self.uart:
            try:self.uart.close()
            except (OSError,serial.SerialException):pass
        self.uart=None

    def _audio_select(self,_event=None):
        if self.sounds:self.sounds.select_device(None if self.audio_device.get()=='系统默认输出' else self.audio_device.get())
        self.canvas.focus_set()

    def _audio_preview(self):
        if self.sounds:self.sounds.preview()
        self.canvas.focus_set()

    def _audio_health(self):
        sounds=self.sounds
        if not sounds:return
        self.audio_selector.configure(values=['系统默认输出',*sounds.devices])
        if self.uart is None:status='板卡断开，正在重新连接；可点试听检查电脑声音'
        elif sounds.error:status='音频设备异常，正在重试：'+sounds.error
        elif not sounds.thread:status='电脑音频已关闭'
        elif not sounds.ready:status='正在初始化电脑音频…'
        elif not sounds.last_received or time.perf_counter()-sounds.last_received>3:status='已连接 '+self.uart.port+'；等待板卡音频数据，请确认游戏已加载'
        else:
            p=sounds.last_packet
            status=f'{self.uart.port} · 音乐 {p.music}% · 音效 {p.sfx}% · 已接收 {sounds.received} 条'
            if p.muted:status+=' · 游戏静音'
            elif not p.flags&3:status+=' · 音乐和音效开关已关闭'
            elif sounds.music_playing:status+=' · 播放 '+(sounds.track or '')
            else:status+=' · 音频就绪'
        self.audio_status.set(status)

    def _on_key(self, event) -> None:
        if event.widget not in (self.canvas,self.root):return
        key = event.keysym.lower()
        if key == "q" and event.state & 4:
            self.close()
            return
        if key == "f11":
            if key not in self.controls.keys:
                self.root.attributes("-fullscreen", not bool(self.root.attributes("-fullscreen")))
            self.controls.key(key, True)
            return
        self._send(self.controls.key(key, True))

    def _on_release(self, event) -> None:
        self.controls.key(event.keysym, False)
        self._send()

    def _mouse_button(self, bit: int, pressed: bool) -> None:
        if pressed:
            self.canvas.focus_set()
        self.controls.button(bit, pressed)
        self._send()

    def _on_wheel(self, event) -> None:
        if event.delta:
            self.wheel = -1 if event.delta > 0 else 1
            self._send()

    def _focus_lost(self, _event) -> None:
        self.root.after_idle(self._check_focus)

    def _check_focus(self) -> None:
        if not self.closed and self.root.focus_get() is None:
            self.focused = False
            self.controls.clear()
            self._send(cancel=True)

    def _focus_gained(self, _event) -> None:
        self.focused = True

    def _pointer_left(self, _event) -> None:
        self.controls.clear()
        self._send(cancel=True)

    def poll(self) -> bool:
        if self.closed:
            return False
        try:
            self.root.update_idletasks()
            self.root.update()
        except self.tk.TclError:
            self.closed = True
            return False
        now = time.monotonic()
        self._audio_health()
        if now - self.last_repeat >= 1 / 60:
            self._send()
        return not self.closed

    def close(self) -> None:
        if self.closed:
            return
        try:
            if self.uart:self.uart.write(control_packet(CONTROL_CANCEL | POINTER_HIDDEN, self.pointer_x, self.pointer_y, 0, self.sequence))
        except (OSError, serial.SerialException):
            pass
        self.closed = True
        try:
            self.root.destroy()
        except self.tk.TclError:
            pass


def load(port: str, firmware: Path, assets: Path, seconds: int, interactive: bool, output: Path,reuse_ddr_assets: bool=False, audio: bool=True) -> None:
    payload = firmware.read_bytes()
    asset_payload = assets.read_bytes()
    if not 0 < len(payload) <= FIRMWARE_LIMIT:
        raise ValueError("Firmware outside bootloader range")
    if not 0 < len(asset_payload) <= 16 * 1024 * 1024:
        raise ValueError("Source asset bundle outside board DDR transfer range")
    bridge = checked_boot_upgrade(firmware, payload)
    log = bytearray()
    with serial.Serial(port, 115200, timeout=0.1, write_timeout=10) as uart:
        deadline = time.monotonic() + 12
        while b"READY TS2D" not in log:
            data = uart.read(4096)
            log.extend(data)
            if data:
                print(data.decode("ascii", errors="replace"), end="", flush=True)
            if time.monotonic() > deadline:
                raise RuntimeError("No bootloader READY; configure the compatible TS2D bitstream first")
        if bridge is not None:
            send_firmware(uart, bridge)
            wait_uart(uart, log, (b"DDR_LOAD=PASS\r\n", b"BOOT_UPGRADE=PASS\r\n",
                      b"READY TS2D MAX_BYTES=917504\r\n"), 20,
                      "Boot RAM upgrade did not restart the larger checked loader")
        send_firmware(uart, payload)
        asset_deadline = time.monotonic() + 20
        while b"ASSET_READY" not in log:
            data = uart.read(4096)
            log.extend(data)
            if data:
                print(data.decode("ascii", errors="replace"), end="", flush=True)
            if time.monotonic() > asset_deadline:
                raise RuntimeError("Firmware did not request the source-derived UI asset bundle")
        asset_session=begin_fast_assets(uart,log)
        asset_transfer_baud=uart.baudrate
        cached=False
        if reuse_ddr_assets:
            uart.write(b"ASCK"+struct.pack("<II",len(asset_payload),zlib.crc32(asset_payload)));uart.flush()
            cache_reply=bytearray();cache_deadline=time.monotonic()+180
            while b"ASSET_CACHE=PASS" not in cache_reply and b"ASSET_CACHE=MISS" not in cache_reply:
                data=uart.read(4096);cache_reply.extend(data);log.extend(data)
                if data:print(data.decode("ascii",errors="replace"),end="",flush=True)
                if time.monotonic()>cache_deadline:raise RuntimeError("No DDR cache response; this option requires cache-aware firmware")
            cached=b"ASSET_CACHE=PASS" in cache_reply
        if not cached:
            uart.write(b"ASST" + struct.pack("<II", len(asset_payload), zlib.crc32(asset_payload)))
            for offset in range(0, len(asset_payload), 256):
                uart.write(asset_payload[offset : offset + 256])
                sent = min(offset + 256, len(asset_payload))
                if sent % (512 * 1024) == 0 or sent == len(asset_payload):
                    print(f"ASSET_TX_BYTES={sent}/{len(asset_payload)}", flush=True)
        uart.flush()
        # Larger full-game atlases retain byte-for-byte decoded CRC checking.
        asset_deadline = time.monotonic() + 180
        while b"ASSET_LOAD=PASS" not in log:
            data = uart.read(4096)
            log.extend(data)
            if data:
                print(data.decode("ascii", errors="replace"), end="", flush=True)
            if time.monotonic() > asset_deadline:
                raise RuntimeError("Source-derived asset bundle failed DDR CRC or RLE decode")
        baked_assets = load_baked_extension(uart, log, firmware, reuse_ddr_assets)
        finish_fast_assets(uart,log,asset_session)
        deadline = time.monotonic() + seconds
        if interactive:
            uart.timeout = 0.005
        input_window = BoardInputWindow(uart) if interactive else None
        try:
            with PCAudioBridge(enabled=interactive and audio) as sounds:
                if input_window:input_window.sounds=sounds
                while interactive or time.monotonic() < deadline:
                    data = uart.read(4096)
                    if data:
                        sounds.feed(data)
                        log.extend(data)
                        print(data.decode("ascii", errors="replace"), end="", flush=True)
                    if interactive and not input_window.poll():
                        break
        finally:
            if input_window:
                input_window.close()
    text = log.decode("ascii", errors="replace")
    output.mkdir(parents=True, exist_ok=True)
    (output / "uart.log").write_text(text, encoding="utf-8")
    complete_lines=text.splitlines()
    # A timed capture may stop in the middle of the last UART packet.
    # Preserve raw bytes, but do not interpret that fragment as a sample.
    if text and not text.endswith(("\r","\n")):complete_lines=complete_lines[:-1]
    metrics = [
        dict((key, int(value)) for key, value in re.findall(r"(\w+)=(\d+)", line))
        for line in complete_lines
        if line.startswith("GAME=")
    ]
    passed = (
        "DDR_LOAD=PASS" in text
        and "GAME_FIRST_PRESENT=PASS" in text
        and "FAIL" not in text
        and "TRAP" not in text
        and bool(metrics)
        and all(item.get("UNDERFLOW", -1) == 0 and item.get("AXI_ERROR", -1) == 0 for item in metrics)
    )
    manifest = {
        "schema": "usurper-ts2d-board.v1",
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "firmware_sha256": hashlib.sha256(payload).hexdigest(),
        "boot_ram_upgrade_sha256": hashlib.sha256(bridge).hexdigest() if bridge is not None else None,
        "firmware_loader_limit_bytes": FIRMWARE_LIMIT if bridge is not None else LEGACY_FIRMWARE_LIMIT,
        "assets_sha256": hashlib.sha256(asset_payload).hexdigest(),
        "assets_reused_after_crc":cached,
        "asset_transfer_baud":asset_transfer_baud,
        "warden_baked_assets":baked_assets,
        "port": port,
        "metrics": metrics,
        "status": "RUNTIME_PASS" if passed else "RUNTIME_FAIL",
        "visual_status": "PENDING_USER_OBSERVATION",
        "render_size": [1024, 576],
        "scanout_size": [1920, 1080],
        "game_aspect": "16:9",
        "reference_logical_size": [640, 360],
        "sprite_scale_from_reference": 1.6,
        "assets_loaded_to_ddr": "SOURCE_GEOMETRY_AND_GODOT_PNG_AND_FUSION_FONT",
        "scope": "DDR load/readback, renderer completion, input loop and scanout errors",
    }
    (output / "board_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not passed:
        raise RuntimeError("Board run failed or lacks required samples; inspect uart.log")


def discover_game_port(port='auto'):
    from serial.tools import list_ports
    ports=list(list_ports.comports())
    if port.lower()!='auto' and any(p.device.lower()==port.lower() for p in ports):return port
    candidates=[p.device for p in ports if p.vid==0x0403 and p.pid==0x6011 and 'MI_02' in p.hwid.upper()]
    # Windows FTDI serial descriptors omit the interface ID. Resolve the PnP
    # parent rather than guessing channel C from numerical COM ordering.
    if not candidates:
        import sys,subprocess
        if sys.platform=='win32':
            script="Get-PnpDevice -Class Ports -PresentOnly | ForEach-Object { $p = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Parent' -ErrorAction SilentlyContinue).Data; if ($p -like 'USB\\VID_0403&PID_6011&MI_02*' -and $_.FriendlyName -match '\\((COM\\d+)\\)') { $Matches[1] } }"
            run=subprocess.run(['powershell','-NoProfile','-Command',script],capture_output=True,text=True,timeout=12,creationflags=subprocess.CREATE_NO_WINDOW)
            candidates=[s.strip() for s in run.stdout.splitlines() if re.fullmatch(r'COM\d+',s.strip())]
    if len(candidates)!=1:raise serial.SerialException('未找到唯一的 FT4232 通道 C 游戏串口')
    return candidates[0]

def control_only(port: str, output: Path, audio: bool = True, audio_device=None) -> None:
    output.mkdir(parents=True, exist_ok=True)
    with PCAudioBridge(enabled=audio,device=audio_device) as sounds:
        window = BoardInputWindow(None);window.sounds=sounds;next_connect=0;next_status=0
        try:
            with (output / "controls_uart.log").open("a", encoding="utf-8") as log:
                while window.poll():
                    now=time.monotonic()
                    if window.uart is None:
                        if now>=next_connect:
                            next_connect=now+2
                            try:
                                window.uart=serial.Serial(discover_game_port(port),115200,timeout=.005,write_timeout=1)
                                window.connection_error=None;window.controls.clear()
                            except (OSError,serial.SerialException) as error:window.connection_error=str(error)
                        time.sleep(.01);continue
                    try:data = window.uart.read(4096)
                    except (OSError,serial.SerialException) as error:window.disconnect(error);continue
                    if data:
                        sounds.feed(data)
                        log.write(data.decode("ascii", errors="replace"))
                    if now>=next_status:
                        next_status=now+1;log.flush()
                        (output/'pc_audio.json').write_text(json.dumps(sounds.statistics(),ensure_ascii=False,indent=2),encoding='utf-8')
        finally:
            uart=window.uart
            window.close()
            if uart:uart.close()
            (output/'pc_audio.json').write_text(json.dumps(sounds.statistics(),ensure_ascii=False,indent=2),encoding='utf-8')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default='auto')
    parser.add_argument("--audio-device",help='Exact output device name; default follows Windows')
    parser.add_argument("--firmware", type=Path, default=ROOT / "Build/game.bin")
    parser.add_argument("--assets", type=Path, default=ROOT / "Assets/Graphics/assets.rle")
    parser.add_argument("--reuse-ddr-assets",action="store_true",help="Try retained compressed DDR assets after CRC; retransmit on mismatch. Requires cache-aware firmware.")
    parser.add_argument("--seconds", type=int, default=55)
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--control-only", action="store_true", help="control an already loaded board without uploading again")
    parser.add_argument("--no-audio", action="store_true", help="Disable desktop audio playback")
    parser.add_argument("--output", type=Path, default=ROOT / "Logs/board")
    args = parser.parse_args()
    if args.control_only:
        control_only(args.port, args.output, not args.no_audio,args.audio_device)
    else:
        load(discover_game_port(args.port), args.firmware, args.assets, args.seconds, args.interactive, args.output,args.reuse_ddr_assets,not args.no_audio)
