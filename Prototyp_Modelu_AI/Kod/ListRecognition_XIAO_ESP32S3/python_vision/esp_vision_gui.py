import sys
import time
import threading
import re
from pathlib import Path

import serial
from serial.tools import list_ports
from PIL import Image

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QPixmap, QImage, QFont
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QLabel, QPushButton, QComboBox,
    QFileDialog, QVBoxLayout, QHBoxLayout, QGridLayout, QFrame, QTextEdit,
    QProgressBar, QMessageBox, QSizePolicy
)

# -------------------- USTAWIENIA ESP32 --------------------
DEFAULT_PORT = "/dev/cu.usbmodem1101"
BAUD = 115200
WIDTH = 240
HEIGHT = 240
CHUNK_SIZE = 64
CHUNK_DELAY = 0.002
IMAGE_SIZE = WIDTH * HEIGHT * 3


def image_to_pixmap(image: Image.Image) -> QPixmap:
    image = image.convert("RGB")
    data = image.tobytes("raw", "RGB")
    qimage = QImage(data, image.width, image.height, image.width * 3,
                    QImage.Format.Format_RGB888).copy()
    return QPixmap.fromImage(qimage)


class TransferWorker(QThread):
    log = Signal(str)
    progress = Signal(int, str)
    prepared_image = Signal(object)
    classification = Signal(str)
    probability = Signal(str, float)
    status = Signal(str)
    finished_ok = Signal()
    failed = Signal(str)

    def __init__(self, port: str, image_path: str):
        super().__init__()
        self.port = port
        self.image_path = image_path
        self._stop_requested = False

    def run(self):
        try:
            self.status.emit("Przygotowywanie obrazu")
            original = Image.open(self.image_path).convert("RGB")
            image = original.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
            raw = image.tobytes()
            self.prepared_image.emit(image.copy())

            if len(raw) != IMAGE_SIZE:
                raise RuntimeError(
                    f"Nieprawidłowy rozmiar: {len(raw)} B; oczekiwano {IMAGE_SIZE} B."
                )

            self.log.emit(f"[INFO] Port: {self.port}")
            self.log.emit(f"[INFO] Obraz: {WIDTH}x{HEIGHT} RGB")
            self.log.emit(f"[INFO] Rozmiar: {len(raw):,} bajtów".replace(",", " "))
            self.status.emit("Łączenie z ESP32")

            with serial.Serial(self.port, BAUD, timeout=1, write_timeout=30) as ser:
                time.sleep(2)
                ser.reset_input_buffer()
                ser.write(f"IMG {len(raw)}\n".encode("ascii"))
                ser.flush()

                deadline = time.monotonic() + 120
                ready = False
                while time.monotonic() < deadline:
                    line = ser.readline().decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    self.log.emit(f"[ESP32] {line}")

                    if line.startswith("ERROR,"):
                        raise RuntimeError(line)

                    if line.startswith("READY,"):
                        parts = line.split(",")
                        if len(parts) >= 3:
                            board_width, board_height = int(parts[1]), int(parts[2])
                            if (board_width, board_height) != (WIDTH, HEIGHT):
                                raise RuntimeError(
                                    f"ESP32 oczekuje {board_width}x{board_height}, "
                                    f"a obraz ma {WIDTH}x{HEIGHT}."
                                )
                        ready = True
                        break

                if not ready:
                    raise TimeoutError("Brak odpowiedzi READY od ESP32.")

                self.log.emit("[INFO] Rozpoczynam przesyłanie obrazu.")
                self.status.emit("Przesyłanie obrazu")
                sent = 0
                started = time.monotonic()
                last_update = 0.0

                while sent < len(raw):
                    chunk = raw[sent:sent + CHUNK_SIZE]
                    position = 0
                    while position < len(chunk):
                        written = ser.write(chunk[position:])
                        if written is None or written <= 0:
                            raise RuntimeError("Nie udało się wysłać danych.")
                        position += written
                        sent += written

                    now = time.monotonic()
                    if now - last_update >= 0.1 or sent == len(raw):
                        elapsed = max(now - started, 0.001)
                        speed = sent / elapsed
                        remaining = (len(raw) - sent) / speed if speed else 0
                        percent = int(sent * 100 / len(raw))
                        detail = (
                            f"{percent}%  •  {sent:,} / {len(raw):,} B  •  "
                            f"{speed / 1024:.1f} KB/s  •  pozostało {remaining:.1f} s"
                        ).replace(",", " ")
                        self.progress.emit(percent, detail)
                        last_update = now
                    time.sleep(CHUNK_DELAY)

                ser.flush()
                self.log.emit("[INFO] Wysyłanie zakończone. Czekam na klasyfikację...")
                self.status.emit("Oczekiwanie na wynik AI")
                deadline = time.monotonic() + 180

                while time.monotonic() < deadline:
                    line = ser.readline().decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    self.log.emit(f"[ESP32] {line}")

                    if line.startswith("ERROR,"):
                        raise RuntimeError(line)

                    if line == "FINISHED":
                        self.log.emit("[INFO] Zakończono.")
                        self.status.emit("Gotowe")
                        self.progress.emit(100, "Transfer zakończony")
                        self.finished_ok.emit()
                        return

                    # Obsługiwane formaty, np.:
                    # CLASS,Letter,92.4
                    # Letter: 92.4%
                    # Letter = 0.924
                    # Predicted class: Letter, confidence: 0.924
                    parsed = self.parse_probability(line)
                    if parsed:
                        label, confidence = parsed
                        self.probability.emit(label, confidence)
                        self.classification.emit(f"{label} — {confidence:.1f}%")
                    elif not line.startswith(("READY,", "INFO,", "DEBUG,")):
                        self.classification.emit(line)

                raise TimeoutError("ESP32 nie przesłało komunikatu FINISHED w ciągu 180 s.")

        except Exception as exc:
            self.failed.emit(str(exc))
            self.status.emit("Błąd")

    @staticmethod
    def parse_probability(line: str):
        """Rozpoznaje popularne formaty etykiety i confidence przesyłane przez ESP32."""
        text = line.strip()

        # Format CSV: CLASS,Letter,92.4 lub RESULT,Letter,0.924
        csv_match = re.match(
            r"^(?:CLASS|RESULT|PREDICTION),\s*([^,]+),\s*(-?\d+(?:[.,]\d+)?)\s*%?$",
            text, re.IGNORECASE
        )
        if csv_match:
            label = csv_match.group(1).strip()
            value = float(csv_match.group(2).replace(",", "."))
            if 0.0 <= value <= 1.0:
                value *= 100.0
            if 0.0 <= value <= 100.0:
                return label, value

        # Format: Nazwa klasy: 92%, Nazwa klasy = 0.92
        simple_match = re.match(
            r"^(.+?)\s*[:=]\s*(-?\d+(?:[.,]\d+)?)\s*(%?)$",
            text, re.IGNORECASE
        )
        if simple_match:
            label = simple_match.group(1).strip()
            value = float(simple_match.group(2).replace(",", "."))
            has_percent = simple_match.group(3) == "%"
            if not has_percent and 0.0 <= value <= 1.0:
                value *= 100.0
            if label and 0.0 <= value <= 100.0:
                return label, value

        # Format np. "Predicted class: Letter, confidence: 0.924"
        confidence_match = re.search(
            r"(?:class|label|prediction)\s*[:=]\s*([^,;]+).*?"
            r"(?:confidence|probability|score|pewnosc|prawdopodobienstwo)\s*[:=]\s*(\d+(?:[.,]\d+)?)\s*(%?)",
            text, re.IGNORECASE
        )
        if confidence_match:
            label = confidence_match.group(1).strip()
            value = float(confidence_match.group(2).replace(",", "."))
            if confidence_match.group(3) != "%" and 0.0 <= value <= 1.0:
                value *= 100.0
            if label and 0.0 <= value <= 100.0:
                return label, value

        return None


