// =============================================================================
// SMARTHOUSE - OBUDOWA NASCIENNA PANELU STM32F746G-DISCO (spec: obudowa-scienna, poprawka-obudowy)
// =============================================================================
// Druk: Creality Ender-3 Pro, Devil Design PLA, profil ender3_config.ini (jak obudowa e-paper).
// Wszystkie czesci drukuja sie BEZ supportow. Montaz: plaski tyl pokrywy na tasme dwustronna do kafli.
//
// Uklad wspolrzednych urzadzenia (PRAWOSKRETNY; moduly body(), cover(), fit_test(), fit_bottom()):
//   X w prawo i Y w gore PATRZAC NA EKRAN, Z w strone patrzacego.
//   Front (plyta z oknem) na Z = depth, tyl pokrywy na Z = 0. Zlacza plytki i tasma FPC LCD u dolu.
//   (Poprzednia wersja miala Z do tylu przy X w prawo - lewoskretnie, czyli lustro.)
//
// Wymiary plytki z plikow ST MB1191 (Gerber, pick-and-place, STEP), BOM i karty Hanrun HR961160C.
// Polozenie wyznacza modul LCD (rant od wewnatrz frontu); PCB nie jest przykrecana - slupki
// pokrywy dociskaja ja przez podkladki 3 mm. Fabryczne nylonowe nozki plytki odkrecic.
//
// Eksport do druku (orientacja na stole; tylko obroty - bez lustra):
//   openscad -D 'part="body"'       -o case_body.stl  smarthouse_case.scad  // front na stole
//   openscad -D 'part="cover"'      -o case_cover.stl smarthouse_case.scad  // plyta na stole, slupki w gore
//   openscad -D 'part="fit"'        -o fit_test.stl   smarthouse_case.scad  // front z rantem - proba okna
//   openscad -D 'part="fit_bottom"' -o fit_bottom.stl smarthouse_case.scad  // dolna scianka - proba wtyczek
// Parametry wypisywane przez echo(PARAM_<nazwa> = ...) czytaja testy (tests/).
// =============================================================================

part = "both";
$fn = 48;

// === PLYTKA (pliki ST MB1191) - od przodu: x od lewej krawedzi PCB, y od dolnej ===
pcb_w = 130.0;
pcb_h = 80.0;
pcb_t = 1.15;                          // STEP
hole_inset = 4.0;                      // Gerber: otwory 4 mm od krawedzi
hole_d = 3.25;

// Modul LCD (STEP, U19): obrys, grubosc, odstep od PCB, polozenie
lcd_w = 105.5;
lcd_h = 67.2;
lcd_t = 4.6;
lcd_gap = 2.0;
lcd_left_on_pcb = 12.45;
lcd_bottom_on_pcb = 6.42;
// Pole aktywne w module (karta RK043FN48H; STEP ma LCD jako bryle) - typowe dla 4,3" 480x272
active_w = 95.04;
active_h = 53.86;
active_left_in_lcd = (lcd_w - active_w) / 2;
active_top_in_lcd = 3.77;
active_bottom_in_lcd = lcd_h - active_h - active_top_in_lcd;   // tasma FPC u dolu

// Zlacza na dolnej krawedzi (pick-and-place: x = 127 - X_Altium), strona zlaczy PCB
cn14_x = 20.50;                        // ST-LINK, mini-B
cn13_x = 41.75;                        // USB FS, micro-AB (Molex 47590-0001) - zasilanie
rj45_x = 80.81;                        // Hanrun HR961160C
parts_h = 13.61;                       // RJ45 - najwyzszy element strony zlaczy (karta Hanrun)
cn14_axis = 2.0;                       // os gniazda nad PCB (typowe top-mount)
cn13_axis = 1.5;
mini_plug_w = 12.0;                    // obrys oslony wtyczek
mini_plug_h = 8.0;
micro_plug_w = 11.0;
micro_plug_h = 8.5;
rj45_face_w = 16.10;                   // karta Hanrun

// === LUZY I KONSTRUKCJA ===
lcd_play = 2.0;                        // modul w rancie: 1 mm na strone
window_margin = lcd_play / 2 + 0.3;
cut_margin = lcd_play / 2 + 0.3;       // wyciecia: luz rantu + tolerancja
wall = 2.5;
pcb_side_play = 1.5;
front_t = 2.0;
rim_h = 2.0;
rim_t = 1.2;
cover_t = 2.5;
ledge = 1.25;                          // pocieniona tylna krawedz scianek = wneka na pokrywe
cover_tol = 0.3;                       // pokrywa klejona na goraco
back_clear = 1.5;
foam_gap = 3.0;                        // podkladka miedzy slupkiem a PCB
post_d = 8.0;
corner_r = 2.0;

