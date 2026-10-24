# Native GXM38 — Chapter 3: font-color integrity and first-room profiling

Data: 2026-10-13. User hardware logs: `logs/chapter-log/geral/Ch3/native-gxm-probe.log`.

## Confirmed findings from physical GXM36

Latest hardware test is `v0.78-gxm36`, Chapter 3, room 98 (`room_dw_couch_overworld_intro`):
- FPS in stable room intervals: about 18.5–21.6.
- Step cost: 23–25 ms/frame. Draw cost: 16–23 ms/frame. Swap: 4–7 ms/frame, averaged across 300 frames.
- Events component: ~23–25 ms/frame. The old `step_hotspot` counter accounted for `STEP_NORMAL` only and missed `STEP_BEGIN` and `STEP_END`, so its four object records did not explain the entire step cost.
- Visible draw calls ~84–156 per frame; zero missing textures. GXM36 used 15–20 RGBA4444 pages in this room and no 8888 pages.
- No evidence of a new crash at the log end.

## GXM37/GXM38 corrections

1. **Exact font atlas RGBA8888**: `bs_page_is_font_atlas` identifies original FONT TPAGs and runtime `font_add_sprite` frames by texture page ID. Only font pages load in 8-bit RGBA so glyph coverage/color remain intact and 4-bit/channel tint artifacts do not reach text. The regular game atlases remain in correctly ordered RGBA4444, preserving 16-bit memory efficiency. Evidence in Chapter3's actual emulated room: `NATIVE_GXM=font_atlas_lossless page=221 w=512 h=512 format=rgba8888`, followed by pages 93 (1024×1024) and 203 (512×512) after cutscene progress.
2. **Complete Step profiling**: Begin, Normal, End Step are now separately profiled. `NATIVE_GXM=step_phases` reports CPU total by dispatch and actual event scripts. `step_hotspot` names the object **and** Step phase, not only Normal Step.
3. **GXM38 Event lookup optimization**: In per-object Step dispatch, already grouped snapshots now reuse resolved handler IDs for consecutive instances of the same exact object. The original snapshot, event order, event code, instance state and scripts remain unchanged. The local run saved 594 event lookups in 300 frames. This alone is a small optimization and is NOT enough to claim 60 FPS or meaningful gain in hardware until retested.

## Vita3K local test

- Launched Chapter3 directly into room 98 using temporary `gxm-emulator-room.txt` marker and emulator-only audio-mute marker. Real DataWin and real GameMaker Runner executed normally, with the native GXP shader compiler mask=0x1FFF.
- GXM37 visual screenshot: `artifacts/experiments/v0.78-native-gxm/GXM37-Vita3K-Ch3-Room98-Lossless-Fonts.png`. The on-screen dialogue was white/neutral without blue rectangular glyph artifacts.
- GXM38 screenshot: `artifacts/experiments/v0.78-native-gxm/GXM38-Vita3K-Ch3-Room98-Text-Font-Validation.png`. Comparison by Pillow over the entire 960×544 RGB capture: **0 pixels changed** between the two. Both use the same scene and preserve draw output.
- GXM37 first 300-frame profiler sample (emulator): Step 2347 μs, draw 4796 μs, swap 2262 μs; Step phases Begin dispatch 519 μs, Normal 687 μs, End 578 μs. The system's effective FPS varied with scene state and external emulator load; not a physical-hardware measurement.
- GXM38 first 300-frame profiler sample: Step 2381 μs, draw 4678 μs, swap 190 μs; Step phases Begin 534 μs, Normal 692 μs, End 589 μs; cached lookups 594/300 frames. Effective FPS in the tested portion of the emulated room remained around 20–21; **no stable FPS improvement was demonstrated**.
- VPKs passed ZIP CRC and title ID checks, 57 entries each. GXM37 `DLTAGXM37` SHA256 `1d6087b6c16e3c88b51851e8bf1537e0635e61a0a3043b0e464cebdb869ea11f`; GXM38 `DLTAGXM38` SHA256 `e976df12791ce57965237b15d259fab28a3bb3c71cdec8d6cd7df41b428efb00`. Python native-GXM contract suite passed.
- Temporary Vita3K markers removed, emulator closed. The original Vita3K configuration was not changed. GXM36 and earlier separate VPKs preserved.

## Hardware follow-up necessary

Install **GXM38** and replay Chapter 3's first room on the physical PSVita. Specifically check text glyph blue edges/blocks and collect new `step_phases`, `step_hotspot`, `step_cache`, `frame_cost`, and `render_metrics` to isolate whether the main slowdown is GameMaker Begin/Normal/End Step, NanoVG draw/texture work or GXM submit. Do not extrapolate emulator CPU duration to Vita ARM hardware or claim 60 FPS. If blue text remains on physical Vita despite lossless font atlas, save a screen photo and log the texture page and shader tag around that draw.
