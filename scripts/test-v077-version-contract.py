from pathlib import Path

root = Path(__file__).resolve().parents[1]
assert (root / "config/version.txt").read_text(encoding="utf-8").strip() == "0.78"
assert (root / "data/assets/build_version.txt").read_text(encoding="utf-8").strip() == "DELTARUNE VITA v0.78"

tool = (root / "tools/SetChapter0BuildVersion.csx").read_text(encoding="utf-8")
build = (root / "scripts/build-butterscotch-probe.ps1").read_text(encoding="utf-8")
assert 'const string newLine = "_version_text = \\"DELTARUNE VITA v0.78\\";";' in tool
assert 'Console.WriteLine("Chapter 0 build label: DELTARUNE VITA v0.78");' in tool
assert '_version_text = \\"DELTARUNE VITA v0.76\\";' in tool
assert "SetChapter0BuildVersion.csx" in build
assert "chapter0\\data.win" in build
assert "UndertaleModCli.exe" in build

print("v0.78 version contract: PASS")
