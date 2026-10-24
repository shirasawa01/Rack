#!/bin/sh
# shot_rack.sh — クラウド（Linux）で、ビルドした改造版 Rack を xvfb の上で起動し、パッチの画面を撮る（試作の画面と見比べる用）。
#   sh reference/shot_rack.sh [patch.vcv] [out.png]    既定：reference/test_cables.vcv → $WORK/shot.png
#   設定を足すとき：SETTINGS='"cableStyle": "round"' sh reference/shot_rack.sh …（settings.json に足す）
# 設定・ログ・プラグインは $WORK に置く（-u）。リポジトリの中は汚さない。要るもの：xvfb、imagemagick（import）。
set -e
RACK=$(cd "$(dirname "$0")/.." && pwd)
PATCH=$(realpath "${1:-$RACK/reference/test_cables.vcv}")
WORK=${WORK:-${TMPDIR:-/tmp}/rack_shot}
mkdir -p "$WORK"
OUT=$(realpath "${2:-$WORK/shot.png}")
cat > "$WORK/settings.json" <<JSON
{"showTipsOnLaunch": false, "autoCheckUpdates": false, "windowMaximized": false, "windowSize": [1600, 870], "windowPos": [0, 0]${SETTINGS:+, $SETTINGS}}
JSON
cd "$RACK"
xvfb-run -a -s "-screen 0 1600x900x24" sh -c "./Rack -d -u '$WORK' '$PATCH' > '$WORK/run.log' 2>&1 & P=\$!; sleep ${WAIT:-10}; import -window root '$OUT'; kill \$P"
echo "$OUT"
