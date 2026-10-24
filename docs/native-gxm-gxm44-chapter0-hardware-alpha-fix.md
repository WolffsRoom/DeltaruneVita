# GXM44 — RGBA4444 transparency/color channel correction for Chapter0 / Vita screenshot

Date: 2026-10-18. The test screenshot was copied from \`logs/chapter-log/GXM/2026-10-18-173838-340318.png\` and the log from \`logs/chapter-log/GXM/native-gxm-probe.log\` (final session \`v0.78-gxm43\`, Chapter0, RGBA4444 atlas, no missing sprites).

## Exact reproduction

- The user's image shows almost pure cyan (0,255,255) text, large solid cyan rectangles in glyph transparent pixels, green artifacts in the yellow choice and a gray/cyan version label, despite an otherwise black background.
- In the GXM43 Vita3K run with the previous baseline channel mapping, the same Chapter0 question renders correctly as white text with transparent edges; \`GXM43-Vita3K-Chapter0-Original.png\`.
- Passing RGBA source \`(R,G,B,A)\` to the GPU as \`(A,G,B,R)\` on Vita3K recreates almost exactly the user's error: \`GXM44-Vita3K-Chapter0-Layout8-BugSimulation.png\`. The region x=180..774, y=245..377 has **92.375% identical pixels** to the user's screenshot; all 8,399 cyan pixels in the simulated region overlap pixels cyan in the user's screenshot (cyan-mask intersection-over-union 0.766). Different UI timings/exposures account for remaining differences.
- With the original packing \`(R,G,B,A)\` on Vita3K, the image returns to correct color/transparency: \`GXM44-Vita3K-Chapter0-Correct-Layout0.png\`. 0.0% of the pixels in the main question/options region differ by more than 12/255 from GXM43's previously validated good frame.
- These controlled tests **confirm which lane swap recreates the issue in the emulator**. Whether the *physical* GXM GPU interprets the 16-bit input in the reverse order is a strong hypothesis from the matching physical-looking symptom, but requires the user's hardware retest. It is not a claim that the hardware has already been verified.

## Changes

1. \`experiments/native-gxm/rgba4444_native.h\`: add a ninth packing variant, index 8, permutation \`{3,1,2,0}\` mapping source RGBA to packed AGBR so that a red/alpha sampler swap returns RGBA correctly. Keep layout0 existing as Vita3K-proven baseline and 0..7 existing variants. Hardware default currently variant8; if emulator-only \`ux0:data/deltarune/deltarunevita/gxm-emulator-mute.txt\` is deliberately present (workaround for Vita3K's old OpenAL crash), default is layout0. Explicit \`gxm-color-layout.txt\` value 0..8 always wins. Setting should remain absent on physical Vita for the new default.
2. \`experiments/native-gxm/bs_renderer_impl.inc\`: write \`NATIVE_GXM=rgba4444_channel_layout variant=<N> first_page=<page>\` at the first atlas upload, regardless of whether the page is page0. Chapter0 was previously missing the layout telemetry because it used page1.
3. Same GXM renderer's \`bs_text\`: replace only Chapter0 text starting with \`DELTARUNE VITA v0.\` with \`DELTARUNE VITA v0.78\` during draw. The previous \`obj_ui_version\` Create override was insufficient because original scripts later overwrite the text. Does not alter external game data, Chapter3/5 text or the VM.
4. Preserved GXM43 and earlier separate VPKs; new \`DLTAGXM44\`, no changes to Modern-GL or Experimental-GL.

## Validation

- \`scripts/test-native-gxm-probe-contracts.py\`: PASS including existing 73 callbacks, alpha and GLSL-derived shaders, RGBA4444 conversion and the new layout8/correct-version tests.
- WSL Ubuntu VitaSDK: VPK produced, 57 ZIP entries, CRC testzip success, correct Title ID \`DLTAGXM44\`.
- Vita3K on Chapter0: game enters real Runner, shader compilation \`mask=0x1FFF\` and expected first frame. Layout8 display is cyan/opaque, layout0 is white/transparent, without missing images. Chapter0 footer reads \`DELTARUNE VITA v0.78\`.
- Compare image \`artifacts/experiments/v0.78-native-gxm/GXM44-Chapter0-Alpha-Red-Comparison.png\` (uploaded screenshot; emulated reproduction; emulated good image).
- **Hardware pending:** obtain first GXM44 physical Vita screenshot and new log, confirm layout8 and white glyphs with proper transparency; test Chapter5 city shaders separately after base channel mapping is confirmed. This patch does not by itself prove the 13 translated GXP shaders are equivalent to Modern-GL or fix every unresolved Chapter5 effect.

## Location / recovery

- Build: \`artifacts/experiments/v0.78-native-gxm/Deltarune-NativeGXM-v0.78-gxm44.vpk\`
- Title ID \`DLTAGXM44\`
- SHA256 \`09f37c13bcf57411640e6772199817c58535afcabe0f52f23bd297f56d745b30\`
- Hardware default: variant8; remove any old \`ux0:data/deltarune/deltarunevita/gxm-color-layout.txt\` if present, which otherwise overrides it.
- Vita3K diagnostic override: place ASCII \`0\` in \`ux0:data/deltarune/deltarunevita/gxm-color-layout.txt\` if needed. Variant0 matches the proven Vita3K output. Variant8 in Vita3K is specifically for reproducing the bad texture swizzle.
