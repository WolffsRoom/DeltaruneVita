DELTARUNE Vita v0.80-53 Animated Borders

Copy the bordersanim folder to:
ux0:data/deltarune/deltarunevita/mods/bordersanim/
Copy over any older version of the folder.

Settings > Screen > Console borders: Dynamic
Settings > Screen > Better borders: Animated

frames_side.bani: the two columns beside the game, at native 960x544 (RGB565).
The game uses this file while the screen zoom is 100% or more.
frames_fast.bani: 480x272 full frame, used when zooming out exposes more of the
screen. Only the five original animations have it; the others show their static
border in that case.
A border with no folder here keeps its static image.
