"""Kontrakt obudowy naściennej panelu STM32F746G-DISCO (spec „poprawka-obudowy”, wcześniej „obudowa-scienna”).

Wymaga OpenSCAD (`openscad` w PATH). Uruchom: python3 -m unittest discover -s enclosure/tests

Układ współrzędnych urządzenia (prawoskrętny, moduły body(), cover(), fit_test(), fit_bottom()):
X w prawo i Y w górę PATRZĄC NA EKRAN, Z w stronę patrzącego. Front (płyta z oknem) na Z = depth,
tył pokrywy na Z = 0. „Patrzeć od przodu” = patrzeć w kierunku −Z.
Wymiary płytki: pliki ST MB1191 (Gerber, pick-and-place, STEP), BOM, karta Hanrun HR961160C.
"""

import os
import re
import subprocess
import tempfile
import unittest

ENCLOSURE = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCAD = os.path.join(ENCLOSURE, "smarthouse_case.scad")
# Profil druku: wzorzec e-paper (Ender-3 Pro, Devil Design PLA) poprawiony do oficjalnego profilu
# Creality w PrusaSlicer – bez Z-hopu (nierówne ścianki), wipe, wolniejsze ścianki zewnętrzne i mosty.
PROFILE = {
    "gcode_flavor": "marlin", "bed_shape": "0x0,220x0,220x220,0x220", "nozzle_diameter": "0.4",
    "retract_length": "5", "retract_speed": "45", "retract_lift": "0", "retract_layer_change": "1",
    "wipe": "1", "machine_limits_usage": "emit_to_gcode", "default_acceleration": "500",
    "machine_max_acceleration_extruding": "500", "machine_max_jerk_x": "8", "machine_max_jerk_y": "8",
    "first_layer_temperature": "215", "temperature": "210",
    "first_layer_bed_temperature": "60", "bed_temperature": "60",
    "min_fan_speed": "100", "disable_fan_first_layers": "3",
    "layer_height": "0.2", "first_layer_height": "0.2", "perimeters": "3",
    "top_solid_layers": "4", "bottom_solid_layers": "4", "fill_density": "20%", "fill_pattern": "gyroid",
    "perimeter_speed": "40", "external_perimeter_speed": "25", "bridge_speed": "25",
    "first_layer_speed": "20", "extrusion_width": "0.45",
    "support_material": "0", "brim_width": "5", "elefant_foot_compensation": "0.1",
}

# --- Dane ST (od przodu: x od lewej krawędzi PCB, y od dolnej) ------------------------------
PCB_W, PCB_H, PCB_T = 130.0, 80.0, 1.15                 # Gerber (obrys), STEP
HOLE_INSET, HOLE_D = 4.0, 3.25                          # wiercenia Gerber
LCD_W, LCD_H, LCD_T = 105.5, 67.2, 4.6                  # STEP (U19)
LCD_GAP = 2.0                                           # STEP: LCD 2,0 mm nad PCB
LCD_LEFT_ON_PCB, LCD_BOTTOM_ON_PCB = 12.45, 6.42        # STEP
ACTIVE_W, ACTIVE_H = 95.04, 53.86                       # karta RK043FN48H (typowe dla 4,3″)
ACTIVE_LEFT_IN_LCD = (LCD_W - ACTIVE_W) / 2             # 5.23
ACTIVE_TOP_IN_LCD = 3.77
ACTIVE_BOTTOM_IN_LCD = LCD_H - ACTIVE_H - ACTIVE_TOP_IN_LCD   # 9.57: taśma FPC u dołu
CN14_X, CN13_X, CN12_X, RJ45_X = 20.50, 41.75, 59.24, 80.81   # pick-and-place (127 − X_Altium)
AUDIO_X = (105.0, 116.2)
PARTS_H = 13.61                                         # RJ45 HR961160C – najwyższy element strony złączy
CN14_AXIS, CN13_AXIS = 2.0, 1.5                         # oś gniazda nad PCB (mini-B, micro-AB top-mount)
MINI_PLUG_W, MINI_PLUG_H = 12.0, 8.0                    # obrys wtyczek (osłona)
MICRO_PLUG_W, MICRO_PLUG_H = 11.0, 8.5
RJ45_FACE_W = 16.10                                     # karta Hanrun

