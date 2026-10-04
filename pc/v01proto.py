"""
v01proto.py — giao thức khung UART phía PC.

Cài đặt cùng định dạng khung, mã kiểm tra và cơ chế đồng bộ lại như
protocol.c trên STM32. Giao diện (gui.py) và công cụ kiểm thử
(fuzz_test.py) đều dùng module này.

Phụ thuộc: pyserial.
"""
from __future__ import annotations

import struct
import threading
import time
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Callable, Optional

# ===========================================================================
# Hằng số khung
# ===========================================================================
SOF1 = 0xAA
SOF2 = 0x55
HDR_LEN = 5
MAX_DATA = 128          # bằng PROTO_MAX_DATA trong proto_cfg.h

CKS_SUM8, CKS_CRC8, CKS_CRC16 = 0, 1, 2
CKS_MODE = CKS_CRC16    # phải trùng PROTO_CKS_MODE của firmware
CKS_LEN = 2 if CKS_MODE == CKS_CRC16 else 1

RESP_FLAG = 0x80
IBT_S = 0.05            # inter-byte timeout, giây


class Cmd(IntEnum):
    PING         = 0x01
    GET_VERSION  = 0x02
    ECHO         = 0x05
    LED_SET      = 0x10
    LED_GET      = 0x11
    PWM_SET      = 0x12
    ADC_READ     = 0x20
    DHT_READ     = 0x21
    STREAM_START = 0x30
    STREAM_STOP  = 0x31
    STATS_GET    = 0x40
    STATS_RESET  = 0x41
    STREAM_DATA  = 0xF0
    NACK         = 0xFF


class Err(IntEnum):
    OK            = 0x00
    CKS           = 0x01
    LEN           = 0x02
    UNKNOWN_CMD   = 0x03
    PARAM         = 0x04
    BUSY          = 0x05
    TIMEOUT       = 0x06
    NOT_SUPPORTED = 0x07
    SENSOR        = 0x08
    OVERRUN       = 0x09


ERR_TEXT = {
    Err.OK: "OK",
    Err.CKS: "Sai CRC",
    Err.LEN: "Độ dài không hợp lệ",
    Err.UNKNOWN_CMD: "Lệnh không tồn tại",
    Err.PARAM: "Tham số sai",
    Err.BUSY: "Thiết bị đang bận",
    Err.TIMEOUT: "Quá hạn giữa các byte",
    Err.NOT_SUPPORTED: "Không hỗ trợ",
    Err.SENSOR: "Cảm biến lỗi",
    Err.OVERRUN: "Tràn hàng đợi",
}

STREAM_SRC_ADC1CH, STREAM_SRC_ADC2CH, STREAM_SRC_TEST = 0, 1, 2


# ===========================================================================
# Mã kiểm tra (cùng thuật toán với checksum.c)
# ===========================================================================
def sum8(d: bytes) -> int:
    return (-sum(d)) & 0xFF


def crc8(d: bytes) -> int:
    crc = 0x00
    for b in d:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc16(d: bytes) -> int:
    """CRC-16/CCITT-FALSE. Giá trị kiểm tra: crc16(b'123456789') = 0x29B1."""
    crc = 0xFFFF
    for b in d:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _cks(body: bytes) -> bytes:
    if CKS_MODE == CKS_SUM8:
        return bytes([sum8(body)])
    if CKS_MODE == CKS_CRC8:
        return bytes([crc8(body)])
    return struct.pack("<H", crc16(body))


# ===========================================================================
# Đóng gói
# ===========================================================================
def build(seq: int, cmd: int, data: bytes = b"") -> bytes:
    if len(data) > MAX_DATA:
        raise ValueError(f"DATA dài {len(data)} > {MAX_DATA}")
    body = bytes([len(data), seq & 0xFF, cmd & 0xFF]) + data
    return bytes([SOF1, SOF2]) + body + _cks(body)


@dataclass
class Frame:
    seq: int
    cmd: int
    data: bytes

    @property
    def is_nack(self) -> bool:
        return self.cmd == Cmd.NACK

    @property
    def err(self) -> Optional[Err]:
        if self.is_nack and len(self.data) >= 2:
            try:
                return Err(self.data[1])
            except ValueError:
                return None
        return None

    def __repr__(self) -> str:
        return (f"Frame(seq={self.seq}, cmd=0x{self.cmd:02X}, "
                f"len={len(self.data)}, data={self.data.hex(' ')})")


@dataclass
class Stats:
    rx_bytes: int = 0
    rx_ok: int = 0
    err_cks: int = 0
    err_len: int = 0
    err_timeout: int = 0
    resync: int = 0


