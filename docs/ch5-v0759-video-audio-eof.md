# Chapter 5 video: v0.75-9

The v0.75-8 Vita log confirms that the Chapter 5 movie and fade-out render correctly with Modern-GL shaders. It also identifies two independent playback regressions.

- `ch5_intro_audio.ogg` resolves to the German language mod and its OpenAL source plays through the track, but its effective gain stays at `0.000`. The first synchronization seek occurs immediately after the room-transition crossfade starts. The source gain is still zero at that instant although the fade target is audible. Muting before the seek and restoring the sampled zero cancels the crossfade. The Chapter 5 synchronization now leaves a zero-gain fade in progress untouched; audible seeks retain the short mute/fade protection.
- The scripted `video_pause()` and `video_resume()` occur before the MP4 reaches EOF on this Vita run. The EOF handler previously waited forever for a future resume, so `video_end` was never dispatched and the black frame remained. EOF now schedules release when the final scripted resume has already happened; the existing resume handler still covers EOF that occurs before resume.

Vita validation: replay `room_dw_garden_video` with the same German mod and confirm audible soundtrack, `VIDEO=ch5_eof_release_scheduled`, `VIDEO=ch5_eof_release_after_final_pause`, `VIDEO_ASYNC=video_end_dispatched`, and the transition to `room_dw_garden_meetflowery`. The PC build cannot verify Vita AvPlayer/OpenAL behavior.