# --- Konstrukcja --------------------------------------------------------------------------
LCD_PLAY = 2.0
WINDOW_MARGIN = LCD_PLAY / 2 + 0.3                      # 1.3
CUT_MARGIN = LCD_PLAY / 2 + 0.3                         # 1.3: luz rantu + tolerancja
WALL, PCB_SIDE_PLAY = 2.5, 1.5
FRONT_T, RIM_H, RIM_T = 2.0, 2.0, 1.2
COVER_T, LEDGE, COVER_TOL = 2.5, 1.25, 0.3
BACK_CLEAR, FOAM_GAP, POST_D = 1.5, 3.0, 8.0
VENT_COUNT, VENT_W, VENT_H, VENT_PITCH, VENT_Z0 = 8, 2.0, 14.0, 8.0, 5.0
FIT_BOTTOM_H = 12.0

# --- Wyprowadzone ---------------------------------------------------------------------------
INNER_W, INNER_H = PCB_W + 2 * PCB_SIDE_PLAY, PCB_H + 2 * PCB_SIDE_PLAY     # 133 × 83
W, H = INNER_W + 2 * WALL, INNER_H + 2 * WALL                              # 138 × 88
PCB_X0 = PCB_Y0 = WALL + PCB_SIDE_PLAY                                     # 4.0
PARTS_Z0 = COVER_T + BACK_CLEAR                                            # 4.0
PCB_CONN_Z = PARTS_Z0 + PARTS_H                                            # 17.61: strona złączy PCB
PCB_LCD_Z = PCB_CONN_Z + PCB_T                                             # 18.76
LCD_BACK_Z = PCB_LCD_Z + LCD_GAP                                           # 20.76
FRONT_IN_Z = LCD_BACK_Z + LCD_T                                            # 25.36
D = FRONT_IN_Z + FRONT_T                                                   # 27.36
POST_FRONT_Z = PCB_CONN_Z - FOAM_GAP                                       # 14.61
COVER_W = W - 2 * LEDGE - 2 * COVER_TOL                                    # 134.9
COVER_H = H - 2 * LEDGE - 2 * COVER_TOL                                    # 84.9
LCD_X0, LCD_Y0 = PCB_X0 + LCD_LEFT_ON_PCB, PCB_Y0 + LCD_BOTTOM_ON_PCB      # 16.45, 10.42
ACTIVE_X0 = LCD_X0 + ACTIVE_LEFT_IN_LCD                                    # 21.68
ACTIVE_Y0 = LCD_Y0 + ACTIVE_BOTTOM_IN_LCD                                  # 19.99
WINDOW_W, WINDOW_H = ACTIVE_W + 2 * WINDOW_MARGIN, ACTIVE_H + 2 * WINDOW_MARGIN
WINDOW_X0, WINDOW_Y0 = ACTIVE_X0 - WINDOW_MARGIN, ACTIVE_Y0 - WINDOW_MARGIN
RIM_IN_W, RIM_IN_H = LCD_W + LCD_PLAY, LCD_H + LCD_PLAY
RIM_X0, RIM_Y0 = LCD_X0 - LCD_PLAY / 2, LCD_Y0 - LCD_PLAY / 2
RIM_Z0 = FRONT_IN_Z - RIM_H                                                # 23.36

