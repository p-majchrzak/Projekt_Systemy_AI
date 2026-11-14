DZIUPLA VISION - 4 MODULY (POPRAWIONY PAKIET)
================================================

ARCHITEKTURA
------------
IMG -> SVR -> AI -> SVR -> MAIL

SPRZET
------
SVR  : Seeed XIAO ESP32-C3
IMG  : Seeed XIAO ESP32-C3
AI   : Seeed XIAO ESP32-S3
MAIL : Seeed XIAO ESP32-S3

SIEC
----
SSID: DZIUPLA-VISION
Haslo: Dziupla2026
SVR/AP: 192.168.4.1
IMG: 192.168.4.10
AI: 192.168.4.11
MAIL: 192.168.4.12
Maska: 255.255.255.0
Brama: 192.168.4.1
Panel: http://192.168.4.1 (login admin, haslo admin)
Strona IMG: http://192.168.4.10

STRUKTURA
---------
SVR/serwer/serwer.ino + DziuplaVisionPages.h
IMG/img/img.ino + ImgPage.h
AI/AI/AI.ino
MAIL/MAIL/MAIL.ino
Otwieraj w Arduino IDE plik .ino w jego folderze; pliki .h musza zostac obok .ino.

ARDUINO IDE
-----------
Zainstaluj pakiet "esp32 by Espressif Systems" 3.x.
SVR i IMG: Seeed XIAO ESP32C3.
AI i MAIL: Seeed XIAO ESP32S3.
AI: wlacz PSRAM i wybierz tryb zgodny z Twoja plytka (dla XIAO ESP32-S3 Sense z OPI PSRAM wybierz OPI PSRAM).

AI / EDGE IMPULSE
-----------------
AI wymaga biblioteki Arduino z Edge Impulse zawierajacej ListRecognition_inferencing.h.
Model musi miec wejscie 240x240 i klasy list, brak_listu, unknown.
Kod AI odbiera raw RGB888 przez WiFiServer i odczytuje dokladnie 172800 bajtow.
Nie uzywa WebServer::arg("plain") do bufora obrazu.

MAIL / KONFIGURACJA
-------------------
W MAIL/MAIL/MAIL.ino uzupelnij:
- HOTSPOT_PASSWORD: haslo hotspotu telefonu o SSID IPPM
- BREVO_API_KEY: klucz API v3 z Brevo
- SENDER_EMAIL: zweryfikowany adres nadawcy w Brevo
Telefon musi udostepniac Internet. MAIL wraca potem do DZIUPLA-VISION.

PRZEPLYW
--------
1. IMG przygotowuje zdjecie 240x240 RGB888 (172800 B) w przegladarce.
2. IMG wysyla multipart do SVR.
3. SVR przekazuje dane strumieniowo do AI (bez buforowania calego obrazu w RAM SVR).
4. AI klasyfikuje i zwraca JSON do SVR.
5. AI raportuje wynik do SVR /api/result; SVR przekazuje zadanie do MAIL.
6. MAIL wysyla powiadomienie tylko dla label=list, is_letter=true i confidence >= 0.65.

LOGI I DIAGNOSTYKA
------------------
SVR zbiera logi przez /api/log (GET i POST) oraz heartbeat modulow.
Panel pokazuje stan wezlow, heartbeat i ostatnie logi.

WAZNE
-----
- Nie mozna uczciwie zagwarantowac 100% dzialania bez kompilacji z Twoja wersja
  biblioteki Edge Impulse, konfiguracji Brevo i testu na plytkach.
- Wiadomosc e-mail nie zawiera zalacznika ze zdjeciem.
- Haslo panelu admin/admin jest testowe; nie wystawiaj tego AP do sieci publicznej.
