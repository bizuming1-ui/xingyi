#!/bin/sh
set -eu
npx vite build --config vite.device-demo.config.js
mkdir -p public-device
python3 - <<'PYCODE'
from pathlib import Path
raw=Path('.device-demo-output/device/index.html').read_bytes().replace(b'\x1f',b'\\x1f')
if any(c < 32 and c not in (9,10,13) for c in raw): raise SystemExit('unexpected control character in device demo HTML')
Path('public-device/index.html').write_bytes(raw)
PYCODE