# Wycięcia w dolnej ściance: (środek X, środek Z, szerokość, wysokość); RJ45 otwarte od tyłu
CUT_CN14 = (PCB_X0 + CN14_X, PCB_CONN_Z - CN14_AXIS, MINI_PLUG_W + 2 * CUT_MARGIN, MINI_PLUG_H + 2 * CUT_MARGIN)
CUT_CN13 = (PCB_X0 + CN13_X, PCB_CONN_Z - CN13_AXIS, MICRO_PLUG_W + 2 * CUT_MARGIN, MICRO_PLUG_H + 2 * CUT_MARGIN)
RJ45_CUT_X = PCB_X0 + RJ45_X                                               # 84.81
RJ45_CUT_W = RJ45_FACE_W + 2 * CUT_MARGIN                                  # 18.7
RJ45_CUT_Z1 = PCB_CONN_Z + CUT_MARGIN                                      # 18.91 (od tyłu do tego Z)

HOLES = [(PCB_X0 + HOLE_INSET + i * (PCB_W - 2 * HOLE_INSET), PCB_Y0 + HOLE_INSET + j * (PCB_H - 2 * HOLE_INSET))
         for i in (0, 1) for j in (0, 1)]
VENT_CENTERS = [W / 2 + (i - (VENT_COUNT - 1) / 2) * VENT_PITCH for i in range(VENT_COUNT)]
BED = 220.0


def run_openscad(args):
    return subprocess.run(["openscad", *args], capture_output=True, text=True)


def echo_params():
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "params.echo")
        run_openscad(["-o", out, "-D", 'part="body"', SCAD])
        text = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
    return {m.group(1): float(m.group(2)) for m in re.finditer(r"PARAM_(\w+) = (-?[\d.e+-]+)", text)}


def stl_bbox(path):
    xs, ys, zs = [], [], []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            parts = line.split()
            if len(parts) == 4 and parts[0] == "vertex":
                xs.append(float(parts[1]))
                ys.append(float(parts[2]))
                zs.append(float(parts[3]))
    if not xs:
        return None
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))


def export_bbox(part):
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, f"{part}.stl")
        run_openscad(["-o", out, "-D", f'part="{part}"', SCAD])
        return stl_bbox(out) if os.path.exists(out) else None


class ProbeError(AssertionError):
    pass


def intersects(first, second):
    """True, gdy dwie bryły OpenSCAD mają część wspólną; False, gdy przecięcie jest puste.

    Błąd OpenSCAD (składnia, brak modułu) to ProbeError, a nie „pusto”.
    """
    with tempfile.TemporaryDirectory() as tmp:
        probe = os.path.join(tmp, "probe.scad")
        out = os.path.join(tmp, "probe.stl")
        with open(probe, "w", encoding="utf-8") as f:
            f.write(f"use <{SCAD}>\nintersection() {{ {first} {second} }}\n")
        result = run_openscad(["-o", out, probe])
        if result.returncode == 0 and os.path.exists(out) and os.path.getsize(out) > 0:
            return True
        if "Current top level object is empty" in result.stderr:
            return False
        raise ProbeError(f"OpenSCAD nie zbudował sondy: {result.stderr[-400:]}")


def box(x, y, z, dx, dy, dz):
    return f"translate([{x}, {y}, {z}]) cube([{dx}, {dy}, {dz}]);"


def is_solid(module, x, y, z, dx, dy, dz):
    """True, gdy moduł ma materiał w prostopadłościanie [x, x+dx] × [y, y+dy] × [z, z+dz]."""
    return intersects(f"{module}();", box(x, y, z, dx, dy, dz))


def wall_probe(module, x, z, dx, dz):
    """Materiał w dolnej ściance (przez całą jej grubość) w zakresie X / Z."""
    return is_solid(module, x, -0.1, z, dx, WALL + 0.2, dz)


