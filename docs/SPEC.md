# Smarthouse – panel sterowania Tuya na STM32F746G-DISCO

Wersja: 0.1 (szkic do akceptacji) · Data: 2026-10-31

## 1. Cel

Panel ścienny na płytce STM32F746G-DISCO, który **lokalnie** (bez chmury Tuya) steruje domowymi urządzeniami Tuya przez Ethernet. Ekran pokazuje kafelki urządzeń z ikoną typu i grafiką stanu:

- **krótkie dotknięcie** przełącza urządzenie w stan przeciwny,
- **przytrzymanie** otwiera okno szczegółowego sterowania (jasność, barwa, pozycja rolety, wybór rolet w grupie),
- gdy kafelki nie mieszczą się na ekranie – **strony przewijane** gestem,
- **grupy wirtualne** – urządzenia zbiorcze łączące urządzenia tego samego typu (np. „Wszystkie rolety parter”), tworzone i edytowane przez użytkownika na ekranie; jedno dotknięcie steruje wszystkimi członkami jednocześnie.

Poza zakresem v1: sterowanie przez chmurę, czujniki bateryjne (Garaż, Czujnik ruchu), sceny (różne akcje dla różnych urządzeń), harmonogramy, dodawanie nowych urządzeń Tuya z poziomu ekranu.

## 2. Sprzęt i sieć

| Element | Wartość |
|---|---|
| Płytka | STM32F746G-DISCO (STM32F746NG, Cortex-M7 216 MHz, 1 MB Flash, 320 KB RAM, 8 MB SDRAM, 16 MB QSPI) |
| Ekran | 4.3" 480×272 RGB565, LTDC + DMA2D, dotyk pojemnościowy FT5336 (I²C) |
| Sieć | Ethernet RMII (LAN8742A), podłączony do LAN AP2 (192.168.0.0/24) |
| Programator | ST-LINK/V2-1 (VCP `/dev/cu.usbmodem*` = log diagnostyczny 115200) |

Topologia: AP1 (192.168.1.0/24) ← NAT ← AP2 Archer A8 (192.168.0.0/24) ← STM32.
Urządzenia z AP1 są osiągalne przez NAT po stałym IP; broadcast UDP z AP1 do panelu nie dociera.

## 3. Architektura oprogramowania

| Warstwa | Technologia |
|---|---|
| Build | PlatformIO, `board = disco_f746ng`, `framework = stm32cube` |
| RTOS | FreeRTOS (CMSIS-RTOS v2) |
| Sieć | lwIP (DHCP, sockety BSD, UDP) |
| Krypto | AES-128-ECB (mbedTLS lub tiny-AES), MD5 (tylko klucz broadcastu), CRC32 |
| JSON | cJSON lub jsmn |
| GUI | LVGL 9 (bufor ramki w SDRAM, DMA2D) |
| Czcionki | Montserrat z polskimi znakami (ąćęłńóśźż), 14/16/20 px |

Zadania FreeRTOS:

| Zadanie | Priorytet | Rola |
|---|---|---|
| `gui_task` | normalny | LVGL `lv_timer_handler()` co 5 ms, obsługa dotyku |
| `tuya_task` | normalny | połączenia TCP do urządzeń, kolejka komend, heartbeat, odbiór pushy |
| `discovery_task` | niski | nasłuch UDP 6667 (aktualizacja IP urządzeń z AP2) |
| `lwip/tcpip` | wysoki | stos sieciowy |

Komunikacja GUI ↔ Tuya wyłącznie przez kolejki (`cmd_queue`: GUI→Tuya, `state_queue`: Tuya→GUI). GUI nigdy nie blokuje się na sieci.

## 4. Model urządzeń

### 4.1 Typy (z danych urządzeń)

| Typ panelu | Kategoria Tuya | Urządzenia | Kluczowe DP |
|---|---|---|---|
| `BLIND` roleta | `clkg`, `tdq` | Salon roleta, Salon s1/s2/s3, Kuchnia, Pralnia, Kotłownia, Dziecko roleta, Biuro roleta 2 | DP1 `control` enum `open/stop/close`; DP2 `percent_control` 0–100 (krok 10) |
| `LIGHT` żarówka | `dj` | Sypialnia, Lampka Salon, Dziecko lampa | DP20 `switch_led` bool; DP21 `work_mode` `white/colour`; DP22 jasność 10–1000; DP23 barwa 0–1000; DP24 kolor HSV (hex `hhhhssssvvvv`) |
| `PLUG` gniazdko/przełącznik | `cz` | Oświetlenie na zewnątrz, Salon duże okno, Biurko | DP1 `switch_1` bool; DP19 moc W/10; DP20 napięcie V/10; DP18 prąd mA |
| `GROUP` grupa wirtualna | – | tworzona przez użytkownika (§6.7), np. „Salon – wszystkie rolety” | – (komendy rozsyłane do członków) |

