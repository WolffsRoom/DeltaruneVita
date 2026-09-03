DELTARUNE Vita - Trophies

This folder isolates the trophy catalogue originally bundled with the Spanish
translation's Chapter 0. The Vita runner now renders the list from Game
Settings > System > Trophies, independently of the selected language data.win.

Progress is read from:
ux0:data/deltarune_saves/trophies.ini

The Vita runner now enables DELTARUNE's original PS4 trophy event manager and
mirrors every unlock to this file. This local path always works.

Native Vita trophy support is optional. The VPK contains the unsigned pack at:
sce_sys/trophy/DLTVITA01_00/TROPHY.TRP

Its NP Communication ID is also stored in param.sfo as DLTVITA01_00. Install
NoTrpDrm separately in the taiHEN directory used by the console and add it
under *main in config.txt, for example:

*main
ur0:tai/NoTrpDrm.suprx

Use ux0:tai instead only when that is where the active config.txt is stored.
Reboot the Vita or reload taiHEN configuration after installing the plugin.
NoTrpDrm's upstream implementation recognizes retail firmware 3.60 through
3.68. A spoofed system version does not change the underlying firmware.

Without the plugin, TRP or a supported firmware, the runner falls back to the
local Game Settings list and never prevents the game from starting. Treat the
custom trophy set as local homebrew data; do not attempt to synchronize it to
PSN.

Catalogues:
- catalog_en.txt: English
- catalog_ptbr.txt: Portuguese (Brazil)
- catalog_es.txt: Spanish

The original PC-positioned obj_trophy_manager and the Spanish border manager
are disabled on Vita. They are not required by this native interface.
