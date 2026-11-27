# Integracja CarStartera z Root

CarStarter: ID **0x03**, adres nRF **E7 E7 E7 E7 C3** (kolejność tablicy
przekazywanej do sterownika), klucz z `communicationDevices[2]`.
Root: ID 0x01 / `communicationDevices[0]`. Parametry radia: kanał 100,
250 kb/s, dynamic payload, adres 5 B, AutoACK. Nie zmieniono wersji SecureProtocol.

## Zmiany potrzebne po stronie Root

1. Dodać `SecureCommunication_AuthorizePeer()` dla `communicationDevices[2]`.
2. Dodać `Communication_AddRoute()` do tego wpisu, niezależnie od istniejącej
   trasy MeasurementUnit.
3. Skopiować `RootCarClient.c/.h` z tego folderu oraz `Core/Inc/CarProtocol.h`.
4. Utworzyć statyczny, wyzerowany `RootCarClient`. W zadaniu komunikacji przekazywać
   odebrane wiadomości do `RootCarClient_OnMessage(&client, &message, HAL_GetTick())`.
5. Z tego samego zadania zlecać START/STOP oraz ustawienia przez funkcje klienta.
   Dane z innych zadań przekazywać przez istniejący mechanizm kolejek Root.
   Każda intencja START lub STOP ma nowy, niezerowy request ID. START używa
   identyfikatora trwałego, a ROOT numeruje żądania STOP w swojej sesji Secure.
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
| START | 0x23 | session u32, window u16, requestId u32 (10 B), opcjonalnie minutes u8 w zakresie 1–30 (11 B) |
| SET_TIMEOUT | 0x24 | session u32, window u16, seconds u16 (8 B) |
| GET_TIMEOUT | 0x25 | pusty |
| STOP | 0x26 | session u32, window u16, requestId u32 (10 B) |
| SET_DEBUG | 0x27 | jeden bajt 00 (off) lub 01 (on); odpowiedź 0x27 z aktualną wartością |
| READY | 0x28 | session u32, window u16, remainingSeconds u8, version=1 u8 |
| TELEMETRY | 0x29 | opis poniżej, 11 B |
| RESULT | 0x2A | command u8, result u8, requestId u32 (6 B). Odpowiedź na STOP zawiera też rootSession u32 (10 B) |
| TIMEOUT | 0x2B | seconds u16 |
| SET_THRESHOLDS | 0x2C | session u32, window u16, runningMv u16, stoppedMv u16 (10 B) |
| GET_THRESHOLDS | 0x2D | pusty |
| THRESHOLDS | 0x2E | runningMv u16, stoppedMv u16 (4 B) |

READY jest wysyłany po raporcie oraz co około 10 s podczas aktywności. Okno
ważności tokena ma 30 s. Kolejny READY w tym samym oknie zawiera **pozostały**
czas, a nie przedłuża tokena. Root powinien zostawić margines na transmisję.
Po STOP MCU tworzy nowe okno. Podczas pracy silnika odnawia je co 30 s przy
włączonym radiu.
Licznik okna nie jest zawijany. Po 65535 oknach polecenia zmieniające stan są
blokowane do restartu, bez automatycznego resetowania pracującego sterownika.
Polecenia pomiaru/statusu pozostają dostępne. Nie kolejkować START na wiele
cykli ani nie przenosić go automatycznie do kolejnego tokena bez intencji użytkownika.

GET_TEMPERATURE rozpoczyna świeżą konwersję; odpowiedź używa istniejącego
typu TEMPERATURE **0x02**, ASCII np. `-12.34` lub `ERR`, bez końcowego NUL.
GET_VOLTAGE/GET_STATUS zwracają TELEMETRY, TIMEOUT i READY z najnowszego cache.
Raporty automatyczne zachowują tę samą kolejność.
Stan świeżości ADC jest jawny w flagach.

SET_DEBUG wymaga uwierzytelnienia ROOT i poprawnego bajtu 00/01. Nie zawiera
tokenu okna. Niepoprawna długość lub wartość daje RESULT BAD_REQUEST.
Root wysyła parametr po READY i potwierdza zmianę dopiero po odebraniu 0x27
z oczekiwaną wartością. Flaga steruje szczegółowymi logami USB CarStartera,
jest zachowywana podczas STOP i wraca do OFF po resecie MCU.

SET_THRESHOLDS wymaga aktualnego tokena i wartości `0 < stoppedMv < runningMv
<= 16000`. CarStarter zwraca THRESHOLDS po zatwierdzeniu zapisu w W25Q64,
a przy odrzuceniu zwraca RESULT. Podczas sekwencji silnika lub odliczania
urządzenie zwraca BUSY. GET_THRESHOLDS zwraca bieżące progi.
Po TELEMETRY ROOT wstrzymuje polecenia progów do odebrania kolejnego READY.
Od READY odczekuje 50 ms na przełączenie radia CarStartera do nasłuchu.
ROOT może ponowić odczyt GET_THRESHOLDS maksymalnie dwa razy w ciągu 5 s,
z zachowaniem tej samej reguły. Odpowiedź na GET lub udany SET ma typ
THRESHOLDS (0x2E).

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
| 1 | STARTED — ważny pomiar napięcia osiągnął zapisany próg `runningMv` |
| 2 | START_FAILED — brak potwierdzenia progu `runningMv` w ciągu 15 s od końca impulsu |
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
Gdy wiadomość zawiera `minutes`, CarStarter zapisuje limit pracy wraz z ID w jednym rekordzie
W25Q64. Wersja 10 B korzysta z zapisanego limitu. Po zapisie CarStarter ponownie sprawdza
okno przed rozpoczęciem START. Po zakończeniu sekwencji nadaje wynik i nową telemetrię.
STOP zwraca ACCEPTED po rozpoczęciu impulsu i końcowy wynik z tym samym ID
oraz sesją Secure nadawcy polecenia. Przy wcześniej rozpoznanym zatrzymaniu
wysyła od razu STOPPED. Zmiana formatu STOP wymaga aktualizacji obu firmware’ów.

Dla START i STOP po przekazaniu komendy do TX ROOT wyznacza termin na koniec
ważności bieżącego READY plus 25 s. Pierwsze ACCEPTED uruchamia termin 25 s
na wynik. Brak odpowiedzi po przekazaniu do TX oznacza UNKNOWN. ROOT zleca
odczyt statusu. Spóźniony wynik dopasowany po typie i ID jest logowany jako
`CAR LATE RESULT`. Jeśli dotyczy operacji nadal widocznej w `car cache`,
aktualizuje jej stan. Dla STOP dopasowanie obejmuje również sesję ROOT.

Dla ustawienia czasu oraz żądań bez request ID zwracane ID wynosi 0.
Wynik gaszenia z lokalnego odliczania ma `command=0`.
Odrzucone przez warstwę Secure ramki nie dostają aplikacyjnej odpowiedzi.
Brak odpowiedzi nie jest dowodem, że polecenie nie dotarło: odczytać status.

Po utracie/wymazaniu trwałego licznika sesji w Root jego stare generacje mogą
być odrzucane przez CarStarter. Wymaga to świadomego ponownego provisioningu,
a nie automatycznego zerowania ochrony anty-replay.