### 4.2 Stan urządzenia (w RAM)

```c
typedef enum { DEV_UNKNOWN, DEV_ONLINE, DEV_OFFLINE } dev_link_t;
typedef struct {
    dev_link_t link;
    bool       busy;          // komenda wysłana, czekamy na potwierdzenie
    bool       on;            // LIGHT/PLUG
    uint8_t    open_pct;      // BLIND: znormalizowane 0 = zamknięta, 100 = otwarta
    bool       moving;        // BLIND: ruch w toku (DP1 != stop)
    uint16_t   brightness;    // LIGHT 10–1000
    uint16_t   temp;          // LIGHT 0–1000
    uint16_t   power_dw;      // PLUG moc w 0.1 W
    uint32_t   last_seen_ms;
} dev_state_t;
```

Rolety: każde urządzenie ma flagę `invert` w konfiguracji – mapuje DP2 na `open_pct` (do ustalenia testem, patrz §10, O1).

### 4.3 Konfiguracja

- `tools/tuya_devices.json` – dane z chmury (ID, `local_key`, DP), **poza gitem**.
- `config/layout.yaml` – **w gicie**: kolejność kafelków, nazwy wyświetlane, typ, IP, `invert`, grupy startowe, urządzenia ukryte.
- `tools/gen_config.py` generuje `src/generated/devices_cfg.c` (poza gitem) z obu plików. Build bez tego pliku kończy się czytelnym błędem.

### 4.4 Grupy wirtualne

```c
#define GROUP_MAX          16
#define GROUP_MEMBERS_MAX  16
typedef struct {
    uint8_t  id;                         // stały, 1..255 (0 = wolny slot)
    char     name[32];                   // UTF-8
    dev_type_t type;                     // BLIND / LIGHT / PLUG – wszyscy członkowie tego typu
    uint8_t  member_count;
    char     members[GROUP_MEMBERS_MAX][24]; // devId Tuya (stabilne między przebudowami firmware'u)
    uint8_t  position;                   // miejsce na liście kafelków
} group_cfg_t;
```

- Członkowie wyłącznie **tego samego typu**; urządzenie może należeć do wielu grup.
- Grupa nie ma własnego połączenia – komenda grupy = ta sama komenda wysłana **równolegle** do wszystkich członków (fan-out w `tuya_task`, wszystkie ramki wysłane w ≤ 100 ms).
- Stan zbiorczy wyliczany z członków online:
  - LIGHT/PLUG: `wszystkie wł.` / `część wł.` / `wszystkie wył.`;
  - BLIND: `open_pct` = średnia członków; `moving` = którykolwiek w ruchu.
- Członek offline nie blokuje grupy; kafelek grupy pokazuje „3/4 online”.
- Trwałość: grupy zapisane w QSPI flash (sektor 4 KB, 2 sloty naprzemiennie, nagłówek z wersją + CRC32). Uszkodzony slot → użyj drugiego; oba uszkodzone → grupy startowe z `layout.yaml`.
- Członek, którego `devId` zniknął z konfiguracji (np. po ponownym sparowaniu), jest pomijany i oznaczony w edytorze jako „brak urządzenia”.

## 5. Protokół Tuya 3.3 (lokalny)

Ramka (big-endian):

```
000055AA | seq u32 | cmd u32 | len u32 | [ret u32 – tylko odpowiedź] payload | crc32 u32 | 0000AA55
len = długość od payloadu (z ret) do sufiksu włącznie; CRC32 liczone od prefiksu do końca payloadu
```

| cmd | Nazwa | Payload wysyłany |
|---|---|---|
| 7 | CONTROL | `"3.3"` + 12×`0x00` + AES-ECB(JSON `{"devId","uid","t","dps":{…}}`) |
| 8 | STATUS (push z urządzenia) | jak CONTROL |
| 9 | HEART_BEAT | pusty |
| 10 | DP_QUERY | AES-ECB(JSON `{"gwId","devId","uid","t"}`) – bez nagłówka wersji |

