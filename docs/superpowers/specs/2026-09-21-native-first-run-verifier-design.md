# Native first-run verifier

## Goal

Replace the current system-message startup check with a small native scene that
looks like DELTARUNE dialogue. The scene validates the external installation
before any `data.win` is parsed, records a versioned successful result, and runs
again only when requested or when the validation contract changes. This also
allows the complete Chapter 0 `data.win` currently bundled in the VPK to be
removed after the external launcher path is proven.

## Chosen approach

The verifier and its renderer live in the Vita executable. `Wolff.png` is the
only character portrait used by this scene and is packaged as a small VPK
asset. The executable draws a black background, the portrait with nearest
filtering, and a white DELTARUNE-style dialogue box. Text advances as checks
complete and the final page lists actionable failures.

Keeping validation native means it can run when every external `data.win` is
missing or corrupt. A verifier implemented in an embedded `.win` would depend
on the same parser, renderer and data that it is meant to diagnose. A native
system dialog remains available only as an emergency fallback if the verifier
asset or graphics initialization fails.

## State and execution

The state file is `ux0:data/deltarune/deltarunevita/setup.ini`:

```ini
[Setup]
ValidationVersion=1
Status=complete
```

Validation runs when the file is absent, `Status` is not `complete`, or
`ValidationVersion` differs from the executable's contract version. Before the
first check it writes `Status=running`. It writes `Status=complete` only after
all required checks pass. A crash or forced shutdown therefore causes the
verifier to run again instead of accepting an incomplete installation.

The existing `FirstLoad=1` key is accepted as a migration signal. A successful
run replaces it with the versioned state. The Game Settings menu will later be
able to request a run by setting `Status=pending` before restarting.

## Checks

Required checks block startup:

- base directories under `ux0:data/deltarune/deltarunevita`;
- `chapter0/data.win` and the selected chapter's `data.win`;
- minimum plausible size and readable `FORM` header for each required
  `data.win`;
- shared `music`, `sounds` and `ui` directories;
- enough free storage for normal save and cache creation;
- native runtime capabilities that the current build cannot replace with a
  fallback.

Optional checks display a warning and allow startup:

- videos for chapters that have a supported skip/fallback route;
- border packs and optional language/mod data;
- accelerated texture caches, which can be rebuilt or decoded normally;
- plugins used only for optional features.

Plugin validation tests the required capability or loaded module first. Reading
`ur0:tai/config.txt` is supplementary diagnostic information and is never the
sole proof that a plugin works.

## Interaction

The portrait remains at the left edge of the dialogue box. The current check is
shown with animated text. On success, Cross continues automatically after a
short confirmation. On warnings, Cross continues and Triangle opens the detail
page. On a required failure, Cross repeats the checks and Circle exits safely.

Every result is also written to `butterscotch-probe.log` with a stable
`VERIFY=` prefix so reports remain useful when a user cannot photograph the
screen.

## VPK size reduction

The first implementation keeps the embedded Chapter 0 launcher as a fallback.
Tests must prove that a valid external Chapter 0 launches and that missing data
reaches the native verifier without parsing any `.win`. Only then is
`assets/chapter0/data.win` removed from the VPK manifest. The small Wolff portrait
and verifier resources replace it.

## Failure handling

The verifier never creates missing game-data directories merely to make a check
pass. It may create only its state/log parent after recording that the original
layout was absent. Messages show the exact missing path and distinguish missing,
empty, invalid-header and unreadable files.

If native rendering cannot start, the existing Vita message dialog reports the
same collected results. No failure path marks validation complete.

## Tests

- pure C tests for validation-state migration and state transitions;
- filesystem tests with complete, missing, empty and invalid-header layouts;
- renderer contract test for nearest-filtered `Wolff.png` and dialogue layout;
- local host screenshots at 960×544 for visual review;
- VPK manifest test proving Chapter 0 remains during the first rollout and is
  removed only in the size-reduction follow-up;
- Vita tests for first run, successful second run, interrupted validation,
  manual recheck and native-dialog fallback.