class PrintProfileTests(unittest.TestCase):
    def test_profile_matches_ender3_pla_settings(self):
        """Profil druku: Ender-3 Pro + Devil Design PLA, ustawienia z PROFILE (bez Z-hopu)."""
        path = os.path.join(ENCLOSURE, "ender3_config.ini")
        self.assertTrue(os.path.exists(path), "brak enclosure/ender3_config.ini")
        cfg = {}
        with open(path, encoding="utf-8") as f:
            for line in f:
                if "=" in line and not line.lstrip().startswith("#"):
                    key, value = line.split("=", 1)
                    cfg[key.strip()] = value.strip()
        for key, value in PROFILE.items():
            self.assertEqual(cfg.get(key), value, key)

    def test_gcode_uses_profile(self):
        """G-code wygenerowany z aktualnego profilu: bez Z-hopu, przyspieszenia w G-code."""
        for name in ("case_body", "case_cover", "fit_test", "fit_bottom"):
            with open(os.path.join(ENCLOSURE, f"{name}.gcode"), encoding="utf-8") as f:
                text = f.read()
            for key in ("retract_lift", "wipe", "external_perimeter_speed", "bridge_speed",
                        "elefant_foot_compensation", "bed_temperature"):
                self.assertIn(f"; {key} = {PROFILE[key]}\n", text, f"{name}: {key}")
            self.assertRegex(text, r"\nM201 X500 Y500", f"{name}: brak limitów przyspieszeń")


class ParameterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.p = echo_params()

    def test_model_reports_contract_dimensions(self):
        expected = {
            "pcb_w": PCB_W, "pcb_h": PCB_H, "pcb_t": PCB_T, "hole_inset": HOLE_INSET, "hole_d": HOLE_D,
            "lcd_w": LCD_W, "lcd_h": LCD_H, "lcd_t": LCD_T, "lcd_gap": LCD_GAP,
            "lcd_left_on_pcb": LCD_LEFT_ON_PCB, "lcd_bottom_on_pcb": LCD_BOTTOM_ON_PCB,
            "active_w": ACTIVE_W, "active_h": ACTIVE_H, "active_left_in_lcd": ACTIVE_LEFT_IN_LCD,
            "active_top_in_lcd": ACTIVE_TOP_IN_LCD, "active_bottom_in_lcd": ACTIVE_BOTTOM_IN_LCD,
            "cn14_x": CN14_X, "cn13_x": CN13_X, "rj45_x": RJ45_X, "parts_h": PARTS_H,
            "cn14_axis": CN14_AXIS, "cn13_axis": CN13_AXIS,
            "mini_plug_w": MINI_PLUG_W, "mini_plug_h": MINI_PLUG_H,
            "micro_plug_w": MICRO_PLUG_W, "micro_plug_h": MICRO_PLUG_H, "rj45_face_w": RJ45_FACE_W,
            "lcd_play": LCD_PLAY, "window_margin": WINDOW_MARGIN, "cut_margin": CUT_MARGIN,
            "wall": WALL, "pcb_side_play": PCB_SIDE_PLAY, "front_t": FRONT_T, "rim_h": RIM_H, "rim_t": RIM_T,
            "cover_t": COVER_T, "ledge": LEDGE, "cover_tol": COVER_TOL,
            "back_clear": BACK_CLEAR, "foam_gap": FOAM_GAP, "post_d": POST_D,
            "width": W, "height": H, "depth": D, "cover_w": COVER_W, "cover_h": COVER_H,
            "pcb_conn_z": PCB_CONN_Z, "front_in_z": FRONT_IN_Z, "post_front_z": POST_FRONT_Z,
            "window_w": WINDOW_W, "window_h": WINDOW_H, "rim_in_w": RIM_IN_W, "rim_in_h": RIM_IN_H,
            "inner_w": INNER_W, "inner_h": INNER_H,
            "vent_count": VENT_COUNT, "vent_w": VENT_W, "vent_h": VENT_H, "vent_pitch": VENT_PITCH,
            "vent_z0": VENT_Z0, "fit_bottom_h": FIT_BOTTOM_H,
        }
        for key, value in expected.items():
            self.assertIn(key, self.p, f"PARAM_{key} nie jest wypisywany przez echo")
            self.assertAlmostEqual(self.p[key], value, places=2, msg=key)

    def test_window_covers_active_area_with_module_play(self):
        self.assertIn("window_w", self.p, "brak PARAM_window_w")
        shift = LCD_PLAY / 2
        self.assertLess(WINDOW_X0, ACTIVE_X0 - shift)
        self.assertGreater(WINDOW_X0 + self.p["window_w"], ACTIVE_X0 + ACTIVE_W + shift)
        self.assertLess(WINDOW_Y0, ACTIVE_Y0 - shift)
        self.assertGreater(WINDOW_Y0 + self.p["window_h"], ACTIVE_Y0 + ACTIVE_H + shift)
        overlaps = {
            "lewa": WINDOW_X0 - LCD_X0,
            "prawa": LCD_X0 + LCD_W - (WINDOW_X0 + self.p["window_w"]),
            "dolna": WINDOW_Y0 - LCD_Y0,
            "górna": LCD_Y0 + LCD_H - (WINDOW_Y0 + self.p["window_h"]),
        }
        for side, overlap in overlaps.items():
            self.assertGreaterEqual(overlap, 2.0, f"front zachodzi na moduł z {side} strony tylko {overlap:.2f} mm")

    def test_fits_on_bed(self):
        self.assertIn("width", self.p, "brak PARAM_width")
        self.assertLessEqual(self.p["width"], BED - 20)
        self.assertLessEqual(self.p["height"], BED - 20)