- AES-128-ECB, klucz = 16 znaków `local_key`, padding PKCS#7.
- Jedno trwałe połączenie TCP na urządzenie (urządzenia przyjmują zwykle tylko jedno), heartbeat co 10 s, brak odpowiedzi 3× → `DEV_OFFLINE`, ponowne łączenie z backoffem 1→2→4…→30 s.
- Stan aktualizowany z pushy (cmd 8) – bez cyklicznego odpytywania; DP_QUERY po (ponownym) połączeniu.
- Discovery: UDP 6667, payload AES-ECB kluczem `md5("yGAdlopoPVldABfn")`, pole `gwId` + `ip` aktualizuje IP urządzenia.

## 6. Interfejs użytkownika

### 6.1 Układ ekranu (480×272)

```
┌──────────────────────────────────────────────┐ 0
│ Smarthouse            ● sieć   12:34      1/2 │ pasek stanu 28 px
├──────────────────────────────────────────────┤ 28
│ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐  │
│ │  ikona │ │  ikona │ │  ikona │ │  ikona │  │ wiersz 1
│ │ nazwa  │ │ nazwa  │ │ nazwa  │ │ nazwa  │  │
│ └────────┘ └────────┘ └────────┘ └────────┘  │
│ ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐  │ wiersz 2
│ └────────┘ └────────┘ └────────┘ └────────┘  │
├──────────────────────────────────────────────┤ 252
│                   ● ○ ○                      │ wskaźnik stron 20 px
└──────────────────────────────────────────────┘ 272
```

- Siatka 4×2 = **8 kafelków na stronę**, kafelek ok. 110×104 px, odstęp 6 px.
- Kafelek: ikona typu 48×48 (grafika zależna od stanu), nazwa (max 2 linie, skracana „…”), linia stanu („Wł.”, „60%”, „Otwarta”, „Offline”).

### 6.2 Ikony i grafika stanu

| Typ | Stan | Grafika |
|---|---|---|
| LIGHT | wył. | żarówka szara, tło kafelka ciemne |
| LIGHT | wł. | żarówka żółta z poświatą; intensywność poświaty ∝ jasność; linia stanu „Wł. 60%” |
| BLIND | zamknięta / częściowo / otwarta | okno z lamelami; zasłonięta część okna ∝ `100 − open_pct`; linia stanu „Zamknięta” / „40%” / „Otwarta” |
| BLIND | w ruchu | jak wyżej + animowana strzałka ↑/↓ |
| PLUG | wył. / wł. | wtyczka szara / zielona; gdy wł. linia stanu „Wł. 23.4 W” |
| GROUP | – | ikona typu członków na tle „stosu” kafelków + plakietka z liczbą członków; grafika wg stanu zbiorczego (część wł. = połowa poświaty / roleta wg średniej); linia stanu np. „2/4 wł.”, „Średnio 40%” |
| dowolny | offline | ikona wyszarzona 40% + znaczek przekreślonego Wi-Fi; dotyk ignorowany, przytrzymanie pokazuje „Urządzenie offline” |
| dowolny | busy | mały spinner w rogu kafelka do czasu potwierdzenia (max 3 s) |

Ikony: własne grafiki wektorowe (SVG → `lv_image` C array, RGB565A8), jeden zestaw kolorów jasny-na-ciemnym.

### 6.3 Zachowanie dotyku

| Gest | Próg | Akcja |
|---|---|---|
| Dotknięcie | < 500 ms, ruch < 10 px | przełącz stan (§6.4) |
| Przytrzymanie | ≥ 500 ms bez ruchu | otwórz okno szczegółów (§6.5); wibracja/wizualny „pulse” kafelka przy przekroczeniu progu |
| Przesunięcie poziome | > 40 px | zmiana strony (z przyciąganiem do strony) |
| Dotknięcie kropki stron | – | skok do strony |

### 6.4 Przełączanie stanu (jedno kliknięcie)