vent_count = 8;
vent_w = 2.0;
vent_h = 14.0;
vent_pitch = 8.0;
vent_z0 = 5.0;

fit_bottom_h = 12.0;                   // wydruk probny: pas dolnej scianki

// === WYPROWADZONE ===
inner_w = pcb_w + 2 * pcb_side_play;
inner_h = pcb_h + 2 * pcb_side_play;
width = inner_w + 2 * wall;
height = inner_h + 2 * wall;
pcb_x0 = wall + pcb_side_play;
pcb_y0 = wall + pcb_side_play;
parts_z0 = cover_t + back_clear;
pcb_conn_z = parts_z0 + parts_h;       // strona zlaczy PCB
pcb_lcd_z = pcb_conn_z + pcb_t;
lcd_back_z = pcb_lcd_z + lcd_gap;
front_in_z = lcd_back_z + lcd_t;       // wewnetrzna strona frontu = lico LCD
depth = front_in_z + front_t;
post_front_z = pcb_conn_z - foam_gap;
cover_w = width - 2 * ledge - 2 * cover_tol;
cover_h = height - 2 * ledge - 2 * cover_tol;

lcd_x0 = pcb_x0 + lcd_left_on_pcb;
lcd_y0 = pcb_y0 + lcd_bottom_on_pcb;
active_x0 = lcd_x0 + active_left_in_lcd;
active_y0 = lcd_y0 + active_bottom_in_lcd;
window_w = active_w + 2 * window_margin;
window_h = active_h + 2 * window_margin;
window_x0 = active_x0 - window_margin;
window_y0 = active_y0 - window_margin;
rim_in_w = lcd_w + lcd_play;
rim_in_h = lcd_h + lcd_play;
rim_x0 = lcd_x0 - lcd_play / 2;
rim_y0 = lcd_y0 - lcd_play / 2;
rim_z0 = front_in_z - rim_h;

// wyciecia: [srodek X, srodek Z, szerokosc, wysokosc]
cut_cn14 = [pcb_x0 + cn14_x, pcb_conn_z - cn14_axis, mini_plug_w + 2 * cut_margin, mini_plug_h + 2 * cut_margin];
cut_cn13 = [pcb_x0 + cn13_x, pcb_conn_z - cn13_axis, micro_plug_w + 2 * cut_margin, micro_plug_h + 2 * cut_margin];
rj45_cut_x = pcb_x0 + rj45_x;
rj45_cut_w = rj45_face_w + 2 * cut_margin;
rj45_cut_z1 = pcb_conn_z + cut_margin; // otwarte od tylu do tego Z (bez mostka w druku)

echo(PARAM_pcb_w = pcb_w, PARAM_pcb_h = pcb_h, PARAM_pcb_t = pcb_t, PARAM_hole_inset = hole_inset,
     PARAM_hole_d = hole_d);
echo(PARAM_lcd_w = lcd_w, PARAM_lcd_h = lcd_h, PARAM_lcd_t = lcd_t, PARAM_lcd_gap = lcd_gap,
     PARAM_lcd_left_on_pcb = lcd_left_on_pcb, PARAM_lcd_bottom_on_pcb = lcd_bottom_on_pcb);
echo(PARAM_active_w = active_w, PARAM_active_h = active_h, PARAM_active_left_in_lcd = active_left_in_lcd,
     PARAM_active_top_in_lcd = active_top_in_lcd, PARAM_active_bottom_in_lcd = active_bottom_in_lcd);
echo(PARAM_cn14_x = cn14_x, PARAM_cn13_x = cn13_x, PARAM_rj45_x = rj45_x, PARAM_parts_h = parts_h,
     PARAM_cn14_axis = cn14_axis, PARAM_cn13_axis = cn13_axis);
echo(PARAM_mini_plug_w = mini_plug_w, PARAM_mini_plug_h = mini_plug_h, PARAM_micro_plug_w = micro_plug_w,
     PARAM_micro_plug_h = micro_plug_h, PARAM_rj45_face_w = rj45_face_w);
echo(PARAM_lcd_play = lcd_play, PARAM_window_margin = window_margin, PARAM_cut_margin = cut_margin);
echo(PARAM_wall = wall, PARAM_pcb_side_play = pcb_side_play, PARAM_front_t = front_t, PARAM_rim_h = rim_h,
     PARAM_rim_t = rim_t);
