# NXRUNE Dynamic Border Parity Design

## Goal

Reproduce the NXRUNE/console dynamic border behavior for DELTARUNE Chapters 1-5 while keeping all border artwork external to `data.win` on PS Vita.

## Reference

The patched Steam `data.win` files under `tools/nxrune-analysis` / `artifacts/nxrune-border-analysis` are reference-only. No patched `data.win` or embedded NXRUNE texture is shipped.

## Architecture

Split border behavior into two layers:

1. `vita_border_rules.[ch]`: platform-independent border decisions. It maps chapter + room + GameMaker state to an external border asset and desired overlay state.
2. `vita_borders.[ch]`: VitaGL texture loading, crossfade, memory pressure, drawing and overlay fade.

The GameMaker runner remains the authority for story state. A small Vita-only bridge samples existing VM globals/instances (`plot`, `darkzone`, `flag[1324]`, selected scene object fields) and sends a compact state snapshot to `vita_borders` once per frame. No NXRUNE GML is injected into the Vita `data.win`.

## Behavior model

### Base border

The resolver must use the exact room groups recovered from NXRUNE instead of substring guessing. Chapters 3-5 may also depend on plot/flags.

### In-room overrides

Scene-specific overrides have higher priority than the base room mapping. Chapter 3 includes TV Black, Blue Stars, Red Smiles, Green Room/Blue Light and Word transitions inside a room. Chapter 5 includes Castle Top/Gold transitions and the Pink alternate border.

### Visibility

NXRUNE's `hide_border(speed, color)` is modeled as a full-screen overlay drawn after the border and before the centered game surface. `show_border(speed)` fades that overlay back to transparent. This preserves the center game image while hiding only the surround once the application surface is composited.

### Crossfade

`set_border(asset, >=1)` switches immediately at the logical level; normal changes crossfade using the existing Vita texture transition. The external file remains the only border image source.

## State snapshot

The first implementation exposes at least:

- chapter
- room name
- global.plot
- global.darkzone
- global.flag[1324] (Chapter 5 time-of-day)
- selected object state needed for verified NXRUNE rules
- explicit scene signal bits for one-shot border transitions that cannot be derived from globals alone

Missing/undefined VM values fall back conservatively to the room mapping, never to substring auto-detection.

## Compatibility

- Keep `borders_config.txt` as an explicit user override.
- Keep Simple mode unchanged.
- Preserve Better Borders behavior for Chapter 1.
- Preserve the existing memory-pressure and retained-same-border optimizations.
- Do not load an external border before actual gameplay begins.

## Verification

A desktop unit/regression test validates resolver outputs for representative rooms and state boundaries from all five chapters. Static source tests cover overlay and Chapter 3/5 transition hooks. Vita build verification must complete before producing a VPK.
