# Sterowanie głosem – rozpoznanie (2026-11-01)

Notatka z rozpoznania na przyszłość. Nic z tego nie jest jeszcze zaimplementowane.

## Sprzęt na płytce (STM32F746G-DISCO, MB1191)

- 2 cyfrowe mikrofony MEMS **IMP34DT05** (U20, U21), podłączone do kodeka **WM8994** (U11); odczyt przez SAI2, sterowniki w BSP ST (`BSP_AUDIO_IN_*`, wejście cyfrowych mikrofonów), 16 kHz.
- Mikrofony są **od strony ekranu**, przy prawej krawędzi PCB, poza modułem LCD. Położenie patrząc na ekran (przeliczone z pick-and-place `mb1191-C01_PickandPlace.txt`, `x = 127 − X`, `y = Y + 14,75`):
  - U20: x ≈ 125,4, y ≈ 52,5 mm od lewej / dolnej krawędzi PCB,
  - U21: x ≈ 125,4, y ≈ 31,6 mm.
- **Obudowa:** obecny front (`enclosure/smarthouse_case.scad`) zakrywa mikrofony pełną ścianką. Przed użyciem głosu trzeba dodać 2 otwory Ø ~1,5–2 mm (w układzie obudowy x ≈ 129,4, y ≈ 56,5 i 35,6, bo PCB_x0 = PCB_y0 = 4), najlepiej z uszczelką do płytki (między PCB a frontem jest ~6,6 mm pustej przestrzeni). Położenie sprawdzić wydrukiem `fit_test`.

## Warianty

### A. Rozpoznawanie lokalnie na płytce (model)

Słowo budzące + ok. 10–15 stałych komend (np. „rolety w górę”, „światło w salonie”, „wyłącz wszystko”), mały model (TensorFlow Lite Micro + CMSIS-NN, konwersja ewentualnie przez STM32Cube.AI).

- Plusy: całkiem lokalnie, bez internetu i abonamentu.
- Minusy: tylko stałe frazy; trzeba nagrać próbki (kilkadziesiąt na frazę od każdego domownika + szum tła); słabe działanie z daleka (brak AEC / beamformingu).
- Zasoby (szacunek): model 20–80 KB flash, bufory i arena w SDRAM 8 MB, ciągłe nasłuchiwanie ~20–30 % CPU przy 216 MHz; nowe zadanie audio dopisać do watchdoga.

| Narzędzie | Stan (XI 2026) |
|---|---|
| **Edge Impulse** | Darmowy plan Developer (od V 2025): 3 prywatne projekty, trening GPU do 60 min/zadanie; eksport biblioteki C++, demo KWS dla STM32CubeIDE. Najlepsza opcja do prototypu. |
| **Picovoice (Porcupine / Rhino)** | Dawny darmowy plan (3 użytkowników) zniknął – dziś B2B, jednorazowy trial. Rhino na MCU: tylko Cortex-M4 (demo STM32F411), brak polskiego na liście MCU, wymaga AccessKey. ❌ |
| **ST Model Zoo** | Brak gotowych modeli KWS (moderator ST, IV 2026); STM32Cube.AI tylko do konwersji własnego modelu. |
| **Otwarte projekty** | np. `bamert/stm32_speech_commands` (STM32L4, 35 słów, 96 %, 190 ms) – starsze, ale dobry punkt wyjścia. |

Dane po polsku do treningu (wiedza ogólna, niesprawdzone): Mozilla Common Voice, Multilingual Spoken Words Corpus.

### B. Usługa w chmurze (wysyłasz nagranie → tekst / komenda + prawdopodobieństwo)

| Usługa | Co zwraca | Darmowo | Polski |
|---|---|---|---|
| **Wit.ai (Meta)** | `/speech`: intencja + encje + confidence 0–1 | kiedyś całkiem darmowe; obecnych warunków i statusu rozwoju nie potwierdziłem | w 2016 beta – sprawdzić na koncie |
| **Azure AI Speech** (+ CLU) | tekst + confidence, lista fraz podbijająca komendy; CLU: intencja + confidence | **5 h/mies.** (F0) | pl-PL |
| **Google Speech-to-Text** | tekst + confidence alternatyw, podpowiedzi fraz | **60 min/mies.** (wg źródeł zewnętrznych tylko API V1) | pl-PL |
| **Groq (Whisper large-v3)** | sam tekst (miary pewności segmentów w `verbose_json`); dopasowanie do komendy po stronie płytki | darmowy plan z limitami (~2000 zapytań/dzień wg źródeł zewnętrznych, niepotwierdzone) | dobry |

