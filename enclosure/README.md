# Wall enclosure for the STM32F746G-DISCO panel

An enclosure stuck to flat wall tiles by its flat back with double-sided tape. The screen is in its normal orientation (as in the firmware): the board's connectors are at the bottom and the cables exit through separate cut-outs in the bottom wall – micro-USB (power, CN13), Ethernet (RJ45) and mini-USB ST-LINK (CN14, programming without opening the case). The model is parametric (`smarthouse_case.scad`, OpenSCAD) and prints without supports. It is modeled on the e-paper enclosure from [family-todo-epaper](https://github.com/marekpiotrowskimp/family-todo-epaper/tree/main/enclosure) (same print profile and testing approach).

## Files

| File | Description |
|---|---|
| `smarthouse_case.scad` | Parametric source (OpenSCAD) |
| `fit_bottom.stl` / `.gcode` | **Test print 1:** a 12 mm strip of the bottom wall with the cut-outs – check your plugs |
| `fit_test.stl` / `.gcode` | **Test print 2:** front with the window and the LCD module rim |
| `case_body.stl` / `.gcode` | Body: front with window and rim, walls, connector cut-outs, ventilation, cover recess |
| `case_cover.stl` / `.gcode` | Flat cover with 4 posts pressing the board |
| `ender3_config.ini` | PrusaSlicer profile (Ender-3 Pro + Devil Design PLA): the e-paper profile adjusted to the official Creality profile |
| `tests/test_enclosure.py` | Dimension, geometry and orientation tests (`python3 -m unittest discover -s enclosure/tests`) |

## Coordinate system

Right-handed: **X to the right and Y up when looking at the screen, Z towards the viewer**; front at Z = 27.36, back of the cover at Z = 0. The STL files are rotated for printing (front on the bed) by rotation only – no mirroring. The `OrientationTests` test ensures that, looking at the screen, ST-LINK is on the left and RJ45 on the right (the previous version of the model was mirrored).

## Where the dimensions come from

ST data for the MB1191 board (`mb1191_manufacturing.zip`, `mb1191-bom.zip` from the 32F746GDISCOVERY page → CAD Resources); the connectors and the LCD position are the same in revisions B, C and E. Positions "from the front": x from the left edge of the PCB, y from the bottom.

| Data | Value | Source |
|---|---|---|
| PCB | 130 × 80 × 1.15 mm; Ø3.25 holes, 4 mm from the edge | Gerber, STEP model |
| LCD module | 105.5 × 67.2 × 4.6 mm, 2.0 mm above the PCB; left edge 12.45, bottom 6.42 | STEP model (U19) |
| Active area | 95.04 × 53.86; 5.23 from the sides, 3.77 from the top, 9.57 from the bottom (FPC ribbon) | Rocktech RK043FN48H datasheet (typical for 4.3″) |
| Connectors (x centers) | CN14 ST-LINK 20.50, CN13 USB FS 41.75, CN12 USB HS 59.24, RJ45 80.81 | pick-and-place |
| RJ45 Hanrun HR961160C | face 16.10 wide, 13.61 high – the tallest part on the connector side | BOM, Hanrun datasheet |
| Assumed (no ST data) | USB socket axis above the PCB: micro 1.5, mini 2.0; plug shells: micro 11 × 8.5, mini 12 × 8 | typical – check with `fit_bottom` |

## Design and dimensions

| | |
|---|---|
| Enclosure (external) | **138 × 88 × 27.36 mm**, 2.5 mm walls, corner radius 2 |
| Window | **97.64 × 56.46 mm** = active area + 1.3 mm on each side; the front overlaps the module by 3.9 / 3.9 / 8.3 / 2.5 mm (L / R / bottom / top) |
| LCD module rim | 107.5 × 69.2 mm inside (module + 2 mm clearance), 2 mm high, 1.2 mm thick – sets the board position |
| Interior | 133 × 83 mm (PCB + 1.5 mm on each side) |
| Depth (from the back) | cover 0–2.5; connector-side parts 4.0–17.61; PCB 17.61–18.76; LCD 20.76–25.36; front 25.36–27.36 |
| Cut-outs (bottom wall) | CN14 **14.6 × 10.6**, CN13 **13.6 × 11.1**, RJ45 **18.7 mm** wide (open from the back, up to Z 18.91) = plug outline + 1.3 mm per side; USB HS and audio jacks stay behind a solid wall |
| Cover | 134.9 × 84.9 × 2.5 mm in the recess (flush with the back), 4 Ø8 posts aligned with the PCB holes, post faces at Z 14.61 (3 mm in front of the PCB) |
| Ventilation | 8 slots, 2 × 14 mm, in the top wall |