# ===========================================================================
# Máy trạng thái tách khung (tương ứng protocol.c)
# ===========================================================================
class Parser:
    S_SOF1, S_SOF2, S_LEN, S_SEQ, S_CMD, S_DATA, S_CKS = range(7)

    def __init__(self) -> None:
        self.stats = Stats()
        self._reset()
        self._pend = bytearray()
        self._t_last = 0.0

    def _reset(self) -> None:
        self.state = self.S_SOF1
        self._len = self._seq = self._cmd = 0
        self._data = bytearray()
        self._cks = bytearray()
        self._swallowed = bytearray()

    def _resync(self, which: str):
        self.stats.resync += 1
        setattr(self.stats, which, getattr(self.stats, which) + 1)
        back = bytes(self._swallowed)
        self._reset()
        self._pend[:0] = back          # chèn vào đầu hàng đợi

    def feed(self, chunk: bytes):
        """Đưa một khối byte vào bộ tách khung; trả về lần lượt các Frame hợp lệ."""
        now = time.monotonic()
        if self.state != self.S_SOF1 and (now - self._t_last) > IBT_S:
            self.stats.err_timeout += 1
            self._reset()
            self._pend.clear()
        self._t_last = now
        self.stats.rx_bytes += len(chunk)

        self._pend.extend(chunk)
        while self._pend:
            b = self._pend.pop(0)
            f = self._one(b)
            if f is not None:
                yield f

    # -- xử lý một byte ----------------------------------------------------
    def _one(self, b: int) -> Optional[Frame]:
        st = self.state
        if st == self.S_SOF1:
            if b == SOF1:
                self.state = self.S_SOF2
        elif st == self.S_SOF2:
            if b == SOF2:
                self._swallowed.clear()
                self.state = self.S_LEN
            elif b != SOF1:
                self.state = self.S_SOF1
        elif st == self.S_LEN:
            self._swallowed.append(b)
            if b > MAX_DATA:
                self._resync("err_len")
            else:
                self._len = b
                self.state = self.S_SEQ
        elif st == self.S_SEQ:
            self._swallowed.append(b)
            self._seq = b
            self.state = self.S_CMD
        elif st == self.S_CMD:
            self._swallowed.append(b)
            self._cmd = b
            self._data.clear()
            self.state = self.S_DATA if self._len else self.S_CKS
        elif st == self.S_DATA:
            self._swallowed.append(b)
            self._data.append(b)
            if len(self._data) >= self._len:
                self._cks.clear()
                self.state = self.S_CKS
        elif st == self.S_CKS:
            self._swallowed.append(b)
            self._cks.append(b)
            if len(self._cks) >= CKS_LEN:
                body = bytes([self._len, self._seq, self._cmd]) + bytes(self._data)
                if bytes(self._cks) == _cks(body):
                    self.stats.rx_ok += 1
                    f = Frame(self._seq, self._cmd, bytes(self._data))
                    self._reset()
                    return f
                self._resync("err_cks")
        return None


