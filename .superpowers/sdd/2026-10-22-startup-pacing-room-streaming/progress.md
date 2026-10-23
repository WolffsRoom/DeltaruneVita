# SDD ledger — plan: docs/superpowers/plans/2026-10-22-startup-pacing-room-streaming.md
Workspace: existing feature branch feature/data-win-convention-and-docs; isolated branch, shared dirty checkout preserved.
Ruling: sdd-workspace helper is incompatible with the Windows drive-letter path in this checkout; use the equivalent plan-scoped directory manually — cost if wrong: only loss of helper-generated brief metadata, not product behavior.
Pre-flight: Task 1 produces VitaBackgroundWork consumed by Tasks 3, 5, and 6; interface names agree.
Pre-flight: Task 4 produces DataWinRoomPayload consumed by Task 5; interface names agree.
Pre-flight: Task 6 consumes Task 1 budget/generation semantics and Task 5 room identity; interface semantics agree.
Pre-flight: Task 7 consumes timing points from Tasks 3, 4, and 6; no ownership conflict.
Task 1: Ruling: src/vita-probe/CMakeLists.txt was already dirty before this plan; commit only new Task 1 files and preserve the integration hunk in the checkout — cost if wrong: the final integration commit must explicitly include or reconstruct that one source-list entry.
Task 1: complete (tests: test-vita-background-work.py PASS; test-capunlocker-contract.py PASS)
Task 2: Ruling: playable_main.c and CMakeLists.txt predate this plan as dirty files; commit new pacer/test files and preserve reviewed integration hunks in checkout — cost if wrong: final integration must retain the include, source-list entry, room-reset, and limiter replacement.
Task 2: complete (tests: test-vita-frame-pacer.py PASS; test-experimental-gl-contract.py PASS)
Task 3: Ruling: keep startup preload service alive but idle after adoption so later room/texture tasks can reuse one worker — matches the single-service spec; cost if wrong: a small service allocation remains for the session.
Task 3: complete (tests: test-chapter0-startup-preload.py PASS; test-vita-video-state.py PASS; VitaSDK build PASS, Deltarune-v0.78-6.vpk)
Task 4: Ruling: data_win.c/h already contain unrelated uncommitted work, so do not commit whole files; retain staged-payload hunks in checkout and commit only the new regression test — cost if wrong: final integration must preserve four DataWin staging symbols.
Task 4: complete (tests: test-room-payload-staging.py PASS; test-modern-room-jump-memory-contracts.py PASS)
Task 5: Ruling: implement one in-flight destination payload instead of the proposed two-entry cache for this first safe version — avoids retaining multiple room payloads in memory on Vita; cost if wrong: backtracking does not get a cache hit, but transition correctness and lower peak memory are preserved.
Task 5: complete (tests: test-vita-room-prefetch.py PASS; room memory and Chapter 3 GPU budget contracts PASS; VitaSDK build PASS, Deltarune-v0.78-7.vpk)
Task 6: Ruling: implement and validate CPU-only texture staging plus separate-file synchronous adoption, but do not asynchronously delay after Room End or move GL upload/decode yet — current transition has no safe commit boundary and forcing one would risk softlock/GPU crash; cost if wrong: final-battle transition spikes from PNG decode/upload remain for a later phased state machine.
Task 6: complete with ruled scope (tests: test-vita-texture-staging.py PASS; test-finale-render.py Legacy/Modern PASS; room memory contracts PASS)
Task 7: complete (tests: test-vita-io-profile.py PASS; startup and room prefetch contracts PASS)
Task 8: local verification complete (13/13 focused commands PASS; finale render Legacy/Modern PASS; VitaSDK build PASS; VPK v0.78-8 CRC PASS, SHA-256 AF42217D14D966E0E3B99527E0B0DC83487134CD6268A3F1AFF6CF0898DC3C0A).
Task 8: hardware checklist remains intentionally external: VitaGL/GXM timing, GPU stability, CapUnlocker A/B, and IOStaging A/B cannot be established by the host tests.