class ImagePanel(QFrame):
    def __init__(self, title: str, subtitle: str):
        super().__init__()
        self.setObjectName("imagePanel")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(16, 16, 16, 16)
        layout.setSpacing(8)

        heading = QHBoxLayout()
        self.title_label = QLabel(title)
        self.title_label.setObjectName("panelTitle")
        heading.addWidget(self.title_label)
        heading.addStretch()
        self.tag = QLabel("BRAK OBRAZU")
        self.tag.setObjectName("tag")
        heading.addWidget(self.tag)
        layout.addLayout(heading)

        self.preview = QLabel("Wybierz zdjęcie, aby wyświetlić podgląd")
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumSize(220, 220)
        self.preview.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.preview.setObjectName("preview")
        layout.addWidget(self.preview, 1)

        self.caption = QLabel(subtitle)
        self.caption.setObjectName("muted")
        self.caption.setWordWrap(True)
        layout.addWidget(self.caption)
        self._pixmap = None

    def set_image(self, image: Image.Image, tag: str, caption: str):
        self._pixmap = image_to_pixmap(image)
        self.tag.setText(tag)
        self.caption.setText(caption)
        self._scale_image()

    def set_placeholder(self, text: str):
        self._pixmap = None
        self.preview.setPixmap(QPixmap())
        self.preview.setText(text)
        self.tag.setText("BRAK OBRAZU")

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._scale_image()

    def _scale_image(self):
        if self._pixmap:
            self.preview.setText("")
            self.preview.setPixmap(self._pixmap.scaled(
                self.preview.size(),
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation
            ))


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ESP Vision • Panel transmisji AI")
        self.resize(1240, 900)
        self.image_path = None
        self.worker = None
        self._build_ui()
        self.refresh_ports()
        self._style()

    def _build_ui(self):
        root = QWidget()
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(26, 22, 26, 24)
        outer.setSpacing(18)

        header = QHBoxLayout()
        brand = QVBoxLayout()
        eyebrow = QLabel("ESP32  /  COMPUTER VISION")
        eyebrow.setObjectName("eyebrow")
        title = QLabel("ESP Vision")
        title.setObjectName("mainTitle")
        subtitle = QLabel("Przesyłanie obrazu i monitorowanie klasyfikacji w czasie rzeczywistym")
        subtitle.setObjectName("muted")
        brand.addWidget(eyebrow)
        brand.addWidget(title)
        brand.addWidget(subtitle)
        header.addLayout(brand)
        header.addStretch()

        self.status_pill = QLabel("●  GOTOWY DO PRACY")
        self.status_pill.setObjectName("statusPill")
        header.addWidget(self.status_pill, 0, Qt.AlignmentFlag.AlignVCenter)
        outer.addLayout(header)

        controls = QFrame()
        controls.setObjectName("controlPanel")
        control_layout = QHBoxLayout(controls)
        control_layout.setContentsMargins(16, 14, 16, 14)
        control_layout.setSpacing(10)

        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(230)
        self.refresh_button = QPushButton("↻  Porty")
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.choose_button = QPushButton("＋  Wybierz zdjęcie")
        self.choose_button.setObjectName("secondaryButton")
        self.choose_button.clicked.connect(self.choose_image)
        self.start_button = QPushButton("▶  Wyślij do ESP32")
        self.start_button.setObjectName("primaryButton")
        self.start_button.clicked.connect(self.start_transfer)

        control_layout.addWidget(QLabel("PORT SZEREGOWY"))
        control_layout.addWidget(self.port_combo)
        control_layout.addWidget(self.refresh_button)
        control_layout.addSpacing(8)
        control_layout.addWidget(self.choose_button)
        control_layout.addStretch()
        control_layout.addWidget(self.start_button)
        outer.addWidget(controls)

        self.selected_label = QLabel("Nie wybrano pliku")
        self.selected_label.setObjectName("muted")
        outer.addWidget(self.selected_label)

        images = QGridLayout()
        images.setSpacing(16)
        self.source_panel = ImagePanel("01  /  Zdjęcie źródłowe", "Oryginalny plik wybrany z komputera.")
        self.sent_panel = ImagePanel("02  /  Obraz wysyłany", "Podgląd obrazu po konwersji do RGB i zmianie rozmiaru na 240 × 240 px.")
        images.addWidget(self.source_panel, 0, 0)
        images.addWidget(self.sent_panel, 0, 1)
        outer.addLayout(images, 3)

        transfer = QFrame()
        transfer.setObjectName("controlPanel")
        transfer_layout = QVBoxLayout(transfer)
        transfer_layout.setContentsMargins(16, 14, 16, 14)
        transfer_layout.setSpacing(9)
        top = QHBoxLayout()
        lbl = QLabel("TRANSMISJA DANYCH")
        lbl.setObjectName("sectionLabel")
        self.progress_text = QLabel("Oczekiwanie na rozpoczęcie")
        self.progress_text.setObjectName("muted")
        top.addWidget(lbl)
        top.addStretch()
        top.addWidget(self.progress_text)
        transfer_layout.addLayout(top)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(9)
        transfer_layout.addWidget(self.progress)
        outer.addWidget(transfer)

        bottom = QGridLayout()
        bottom.setSpacing(16)

        terminal_frame = QFrame()
        terminal_frame.setObjectName("terminalPanel")
        terminal_layout = QVBoxLayout(terminal_frame)
        terminal_layout.setContentsMargins(16, 14, 16, 14)
        terminal_header = QHBoxLayout()
        terminal_title = QLabel("TERMINAL URZĄDZENIA")
        terminal_title.setObjectName("sectionLabel")
        self.clear_button = QPushButton("Wyczyść")
        self.clear_button.setObjectName("textButton")
        self.clear_button.clicked.connect(lambda: self.terminal.clear())
        terminal_header.addWidget(terminal_title)
        terminal_header.addStretch()
        terminal_header.addWidget(self.clear_button)
        terminal_layout.addLayout(terminal_header)
        self.terminal = QTextEdit()
        self.terminal.setReadOnly(True)
        self.terminal.setMinimumHeight(190)
        self.terminal.setPlaceholderText("Logi ESP32 i Pythona pojawią się tutaj…")
        self.terminal.setObjectName("terminal")
        terminal_layout.addWidget(self.terminal)
        bottom.addWidget(terminal_frame, 0, 0)

        result_frame = QFrame()
        result_frame.setObjectName("resultPanel")
        result_layout = QVBoxLayout(result_frame)
        result_layout.setContentsMargins(18, 16, 18, 16)
        result_layout.setSpacing(10)
        result_heading = QLabel("03  /  WYNIK KLASYFIKACJI")
        result_heading.setObjectName("sectionLabel")
        result_layout.addWidget(result_heading)
        self.result_title = QLabel("Oczekiwanie na analizę")
        self.result_title.setObjectName("resultTitle")
        self.result_title.setWordWrap(True)
        result_layout.addWidget(self.result_title)
        self.result_detail = QLabel("Po zakończeniu pracy ESP32 wynik zostanie pokazany w tym miejscu.")
        self.result_detail.setObjectName("muted")
        self.result_detail.setWordWrap(True)
        result_layout.addWidget(self.result_detail)
        self.probabilities_label = QLabel("Procenty pojawią się, jeśli ESP32 prześle wyniki modelu.")
        self.probabilities_label.setObjectName("probabilities")
        self.probabilities_label.setWordWrap(True)
        self.probabilities_label.setTextFormat(Qt.TextFormat.RichText)
        self.probabilities_label.setMinimumHeight(150)
        result_layout.addWidget(self.probabilities_label)
        result_layout.addStretch()
        self.result_status = QLabel("●  BRAK WYNIKU")
        self.result_status.setObjectName("resultBadge")
        result_layout.addWidget(self.result_status)
        bottom.addWidget(result_frame, 0, 1)
        bottom.setColumnStretch(0, 3)
        bottom.setColumnStretch(1, 2)
        outer.addLayout(bottom, 2)

        footer = QLabel("LOCAL CONNECTION  •  USB SERIAL  •  115200 BAUD")
        footer.setObjectName("footer")
        outer.addWidget(footer)

    def _style(self):
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background: #F6F8FB;
                color: #172033;
                font-family: "Helvetica Neue", Arial, sans-serif;
                font-size: 13px;
            }
            QLabel#eyebrow, QLabel#sectionLabel, QLabel#footer {
                color: #7B879A; font-size: 10px; font-weight: 700; letter-spacing: 1px;
            }
            QLabel#mainTitle { font-size: 32px; font-weight: 750; color: #121A2B; }
            QLabel#muted { color: #788397; font-size: 12px; }
            QLabel#panelTitle { color: #182237; font-size: 14px; font-weight: 700; }
            QLabel#tag {
                background: #EDF1F6; color: #788397; border-radius: 8px;
                padding: 5px 8px; font-size: 9px; font-weight: 700;
            }
            QLabel#statusPill {
                background: #E8F7EF; color: #168452; padding: 10px 14px;
                border-radius: 12px; font-size: 10px; font-weight: 700;
            }
            QFrame#controlPanel, QFrame#imagePanel, QFrame#terminalPanel, QFrame#resultPanel {
                background: #FFFFFF; border: 1px solid #E7EBF2; border-radius: 16px;
            }
            QLabel#preview {
                background: #F7F9FC; border: 1px dashed #D9E0EA; border-radius: 12px;
                color: #98A2B3; padding: 8px;
            }
            QComboBox {
                background: #F8FAFD; border: 1px solid #E1E6EF; border-radius: 9px;
                padding: 10px 12px; min-height: 18px; color: #263248;
            }
            QPushButton {
                background: #F0F3F8; color: #344056; border: 1px solid #E2E7EF;
                border-radius: 10px; padding: 10px 14px; font-weight: 650;
            }
            QPushButton:hover { background: #E8EDF5; }
            QPushButton:disabled { color: #AAB2C0; background: #F4F6F9; }
            QPushButton#primaryButton {
                background: #2563EB; color: white; border: none; padding: 12px 18px;
            }
            QPushButton#primaryButton:hover { background: #1D4ED8; }
            QPushButton#secondaryButton { background: #FFFFFF; }
            QPushButton#textButton { border: none; background: transparent; color: #6F7B90; padding: 5px; }
            QProgressBar { background: #E9EEF5; border: none; border-radius: 4px; }
            QProgressBar::chunk { background: #3478F6; border-radius: 4px; }
            QTextEdit#terminal {
                background: #FBFCFE; border: 1px solid #EDF0F5; border-radius: 10px;
                padding: 10px; color: #354156; font-family: Menlo, Consolas, monospace;
                font-size: 11px;
            }
            QLabel#resultTitle { font-size: 22px; font-weight: 750; color: #172033; }
            QLabel#probabilities {
                background: #F7F9FC; color: #172033; border: 1px solid #E7ECF3;
                border-radius: 12px; padding: 12px; font-size: 12px;
            }
            QLabel#resultBadge {
                color: #7B879A; background: #F0F3F8; border-radius: 8px;
                padding: 8px 10px; font-size: 10px; font-weight: 700;
            }
            QLabel#footer { font-size: 9px; }
        """)

    def refresh_ports(self):
        current = self.port_combo.currentText() or DEFAULT_PORT
        ports = [p.device for p in list_ports.comports()]
        self.port_combo.clear()
        self.port_combo.addItems(ports)
        if current in ports:
            self.port_combo.setCurrentText(current)
        elif DEFAULT_PORT in ports:
            self.port_combo.setCurrentText(DEFAULT_PORT)
        if not ports:
            self.port_combo.addItem(DEFAULT_PORT)
            self.log("[WARN] Nie wykryto portów. Sprawdź kabel USB i uprawnienia macOS.")

    def choose_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Wybierz zdjęcie", str(Path.home() / "Desktop"),
            "Pliki graficzne (*.png *.jpg *.jpeg *.bmp *.webp)"
        )
        if not path:
            return
        try:
            image = Image.open(path).convert("RGB")
            self.image_path = path
            self.class_probabilities = {}
            self.source_panel.set_image(image, "ORYGINAŁ", f"{image.width} × {image.height} px")
            prepared = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
            self.sent_panel.set_image(prepared, "GOTOWY DO WYSŁANIA", f"{WIDTH} × {HEIGHT} px • RGB • {IMAGE_SIZE:,} B".replace(",", " "))
            self.selected_label.setText(f"Plik: {path}")
            self.result_title.setText("Oczekiwanie na analizę")
            self.result_detail.setText("Kliknij „Wyślij do ESP32”, aby rozpocząć transmisję i odebrać wynik.")
            self.result_status.setText("●  GOTOWY")
            self.probabilities_label.setText("Procenty pojawią się, jeśli ESP32 prześle wyniki modelu.")
            self.log(f"[INFO] Wybrano zdjęcie: {path}")
        except Exception as exc:
            QMessageBox.critical(self, "Nie można otworzyć zdjęcia", str(exc))

    def start_transfer(self):
        if not self.image_path:
            QMessageBox.information(self, "Brak zdjęcia", "Najpierw wybierz zdjęcie z komputera.")
            return
        if self.worker and self.worker.isRunning():
            return
        port = self.port_combo.currentText().strip()
        if not port:
            QMessageBox.warning(self, "Brak portu", "Wybierz port szeregowy ESP32.")
            return

        self.class_probabilities = {}
        self.progress.setValue(0)
        self.progress_text.setText("Uruchamianie…")
        self.result_title.setText("Analiza w toku")
        self.result_detail.setText("Obraz jest przesyłany do ESP32. Czekam na wynik klasyfikacji.")
        self.result_status.setText("●  W TRAKCIE")
        self.probabilities_label.setText("Oczekiwanie na prawdopodobieństwa z modelu AI…")
        self.status_pill.setText("●  PRACA W TOKU")
        self.start_button.setEnabled(False)
        self.choose_button.setEnabled(False)
        self.worker = TransferWorker(port, self.image_path)
        self.worker.log.connect(self.log)
        self.worker.progress.connect(self.update_progress)
        self.worker.prepared_image.connect(
            lambda img: self.sent_panel.set_image(img, "WYSYŁANY", "Obraz przygotowany do transmisji: RGB, 240 × 240 px.")
        )
        self.worker.classification.connect(self.show_classification)
        self.worker.probability.connect(self.show_probability)
        self.worker.status.connect(self.set_status)
        self.worker.finished_ok.connect(self.transfer_finished)
        self.worker.failed.connect(self.transfer_failed)
        self.worker.start()

    def log(self, message: str):
        self.terminal.append(message)
        scrollbar = self.terminal.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def update_progress(self, percent: int, detail: str):
        self.progress.setValue(percent)
        self.progress_text.setText(detail)

    def set_status(self, status: str):
        self.status_pill.setText(f"●  {status.upper()}")

    def show_classification(self, result: str):
        # Nie nadpisuj czytelnej listy prawdopodobieństw komunikatem RESULT.
        if not self.class_probabilities:
            self.result_title.setText(result)
        self.result_detail.setText("Wyniki odebrane z modelu klasyfikacji ESP32.")
        self.result_status.setText("●  ODEBRANO Z ESP32")

    def show_probability(self, label: str, confidence: float):
        self.class_probabilities[label] = max(0.0, min(100.0, confidence))
        ranked = sorted(self.class_probabilities.items(), key=lambda item: item[1], reverse=True)
        best_label, best_score = ranked[0]
        self.result_title.setText(best_label.replace("_", " ").title())
        self.result_detail.setText(f"Najwyższe prawdopodobieństwo: {best_score:.2f}%")

        rows = [
            '<div style="font-size:11px;color:#6F7B90;font-weight:700;letter-spacing:1px;margin-bottom:10px;">PRAWDOPODOBIEŃSTWO DLA KAŻDEJ KLASY</div>'
        ]
        for index, (class_name, score) in enumerate(ranked):
            safe_name = (class_name.replace("&", "&amp;").replace("<", "&lt;")
                         .replace(">", "&gt;").replace("_", " "))
            is_best = index == 0
            bar = "#2563EB" if is_best else "#93A9CB"
            tag = '<span style="color:#2563EB;font-size:10px;font-weight:700;">NAJLEPSZY WYNIK</span>' if is_best else ''
            rows.append(
                '<div style="margin-bottom:13px;">'
                '<table width="100%" cellspacing="0" cellpadding="0"><tr>'
                f'<td style="font-size:13px;font-weight:{700 if is_best else 500};color:#172033;">{safe_name}</td>'
                f'<td align="right" style="font-size:14px;font-weight:750;color:{bar};">{score:.2f}%</td>'
                '</tr></table>'
                f'<div style="height:8px;background:#E5EAF2;border-radius:4px;margin-top:6px;">'
                f'<div style="height:8px;width:{score:.2f}%;background:{bar};border-radius:4px;"></div></div>'
                f'<div style="height:3px;">{tag}</div>'
                '</div>'
            )
        self.probabilities_label.setText(''.join(rows))
        self.result_status.setText("●  ODEBRANO WYNIKI AI")

    def transfer_finished(self):
        self.start_button.setEnabled(True)
        self.choose_button.setEnabled(True)
        self.status_pill.setText("●  ZAKOŃCZONO")
        self.result_status.setText("●  ANALIZA ZAKOŃCZONA")
        if self.result_title.text() == "Analiza w toku":
            self.result_title.setText("ESP32 zakończyło pracę")
            self.result_detail.setText("Odebrano FINISHED, ale firmware nie wysłało osobnej linii z nazwą klasy.")
        else:
            self.result_detail.setText("Klasyfikacja odebrana z ESP32. Sprawdź terminal, aby zobaczyć pełny komunikat.")

    def transfer_failed(self, message: str):
        self.start_button.setEnabled(True)
        self.choose_button.setEnabled(True)
        self.status_pill.setText("●  WYSTĄPIŁ BŁĄD")
        self.result_title.setText("Nie udało się zakończyć transmisji")
        self.result_detail.setText(message)
        self.result_status.setText("●  BŁĄD")
        self.log(f"[BŁĄD] {message}")
        QMessageBox.critical(self, "Błąd transmisji", message)


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
