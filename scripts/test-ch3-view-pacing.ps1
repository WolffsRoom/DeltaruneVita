$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$runner = Get-Content "$root/src/butterscotch/src/runner.c" -Raw
$view = $runner.Substring($runner.IndexOf('bool toSurface = view->surfaceId != -1;'))
$view = $view.Substring(0, $view.IndexOf('int32_t viewX, viewY, viewW, viewH;'))
if ($view.IndexOf('runner->viewCurrent = (int32_t) vi;') -gt $view.IndexOf('Runner_surfaceSetTarget(runner, view->surfaceId)')) { throw 'Surface target selected before its camera/view' }
$main = Get-Content "$root/src/vita-probe/source/playable_main.c" -Raw
if ($main -notmatch 'bool waitForVblank = settings.vsyncEnabled && settings.fpsTargetMode >= 2;') { throw '30/40 FPS stack vblank and deadline waits' }
Write-Output 'Chapter 3 view selection and capped pacing contracts: PASS'
