# Muse Curved V7 enclosure

A 3D-printed case and stand for the Waveshare ESP32-S3-Touch-LCD-1.85 running
Muse.

## Files

| Folder | What's in it |
| --- | --- |
| `bambu/muse_curved_v7_all_parts_P1S.3mf` | Bambu Studio project with all eight parts on one plate (Bambu Lab P1S, 0.4 mm nozzle, 0.20 mm Standard) |
| `stl/` | Each part as an STL, for any slicer |
| `step/` | CAD (STEP) for parts 01–06, to modify the design |

## Parts

| # | Part | Notes |
| --- | --- | --- |
| 01 | `front_shell` | |
| 02 | `black_screen_adapter` | |
| 03 | `display_retaining_ring` | |
| 04 | `rear_shell_with_speaker_clips` | |
| 05 | `button_plunger_print_3` | print 3 |
| 06 | `button_cap_print_3` | print 3 |
| 07 | `curved_stand` | |
| 08 | `curved_cable_cover` | |

Also in `stl/`:

- `optional_button_plunger_long.stl` and `optional_button_plunger_short.stl`:
  alternatives to part 05 if the buttons press too lightly or too hard.
- `test_display_fit_ring.stl`, `test_cover_pin.stl` and
  `test_cover_socket.stl`: small test prints for checking the fit before
  printing the full parts.