# ===========================================================================
# Lớp giao tiếp thiết bị
# ===========================================================================
class Device:
    """
    Kết nối với board qua cổng serial.

    Một luồng nền đọc cổng serial và tách khung. Phản hồi được ghép với lệnh
    theo SEQ, vì khi đang streaming các khung STREAM_DATA có thể xen giữa lệnh
    và phản hồi. Khung STREAM_DATA được chuyển cho callback on_stream.
    """

    def __init__(self, port: str, baud: int = 115200):
        import serial  # chỉ cần pyserial khi mở cổng thật
        self.ser = serial.Serial(port, baud, timeout=0.01, write_timeout=1.0)
        self.parser = Parser()
        self._seq = 0
        self._lock = threading.Lock()
        self._pending: dict[int, list] = {}
        self._stop = threading.Event()
        self.on_stream: Optional[Callable[[int, list[int]], None]] = None
        self.on_frame: Optional[Callable[[Frame], None]] = None
        self._rx = threading.Thread(target=self._loop, daemon=True)
        self._rx.start()

    # ------------------------------------------------------------------
    def close(self) -> None:
        self._stop.set()
        self._rx.join(timeout=1.0)
        self.ser.close()

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                n = self.ser.in_waiting
                chunk = self.ser.read(n if n else 1)
            except Exception:
                break
            if not chunk:
                continue
            for f in self.parser.feed(chunk):
                if f.cmd == Cmd.STREAM_DATA:
                    idx, cnt = struct.unpack_from("<IB", f.data, 0)
                    vals = list(struct.unpack_from(f"<{cnt}H", f.data, 5))
                    if self.on_stream:
                        self.on_stream(idx, vals)
                    continue
                with self._lock:
                    slot = self._pending.get(f.seq)
                    if slot is not None:
                        slot.append(f)
                if self.on_frame:
                    self.on_frame(f)

    # ------------------------------------------------------------------
    def request(self, cmd: int, data: bytes = b"", timeout: float = 0.6) -> Frame:
        """Gửi lệnh và chờ phản hồi cùng SEQ.

        Ném TimeoutError nếu quá hạn, RuntimeError nếu nhận NACK.
        """
        with self._lock:
            self._seq = (self._seq + 1) & 0xFF
            seq = self._seq
            slot: list = []
            self._pending[seq] = slot
        try:
            self.ser.write(build(seq, cmd, data))
            t0 = time.monotonic()
            while time.monotonic() - t0 < timeout:
                with self._lock:
                    if slot:
                        f = slot.pop(0)
                        break
                time.sleep(0.001)
            else:
                raise TimeoutError(f"Không có phản hồi cho lệnh 0x{cmd:02X}")
        finally:
            with self._lock:
                self._pending.pop(seq, None)

        if f.is_nack:
            e = f.err
            raise RuntimeError(f"NACK lệnh 0x{f.data[0]:02X}: "
                               f"{ERR_TEXT.get(e, '?')} ({e})")
        if f.cmd != (cmd | RESP_FLAG):
            raise RuntimeError(f"Phản hồi sai CMD: 0x{f.cmd:02X}")
        return f

    def send_raw(self, raw: bytes) -> None:
        """Gửi chuỗi byte bất kỳ (dùng cho kiểm thử khung lỗi)."""
        self.ser.write(raw)

    # -------------------------- lệnh mức cao --------------------------
    def ping(self) -> float:
        t0 = time.monotonic()
        self.request(Cmd.PING)
        return (time.monotonic() - t0) * 1000.0      # ms

    def version(self) -> tuple[int, int, int, int]:
        d = self.request(Cmd.GET_VERSION).data
        return d[0], d[1], d[2], d[3]

    def led(self, idx: int, state: int) -> int:
        return self.request(Cmd.LED_SET, bytes([idx, state])).data[0]

    def led_mask(self) -> int:
        return self.request(Cmd.LED_GET).data[0]

    def pwm(self, ch: int, permille: int) -> None:
        self.request(Cmd.PWM_SET, bytes([ch]) + struct.pack("<H", permille))

    def adc(self, ch: int = 0) -> tuple[int, int]:
        d = self.request(Cmd.ADC_READ, bytes([ch])).data
        raw, mv = struct.unpack_from("<HH", d, 1)
        return raw, mv

    def dht(self) -> tuple[float, float]:
        d = self.request(Cmd.DHT_READ, timeout=1.5).data
        if d[0] != Err.OK:
            raise RuntimeError(ERR_TEXT.get(Err(d[0]), "Cảm biến lỗi"))
        t, h = struct.unpack_from("<hH", d, 1)
        return t / 10.0, h / 10.0

    def stream_start(self, src: int, rate_hz: int, batch: int) -> None:
        self.request(Cmd.STREAM_START,
                     bytes([src]) + struct.pack("<H", rate_hz) + bytes([batch]))

    def stream_stop(self) -> None:
        self.request(Cmd.STREAM_STOP)

    def stats(self) -> dict:
        d = self.request(Cmd.STATS_GET).data
        k = ("rx_bytes", "rx_ok", "err_cks", "err_len",
             "err_timeout", "resync", "tx_frames", "tx_drop")
        return dict(zip(k, struct.unpack("<8I", d[:32])))

    def stats_reset(self) -> None:
        self.request(Cmd.STATS_RESET)


# ===========================================================================
# Tự kiểm tra khi chạy trực tiếp: python v01proto.py
# ===========================================================================
if __name__ == "__main__":
    assert crc16(b"123456789") == 0x29B1, "CRC16 sai"
    assert crc8(b"123456789") == 0xF4, "CRC8 sai"

    p = Parser()
    frames = list(p.feed(b"\x00\xff" + build(9, Cmd.ECHO, b"\xaa\x55hello")))
    assert len(frames) == 1 and frames[0].data == b"\xaa\x55hello", frames

    bad = bytearray(build(1, Cmd.ECHO, b"abcd"))
    bad[6] ^= 0x01
    assert not list(Parser().feed(bytes(bad))), "khung hỏng vẫn lọt"

    print("v01proto: tất cả kiểm tra nội bộ ĐẠT "
          f"(CRC16('123456789') = 0x{crc16(b'123456789'):04X})")
