"""
gui.py — giao diện điều khiển board và vẽ đồ thị dữ liệu thời gian thực.

Phụ thuộc: pyserial, PySide6, pyqtgraph, numpy.
Chạy: python gui.py

Cổng serial được đọc trong luồng nền của lớp Device; dữ liệu chuyển sang
luồng giao diện qua Signal của Qt. Mẫu nhận được lưu vào bộ đệm vòng và đồ
thị được vẽ lại theo chu kỳ 30 ms.
"""
from __future__ import annotations

import collections
import sys
import time

import numpy as np
import pyqtgraph as pg
from PySide6 import QtCore, QtWidgets
from serial.tools import list_ports

import v01proto as P

PLOT_POINTS = 2000


class Bridge(QtCore.QObject):
    """Chuyển dữ liệu từ luồng serial sang luồng giao diện."""
    stream = QtCore.Signal(int, object)
    frame = QtCore.Signal(object)


class MainWindow(QtWidgets.QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Giao thức khung UART giữa PC và STM32")
        self.resize(1180, 720)

        self.dev: P.Device | None = None
        self.bridge = Bridge()
        self.bridge.stream.connect(self._on_stream)

        self.ring = collections.deque(maxlen=PLOT_POINTS)
        self.first_idx: int | None = None
        self.last_idx = -1
        self.gap_count = 0
        self.sample_count = 0
        self.t_rate = time.monotonic()
        self.rate_shown = 0.0

        self._build_ui()

        self.redraw = QtCore.QTimer(self, interval=30, timeout=self._redraw)
        self.poll = QtCore.QTimer(self, interval=1000, timeout=self._refresh_stats)

    # ------------------------------------------------------------------
    def _build_ui(self) -> None:
        central = QtWidgets.QWidget()
        self.setCentralWidget(central)
        root = QtWidgets.QHBoxLayout(central)

        side = QtWidgets.QVBoxLayout()
        side.setSpacing(8)
        root.addLayout(side, 0)

        # ---- kết nối --------------------------------------------------
        gb = QtWidgets.QGroupBox("Kết nối")
        f = QtWidgets.QFormLayout(gb)
        self.cb_port = QtWidgets.QComboBox()
        self.cb_baud = QtWidgets.QComboBox()
        self.cb_baud.addItems(["115200", "230400", "460800", "921600"])
        self.btn_scan = QtWidgets.QPushButton("Quét cổng")
        self.btn_conn = QtWidgets.QPushButton("Kết nối")
        self.btn_conn.setCheckable(True)
        f.addRow("Cổng", self.cb_port)
        f.addRow("Baud", self.cb_baud)
        f.addRow(self.btn_scan, self.btn_conn)
        self.lbl_ver = QtWidgets.QLabel("—")
        f.addRow("Firmware", self.lbl_ver)
        side.addWidget(gb)
        self.btn_scan.clicked.connect(self._scan)
        self.btn_conn.toggled.connect(self._toggle_conn)

        # ---- điều khiển ----------------------------------------------
        gb = QtWidgets.QGroupBox("Điều khiển")
        v = QtWidgets.QVBoxLayout(gb)
        row = QtWidgets.QHBoxLayout()
        self.led_btns = []
        for i in range(4):
            b = QtWidgets.QPushButton(f"LED{i}")
            b.setCheckable(True)
            b.toggled.connect(lambda on, k=i: self._led(k, on))
            row.addWidget(b)
            self.led_btns.append(b)
        v.addLayout(row)

        for ch in range(2):
            h = QtWidgets.QHBoxLayout()
            s = QtWidgets.QSlider(QtCore.Qt.Horizontal, minimum=0, maximum=1000)
            lab = QtWidgets.QLabel("0.0 %")
            s.valueChanged.connect(
                lambda val, c=ch, l=lab: (l.setText(f"{val/10:.1f} %"),
                                          self._pwm(c, val)))
            h.addWidget(QtWidgets.QLabel(f"PWM{ch}"))
            h.addWidget(s)
            h.addWidget(lab)
            v.addLayout(h)

        h = QtWidgets.QHBoxLayout()
        b_ping = QtWidgets.QPushButton("Ping")
        b_adc = QtWidgets.QPushButton("Đọc ADC")
        b_dht = QtWidgets.QPushButton("Đọc DHT11")
        b_ping.clicked.connect(self._ping)
        b_adc.clicked.connect(self._adc)
        b_dht.clicked.connect(self._dht)
        for b in (b_ping, b_adc, b_dht):
            h.addWidget(b)
        v.addLayout(h)
        side.addWidget(gb)

        # ---- streaming ------------------------------------------------
        gb = QtWidgets.QGroupBox("Truyền liên tục (streaming)")
        f = QtWidgets.QFormLayout(gb)
        self.cb_src = QtWidgets.QComboBox()
        self.cb_src.addItems(["ADC 1 kênh (PA0)", "ADC 2 kênh (PA0+PA1)",
                              "Sóng sin thử nghiệm"])
        self.sp_rate = QtWidgets.QSpinBox(minimum=1, maximum=20000, value=100)
        self.sp_rate.setSuffix(" Hz")
        self.sp_batch = QtWidgets.QSpinBox(minimum=1, maximum=60, value=20)
        self.sp_batch.setSuffix(" mẫu/khung")
        self.btn_stream = QtWidgets.QPushButton("Bắt đầu")
        self.btn_stream.setCheckable(True)
        self.btn_stream.toggled.connect(self._stream)
        f.addRow("Nguồn", self.cb_src)
        f.addRow("Tần số", self.sp_rate)
        f.addRow("Gom", self.sp_batch)
        f.addRow(self.btn_stream)
        self.lbl_rate = QtWidgets.QLabel("— mẫu/s")
        self.lbl_gap = QtWidgets.QLabel("0 chỗ đứt")
        f.addRow("Thực đo", self.lbl_rate)
        f.addRow("Mất mẫu", self.lbl_gap)
        side.addWidget(gb)

        # ---- thống kê -------------------------------------------------
        gb = QtWidgets.QGroupBox("Thống kê")
        vv = QtWidgets.QVBoxLayout(gb)
        self.txt_stats = QtWidgets.QPlainTextEdit(readOnly=True)
        self.txt_stats.setMaximumHeight(170)
        self.txt_stats.setStyleSheet("font-family: monospace; font-size: 11px;")
        vv.addWidget(self.txt_stats)
        b_rst = QtWidgets.QPushButton("Xoá bộ đếm")
        b_rst.clicked.connect(self._stats_reset)
        vv.addWidget(b_rst)
        side.addWidget(gb)
        side.addStretch(1)

        # ---- đồ thị + nhật ký ----------------------------------------
        right = QtWidgets.QVBoxLayout()
        root.addLayout(right, 1)

        pg.setConfigOptions(antialias=True)
        self.plot = pg.PlotWidget()
        self.plot.setLabel("left", "Giá trị ADC", units="LSB")
        self.plot.setLabel("bottom", "Mẫu")
        self.plot.showGrid(x=True, y=True, alpha=0.3)
        self.plot.setYRange(0, 4095)
        self.curve = self.plot.plot(pen=pg.mkPen(width=1.6))
        right.addWidget(self.plot, 3)

        self.log = QtWidgets.QPlainTextEdit(readOnly=True, maximumBlockCount=500)
        self.log.setStyleSheet("font-family: monospace; font-size: 11px;")
        right.addWidget(self.log, 1)

        self._scan()

    # ------------------------------------------------------------------
    def _say(self, msg: str) -> None:
        self.log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {msg}")

    def _guard(self) -> bool:
        if self.dev is None:
            self._say("Chưa kết nối.")
            return False
        return True

    def _scan(self) -> None:
        self.cb_port.clear()
        self.cb_port.addItems([p.device for p in list_ports.comports()])

    # ------------------------------------------------------------------
    def _toggle_conn(self, on: bool) -> None:
        if on:
            try:
                self.dev = P.Device(self.cb_port.currentText(),
                                    int(self.cb_baud.currentText()))
                self.dev.on_stream = lambda i, v: self.bridge.stream.emit(i, v)
                pv, ck, ma, mi = self.dev.version()
                names = {0: "SUM8", 1: "CRC8", 2: "CRC16"}
                self.lbl_ver.setText(
                    f"giao thức v{pv >> 4}.{pv & 15} · {names.get(ck, '?')} "
                    f"· fw {ma}.{mi}")
                self._say(f"Đã kết nối {self.cb_port.currentText()} — ping "
                          f"{self.dev.ping():.1f} ms")
                self.btn_conn.setText("Ngắt")
                self.redraw.start()
                self.poll.start()
            except Exception as e:
                self._say(f"LỖI kết nối: {e}")
                self.dev = None
                self.btn_conn.setChecked(False)
        else:
            self.redraw.stop()
            self.poll.stop()
            if self.dev:
                try:
                    self.dev.close()
                except Exception:
                    pass
            self.dev = None
            self.btn_conn.setText("Kết nối")
            self._say("Đã ngắt kết nối.")

    # ------------------------------------------------------------------
    def _led(self, idx: int, on: bool) -> None:
        if not self._guard():
            return
        try:
            m = self.dev.led(idx, 1 if on else 0)
            self._say(f"LED{idx} = {int(on)} → mask 0b{m:04b}")
        except Exception as e:
            self._say(f"LỖI LED: {e}")

    def _pwm(self, ch: int, val: int) -> None:
        if self.dev is None:
            return
        try:
            self.dev.pwm(ch, val)
        except Exception as e:
            self._say(f"LỖI PWM: {e}")

    def _ping(self) -> None:
        if self._guard():
            try:
                self._say(f"Ping khứ hồi: {self.dev.ping():.2f} ms")
            except Exception as e:
                self._say(f"LỖI ping: {e}")

    def _adc(self) -> None:
        if self._guard():
            try:
                raw, mv = self.dev.adc(0)
                self._say(f"ADC0 = {raw} LSB ({mv} mV)")
            except Exception as e:
                self._say(f"LỖI ADC: {e}")

    def _dht(self) -> None:
        if self._guard():
            try:
                t, h = self.dev.dht()
                self._say(f"DHT11: {t:.1f} °C, {h:.1f} %RH")
            except Exception as e:
                self._say(f"LỖI DHT: {e}")

    # ------------------------------------------------------------------
    def _stream(self, on: bool) -> None:
        if self.dev is None:
            self.btn_stream.setChecked(False)
            return
        try:
            if on:
                self.ring.clear()
                self.first_idx = None
                self.last_idx = -1
                self.gap_count = 0
                self.sample_count = 0
                self.t_rate = time.monotonic()
                self.dev.stream_start(self.cb_src.currentIndex(),
                                      self.sp_rate.value(),
                                      self.sp_batch.value())
                self.btn_stream.setText("Dừng")
                self._say(f"Bắt đầu streaming {self.sp_rate.value()} Hz, "
                          f"{self.sp_batch.value()} mẫu/khung")
            else:
                self.dev.stream_stop()
                self.btn_stream.setText("Bắt đầu")
                self._say("Đã dừng streaming.")
        except Exception as e:
            self._say(f"LỖI streaming: {e}")
            self.btn_stream.setChecked(False)

    def _on_stream(self, idx: int, vals: list) -> None:
        """Nhận một khung STREAM_DATA (chạy trong luồng giao diện)."""
        if self.first_idx is None:
            self.first_idx = idx
        elif idx != self.last_idx + 1:
            self.gap_count += 1          # chỉ số không liên tục: mất khung
        self.last_idx = idx + len(vals) - 1
        self.ring.extend(vals)
        self.sample_count += len(vals)

    def _redraw(self) -> None:
        if self.ring:
            self.curve.setData(np.fromiter(self.ring, dtype=np.uint16))
        now = time.monotonic()
        dt = now - self.t_rate
        if dt >= 1.0:
            self.rate_shown = self.sample_count / dt
            self.sample_count = 0
            self.t_rate = now
            self.lbl_rate.setText(f"{self.rate_shown:.0f} mẫu/s")
            self.lbl_gap.setText(f"{self.gap_count} chỗ đứt")

    # ------------------------------------------------------------------
    def _refresh_stats(self) -> None:
        if self.dev is None:
            return
        try:
            s = self.dev.stats()
        except Exception:
            return
        p = self.dev.parser.stats
        tot = s["rx_ok"] + s["err_cks"] + s["err_len"] + s["err_timeout"]
        good = (100.0 * s["rx_ok"] / tot) if tot else 100.0
        self.txt_stats.setPlainText(
            "TRÊN BOARD (khung PC -> board)\n"
            f"  byte nhận   : {s['rx_bytes']}\n"
            f"  khung đúng  : {s['rx_ok']}   ({good:.3f} %)\n"
            f"  sai CRC     : {s['err_cks']}\n"
            f"  sai LEN     : {s['err_len']}\n"
            f"  quá hạn     : {s['err_timeout']}\n"
            f"  tự đồng bộ  : {s['resync']}\n"
            f"  khung đã gửi: {s['tx_frames']}  (bỏ {s['tx_drop']})\n"
            "TRÊN PC (khung board -> PC)\n"
            f"  khung đúng  : {p.rx_ok}\n"
            f"  sai CRC     : {p.err_cks}   tự đồng bộ: {p.resync}")

    def _stats_reset(self) -> None:
        if self._guard():
            try:
                self.dev.stats_reset()
                self.dev.parser.stats = P.Stats()
                self._say("Đã xoá bộ đếm hai phía.")
            except Exception as e:
                self._say(f"LỖI: {e}")

    def closeEvent(self, e) -> None:
        if self.dev:
            try:
                self.dev.close()
            except Exception:
                pass
        e.accept()


def main() -> None:
    app = QtWidgets.QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
