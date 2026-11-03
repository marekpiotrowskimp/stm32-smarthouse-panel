# Obudowa naścienna panelu STM32F746G-DISCO

Obudowa naklejana płaskim tyłem taśmą dwustronną na płaskie kafle. Ekran w normalnej orientacji (taka jak w firmware): złącza płytki są u dołu, a kable wychodzą osobnymi wycięciami w dolnej ściance – micro-USB (zasilanie, CN13), Ethernet (RJ45) i mini-USB ST-LINK (CN14, programowanie bez otwierania). Model jest parametryczny (`smarthouse_case.scad`, OpenSCAD) i drukuje się bez podpór. Wzorowany na obudowie e-paper z `paper-int-massage/enclosure` (ten sam profil druku i sposób testowania).

## Pliki

| Plik | Opis |
|---|---|
| `smarthouse_case.scad` | Źródło parametryczne (OpenSCAD) |
| `fit_bottom.stl` / `.gcode` | **Wydruk próbny 1:** pas dolnej ścianki (12 mm) z wycięciami – sprawdź swoje wtyczki |
| `fit_test.stl` / `.gcode` | **Wydruk próbny 2:** front z oknem i rantem na moduł LCD |
| `case_body.stl` / `.gcode` | Korpus: front z oknem i rantem, ścianki, wycięcia na złącza, wentylacja, wnęka na pokrywę |
| `case_cover.stl` / `.gcode` | Płaska pokrywa z 4 słupkami dociskającymi płytkę |
| `ender3_config.ini` | Profil PrusaSlicer (Ender-3 Pro + Devil Design PLA): wzorzec e-paper poprawiony do oficjalnego profilu Creality |
| `tests/test_enclosure.py` | Testy wymiarów, geometrii i kierunku (`python3 -m unittest discover -s enclosure/tests`) |

## Układ współrzędnych

Prawoskrętny: **X w prawo i Y w górę patrząc na ekran, Z w stronę patrzącego**; front na Z = 27,36, tył pokrywy na Z = 0. Pliki STL są obrócone do druku (front na stole) wyłącznie obrotem – bez lustra. Test `OrientationTests` pilnuje, że patrząc na ekran ST-LINK jest po lewej, a RJ45 po prawej (poprzednia wersja modelu była lustrzana).

## Skąd wymiary

Dane z plików ST dla płytki MB1191 (`mb1191_manufacturing.zip`, `mb1191-bom.zip` ze strony 32F746GDISCOVERY → CAD Resources); złącza i miejsce na LCD są takie same w rewizjach B, C i E. Pozycje „od przodu”: x od lewej krawędzi PCB, y od dolnej.

| Dane | Wartość | Źródło |
|---|---|---|
| PCB | 130 × 80 × 1,15 mm; otwory Ø3,25, 4 mm od krawędzi | Gerber, model STEP |
| Moduł LCD | 105,5 × 67,2 × 4,6 mm, 2,0 mm nad PCB; lewa krawędź 12,45, dolna 6,42 | model STEP (U19) |
| Pole aktywne | 95,04 × 53,86; 5,23 z boków, 3,77 od góry, 9,57 od dołu (taśma FPC) | karta Rocktech RK043FN48H (typowe dla 4,3″) |
| Złącza (środki x) | CN14 ST-LINK 20,50, CN13 USB FS 41,75, CN12 USB HS 59,24, RJ45 80,81 | pick-and-place |
| RJ45 Hanrun HR961160C | front 16,10 szer., 13,61 wys. – najwyższy element strony złączy | BOM, karta Hanrun |
| Założone (bez danych ST) | oś gniazd USB nad PCB: micro 1,5, mini 2,0; osłony wtyczek: micro 11 × 8,5, mini 12 × 8 | typowe – sprawdź `fit_bottom` |

## Konstrukcja i wymiary

| | |
|---|---|
| Obudowa (zewnętrznie) | **138 × 88 × 27,36 mm**, ścianki 2,5 mm, narożniki r = 2 |
| Okno | **97,64 × 56,46 mm** = pole aktywne + 1,3 mm z każdej strony; front zachodzi na moduł 3,9 / 3,9 / 8,3 / 2,5 mm (L / P / dół / góra) |
| Rant na moduł LCD | 107,5 × 69,2 mm wewnątrz (moduł + 2 mm luzu), 2 mm wysokości, 1,2 mm grubości – wyznacza położenie płytki |
| Wnętrze | 133 × 83 mm (PCB + 1,5 mm z każdej strony) |
| Głębokość (od tyłu) | pokrywa 0–2,5; elementy strony złączy 4,0–17,61; PCB 17,61–18,76; LCD 20,76–25,36; front 25,36–27,36 |
| Wycięcia (dolna ścianka) | CN14 **14,6 × 10,6**, CN13 **13,6 × 11,1**, RJ45 **18,7 mm** szer. (otwarte od tyłu, do Z 18,91) = obrys wtyczki + 1,3 mm na stronę; USB HS i jacki audio za pełną ścianką |
| Pokrywa | 134,9 × 84,9 × 2,5 mm we wnęce (na równo z tyłem), 4 słupki Ø8 w osiach otworów PCB, czoło na Z 14,61 (3 mm przed PCB) |
| Wentylacja | 8 szczelin 2 × 14 mm w górnej ściance |

