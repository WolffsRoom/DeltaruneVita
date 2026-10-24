# Deltarune Native GXM18 — Chapter 3 inventory and performance

## Physical log and core dump
- `logs/chapter-log/geral/Ch3/native-gxm-probe.log` for GXM17 ends with `NATIVE_GXM=runner_renderer unsupported=drawText_spritefont` on room index 98 after `New Game` and opening inventory.
- Dump: `logs/chapter-log/geral/Ch3/psp2core-1791865541-0x00014a3615-eboot.bin.psp2dmp`.
- Chapter 3 room 98 = `room_dw_couch_overworld_intro`, 2040x480, 30 room-speed.
- GXM17 performance in this sequence: ~15-23 FPS in several 5 s windows, draw samples 16-24 ms; Runner step samples 22-24 ms. Earlier PLACE_MENU (~room 75) draws up to 341 items and averages ~23-27 ms drawing.
- `rgba4444=24`, `bc3=0`, `cache_mib=29`, `missing=0`; the failure is the explicit unsupported glyph renderer callback, not a proven GPU memory exhaustion.

## GXM18 changes
1. `bs_text` now supports the GameMaker runtime `font_add_sprite` layout: glyph index from `glyph - font->glyphs` selects the source sprite frame TPAG; uploaded original TXTR page reused via existing on-demand cache; applies TPAG source UV and GameMaker `targetY - spriteOriginYAdjust` to the y coordinate. Invalid indices skip glyph safely and do not intentionally crash.
2. Adds `sprite_font` event in native log (limited) and cumulative spritefont_glyphs metric.
3. Conservative image rectangle culling transforms each rectangle through the current NanoVG camera/model matrix and checks if all 4 corners' bounding box is beyond the actual render target bounds, with 2px tolerance. Handles scaling/rotation, GUI and offscreen surfaces. Reports cumulative `offscreen_culls`; unrotated sprite rectangles are tested before TXTR atlas decode to avoid populating GPU caches with invisible pages; no room-specific hardcoded hack.
4. Exposes Runner profiling counters as per-300-frame log: events, alarms, spatial, collisions, other, DrawViews and GUI average microseconds. This is essential to discriminate VM from GPU draw cost.
5. Keeps `gxm-texture-mode.txt` configuration default RGBA4444 (0), Morton BC3 opt-in (1), RGBA8888 (2). Previous GXM17 build untouched.

## Auditoria
The copied `logs/chapter-log/geral/summary.log` still begins with `GXM_AUDIT=v0.78-audit2`, and Ch3 logs report `CAPABILITY chapter=10` for two features (pre-a3 formatter bug). They are older metadata-only audit records, not evidence that the a3 bounds checker ran on the device. The a3 was designed to write to its separate `gxm-audit-a3` folder.
Those logs pass metadata checks for six chapters and list 133 custom shaders without native GXP implementation. They do not execute story rooms, dialogs, battle sequences or inventory.

## Expected next test
- Install `artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm18.vpk` (Title ID DLTAGXM18).
- Launch Ch3, select New Game, check visuals and FPS of `room_dw_couch_overworld_intro`, open inventory and navigate tabs.
- On device collect fresh native-gxm-probe.log and the new `NATIVE_GXM=runner_profile`, `offscreen_culls` and `spritefont_glyphs` counters.
- Static contract and compile pass do not guarantee real hardware compatibility, 60 FPS, or correct sprite-font text in every scene.
