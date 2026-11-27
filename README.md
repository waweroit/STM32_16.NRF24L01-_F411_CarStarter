# STM32F411 CarStarter — bez RTOS

Sterownik samochodu z nRF24L01+, DS18B20, ADC1/DMA, USB CDC i zewnętrzną
pamięcią W25Q64. Logika aplikacji znajduje się w `Core/Src/CarStarter.c`,
parametry sprzętowe i czasy w `Core/Inc/CarConfig.h`.

## Przyjęte działanie

- Po uruchomieniu i po wybudzeniu: pomiar temperatury i napięcia, raport do Root,
  około 30 s aktywności, następnie STOP na nominalnie 60 s. Pełny cykl trwa
  około 90 s plus obsługa peryferiów; raporty w spoczynku nie są co 60 s.
- RTC: LSI, preskalery 127/249, Wakeup Counter 59. Dokładność zależy od LSI.
  Timer RTC jest uzbrajany bezpośrednio przed STOP. PB12 także wybudza MCU.
- Podczas pracy silnika, rozruchu i odliczania gaszenia MCU oraz radio są
  aktywne. Token READY odnawia się co 30 s, a raport pojawia się co 60 s.
- Po inicjalizacji USB sterownik czeka 10 s na ponowne otwarcie portu COM.
  Opóźnienie dotyczy startu MCU, także w zwykłym Debug i Release.
- USB domyślnie pozwala na STOP (`CAR_USB_KEEP_AWAKE=0`). Podczas STOP USB
  jest odłączane i ponownie inicjalizowane po wybudzeniu. Włączenie logów
  przez radio również pozwala na sen.
- Domyślny czas gaszenia: 900 s. Root może zapisać 30–3600 s. Nowy limit działa
  również w bieżącym cyklu, liczony od zakończenia próby rozruchu; jeżeli już
  upłynął, gaszenie następuje przy następnej obsłudze aplikacji.
- Pojedynczy klik: anulowanie automatycznego gaszenia; także podczas rozruchu.
  Podwójny klik: osobne gaszenie po 20 s, gdy silnik jest rozpoznany jako pracujący.
  Pojedynczy klik nie anuluje tego osobnego odliczania (jak w starym projekcie).
  Długie naciśnięcie: log. Debounce 20 ms, okno drugiego kliknięcia 1 s.
- Kliknięcia są obsługiwane przez maszynę stanów, bez oczekiwania na puszczenie.
  Przycisk jest wejściem aplikacyjnym, a nie fizycznym NRST.

## Rozruch i zgaszenie

Po uwierzytelnionym START z aktualnym tokenem okna i nowym request ID:

1. Sprawdzenie świeżego ADC i stanu silnika. Trwały zapis request ID i czasu
   pracy, jeśli START zawiera liczbę minut. Po zapisie ponownie sprawdzane
   jest okno.
2. PB9 HIGH — zasilanie kluczyka; 200 ms oczekiwania.
3. PA15 HIGH na 2200 ms, następnie LOW. PB9 pozostaje w stanie HIGH.
4. Po zakończeniu impulsu PA15 CarStarter czeka maksymalnie 15 s na ważny pomiar
   równy zapisanemu progowi pracy (`runningMv`) lub wyższy. PB9 pozostaje w stanie HIGH.
5. CarStarter wyłącza PB9 po potwierdzeniu rozruchu lub po upływie 15 s.
6. Po potwierdzeniu CarStarter uruchamia lokalny timer gaszenia, o ile użytkownik
   go nie anulował. Po upływie 15 s bez potwierdzenia zgłasza `START_FAILED`.
7. Wysłanie wyniku i nowej telemetrii o stanie silnika oraz odliczaniu.

Pracę silnika **szacujemy z napięcia**, bez sygnału obrotów/alternatora:
Domyślnie napięcie >=12,90 V oznacza pracę, a <12,86 V oznacza zatrzymanie.
W przedziale pomiędzy progami podtrzymywany jest poprzedni stan. Nieprawidłowy
ADC daje stan `unknown`. Po wykryciu pracy MCU i radio pozostają wtedy aktywne
do potwierdzenia zatrzymania.
Kolejny START przy pracy silnika jest odrzucany. Nie dodano arbitralnego progu
minimalnego napięcia do rozruchu (`CAR_MIN_START_MV=0`).

Gaszenie używa jednego impulsu PA15 o długości 2200 ms. Od 5 do 15 s po
impulsie sterownik szuka ważnego pomiaru poniżej ustawionego progu zatrzymania.
Gdy silnik jest już rozpoznany jako zatrzymany, odliczanie
jest anulowane bez impulsu; przy braku świeżego ADC raportowany jest błąd.
Utrata łączności nie zatrzymuje lokalnego odliczania.

Podczas rozruchu i impulsu gaszenia RX jest odroczony, aby kasowanie sektora NOR
nie wydłużało załączenia przekaźników. Odpowiedzi TX są nadal obsługiwane,
a RX wraca na czas weryfikacji napięcia po impulsie gaszenia.
Operacje SPI mają ograniczone czasy oczekiwania; transmisje RF mogą wprowadzić
opóźnienie do około 30 ms. Nie jest to sterowanie o gwarantowanej latencji.