Zużycie: 20 komend/dzień × 3 s ≈ 30 min/mies. – mieści się w każdym z limitów.

Konsekwencje dla panelu:
- **Nie słuchać ciągle** (limity, prywatność) → przycisk mikrofonu na ekranie (dotknij, mów 2–3 s) albo lokalne słowo budzące z wariantu A.
- **HTTPS:** mbedTLS na F746 (do zrobienia, RAM w SDRAM) albo pośrednik w LAN (np. Mac) – prościej, ale panel przestaje być samodzielny.
- Bez internetu działa tylko dotyk; sterowanie Tuya zostaje lokalne.
- Nagrania trafiają do Microsoft / Google / Meta.

## Proponowane kolejne kroki

1. **Test usług na Macu** (bez firmware): kilka nagranych komend po polsku → Wit.ai i Azure, porównanie rozpoznania i confidence. Konta i klucze zakłada użytkownik.
2. **Prototyp nagrywania z płytki:** WM8994 + SAI2, surowy dźwięk do Maca – jakość przez obudowę i z kilku metrów; nagrania przydadzą się też do treningu.
3. Wybór wariantu (A, B albo A jako słowo budzące + B), potem integracja: komenda → istniejące funkcje grup / urządzeń, panel pokazuje, co rozpoznał.

Każdy krok jako osobne zadanie z kontraktem `/spec`.

## Przy okazji: podświetlenie

Dotyk już ustawia 100 %, a po bezczynności ekran ściemnia się do 20 % – obecnie po **5 min** (`UI_IDLE_DIM_MS` w `lib/ui_model/include/ui_idle.h`). Pomysł: skrócić do **60 s** (razem z powrotem na stronę 1; `ui_idle_tick` obsługuje oba zdarzenia naraz). Wymaga poprawy testów `test/test_ui_idle` (5 min wpisane na stałe) i decyzji, czy 20 % czy całkowite wygaszenie.

## Źródła

- [Edge Impulse – Developer Plan](https://edgeimpulse.com/blog/introducing-the-developer-plan/)
- [Edge Impulse – demo KWS dla STM32CubeIDE](https://forum.edgeimpulse.com/t/continuous-audio-classification-keyword-spotting-demo-for-stm32cubeide/802)
- [Picovoice FAQ](https://picovoice.ai/docs/faq/general/)
- [Rhino – microcontroller quick start](https://picovoice.ai/docs/quick-start/rhino-microcontroller)
- [Picovoice free tier (2021) – CNX Software](https://www.cnx-software.com/2021/12/08/picovoice-offline-voice-ai-engine-gets-free-tier-for-up-to-3-users/)
- [ST Community – keyword spotting](https://community.st.com/edge-ai-134/keyword-spotting-164924)
- [bamert/stm32_speech_commands](https://github.com/bamert/stm32_speech_commands)
- [Wit.ai – confidence (Flow XO)](https://support.flowxo.com/article/226-wit-ai)
- [Wit.ai – języki (Stack Overflow, 2016)](https://kiwix.lema.org/stackoverflow.com_en_all_2023-11/questions/38405992/wit-ai-support-for-languages-other-than-english)
- [Azure Speech – cennik](https://azure.microsoft.com/pricing/details/speech/)
- [Google Speech-to-Text – darmowy plan (CostBench)](https://www.costbench.com/software/ai-transcription-apis/google-speech-to-text/free-plan/)
- [Groq – speech-to-text](https://console.groq.com/docs/speech-to-text)
- [Groq – limity (developersdigest)](https://www.developersdigest.tech/rate-limits/groq)
- [Porównanie darmowych API Whisper (dev.to)](https://dev.to/build996/free-whisper-api-groq-deepgram-assemblyai-compared-562m)