class OrientationTests(unittest.TestCase):
    """Strzeże przed lustrzanym odbiciem (błąd poprzedniej wersji)."""

    def test_front_plate_is_at_max_z(self):
        self.assertTrue(is_solid("body", PCB_X0, 40, D - 0.5, 8, 10, 0.4), "front ma leżeć przy Z = depth")
        self.assertFalse(is_solid("body", PCB_X0, 40, 0.1, 8, 10, 0.4), "przy Z = 0 nie może być frontu")
        cx, cy = ACTIVE_X0 + ACTIVE_W / 2, ACTIVE_Y0 + ACTIVE_H / 2
        self.assertFalse(is_solid("body", cx, cy, D - FRONT_T - 0.1, 2, 2, FRONT_T + 0.2), "okno przy Z = depth")

    def test_stlink_left_and_rj45_right_seen_from_front(self):
        cx, cz, _, _ = CUT_CN14
        self.assertLess(cx, W / 2, "CN14 ma być po lewej patrząc na ekran")
        self.assertGreater(RJ45_CUT_X, W / 2, "RJ45 ma być po prawej patrząc na ekran")
        self.assertFalse(wall_probe("body", cx - 1, cz - 1, 2, 2), "brak wycięcia CN14 po lewej")
        self.assertTrue(wall_probe("body", W - cx - 1, cz - 1, 2, 2), "lustrzane miejsce CN14 ma być pełne")
        self.assertFalse(wall_probe("body", RJ45_CUT_X - 1, PCB_CONN_Z - 8, 2, 2), "brak wycięcia RJ45 po prawej")
        self.assertTrue(wall_probe("body", W - RJ45_CUT_X - 0.2, PCB_CONN_Z - 8, 0.4, 2),
                        "lustrzane miejsce RJ45 ma być pełne")


