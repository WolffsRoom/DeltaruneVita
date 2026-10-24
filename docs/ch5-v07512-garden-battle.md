# Chapter 5 Floradinn battle: v0.75-12

The v0.75-11 Vita trace confirms one active `obj_tensionbar` and one enabled view during the duplicated TP display. The runner now prevents that persistent HUD instance from executing its normal Draw event more than once in one frame if a Draw event rebuilds the drawable cache.

The battle-start stall loaded many 1024x1024 pages at once and then evicted pages that the same battle requested again. While `obj_battlecontroller` is active in `room_dw_garden_floradinnencounter`, the renderer now protects the measured Garden battle working set from LRU eviction. This does not preload extra pages or increase the normal cache limit; it chooses exploration pages as victims before battle pages already resident in memory.

Routine per-page cache-hit/upload messages are no longer written synchronously to the probe log. Eviction, recovery, memory and performance diagnostics remain available.

Vita validation: enter the Floradinn battle normally, confirm a single TP bar, compare stable-battle `PERF_SUMMARY` averages against v0.75-11, and check that pages 63/64/68/70/100/103/104/106/113/119/120 are not repeatedly evicted while the battle controller is active.
