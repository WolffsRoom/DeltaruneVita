import gzip
import re
import sys
from pathlib import Path


def main() -> int:
    source = Path(sys.argv[1])
    target = Path(sys.argv[2])
    with gzip.open(source, "rb") as handle:
        data = handle.read()
    strings = re.findall(rb"[\x20-\x7e\r\n\t]{4,}", data)
    target.write_bytes(b"\n".join(strings))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
