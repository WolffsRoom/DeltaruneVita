from pathlib import Path


root = Path(__file__).resolve().parents[1]
audio = (root / "src/butterscotch/src/audio/openal/al_audio_system.c").read_text(encoding="utf-8")

# AL_INITIAL is the normal state immediately after a recovered stream has been
# rewound, re-queued and submitted to Vita's asynchronous mixer. Treating that
# state as another underrun rebuilds the queue on every game frame. Only a
# genuinely stopped source may enter destructive underrun recovery.
assert "stateBeforeRecovery == AL_STOPPED" in audio
assert "stateBeforeRecovery == AL_INITIAL" not in audio

print("vita stream recovery contracts: PASS")
