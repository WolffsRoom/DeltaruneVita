from pathlib import Path


root = Path(__file__).resolve().parents[1]
runner = (root / "src/butterscotch/src/runner.c").read_text(encoding="utf-8")
audio = (root / "src/butterscotch/src/audio/openal/al_audio_system.c").read_text(encoding="utf-8")

# Views redirected to small puzzle-monitor surfaces do not describe the
# application-surface viewport. Including their ports changes displayScale and
# shifts the complete room when the three monitors are enabled.
compute_start = runner.index("void Runner_computeViewDisplayScale")
compute_end = runner.index("static void expandViewAxis", compute_start)
compute = runner[compute_start:compute_end]
assert "view->surfaceId != -1" in compute

# OpenAL may return a prebuffered paused stream to AL_INITIAL. It still has a
# valid queue and must resume just like the STOPPED seek-rebuild path.
resume_start = audio.index("static void resumeInstanceIfReady")
resume_end = audio.index("static stb_vorbis* openStreamingVorbis", resume_start)
resume = audio[resume_start:resume_end]
assert "state == AL_STOPPED || state == AL_INITIAL" in resume
assert "AL_BUFFERS_QUEUED" in resume

# room_board_1_sword is submission-bound on Vita. Keep a stable half of its
# decorative trees, skipping both their updates and draws rather than changing
# the safety-oriented one-quad submission mode.
assert 'strcmp(objectName, "obj_board_tree") == 0' in runner
assert "eventType == EVENT_STEP || eventType == EVENT_DRAW" in runner
assert "(instance->instanceId & 1U) != 0" in runner

print("Chapter 3 room regressions: PASS")
