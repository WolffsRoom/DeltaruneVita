# Chapter 5 Flowey fade: v0.75-10

The v0.75-9 Vita test confirmed that the movie plays with sound and returns to gameplay, but the host fullscreen fade started before the soundtrack's last second. The host overlay used the original GML `_timer >= 830` trigger and advanced opacity for 100 frames. That timer does not track the separately played OGG, and the user's language mod can supply a different soundtrack duration.

The host overlay now finds the active `ch5_intro_audio.ogg` stream, reads its decoded length and live playback position, and begins a one-second fade when one second remains. Opacity only increases, so a late GML synchronization seek cannot flash the image back in. If the OGG has not yet become available, the MP4 clock provides a fallback.

Vita validation: replay `room_dw_garden_video` with the German music mod, confirm `CH5_VIDEO_FADE=audio_final_second`, observe that the image remains visible until the final second of the soundtrack, and confirm the transition to `room_dw_garden_meetflowery`.