## Parametry druku (Ender-3 Pro, Devil Design PLA)

Profil `ender3_config.ini`: dysza 0,4 / warstwa 0,2, 3 perymetry, 4 + 4 warstwy pełne, 20 % gyroid, 215/210 °C, stół 60 °C, brim 5 mm, **bez supportów**.

Zmiany względem profilu obudowy e-paper (zgodnie z oficjalnym profilem Creality Ender-3 w PrusaSlicer), po wydrukach z nierównymi ściankami (część warstw cofnięta, część wystająca poza obrys):

- **bez Z-hopu** (`retract_lift` 0,2 → 0) – pojedyncza śruba Z ma luz, a podnoszenie i opuszczanie przy każdej retrakcji mogło dawać warstwy na różnej wysokości (najbardziej prawdopodobna przyczyna w profilu);
- retrakcja przy zmianie warstwy i wipe;
- ścianka zewnętrzna 35 → 25 mm/s, pozostałe 45 → 40, mosty 60 → 25 mm/s (przepływ 0,95);
- kompensacja „stopy słonia” 0,1 mm (front leży na stole);
- przyspieszenia 500 mm/s² i jerk 8 wysyłane w G-code (M201–M205);
- stół stale 60 °C (bez zmiany temperatury po pierwszej warstwie).

G-code z poprawnego modelu ma identyczny obrys na każdej warstwie (±0,01 mm), więc jeśli ścianki dalej będą nierówne, przyczyna jest mechaniczna: oś Z (zacięcia, krzywa śruba, sprzęgło), ekscentryki rolek, pęknięte ramię ekstrudera, wilgotny filament.

## Czas druku i filament (PrusaSlicer 2.9.4)

| Część | Orientacja na stole | Czas | Filament |
|---|---|---|---|
| `fit_bottom` | front na stole | ~1 h 27 min | 4,8 m (11,5 cm³) |
| `fit_test` | front na stole | ~1 h 31 min | 5,6 m (13,5 cm³) |
| `case_body` | front na stole, ścianki rosną w górę | ~4 h 22 min | 15,6 m (37,4 cm³) |
| `case_cover` | płyta na stole, słupki w górę | ~2 h 52 min | 10,4 m (24,9 cm³) |
| **Razem (korpus + pokrywa)** | | **~7 h 14 min** | **26,0 m (~62 cm³)** |

## Montaż

1. **Wydruki próbne.** `fit_bottom`: włóż wtyczki micro-USB, mini-USB i RJ45 w wycięcia – mają przejść swobodnie. `fit_test`: połóż oknem do stołu i włóż płytkę ekranem w rant; moduł LCD ma wpaść swobodnie, a w oknie ma być widać cały obraz. ⚠️ **Nigdy nie wciskaj szkła.** Poprawki – sekcja niżej.
2. **Płytka.** Odkręć 4 fabryczne nylonowe nóżki. Przestaw zworkę **JP1 na `usb_fs`** (zasilanie z micro-USB CN13 – zasilacz USB nie uruchomi płytki przez ST-LINK CN14).
3. **Korpus.** Włóż płytkę ekranem w rant korpusu, złączami do dolnych wycięć.
4. **Podkładki.** Na czoła 4 słupków pokrywy naklej podkładki piankowe lub gumowe ~3 mm.
5. **Pokrywa.** Włóż pokrywę we wnękę – słupki dociskają płytkę do frontu. Przyklej ją na gorąco w kilku punktach na obwodzie (klej da się później rozgrzać).
6. **Kable.** Micro-USB do CN13 i Ethernet do RJ45 przez wycięcia; mini-USB do CN14 tylko do wgrywania firmware.
7. **Ściana.** Na płaski tył pokrywy naklej taśmę dwustronną (np. montażową na kafle) i dociśnij do kafli.

## Zmiana parametrów

| Problem | Parametr |
|---|---|
| Wtyczka nie wchodzi w wycięcie | `micro_plug_w/h`, `mini_plug_w/h`, `rj45_face_w`, `cut_margin` |
| Wtyczka trafia za wysoko / za nisko | `cn13_axis`, `cn14_axis` |
| Moduł LCD nie wchodzi w rant / lata | `lcd_play` (okno i wycięcia rosną razem z nim) |
| W oknie brakuje obrazu z boku / u góry lub dołu | `lcd_left_on_pcb` (12,45), `lcd_bottom_on_pcb` (6,42), `active_top_in_lcd` (3,77) |
| Pokrywa nie domyka się / płytka lata | `foam_gap` (3,0), `back_clear` (1,5) |
| Pokrywa nie wchodzi we wnękę | `cover_tol` (0,3) |

Ponowne wygenerowanie plików do druku (z katalogu `enclosure/`):

```bash
openscad -D 'part="body"'       -o case_body.stl  smarthouse_case.scad
openscad -D 'part="cover"'      -o case_cover.stl smarthouse_case.scad
openscad -D 'part="fit"'        -o fit_test.stl   smarthouse_case.scad
openscad -D 'part="fit_bottom"' -o fit_bottom.stl smarthouse_case.scad
for n in case_body case_cover fit_test fit_bottom; do
  /Applications/PrusaSlicer.app/Contents/MacOS/PrusaSlicer --export-gcode \
    --load ender3_config.ini --center 110,110 -o $n.gcode $n.stl
done
python3 -m unittest discover -s tests
```