## Print settings (Ender-3 Pro, Devil Design PLA)

Profile `ender3_config.ini`: 0.4 nozzle / 0.2 layer, 3 perimeters, 4 + 4 solid layers, 20% gyroid, 215/210 °C, bed 60 °C, 5 mm brim, **no supports**.

Changes compared with the e-paper enclosure profile (following the official Creality Ender-3 profile in PrusaSlicer), after prints with uneven walls (some layers set back, some sticking out of the outline):

- **no Z-hop** (`retract_lift` 0.2 → 0) – the single Z lead screw has play, so lifting and lowering on every retraction could leave layers at different heights (the most likely cause in the profile);
- retract on layer change and wipe;
- external wall 35 → 25 mm/s, other walls 45 → 40, bridges 60 → 25 mm/s (flow 0.95);
- 0.1 mm elephant-foot compensation (the front lies on the bed);
- 500 mm/s² acceleration and jerk 8 sent in the G-code (M201–M205);
- bed constantly at 60 °C (no temperature change after the first layer).

G-code from the correct model has an identical outline on every layer (±0.01 mm), so if the walls are still uneven the cause is mechanical: Z axis (binding, bent lead screw, coupler), roller eccentrics, a cracked extruder arm, damp filament.

## Print time and filament (PrusaSlicer 2.9.4)

| Part | Orientation on the bed | Time | Filament |
|---|---|---|---|
| `fit_bottom` | front on the bed | ~1 h 27 min | 4.8 m (11.5 cm³) |
| `fit_test` | front on the bed | ~1 h 31 min | 5.6 m (13.5 cm³) |
| `case_body` | front on the bed, walls grow upwards | ~4 h 22 min | 15.6 m (37.4 cm³) |
| `case_cover` | plate on the bed, posts up | ~2 h 52 min | 10.4 m (24.9 cm³) |
| **Total (body + cover)** | | **~7 h 14 min** | **26.0 m (~62 cm³)** |

## Assembly

1. **Test prints.** `fit_bottom`: insert the micro-USB, mini-USB and RJ45 plugs into the cut-outs – they should pass freely. `fit_test`: lay it window-down and put the board screen-first into the rim; the LCD module should drop in freely and the whole image should be visible in the window. ⚠️ **Never press on the glass.** Adjustments – see the section below.
2. **Board.** Unscrew the 4 factory nylon feet. Move jumper **JP1 to `usb_fs`** (power from micro-USB CN13 – a USB power supply won't start the board through ST-LINK CN14).
3. **Body.** Put the board screen-first into the body's rim, connectors towards the bottom cut-outs.
4. **Pads.** Stick ~3 mm foam or rubber pads on the faces of the 4 cover posts.
5. **Cover.** Put the cover into the recess – the posts press the board against the front. Fix it with hot glue at a few points around the edge (the glue can be heated later).
6. **Cables.** Micro-USB to CN13 and Ethernet to RJ45 through the cut-outs; mini-USB to CN14 only for flashing firmware.
7. **Wall.** Put double-sided tape (e.g. mounting tape for tiles) on the flat back of the cover and press it against the tiles.

## Changing parameters

| Problem | Parameter |
|---|---|
| A plug doesn't fit its cut-out | `micro_plug_w/h`, `mini_plug_w/h`, `rj45_face_w`, `cut_margin` |
| A plug sits too high / too low | `cn13_axis`, `cn14_axis` |
| The LCD module doesn't fit the rim / rattles | `lcd_play` (the window and cut-outs grow with it) |
| Part of the image is missing at a side / top or bottom | `lcd_left_on_pcb` (12.45), `lcd_bottom_on_pcb` (6.42), `active_top_in_lcd` (3.77) |
| The cover doesn't close / the board rattles | `foam_gap` (3.0), `back_clear` (1.5) |
| The cover doesn't fit the recess | `cover_tol` (0.3) |

Regenerating the print files (from the `enclosure/` directory):

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
