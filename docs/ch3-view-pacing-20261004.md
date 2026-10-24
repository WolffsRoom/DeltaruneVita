# Chapter 3 view selection and frame pacing — 2026-10-04

Input: `butterlog4-room_board_2_20261002-065143.log`.

The log shows sustained 39–43 ms work in portions of room_board_2 and repeated approximately 375 ms draw stalls attributed to obj_board_controller. These cannot be eliminated by a presentation buffer alone. The log does not contain viewport geometry or a screenshot proving the origin of the reported crop.

Changes:
- Select viewCurrent before binding a view-owned surface. Both renderers consult the current view/camera during target selection; previously they could see the preceding view.
- Capped 30/40 FPS use the software deadline without an additional VBlank wait. 60 FPS and uncapped modes retain the VSync setting. This may change tearing behavior and needs hardware comparison.

Validation:
- Desktop build passed.
- Chapter 3 photograph crop and grayscale pixel tests passed on Legacy, Modern and Modern GL2.
- Six desktop smoke cases completed 600 frames: rhythm, gameshow and board_2 with Legacy/Modern.
- View-selection/pacing contract and existing Chapter 3 room contracts passed.
- Direct room_board_2 startup leaves its internal game screen black; it does not reproduce a normal Round 2 session and does not establish that the reported crop is fixed.

Hardware follow-up: enter Round 2 through normal gameplay, compare the complete internal screen at 30 and 60 FPS with identical renderer/settings, and collect a screenshot plus the log. Sustained CPU/render overload and obj_board_controller stalls remain open.
