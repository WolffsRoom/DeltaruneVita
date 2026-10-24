# Chapter 5 Garden petal transition: v0.75-11

The Vita log in `logs/chapter-log/Chapter5/TransicaoPetalas` shows that `room_dw_garden_floradinnencounter` selected the custom `border_dw_garden_cliff_bottom_frame_0.png` at room entry. The custom source bypassed the Garden scene's blank-surround rule and prevented its reveal from selecting `border_dw_garden_0.png`.

The scripted Floradinn encounter now selects the Garden border before broad custom room mappings are considered. The existing scene rule keeps it hidden while `obj_sakurafubuki_new.transition_stage == 0` and reveals it with a black overlay fading at `1/90` per frame once the petal transition reaches stage 1. Other rooms keep their border configuration.

Vita validation: enter the scene through gameplay, confirm black margins before the petal transition, then observe `CONSOLE_BORDER=chapter5_garden_reveal_begin` and `border_dw_garden_0.png` fading in as the petals move. A direct Room Navigator jump may bypass the gameplay trigger for stage 1.
