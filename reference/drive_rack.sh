#!/bin/sh
# drive_rack.sh — shot_rack.sh と同じく xvfb の上で改造版 Rack を起動し、xdotool でマウスを動かしてから撮る（触ったときの確かめ）。
#   ACTIONS='mousemove 430 365 mousedown 1 mousemove 430 300 mousemove 430 200 mouseup 1' sh reference/drive_rack.sh [patch.vcv] [out.png]
# ACTIONS は xdotool のコマンド（画面の座標、ウィンドウは左上 0,0・1600x870）。各コマンドの間に STEP 秒（既定 0.1）待つ。
# 操作のあと AFTER 秒（既定 4）待ってから撮る。要るもの：xvfb、imagemagick（import）、xdotool。
set -e
RACK=$(cd "$(dirname "$0")/.." && pwd)
PATCH=$(realpath "${1:-$RACK/reference/test_cables.vcv}")
WORK=${WORK:-${TMPDIR:-/tmp}/rack_shot}
mkdir -p "$WORK"
# PLUGIN=<understudy>/vcv/dist/Understudy を付けると、そのプラグインも読ませる（-u では $WORK/plugins を見る）
if [ -n "$PLUGIN" ]; then mkdir -p "$WORK/plugins" && rm -rf "$WORK/plugins/$(basename "$PLUGIN")" && cp -r "$PLUGIN" "$WORK/plugins/"; fi
OUT=$(realpath "${2:-$WORK/drive.png}")
cat > "$WORK/settings.json" <<JSON
{"showTipsOnLaunch": false, "autoCheckUpdates": false, "windowMaximized": false, "windowSize": [1600, 870], "windowPos": [0, 0]${SETTINGS:+, $SETTINGS}}
JSON
cat > "$WORK/actions.sh" <<SH
set -- $ACTIONS
while [ \$# -gt 0 ]; do
	case "\$1" in
		mousemove) xdotool mousemove "\$2" "\$3"; shift 3 ;;
		mousedown|mouseup|click) xdotool "\$1" "\$2"; shift 2 ;;
		keydown|keyup|key) xdotool "\$1" "\$2"; shift 2 ;;
		sleep) sleep "\$2"; shift 2 ;;
		*) echo "unknown action \$1"; exit 1 ;;
	esac
	sleep ${STEP:-0.1}
done
SH
cd "$RACK"
xvfb-run -a -s "-screen 0 1600x900x24" sh -c "./Rack -d -u '$WORK' '$PATCH' > '$WORK/run.log' 2>&1 & P=\$!; sleep ${WAIT:-10}; sh '$WORK/actions.sh'; sleep ${AFTER:-4}; import -window root '$OUT'; kill \$P"
echo "$OUT"