class GeometryTests(unittest.TestCase):
    def test_window_open_at_active_area_corners_and_front_solid_outside(self):
        self.assertTrue(is_solid("body", PCB_X0, 40, FRONT_IN_Z, 8, 10, FRONT_T), "front poza oknem pełny")
        corners = [(ACTIVE_X0, ACTIVE_Y0), (ACTIVE_X0 + ACTIVE_W - 0.5, ACTIVE_Y0),
                   (ACTIVE_X0, ACTIVE_Y0 + ACTIVE_H - 0.5), (ACTIVE_X0 + ACTIVE_W - 0.5, ACTIVE_Y0 + ACTIVE_H - 0.5)]
        for x, y in corners:
            self.assertFalse(is_solid("body", x, y, FRONT_IN_Z - 0.1, 0.5, 0.5, FRONT_T + 0.2),
                             f"okno zasłania pole aktywne w ({x:.2f}, {y:.2f})")

    def test_rim_holds_module_and_inside_is_free(self):
        mid_x, mid_y = RIM_X0 + RIM_IN_W / 2, RIM_Y0 + RIM_IN_H / 2
        z, h = RIM_Z0 + 0.2, RIM_H - 0.4
        self.assertTrue(is_solid("body", RIM_X0 - RIM_T + 0.1, mid_y, z, RIM_T - 0.2, 2, h), "lewy rant")
        self.assertTrue(is_solid("body", RIM_X0 + RIM_IN_W + 0.1, mid_y, z, RIM_T - 0.2, 2, h), "prawy rant")
        self.assertTrue(is_solid("body", mid_x, RIM_Y0 - RIM_T + 0.1, z, 2, RIM_T - 0.2, h), "dolny rant")
        self.assertTrue(is_solid("body", mid_x, RIM_Y0 + RIM_IN_H + 0.1, z, 2, RIM_T - 0.2, h), "górny rant")
        self.assertFalse(is_solid("body", RIM_X0 + 0.1, RIM_Y0 + 0.1, PCB_LCD_Z,
                                  RIM_IN_W - 0.2, RIM_IN_H - 0.2, FRONT_IN_Z - PCB_LCD_Z - 0.01),
                         "miejsce na moduł LCD ma być wolne")

    def test_interior_free_for_pcb_and_parts(self):
        self.assertTrue(is_solid("body", 0, 40, 5, WALL - 0.1, 10, 15), "lewa ścianka pełna")
        self.assertFalse(is_solid("body", WALL + 0.1, 14, PARTS_Z0, INNER_W - 0.2, INNER_H - 28,
                                  PCB_LCD_Z - PARTS_Z0), "wnętrze (PCB + elementy) wolne")
        self.assertFalse(is_solid("body", 14, WALL + 0.1, PARTS_Z0, INNER_W - 26, INNER_H - 0.2,
                                  PCB_LCD_Z - PARTS_Z0))

    def test_cutouts_match_plugs(self):
        for name, (cx, cz, w, h) in {"CN14": CUT_CN14, "CN13": CUT_CN13}.items():
            self.assertFalse(wall_probe("body", cx - w / 2 + 0.2, cz - h / 2 + 0.2, w - 0.4, h - 0.4),
                             f"wycięcie {name} za małe lub brak")
            self.assertTrue(wall_probe("body", cx - w / 2 - 0.7, cz - 1, 0.4, 2), f"{name}: brak materiału z lewej")
            self.assertTrue(wall_probe("body", cx + w / 2 + 0.3, cz - 1, 0.4, 2), f"{name}: brak materiału z prawej")
            self.assertTrue(wall_probe("body", cx - 1, cz + h / 2 + 0.3, 2, 0.4), f"{name}: brak materiału od frontu")
            self.assertTrue(wall_probe("body", cx - 1, cz - h / 2 - 0.7, 2, 0.4), f"{name}: brak materiału od tyłu")
        # RJ45: otwarte od tyłu do PCB_CONN_Z + margines
        self.assertFalse(wall_probe("body", RJ45_CUT_X - RJ45_CUT_W / 2 + 0.2, 0.1,
                                    RJ45_CUT_W - 0.4, RJ45_CUT_Z1 - 0.3), "wycięcie RJ45 za małe lub brak")
        self.assertTrue(wall_probe("body", RJ45_CUT_X - RJ45_CUT_W / 2 - 0.7, 5, 0.4, 5), "RJ45: materiał z lewej")
        self.assertTrue(wall_probe("body", RJ45_CUT_X + RJ45_CUT_W / 2 + 0.3, 5, 0.4, 5), "RJ45: materiał z prawej")
        self.assertTrue(wall_probe("body", RJ45_CUT_X - 1, RJ45_CUT_Z1 + 0.3, 2, 0.4), "RJ45: materiał od frontu")

    def test_bottom_wall_closed_at_unused_connectors(self):
        for name, x in {"CN12 USB HS": CN12_X, "audio CN11": AUDIO_X[0], "audio CN10": AUDIO_X[1]}.items():
            self.assertTrue(wall_probe("body", PCB_X0 + x - 1, PCB_CONN_Z - 4, 2, 3), f"ścianka otwarta przy {name}")

    def test_cover_posts_and_flat_back(self):
        for x, y in HOLES:
            self.assertTrue(is_solid("cover", x - 1, y - 1, COVER_T + 0.1, 2, 2, POST_FRONT_Z - COVER_T - 0.2),
                            f"brak słupka przy otworze ({x}, {y})")
            self.assertFalse(is_solid("cover", x - 3, y - 3, POST_FRONT_Z + 0.1, 6, 6, PCB_CONN_Z - POST_FRONT_Z - 0.2),
                             f"słupek ({x}, {y}) za długi – brak miejsca na podkładkę 3 mm")
        x0 = y0 = LEDGE + COVER_TOL + 0.5
        for x, y in [(x0, y0), (W - x0 - 2, y0), (x0, H - y0 - 2), (W - x0 - 2, H - y0 - 2), (W / 2, H / 2)]:
            self.assertTrue(is_solid("cover", x, y, 0.1, 2, 2, COVER_T - 0.2), f"pokrywa niepełna w ({x:.1f}, {y:.1f})")
        self.assertFalse(is_solid("cover", -1, -1, -5, W + 2, H + 2, 4.99), "nic nie wystaje za tył (Z < 0)")

    def test_top_wall_has_vent_slots(self):
        y, z = H - WALL - 0.1, VENT_Z0 + 1
        self.assertTrue(is_solid("body", VENT_CENTERS[0] + VENT_PITCH / 2 - 0.5, y, z, 1, WALL + 0.2, VENT_H - 2),
                        "między szczelinami ma być materiał")
        for c in VENT_CENTERS:
            self.assertFalse(is_solid("body", c - VENT_W / 2 + 0.2, y, z, VENT_W - 0.4, WALL + 0.2, VENT_H - 2),
                             f"brak szczeliny wentylacyjnej w x = {c}")