| Typ | Stan bieżący | Wysyłane |
|---|---|---|
| LIGHT / PLUG | wł. | `on = false` |
| LIGHT / PLUG | wył. | `on = true` (żarówka wraca do ostatniej jasności) |
| BLIND | w ruchu | `stop` (bezpieczeństwo) |
| BLIND | `open_pct ≥ 50` | `close` |
| BLIND | `open_pct < 50` | `open` |
| GROUP (BLIND) | którykolwiek w ruchu | wszystkie → `stop` |
| GROUP (dowolny) | ≥ 1 członek „otwarty/wł.” | wszystkie → zamknij/wył. |
| GROUP (dowolny) | żaden | wszystkie → otwórz/wł. |

UI pokazuje nowy stan **od razu** (optymistycznie) ze spinnerem; brak potwierdzenia w 3 s → powrót do poprzedniego stanu + czerwony obrys kafelka przez 2 s.

### 6.5 Okna szczegółów (przytrzymanie)

Okno modalne ~440×232 px, przycisk „✕” i zamknięcie dotknięciem poza oknem lub po 30 s bezczynności.

- **LIGHT**: przełącznik wł./wył.; suwak jasności 1–100% (→ DP22 10–1000); suwak barwy ciepła↔zimna (→ DP23); paleta 6 kolorów (→ DP21 `colour` + DP24). Wysyłka przy puszczeniu suwaka, podczas przeciągania max 1 komenda / 300 ms.
- **BLIND**: przyciski ▲ Otwórz / ■ Stop / ▼ Zamknij; suwak pozycji 0–100% krok 10 (→ DP2); aktualna pozycja na żywo.
- **PLUG**: przełącznik wł./wył.; moc [W], napięcie [V], prąd [A] (tylko odczyt, gdy urządzenie je raportuje).
- **GROUP** (selektywne sterowanie): lista członków z polami wyboru (domyślnie wszystkie zaznaczone) i ich bieżącym stanem; elementy sterujące jak dla typu członków (rolety: ▲/■/▼ + pozycja; żarówki: wł./wył. + jasność + barwa; gniazdka: wł./wył.) działają **tylko na zaznaczonych**. Zaznaczenie nie zmienia składu grupy. Przycisk „✎ Edytuj grupę” otwiera edytor (§6.7).

### 6.6 Strony

- Liczba stron = ⌈liczba kafelków / 8⌉; kafelki w kolejności z `layout.yaml`.
- Przewijanie gestem z animacją i przyciąganiem; wskaźnik kropek + „n/N” w pasku stanu.
- Bezczynność 60 s → powrót na stronę 1; bezczynność 5 min → przyciemnienie podświetlenia do 20%, pierwszy dotyk tylko wybudza.
- Ostatni kafelek ostatniej strony to „＋ Nowa grupa” (otwiera edytor §6.7).

### 6.7 Edytor grup wirtualnych

Wejście: kafelek „＋ Nowa grupa” (nowa) lub „✎ Edytuj grupę” w oknie szczegółów grupy (istniejąca).

Kroki (pełny ekran, przyciski „Wstecz” / „Dalej” / „Zapisz”):

1. **Typ** – wybór: Rolety / Światła / Gniazdka (tylko przy tworzeniu; typ istniejącej grupy jest stały).
2. **Członkowie** – przewijana lista urządzeń wybranego typu (ikona, nazwa, stan online) z polami wyboru; min. 2 członków, max 16.
3. **Nazwa** – pole tekstowe z klawiaturą ekranową (polskie znaki, max 31 znaków); propozycja domyślna np. „Rolety (4)”.
4. **Pozycja** – na której stronie / miejscu ma stać kafelek (domyślnie na początku strony 1).

Pozostałe operacje w edytorze istniejącej grupy:
- zmiana członków, nazwy, pozycji,
- **Usuń grupę** – wymaga drugiego potwierdzenia przyciskiem w oknie („Usuń” / „Anuluj”); usuwa tylko grupę, nie urządzenia.

Zapis do flash następuje po „Zapisz”; „Wstecz” z kroku 1 lub zamknięcie bez zapisu porzuca zmiany. Limit 16 grup – przy jego osiągnięciu kafelek „＋” jest nieaktywny z podpowiedzią „Maks. 16 grup”.

## 7. Wymagania z kryteriami akceptacji

Format: **Dane / Gdy / Wtedy**. Każde wymaganie = co najmniej jeden test (jednostkowy `pio test -e native` lub akceptacyjny na płytce).

