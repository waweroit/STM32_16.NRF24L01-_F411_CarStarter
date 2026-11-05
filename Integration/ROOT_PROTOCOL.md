# Integracja CarStartera z Root

CarStarter: ID **0x03**, adres nRF **E7 E7 E7 E7 C3** (kolejność tablicy
przekazywanej do sterownika), klucz z `communicationDevices[2]`.
Root: ID 0x01 / `communicationDevices[0]`. Parametry radia: kanał 76,
1 Mb/s, dynamic payload, adres 5 B, AutoACK. Nie zmieniono wersji SecureProtocol.

## Zmiany potrzebne po stronie Root

1. Dodać `SecureCommunication_AuthorizePeer()` dla `communicationDevices[2]`.
2. Dodać `Communication_AddRoute()` do tego wpisu, niezależnie od istniejącej
   trasy MeasurementUnit.
3. Skopiować `RootCarClient.c/.h` z tego folderu oraz `Core/Inc/CarProtocol.h`.
4. Utworzyć statyczny, wyzerowany `RootCarClient`. W zadaniu komunikacji przekazywać
   odebrane wiadomości do `RootCarClient_OnMessage(&client, &message, HAL_GetTick())`.
5. Z tego samego zadania zlecać START/STOP/ustawienie czasu przez funkcje klienta.
   Dane z innych zadań przekazywać przez istniejący mechanizm kolejek Root.
   Request ID dla nowej intencji START musi być niezerowy i nowy, najlepiej trwały.
6. Dekodować TELEMETRY/RESULT w interfejsie Root. Przy braku wyniku najpierw
   zażądać statusu. Nie zakładać, że AutoACK nRF oznacza wykonanie rozruchu.

Ten klient korzysta z istniejących funkcji Communication; wartości typów są
rzutowane na SecureMessageType_t, więc Root nie musi kopiować całego nagłówka
SecureProtocol z CarStartera. Zachowuje obsługę pozostałych urządzeń.

## Format wiadomości aplikacyjnych

Wszystkie liczby wielobajtowe: **big endian**. Brak paddingu struktur i float.
Każda poniższa wiadomość mieści się w jednej uwierzytelnionej ramce nRF.

| Typ | Kod | Payload |
|---|---|---|
| GET_TEMPERATURE | 0x20 | pusty |
| GET_VOLTAGE | 0x21 | pusty; odpowiedź pełnym raportem |
| GET_STATUS | 0x22 | pusty; odpowiedź pełnym raportem |
| START | 0x23 | session u32, window u16, requestId u32 (10 B) |
| SET_TIMEOUT | 0x24 | session u32, window u16, seconds u16 (8 B) |
| GET_TIMEOUT | 0x25 | pusty |
| STOP | 0x26 | session u32, window u16 (6 B) |
| READY | 0x28 | session u32, window u16, remainingSeconds u8, version=1 u8 |
| TELEMETRY | 0x29 | opis poniżej, 11 B |
| RESULT | 0x2A | command u8, result u8, requestId u32 (6 B) |
| TIMEOUT | 0x2B | seconds u16 |

READY jest wysyłany po raporcie oraz co około 10 s podczas aktywności. Okno
ważności tokena ma 30 s. Kolejny READY w tym samym oknie zawiera **pozostały**
czas, a nie przedłuża tokena. Root powinien zostawić margines na transmisję.
Po STOP MCU tworzy nowe okno; podczas długiej aktywności także co 30 s.
Licznik okna nie jest zawijany. Po 65535 oknach polecenia zmieniające stan są
blokowane do restartu, bez automatycznego resetowania pracującego sterownika.
Polecenia pomiaru/statusu pozostają dostępne. Nie kolejkować START na wiele
cykli ani nie przenosić go automatycznie do kolejnego tokena bez intencji użytkownika.

GET_TEMPERATURE rozpoczyna świeżą konwersję; odpowiedź używa istniejącego
typu TEMPERATURE **0x02**, ASCII np. `-12.34` lub `ERR`, bez końcowego NUL.
GET_VOLTAGE/GET_STATUS zwracają TELEMETRY, TIMEOUT i READY z najnowszego cache.
Stan świeżości ADC jest jawny w flagach.

TELEMETRY:

| Offset | Rozmiar | Znaczenie |
|---|---:|---|
| 0 | 1 | wersja 1 |
| 1 | 1 | silnik: 0 unknown, 1 stopped, 2 running |
| 2 | 1 | flagi: bit0 ADC valid, bit1 temperatura valid, bit2 auto-stop, bit3 stop po double click, bit4 sekwencja w toku, bit5 storage fault |
| 3 | 2 | napięcie akumulatora u16, mV; używać tylko przy bit0=1 |
| 5 | 2 | temperatura i16, 0,01 °C; 0x8000 invalid |
| 7 | 2 | pozostały czas gaszenia w sekundach; 0 bez odliczania lub po upływie |
| 9 | 2 | skonfigurowany limit gaszenia w sekundach |

RESULT:

| Wynik | Znaczenie |
|---:|---|
| 0 | ACCEPTED — rozpoczęto sekwencję |
| 1 | STARTED — ocena rozruchu pozytywna |
| 2 | START_FAILED — ocena rozruchu negatywna/niejednoznaczna |
| 3 | STOPPED |
| 4 | STOP_FAILED — brak potwierdzenia po impulsie |
| 5 | BUSY |
| 6 | ALREADY_RUNNING / brak potwierdzonego zatrzymania |
| 7 | BAD_REQUEST |
| 8 | STORAGE_ERROR |
| 9 | STALE_WINDOW |
| 10 | DUPLICATE — ostatni przyjęty request ID START |
| 11 | SENSOR_ERROR |
| 12 | TIMEOUT_SAVED |
| 13 | TIMER_CANCELLED — pojedynczy klik |

START zwraca najpierw ACCEPTED i później wynik końcowy z tym samym request ID.
Dla ustawienia czasu oraz żądań bez request ID zwracane ID wynosi 0.
Zdarzenia lokalnego odliczania odnoszą się do ostatniej operacji silnika.
Odrzucone przez warstwę Secure ramki nie dostają aplikacyjnej odpowiedzi.
Brak odpowiedzi nie jest dowodem, że polecenie nie dotarło: odczytać status.

Po utracie/wymazaniu trwałego licznika sesji w Root jego stare generacje mogą
być odrzucane przez CarStarter. Wymaga to świadomego ponownego provisioningu,
a nie automatycznego zerowania ochrony anty-replay.