class ExportTests(unittest.TestCase):
    def assert_bbox(self, part, expected):
        bbox = export_bbox(part)
        self.assertIsNotNone(bbox, f"{part}: pusty eksport")
        for got, want in zip(sorted(bbox), sorted(expected)):
            self.assertAlmostEqual(got, want, places=1, msg=f"{part}: {bbox} vs {expected}")

    def test_part_bounding_boxes(self):
        self.assert_bbox("body", (W, H, D))
        self.assert_bbox("cover", (COVER_W, COVER_H, POST_FRONT_Z))
        self.assert_bbox("fit", (W, H, FRONT_T + RIM_H))
        self.assert_bbox("fit_bottom", (W, FIT_BOTTOM_H, D))


class PrintFilesTests(unittest.TestCase):
    def test_stl_files_match_model(self):
        for name, part in [("case_body", "body"), ("case_cover", "cover"), ("fit_test", "fit"),
                           ("fit_bottom", "fit_bottom")]:
            path = os.path.join(ENCLOSURE, f"{name}.stl")
            self.assertTrue(os.path.exists(path), f"brak {name}.stl")
            got, want = stl_bbox(path), export_bbox(part)
            self.assertIsNotNone(got, f"{name}.stl pusty")
            self.assertIsNotNone(want, f"{part}: pusty eksport")
            for a, b in zip(sorted(got), sorted(want)):
                self.assertAlmostEqual(a, b, places=1, msg=f"{name}.stl nieaktualny względem modelu")
            self.assertTrue(os.path.exists(os.path.join(ENCLOSURE, f"{name}.gcode")), f"brak {name}.gcode")


if __name__ == "__main__":
    unittest.main()
