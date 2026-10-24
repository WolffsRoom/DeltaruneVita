# Deltarune GXM Audit a2 + GXM16

## Goal
An independent diagnostic VPK, Title ID DLTGMAUD2, scans Chapter 0-5 sequentially without launching GML room gameplay or modifying game saves. It writes persistent logs for structural and native small-sample GPU checks.

## Installation
- Install `Deltarune-GXM-Audit-v0.78-a2.vpk` alongside the playable game.
- Preserve game data under `ux0:data/deltarune/deltarunevita/chapter0` through `chapter5`.
- Run `Deltarune GXM Audit` from LiveArea. It automatically processes Chapter 0, 1, 2, 3, 4 and 5.
- Hold both L and R when launching to reset its checkpoint and rerun all six chapters.
- Press START at the finished screen to exit.

## Diagnostic output
`ux0:data/deltarune/deltarunevita/gxm-audit/summary.log`
`ux0:data/deltarune/deltarunevita/gxm-audit/chapter0.log` through `chapter5.log`
`ux0:data/deltarune/deltarunevita/gxm-audit/progress.bin` (resume checkpoint)

Each chapter log includes data.win parsing, room headers/names, SHDR shader names and source types (including VitaCg/PSSL metadata), TPAG source-reference errors, each PVR BC3 header and size, texture payload accounting, VM creation status, and small GXM BC3/RGBA4444 GPU allocation tests. Unsupported backend features are explicitly labeled NOT_IMPLEMENTED.

## Fault recovery
- Checkpoints are durably written before risky stages. When the audit is reopened after a crash/power loss, the interrupted chapter is marked INTERRUPTED and the next chapter is analyzed.
- This design does **not** catch hardware exceptions, fix broken shaders, or guarantee crash-free execution. The native code, Vita SDK and GPU can still experience fatal faults.
- The audit does **not** execute every room or test all gameplay GML callbacks. A PASS_STATIC is not proof that gameplay rendering works.
- If all six chapters have already been completed, a rerun requires holding L+R while launching.

## Corrupted BC3 from gxm15
GXM15 copied linear PVRTexTool BC3 blocks directly to sceGxmTextureInitSwizzledArbitrary, producing visually corrupted atlas contents. GXM16 defaults to decoded RGBA4444 shared source-page atlases while adding experimental Morton block-order conversion for compressed BC3.
`ux0:data/deltarune/deltarunevita/gxm-texture-mode.txt` content controls GXM16 texture path:
- `0` = RGBA4444 (default with absent file)
- `1` = Morton-reordered BC3/PVR (experimental; inspect visual result on Vita)
- `2` = RGBA8888 (high-memory compatibility fallback)
Every mode keeps the shared TXTR page cache and on-demand atlas loading. GXM15 is not recommended for playing because its raw BC3 upload produced corrupt textures.

## Remaining native GXM limitations
No complete conversion of the 133 SHDR GameMaker shaders to Vita GXP, and no per-fragment GML fog, advanced alpha discard, projective vertex buffers, or end-to-end video engine parity. Only physical Vita testing can confirm visuals and FPS. The playable VitaGL port was not modified.
