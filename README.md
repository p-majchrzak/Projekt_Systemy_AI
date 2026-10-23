# Dziupla – Inteligentna szafka na listy

## 1. Opis projektu

**Dziupla** to projekt inteligentnej szafki na listy, której zadaniem jest automatyczne wykrywanie korespondencji i wysyłanie powiadomień e-mail do wskazanego odbiorcy.

System wykorzystuje kamerę oraz model sztucznej inteligencji do analizy obrazu. Dzięki temu potrafi rozróżnić list od innych obiektów, ograniczając liczbę fałszywych powiadomień.

## 2. Cele projektu

- Automatyczne wykrywanie listów umieszczanych w skrytce.
- Rozpoznawanie obiektów za pomocą sztucznej inteligencji.
- Automatyczne wysyłanie powiadomień e-mail.
- Ograniczenie konieczności ręcznego sprawdzania zawartości szafki.
- Usprawnienie obiegu dokumentów w dziekanacie.

## 3. Funkcjonalności

- Pobieranie obrazu z kamery.
- Analiza obrazu przez model AI.
- Klasyfikacja obiektu jako listu lub innego przedmiotu.
- Podejmowanie decyzji o wysłaniu powiadomienia.
- Konfiguracja adresu e-mail odbiorcy.
- Wyświetlanie statusu systemu na stronie głównej.
- Możliwość rozbudowy o historię zdarzeń i statystyki.

*Uwaga: zakres funkcjonalności zależy od aktualnego etapu implementacji projektu.*

## 4. Schemat działania

1. Użytkownik umieszcza list w skrytce.
2. Kamera wykrywa zdarzenie i wykonuje zdjęcie.
3. Zdjęcie trafia do modelu sztucznej inteligencji.
4. Model analizuje obraz i rozpoznaje obiekt.
5. System ocenia wynik analizy.
6. Po potwierdzeniu obecności listu wysyłana jest wiadomość e-mail.
7. Wynik zdarzenia może zostać zapisany w historii systemu.

## 5. Dane wejściowe

- Obraz z kamery.
- Dane wizualne wykrytego obiektu.
- Wynik analizy modelu AI.
- Adres e-mail odbiorcy.
- Parametry konfiguracji systemu.

## 6. Dane wyjściowe

- Powiadomienie e-mail o wykryciu listu.
- Wynik klasyfikacji obiektu.
- Informacja o statusie działania systemu.
- Status wysłania wiadomości.
- Historia wykrytych zdarzeń, jeśli funkcja zostanie wdrożona.

## 7. Wymagania

### Wymagania funkcjonalne

- Wykonywanie zdjęć wnętrza skrytki.
- Analizowanie obrazu przez model AI.
- Rozpoznawanie listów i innych obiektów.
- Automatyczne wysyłanie wiadomości e-mail.
- Konfiguracja odbiorcy powiadomień.
- Prezentowanie statusu systemu.

### Wymagania niefunkcjonalne

- Czytelny i intuicyjny interfejs.
- Krótki czas analizy obrazu.
- Ograniczenie liczby fałszywych powiadomień.
- Bezpieczne przechowywanie danych konfiguracyjnych.
- Obsługa błędów kamery, modelu AI i połączenia internetowego.
- Możliwość dalszej rozbudowy systemu.

## 8. Technologie

Projekt może wykorzystywać następujące technologie:

- **Python** – obsługa logiki systemu i przetwarzanie obrazu.
- **Model AI** – rozpoznawanie listów na zdjęciach.
- **Kamera** – rejestrowanie obrazu wnętrza szafki.
- **SMTP lub API pocztowe** – wysyłanie powiadomień e-mail.
- **Git i GitHub** – kontrola wersji i współpraca zespołowa.

Ostateczny zestaw technologii zależy od przyjętej implementacji.

## 9. Twórcy projektu

- Piotr Majchrzak
- Damian Kiełbasa
- Kamil Kwiczal
- Oleksandr Kupchynskyi

## 10. Repozytorium

Kod źródłowy projektu znajduje się w repozytorium GitHub:

[Projekt_Systemy_AI](https://github.com/p-majchrzak/Projekt_Systemy_AI.git)

## 11. Możliwości rozwoju

- Rozbudowa historii wykrytych listów.
- Dodanie statystyk i raportów.
- Obsługa wielu skrytek.
- Udoskonalenie dokładności modelu AI.
- Dodanie konfiguracji progów pewności rozpoznania.
- Rozbudowa panelu administracyjnego.

## 12. Licencja

Licencja projektu nie została jeszcze określona.
