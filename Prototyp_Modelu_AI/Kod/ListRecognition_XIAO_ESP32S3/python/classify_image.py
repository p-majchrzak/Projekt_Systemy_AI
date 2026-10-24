
import time
import sys
import serial
from PIL import Image

PORT = "/dev/cu.usbmodem1101"
BAUD = 115200
IMAGE_PATH = "/Users/piotrmajchrzak/Desktop/p1.jpg"

WIDTH = 240
HEIGHT = 240

CHUNK_SIZE = 64
CHUNK_DELAY = 0.002
IMAGE_SIZE = WIDTH * HEIGHT * 3


def read_line(ser, timeout=120):
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()

        if line:
            print(f"\n[ESP32] {line}")
            return line

    raise TimeoutError("Brak odpowiedzi ESP32.")


def show_progress(sent, total, start_time):
    elapsed = max(time.monotonic() - start_time, 0.001)
    percent = sent / total * 100
    speed = sent / elapsed
    remaining = (total - sent) / speed if speed > 0 else 0

    bar_width = 30
    filled = int(bar_width * sent / total)
    bar = "#" * filled + "-" * (bar_width - filled)

    sys.stdout.write(
        f"\r[TRANSFER] [{bar}] {percent:6.2f}% "
        f"| {sent:,}/{total:,} B "
        f"| {speed / 1024:.1f} KB/s "
        f"| pozostało: {remaining:.1f} s"
    )
    sys.stdout.flush()


def main():
    image = Image.open(IMAGE_PATH).convert("RGB")
    image = image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    raw = image.tobytes()

    if len(raw) != IMAGE_SIZE:
        raise RuntimeError(
            f"Nieprawidłowy rozmiar: {len(raw)} bajtów; "
            f"oczekiwano {IMAGE_SIZE}."
        )

    print(f"[INFO] Port: {PORT}")
    print(f"[INFO] Obraz: {WIDTH}x{HEIGHT} RGB")
    print(f"[INFO] Rozmiar: {IMAGE_SIZE:,} bajtów")

    with serial.Serial(
        PORT,
        BAUD,
        timeout=1,
        write_timeout=30
    ) as ser:
        time.sleep(2)
        ser.reset_input_buffer()

        ser.write(f"IMG {len(raw)}\n".encode("ascii"))
        ser.flush()

        while True:
            response = read_line(ser)

            if response.startswith("READY,"):
                parts = response.split(",")

                if len(parts) >= 3:
                    board_width = int(parts[1])
                    board_height = int(parts[2])

                    if (board_width, board_height) != (WIDTH, HEIGHT):
                        raise RuntimeError(
                            f"ESP32 oczekuje obrazu "
                            f"{board_width}x{board_height}, "
                            f"Python przygotował {WIDTH}x{HEIGHT}."
                        )
                break

            if response.startswith("ERROR,"):
                raise RuntimeError(response)

        print("[INFO] Rozpoczynam przesyłanie obrazu.")

        sent = 0
        start_time = time.monotonic()
        last_display = 0.0

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

            if now - last_display >= 0.1 or sent == len(raw):
                show_progress(sent, len(raw), start_time)
                last_display = now

            time.sleep(CHUNK_DELAY)

        ser.flush()
        print("\n[INFO] Wysyłanie zakończone. Czekam na klasyfikację...")

        while True:
            response = read_line(ser, timeout=180)

            if response == "FINISHED":
                break

            if response.startswith("ERROR,"):
                raise RuntimeError(response)

    print("[INFO] Zakończono.")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"\n[BŁĄD] {error}")