### Sieć i protokół

- **R-NET-1** Dane: kabel w AP2. Gdy: start płytki. Wtedy: adres z DHCP w ≤ 10 s, unikalny MAC wyliczony z UID MCU (prefiks lokalnie administrowany `02:…`), log IP na VCP.
- **R-TUYA-1** Dane: wektor testowy (klucz, JSON, seq). Gdy: kodowanie ramki CONTROL. Wtedy: bajty identyczne z ramką z `tinytuya` dla tych samych danych.
- **R-TUYA-2** Dane: ramka z błędnym CRC lub sufiksem. Gdy: dekodowanie. Wtedy: ramka odrzucona, połączenie nie zrywane, licznik błędów +1.
- **R-TUYA-3** Dane: urządzenie online. Gdy: połączenie nawiązane. Wtedy: DP_QUERY i stan kafelka zgodny z urządzeniem w ≤ 2 s.
- **R-TUYA-4** Dane: zmiana stanu z aplikacji Tuya / pilota. Gdy: urządzenie wysyła push. Wtedy: kafelek aktualizuje się w ≤ 1 s.
- **R-TUYA-5** Dane: urządzenie odłączone od prądu. Gdy: 3 heartbeaty bez odpowiedzi. Wtedy: kafelek „Offline” w ≤ 35 s; po przywróceniu zasilania „Online” bez restartu panelu.
- **R-TUYA-6** Dane: urządzenie z AP2 zmienia IP. Gdy: odebrany broadcast UDP. Wtedy: panel łączy się na nowy IP.

### GUI

- **R-UI-1** Gdy: start. Wtedy: siatka 4×2, ikony typów zgodne z §6.2, polskie znaki poprawne.
- **R-UI-2** Dane: 13 kafelków. Wtedy: 2 strony, przesunięcie palcem zmienia stronę, kropki pokazują bieżącą.
- **R-UI-3** Dane: żarówka wył. Gdy: dotknięcie < 500 ms. Wtedy: żarówka wł. w ≤ 300 ms (pomiar w sieci domowej), grafika „wł.”.
- **R-UI-4** Dane: roleta otwarta (`open_pct ≥ 50`). Gdy: dotknięcie. Wtedy: komenda `close`, animacja ruchu, po zakończeniu grafika „Zamknięta”.
- **R-UI-5** Dane: roleta w ruchu. Gdy: dotknięcie. Wtedy: komenda `stop`.
- **R-UI-6** Gdy: przytrzymanie ≥ 500 ms kafelka żarówki. Wtedy: okno z suwakami jasności i barwy; przesunięcie i puszczenie suwaka zmienia jasność urządzenia.
- **R-UI-7** Dane: grupa 4 rolet, odznaczone 2. Gdy: ▲ Otwórz. Wtedy: otwierają się tylko 2 zaznaczone.
- **R-UI-8** Dane: urządzenie nie potwierdza komendy. Gdy: minie 3 s. Wtedy: kafelek wraca do poprzedniego stanu z czerwonym obrysem.
- **R-UI-9** Gdy: GUI pracuje przy zerwanej sieci. Wtedy: ekran reaguje na dotyk (≥ 25 FPS przy przewijaniu), brak zawieszeń.

### Grupy wirtualne

- **R-GRP-1** Gdy: „＋ Nowa grupa” → Rolety → zaznaczenie 3 rolet → nazwa „Parter” → Zapisz. Wtedy: kafelek „Parter” pojawia się we wskazanym miejscu z plakietką „3”.
- **R-GRP-2** Dane: grupa „Parter” (3 rolety, wszystkie zamknięte). Gdy: dotknięcie kafelka. Wtedy: wszystkie 3 dostają `open`; odstęp między pierwszą a ostatnią wysłaną ramką ≤ 100 ms.
- **R-GRP-3** Dane: grupa, w której 1 roleta jest otwarta. Gdy: dotknięcie. Wtedy: wszystkie członkowie dostają `close`.
- **R-GRP-4** Dane: grupa rolet, jedna w ruchu. Gdy: dotknięcie. Wtedy: wszystkie dostają `stop`.
- **R-GRP-5** Dane: grupa świateł. Gdy: przytrzymanie → suwak jasności 50%. Wtedy: wszystkie zaznaczone żarówki dostają DP22 = 500.
- **R-GRP-6** Gdy: zapisanie grupy, następnie odłączenie zasilania płytki i ponowny start. Wtedy: grupa istnieje z tą samą nazwą, członkami i pozycją.
- **R-GRP-7** Gdy: utrata zasilania w trakcie zapisu grupy. Wtedy: po starcie dostępny jest poprzedni poprawny stan grup (brak uszkodzenia).
- **R-GRP-8** Dane: edytor, krok 2, typ „Światła”. Wtedy: na liście są wyłącznie urządzenia typu LIGHT.
- **R-GRP-9** Dane: 1 z 4 członków offline. Gdy: dotknięcie kafelka grupy. Wtedy: 3 online wykonują komendę, kafelek pokazuje „3/4 online”, brak czerwonego błędu dla całej grupy.
- **R-GRP-10** Gdy: „Usuń grupę” → „Usuń”. Wtedy: kafelek znika, urządzenia członkowskie bez zmian; „Anuluj” nic nie zmienia.