echo(PARAM_cover_t = cover_t, PARAM_ledge = ledge, PARAM_cover_tol = cover_tol, PARAM_back_clear = back_clear,
     PARAM_foam_gap = foam_gap, PARAM_post_d = post_d);
echo(PARAM_width = width, PARAM_height = height, PARAM_depth = depth, PARAM_cover_w = cover_w,
     PARAM_cover_h = cover_h);
echo(PARAM_pcb_conn_z = pcb_conn_z, PARAM_front_in_z = front_in_z, PARAM_post_front_z = post_front_z);
echo(PARAM_window_w = window_w, PARAM_window_h = window_h, PARAM_rim_in_w = rim_in_w, PARAM_rim_in_h = rim_in_h,
     PARAM_inner_w = inner_w, PARAM_inner_h = inner_h);
echo(PARAM_vent_count = vent_count, PARAM_vent_w = vent_w, PARAM_vent_h = vent_h, PARAM_vent_pitch = vent_pitch,
     PARAM_vent_z0 = vent_z0, PARAM_fit_bottom_h = fit_bottom_h);

// Prostopadloscian z zaokraglonymi krawedziami rownoleglymi do Z, z od 0 do d
module rounded_box(w, h, d, r) {
    hull()
        for (x = [r, w - r], y = [r, h - r])
            translate([x, y, 0]) cylinder(r = r, h = d);
}

// Rant wokol modulu LCD, od wewnetrznej strony frontu w strone tylu
module rim() {
    translate([rim_x0 - rim_t, rim_y0 - rim_t, rim_z0])
        difference() {
            cube([rim_in_w + 2 * rim_t, rim_in_h + 2 * rim_t, rim_h]);
            translate([rim_t, rim_t, -1]) cube([rim_in_w, rim_in_h, rim_h + 2]);
        }
}

module window_cut() {
    translate([window_x0, window_y0, front_in_z - 1]) cube([window_w, window_h, front_t + 2]);
}

// Wyciecie w dolnej sciance: srodek X, srodek Z, szerokosc, wysokosc
module wall_cut(c) {
    translate([c[0] - c[2] / 2, -1, c[1] - c[3] / 2]) cube([c[2], wall + 2, c[3]]);
}

module body() {
    difference() {
        rounded_box(width, height, depth, corner_r);
        // wnetrze otwarte od tylu, do wewnetrznej strony frontu
        translate([wall, wall, -1]) cube([inner_w, inner_h, front_in_z + 1]);
        // wneka na pokrywe (scianki pocienione do `ledge`)
        translate([ledge, ledge, -1]) cube([width - 2 * ledge, height - 2 * ledge, cover_t + 1]);
        window_cut();
        // osobne wyciecia na zlacza: ST-LINK, USB FS (zasilanie), RJ45 (otwarte od tylu)
        wall_cut(cut_cn14);
        wall_cut(cut_cn13);
        translate([rj45_cut_x - rj45_cut_w / 2, -1, -1]) cube([rj45_cut_w, wall + 2, rj45_cut_z1 + 1]);
        // szczeliny wentylacyjne w gornej sciance
        for (i = [0 : vent_count - 1])
            translate([width / 2 + (i - (vent_count - 1) / 2) * vent_pitch - vent_w / 2, height - wall - 1, vent_z0])
                cube([vent_w, wall + 2, vent_h]);
    }
    rim();
}

module cover() {
    translate([ledge + cover_tol, ledge + cover_tol, 0]) cube([cover_w, cover_h, cover_t]);
    // slupki w osiach otworow PCB, czolo foam_gap przed strona zlaczy PCB
    for (i = [0, 1], j = [0, 1])
        translate([pcb_x0 + hole_inset + i * (pcb_w - 2 * hole_inset),
                   pcb_y0 + hole_inset + j * (pcb_h - 2 * hole_inset), cover_t - 0.01])
            cylinder(d = post_d, h = post_front_z - cover_t + 0.01);
}

module fit_test() {
    difference() {
        translate([0, 0, front_in_z]) rounded_box(width, height, front_t, corner_r);
        window_cut();
    }
    rim();
}

// Pas dolnej scianki z wycieciami (proba wtyczek) - fragment korpusu
module fit_bottom() {
    intersection() {
        body();
        translate([-1, -1, -1]) cube([width + 2, fit_bottom_h + 1, depth + 2]);
    }
}

// Do druku front na stol: obrot o 180 stopni wokol X (bez lustra)
module front_down() {
    translate([0, height, depth]) rotate([180, 0, 0]) children();
}

if (part == "body") front_down() body();
else if (part == "cover") cover();
else if (part == "fit") front_down() fit_test();
else if (part == "fit_bottom") front_down() fit_bottom();