## Sprzęt

| Funkcja | Pin / konfiguracja |
|---|---|
| Kluczyk | PB9, aktywny HIGH |
| START/STOP | PA15, aktywny HIGH |
| Przycisk | PB12, aktywny HIGH, zewnętrzny pulldown, EXTI rising |
| DS18B20 | PA10, Open Drain, zewnętrzny pullup do 3,3 V |
| Napięcie | PB1 / ADC1_IN9, dzielnik i ochrona wejścia po stronie sprzętu |
| SPI1 | PA5 SCK / PA6 MISO / PA7 MOSI, mode 0, 6 MHz |
| nRF24 CE / CS | PA2 / PA3 |
| W25Q64 CS | PA4; nazwa CubeMX `SPI1_EEPROM_CS` |
| Zasilanie nRF24L01 | PA1 HIGH włącza moduł przez 2N7002 |
| LED | PC13, zakładana aktywność LOW |

Założono jeden DS18B20 z osobnym zasilaniem, nie parasite power. TIM1 ma krok
1 us (96 MHz / 96), obsługuje OneWire w IRQ o priorytecie 1. Biblioteka OneWire
jest przeniesiona ze starego projektu, z zewnętrznymi definicjami funkcji inline,
volatile dla stanu ISR i poprawionymi czasami presence/read. Wymagania czasowe:
[DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf).
Przebiegi rzeczywiste trzeba sprawdzić analizatorem, szczególnie przy długim kablu.

ADC pobiera serie 32 próbek przez DMA2 Stream0 Channel0 w trybie Normal.
Przeliczenie przyjmuje VDDA 3287 mV i mnożnik 4,917 dla dzielnika 470 kΩ / 120 kΩ.
Pomiar jest odświeżany co 1 s, ważny przez 2,5 s. Nieprawidłowe próbki przy
szynach ADC są odrzucane. Raport dotyczy napięcia w mV; nie wylicza procentowego
naładowania akumulatora.

Po wyjściu z STOP sterownik przywraca zegary i czeka 500 ms na stabilizację
zasilania, przed inicjalizacją USB i wybudzeniem pamięci. Po ustawieniu PA1
w stan wysoki odczekuje dodatkowo 200 ms przed inicjalizacją nRF24L01.
Pełne 30-sekundowe okno odbioru jest liczone od gotowości radia. Odzyskiwanie
radia po błędzie w trwającym oknie zachowuje jego dotychczasowy termin.
Gdy silnik wymaga aktywności, po 30 s zmienia się token okna bez wyłączania
radia. W spoczynku po zakończeniu okna sterownik wysyła oczekujące odpowiedzi,
zeruje PWR_UP radia z odczytem kontrolnym i ustawia PA1 w stan niski.
Kolejny raport lub wynik
operacji może ponownie otworzyć okno. W STOP radio pozostaje wyłączone, W25Q64
przechodzi w Deep Power Down, ADC i TIM1 są zatrzymywane, a USB odłączane.

## Pamięć i zabezpieczenie komunikacji

- Pierwsze **8192 B W25Q64** są zarezerwowane na dwa sektory dziennika.
  Ten obszar musi być pusty przy pierwszym użyciu. Obecnych danych z innego
  projektu nie kasujemy automatycznie.
- Rekordy 64 B zawierają wersję, licznik, konfigurację, stan anty-replay i CRC32.
  Znacznik zatwierdzenia zapisuje się na końcu. Przejście do drugiego sektora
  zachowuje poprzednią poprawną kopię. Programowanie stron nie kasuje sektora.
- Licznik uruchomień i stan odbioru Root są trwałe. Każda poprawnie
  uwierzytelniona ramka zapisuje nowy rekord **przed** przekazaniem do aplikacji.
  Nie zapisujemy cyklicznych pomiarów. Częstotliwość poleceń wpływa na zużycie NOR.
- Wewnętrzny sektor Flash STM32 nie jest już używany przez `SecureSessionStorage`.
- Brak/awaria pamięci lub niepoprawna zawartość bez prawidłowej kopii blokuje
  zdalny START; aplikacja nie zeruje samoczynnie liczników. LED miga przy błędzie.
- AES-128-CTR + 8-bajtowy AES-CMAC, format zgodny z referencyjną warstwą Secure.
  Jedyny autoryzowany nadawca to Root 0x01. CarStarter używa wpisu 0x03 z tabeli
  `CommunicationDevices`; Root musi używać identycznego klucza i adresu.
- START ma token bieżącej sesji i okna oraz request ID. Nie wolno ponawiać tego
  samego zamierzonego START z nowym request ID. Ostatni przyjęty ID jest trwały.
- STOP również niesie request ID. Odpowiedzi STOP zawierają ID i sesję Secure
  ROOT. Do obsługi nowej ramki STOP trzeba zaktualizować oba firmware’y.
- Przy resecie MCU wyjścia wracają do LOW, timery silnika nie są wznawiane.
  Silnik pozostający włączony po resecie będzie tylko wykrywany i raportowany.
  Podwójny klik może uruchomić jego lokalne gaszenie.

