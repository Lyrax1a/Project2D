"""Optional post-measurement 460800 UART export handshake; never opens a port.

Use the caller's read_line(timeout) so any bytes prefetched after XD_READY stay
in the same buffer as its existing binary reader. Missing export_baud is legacy
115200 and causes no IO. Read *all* declared binary and GC_END/FC_END before
checking its CRC, then finish(verified=True); a rejected CRC must never ACK.
"""
from __future__ import annotations

import time

BASE_BAUD = 115200
EXPORT_BAUD = 460800
TOKEN = b"TS2DFAST"


class ExportError(RuntimeError):
    pass


class ExportSession:
    def __init__(self, uart, metadata, read_line, *, clock=time.monotonic, sleep=time.sleep):
        requested = metadata.get("export_baud")
        if requested is not None and requested != EXPORT_BAUD:
            raise ExportError(f"Unsupported export_baud={requested!r}")
        self.uart, self.read_line = uart, read_line
        self.clock, self.sleep = clock, sleep
        self.enabled = requested is not None
        self.state = "new"
        self.recovery_line = None

    def _write(self, data):
        # pyserial normally returns all bytes; tolerate a finite short write.
        deadline = self.clock() + 1.0
        pending = memoryview(data)
        while pending:
            n = self.uart.write(pending.tobytes())
            if not isinstance(n, int) or not 0 <= n <= len(pending):
                raise ExportError("Invalid UART write result")
            pending = pending[n:]
            if pending and self.clock() >= deadline:
                raise TimeoutError("Export handshake write timeout")
            if not n:
                self.sleep(.001)
        self.uart.flush()

    @staticmethod
    def _line(value):
        return value.decode("ascii", errors="replace").strip() if isinstance(value, bytes) else value.strip()

    def begin(self):
        if self.state != "new":
            raise ExportError("Export session already started")
        if not self.enabled:
            self.state = "legacy"
            return self
        if self.uart.baudrate != BASE_BAUD:
            raise ExportError("Export header must be received at 115200 baud")
        self.state = "handshake"
        try:
            self._write(TOKEN)
            # Covers the board's <=23 ms physical header drain, token reception
            # and divisor update. No incoming bytes are discarded at any point.
            self.sleep(.050)
            self.uart.baudrate = EXPORT_BAUD
            self._write(b"\x55")
            value = self._line(self.read_line(4.0))
            if value != "XD_READY":
                raise ExportError(f"Expected XD_READY, received {value!r}")
            self.state = "active"
            return self
        except Exception:
            self.abort()
            raise

    def finish(self, *, verified):
        if self.state == "legacy":
            if not verified:
                raise ExportError("Caller rejected legacy export CRC/footer")
            self.state = "done"
            return
        if self.state != "active":
            raise ExportError("Export session is not active")
        if not verified:
            self.abort()
            raise ExportError("Caller rejected export CRC/footer; success ACK was not sent")
        try:
            self._write(b"\x06")
            # Board holds a 50 ms quiet interval after ACK, then restores baud.
            self.uart.baudrate = BASE_BAUD
            value = self._line(self.read_line(2.0))
            if value != "XD_DONE":
                raise ExportError(f"Expected XD_DONE, received {value!r}")
            self.state = "done"
        except Exception:
            self.abort()
            raise

    def abort(self):
        """Best-effort bounded recovery; never send success ACK.

        If a payload read itself failed mid-stream, the board may still be
        transmitting. That transfer is failed, and the caller must not start a
        new command until the board is idle. Normal CRC rejection should happen
        only after the complete payload/footer has been consumed.
        """
        if not self.enabled or self.state in ("new", "done", "aborted"):
            return
        previous = self.state
        self.state = "aborted"
        try:
            if previous == "active" and self.uart.baudrate == EXPORT_BAUD:
                self._write(b"\x15")
        except Exception:
            pass
        finally:
            self.uart.baudrate = BASE_BAUD
        try:
            self.recovery_line = self._line(self.read_line(6.0))
        except Exception:
            self.recovery_line = None

    def __enter__(self):
        return self.begin()

    def __exit__(self, kind, value, traceback):
        if self.enabled and self.state not in ("done", "aborted"):
            self.abort()
            if kind is None:
                raise ExportError("Export context left without verified finish")
        return False