### Niezawodność

- **R-SYS-1** Watchdog sprzętowy (IWDG ~4 s); zawieszenie zadania → restart, na ekranie powód w logu VCP.
- **R-SYS-2** 24 h pracy bez restartu i bez wycieku pamięci (heap min. ≥ 20% w logu co 10 min).
- **R-SYS-3** Klucze `local_key` nie trafiają do gita ani do logów.

## 8. Plan realizacji (etapy = punkty kontrolne)

| Etap | Zakres | Gotowe gdy |
|---|---|---|
| E1 | Szkielet PlatformIO, FreeRTOS, log VCP, Ethernet + DHCP | R-NET-1 |
| E2 | Biblioteka `tuya_proto` (ramki, AES, CRC) + testy natywne | R-TUYA-1, R-TUYA-2 |
| E3 | `tuya_task`: połączenia, heartbeat, query, push, komendy, generator konfiguracji | R-TUYA-3…6 (test z konsoli VCP) |
| E4 | LCD + dotyk + LVGL, siatka kafelków, strony, ikony | R-UI-1, R-UI-2, R-UI-9 |
| E5 | Kliknięcie/przytrzymanie, okna szczegółów | R-UI-3…8 |
| E6 | Grupy wirtualne: model + fan-out (testy natywne), zapis QSPI, edytor na ekranie | R-GRP-1…10 |
| E7 | Watchdog, podświetlenie, test 24 h | R-SYS-1…3 |

## 9. Narzędzia na Macu

- `tools/tuya_fetch_keys.py` – pobranie kluczy (QR w aplikacji Tuya Smart).
- `tools/tuya_map_ips.py` – mapowanie IP w LAN.
- `tools/gen_config.py` – generowanie `devices_cfg.c` (E3).
- `tools/tuya_sim.py` – symulator urządzenia Tuya 3.3 do testów bez fizycznych urządzeń (E3).

## 10. Otwarte kwestie (do decyzji przed E3/E5)

| # | Pytanie | Propozycja |
|---|---|---|
| O1 | Czy DP2 = 100 oznacza roletę otwartą czy zamkniętą (zależy od `control_back_mode`)? | Test: polecenie `open` na jednej rolecie i odczyt DP2 → ustawić `invert` |
| O2 | Grupy startowe (przed pierwszą edycją na ekranie) | „Salon – wszystkie rolety” = Salon roleta + s1 + s2 + s3; „Wszystkie rolety” = 10 rolet |
| O3 | Czy kafelki pojedynczych rolet z grupy mają też zostać osobno? | Tak, za grupą |
| O7 | Czy edycja grup ma być chroniona PIN-em (np. przed dziećmi)? | Nie w v1; opcjonalny 4-cyfrowy PIN w v2 |
| O4 | „Oświetlenie na zewnątrz” odrzuca połączenie lokalne – inny klient trzyma połączenie? | Sprawdzić, czy działa coś typu Home Assistant / LocalTuya; inaczej restart zasilania |
| O5 | Kolejność kafelków / pomieszczenia | Wg pomieszczeń: Salon, Kuchnia, Sypialnia, Dziecko, Biuro, Pralnia, Kotłownia, Zewnątrz |
| O6 | Zegar w pasku stanu (wymaga SNTP z internetu) | Tak, SNTP `pool.ntp.org`, strefa Europe/Warsaw |
