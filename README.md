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
- Podczas rozruchu i odliczania gaszenia nie ma STOP. Raporty co 60 s.
- Skonfigurowane połączenie USB utrzymuje MCU aktywne do diagnostyki
  (`CAR_USB_KEEP_AWAKE`). Bez USB diagnostyka nie blokuje aplikacji.
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

1. Sprawdzenie świeżego ADC, stanu silnika oraz trwałego zapisu request ID.
2. PB9 HIGH — zasilanie kluczyka; 200 ms oczekiwania.
3. PA15 HIGH na 2200 ms, następnie LOW.
4. Obserwacja napięcia do 15 s; wcześniejsze zakończenie powyżej 13,4 V.
5. PB9 LOW. Sukces: wzrost >400 mV oraz napięcie >=12,9 V.
6. Uruchomienie lokalnego czasu gaszenia. Jeżeli napięcie wskazuje pracę silnika,
   ale wzrost jest niejednoznaczny, raportowany jest START_FAILED, a odliczanie
   mimo to pozostaje aktywne (o ile użytkownik go nie anulował).

Pracę silnika **szacujemy z napięcia**, bez sygnału obrotów/alternatora:
>=12,9 V oznacza pracę, <12,6 V oznacza zatrzymanie; pomiędzy progami stan jest
podtrzymywany. Nie stanowi to niezależnego potwierdzenia mechanicznej pracy.
Kolejny START przy pracy silnika jest odrzucany. Nie dodano arbitralnego progu
minimalnego napięcia do rozruchu (`CAR_MIN_START_MV=0`).

Gaszenie: impuls PA15 2200 ms i sprawdzenie napięcia po 5 s. Nigdy nie ponawia
impulsu samoczynnie. Gdy silnik jest już rozpoznany jako zatrzymany, odliczanie
jest anulowane bez impulsu; przy braku świeżego ADC raportowany jest błąd.
Utrata łączności nie zatrzymuje lokalnego odliczania.

W trakcie faz rozruchu i gaszenia RX jest odroczony, aby kasowanie sektora NOR
nie wydłużało impulsów przekaźników. Odpowiedzi TX są nadal obsługiwane.
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
| Zasilanie peryferiów | PA1 HIGH, zgodnie z MeasurementUnit |
| LED | PC13, zakładana aktywność LOW |

Założono jeden DS18B20 z osobnym zasilaniem, nie parasite power. TIM1 ma krok
1 us (96 MHz / 96), obsługuje OneWire w IRQ o priorytecie 1. Biblioteka OneWire
jest przeniesiona ze starego projektu, z zewnętrznymi definicjami funkcji inline,
volatile dla stanu ISR i poprawionymi czasami presence/read. Wymagania czasowe:
[DS18B20 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf).
Przebiegi rzeczywiste trzeba sprawdzić analizatorem, szczególnie przy długim kablu.

ADC pobiera serie 32 próbek przez DMA2 Stream0 Channel0 w trybie Normal.
Przeliczenie używa kalibracji starego projektu: VDDA 3287 mV, mnożnik 5,726.
Pomiar jest odświeżany co 1 s, ważny przez 2,5 s. Nieprawidłowe próbki przy
szynach ADC są odrzucane. Raport dotyczy napięcia w mV; nie wylicza procentowego
naładowania akumulatora.

W STOP nRF przechodzi w Power Down, W25Q64 w Deep Power Down; ADC i TIM1 są
zatrzymywane, USB odłączane. PA1 pozostaje HIGH: bez schematu nie odcinamy
wspólnej szyny zasilającej czujnik/pamięć. Po wybudzeniu przywracane są zegary,
USB, pamięć i radio. Prądu całej płytki nie zmierzono.

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
- Przy resecie MCU wyjścia wracają do LOW, timery silnika nie są wznawiane.
  Silnik pozostający włączony po resecie będzie tylko wykrywany i raportowany.
  Podwójny klik może uruchomić jego lokalne gaszenie.

## Protokół i Root

Szczegółowy format: [Integration/ROOT_PROTOCOL.md](Integration/ROOT_PROTOCOL.md).
Gotowy klient do przeniesienia do Root: `Integration/RootCarClient.c/.h`.
**Firmware Root nie został zmieniony w tej pracy.** Jego obecna wersja obsługuje
MeasurementUnit; wymaga autoryzacji/trasy CarStartera i wywołania klienta.

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