## Protokół i Root

Szczegółowy format: [Integration/ROOT_PROTOCOL.md](Integration/ROOT_PROTOCOL.md).
Projekt Root obsługuje MeasurementUnit i CarStartera przez moduł RootCar.
Pomocniczy klient integracyjny znajduje się w `Integration/RootCarClient.c/.h`.

Na USB ROOT polecenie `car thresholds 12960 12950` zapisuje oba progi w mV
w W25Q64 CarStartera. Dla tych ustawień silnik jest rozpoznany jako pracujący
od 12,960 V, a jako zatrzymany poniżej 12,950 V. Polecenie `car thresholds`
odczytuje bieżące wartości. Zapis progów jest dostępny poza sekwencją
rozruchu lub gaszenia i poza odliczaniem do zgaszenia.
Raport po uruchomieniu i wybudzeniu składa się z telemetrii `0x29`, limitu
gaszenia `0x2B` i końcowego `READY` (`0x28`). CarStarter odpowiada ramką
`0x2E` po odczycie lub po zapisaniu progów na żądanie ROOT.

## Logi diagnostyczne

Port COM CarStartera pokazuje domyślnie wszystkie kompletne, uwierzytelnione
wiadomości odebrane z radia (`[RX]`) oraz wynik każdej próby wysłania wiadomości
(`[TX]` lub `[TX FAIL]`). Każdy wpis zawiera czas HAL w ms, identyfikatory nadawcy i
odbiorcy, typ, długość w bajtach i pełne dane w HEX. Gdy kolejka USB się
zapełni, sterownik pomija nowe wpisy, aby nie blokować radia.

`[RX FRAGMENT]` oznacza odebrany fragment oczekujący na resztę wiadomości,
`[RX REJECTED]` podaje przyczynę odrzucenia ramki, a `[RX LINK ERROR]` wskazuje
błąd odczytu z radia. Do sprawdzenia odbioru można wysłać `car status` na COM
ROOT. Po odebraniu READY ROOT nada polecenie `0x22` do CarStartera.

Na porcie USB ROOT można wysłać `car debug on` lub `car debug off`.
Root czeka na READY, wysyła uwierzytelnione SET_DEBUG (0x27) z jednym bajtem
01 lub 00 i czeka na odpowiedź z aktualną wartością. Potwierdzenie na ROOT ma
postać `CAR debug ON confirmed` albo `CAR debug OFF confirmed`.

Szczegółowe logi są domyślnie wyłączone. Włączenie dodaje informacje o oknie
radiowym, STOP i wybudzeniu, rejestrach radia i adresach ACK po błędzie
transmisji. Podstawowe błędy komunikacji i pamięci oraz komunikat inicjalizacji
są dostępne w obu trybach.

Wartość jest przechowywana w RAM i zachowywana podczas STOP. Po resecie
MCU logowanie szczegółowe wraca do OFF. Parametr nie jest zapisywany jako
ustawienie trwałe i nie zmienia cyklu snu ani czasu gaszenia.

Do pracy i diagnostyki radia używa się zwykłego projektu Debug lub Release.

## Budowanie i weryfikacja

W CubeIDE nowe pliki w Core/Src i Core/Inc są częścią zwykłego projektu.
Po odświeżeniu projektu można budować Debug/Release. Alternatywnie:

```powershell
python tools/build.py
python tools/build.py --debug
python -m pip install --target build/test-deps unicorn pyelftools
python tools/test.py
```

`--gcc <pełna ścieżka do arm-none-eabi-gcc.exe>` pozwala wskazać kompilator.
Wyniki: `build/release/CarStarter.elf/.hex` oraz `build/debug/CarStarter.elf/.hex`.
Skrypt wykrywa lokalny toolchain STM32CubeIDE w C:/ST.

Testy wykonują rzeczywisty kod ARM C w emulatorze Unicorn: sekwencja silnika,
anulowanie timerów, zatrzymanie zewnętrzne, niepoprawny ADC, zmiana limitu,
przepełnienie ticka, rotacja dziennika, 128 punktów zaniku zasilania oraz
uwierzytelnianie/anty-replay po odtworzeniu stanu. Emulator nie testuje
peryferiów, fizycznej W25Q64, radia ani samochodu.

Przed użyciem docelowym trzeba zweryfikować na płytce: kierunek sterowania PA1,
polaryzację przekaźników i przycisku, kalibrację ADC względem multimetru,
odczyt DS18B20, impulsy PB9/PA15, pobór prądu i wybudzenie z STOP przez RTC/PB12,
a następnie łączność z uzupełnionym firmware Root. Nie wykonano flashowania MCU.

## Regenerowanie CubeMX

Wywołania aplikacji w main.c są w sekcjach USER CODE. Ustawienia CS HIGH są
zapisane także w `.ioc`; trzeba zachować rejestrację callbacków TIM, priorytety
IRQ oraz konfigurację RTC/ADC/DMA. Własne moduły aplikacji są osobnymi plikami.
Nie kopiować przykładowego main.c ani spi.c z biblioteki W25Q64 do projektu.
