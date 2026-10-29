#!/bin/sh
set -eu
export VITE_SUPABASE_URL='https://xypisxdlxcqyavixdiji.supabase.co'
export VITE_SUPABASE_ANON_KEY='sb_publishable_Wn_srpPJ3lIYOMWwDASRyQ_mqp37Pnc'
export VITE_PUBLIC_APP_URL='https://bizuming1-ui.github.io/xingyi-isolated-test/'
export VITE_DEVICE_PREVIEW='true'
export VITE_DEVICE_CONFIRM_PREVIEW='false'
export VITE_ISOLATED_DEVICE_TEST='true'
npx vite build --config vite.demo.config.js
mkdir -p public-demo
# Vite's minifier emits a literal U+001F inside two JS string literals; an HTML
# parser rejects the control character. Escaping it preserves JS semantics.
python3 - <<'PYCODE'
from pathlib import Path
raw=Path('.demo-output/index.html').read_bytes().replace(b'\x1f',b'\\x1f')
if any(c < 32 and c not in (9,10,13) for c in raw): raise SystemExit('unexpected control character in demo HTML')
Path('public-demo/index.html').write_bytes(raw)
PYCODE
