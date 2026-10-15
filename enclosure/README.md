# Muse V9 — smooth cradle stand, PLA

This is a STAND-ONLY revision. The included screen shell and speaker back are
byte-for-byte copies of the corrected V8 files. The V8 screen mount uses three
M2 screws; it has not been changed here. If you already have the corrected V8
puck, print only `03_smooth_cradle_stand.stl`.

## Changes

The pedestal is one continuous piece, approximately 80 × 56 × 35 mm. Its shorter
cradle has no tall fins, split base, detachable panel, or flexible release clips.
The front wall is continuous. A rounded 22 × 18 mm opening at the rear connects
to an internal cable cavity. This opening accommodates the specified 14 × 8 mm
plug cross-section; the clearance checks use a 14 × 24 × 8 mm straight plug body.
Your exact cable has not been measured.

Two recessed guides match the existing V8 puck rails. The rail ends rest on
solid stops at 28 mm above the desk. Nominal rail engagement is 7 mm, with 0.25 mm
clearance per side. This is a gravity-seated sliding mount, not a snap latch:
it supports the puck and constrains sideways motion, and releases by lifting.
Carry the assembled device by its base, not by its puck.

## Files

- `03_smooth_cradle_stand.stl`: the new part to print, already base-down.
- `01_screen_shell.stl` and `02_twist_back.stl`: unchanged corrected V8 parts.
- `00_optional_docking_test.stl`: the actual upper 10 mm of the stand. Print this
  first if you want to test your existing puck rails with minimal filament.
  It is a test coupon, not an assembly part.
- `Muse_V9_Bambu_layout.3mf`: three main objects arranged within 180 × 180 mm.
  Geometry only; choose your actual Bambu printer, PLA and nozzle profile.
- `Muse_V9_assembled.glb`: full assembled model, not a print file.
- `Muse_V9_360.html`: browser viewer; drag to rotate, scroll to zoom.
- `validation.json`: mesh checks, collision checks and unchanged-part hashes.
- `build_stand.py`: editable dimensions and construction source. Requires the
  two included approved shell/back STLs alongside it.

## Print and assemble

Print the stand at 100% scale, flat base on the plate. Suggested starting point:
0.4 mm nozzle, 0.16 mm layers, 4 walls, 5 top/bottom layers and 25% infill in PLA.
Inspect the rear opening's roof in the slicer; use removable local/tree support
if needed. The rear opening gives access to remove support. Keep support and
first-layer bulges out of the sliding tracks. No glue or screws are needed for
the stand.

1. Before docking the puck, feed the USB-C plug from the rounded rear opening
   upward through the top cavity. Guide it by hand; do not pull on the socket.
2. Plug it into the puck while holding the puck just above the stand.
3. Align both underside rails with the two recessed guide slots. Lower straight
   down until the rail ends reach the solid stops. Keep slack cable inside the
   cavity and route it out the rear opening.
4. To detach, hold the base and lift the puck straight up. Leave cable slack or
   unplug it first. No sideways prying or release-button operation is needed.

## Verification and limits

The stand STL was reloaded and checked for a closed, consistently wound,
single connected mesh. Digital checks cover the seated puck, sampled vertical
removal positions for both shell and back, the stated straight-plug envelope,
and a positive stop under the rails. The shell and back hashes match the
corrected V8 originals. These checks do not establish physical fit, sliding
force, printer tolerances, durability, cable bend radius, or resistance to
tipping. Start with the optional docking test; do not force a tight PLA joint.

The source's approximate dimensions and clearances are intentional design
choices, not measurements of your individual print or cable.
