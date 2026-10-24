#!/usr/bin/env bash
# Runs headless test scripts in ArmorPaint, see readme.md
# Usage: base/tests/paint_headless/run.sh [script.c]
#   no script: run.c and every phase test (p1_*.c, p2_*.c, ...)
# Exit code: 0 pass, 1 fail, 2 setup error

set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
OUT="$ROOT/paint/build/out"
APP="${ARMORPAINT_APP:-$OUT/ArmorPaint}"
TIMEOUT="${SMOKE_TIMEOUT:-600}"
if [ $# -gt 0 ]; then
	SCRIPTS=("$1")
else
	SCRIPTS=(run.c)
	for f in "$HERE"/p[0-9]*_*.c; do
		[ -f "$f" ] && SCRIPTS+=("$(basename "$f")")
	done
fi

if [ ! -x "$APP" ]; then
	echo "ArmorPaint not found at $APP, build it first: cd paint && ../base/make --compile"
	exit 2
fi
for SCRIPT in "${SCRIPTS[@]}"; do
	if [ ! -f "$HERE/$SCRIPT" ]; then
		echo "Script not found: $HERE/$SCRIPT"
		exit 2
	fi
done

# Start from the default config, a user config can be in sculpt mode or use another layer resolution
CONFIG="$OUT/data/config.json"
BACKUP="$OUT/data/config.json.smoke_backup"
if [ -f "$CONFIG" ]; then
	mv "$CONFIG" "$BACKUP"
fi
restore() {
	rm -f "$CONFIG"
	if [ -f "$BACKUP" ]; then
		mv "$BACKUP" "$CONFIG"
	fi
}
trap restore EXIT

RUN=()
if command -v timeout > /dev/null; then
	RUN+=(timeout "$TIMEOUT")
fi
if [ "$(uname)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	# No display: virtual X server, without a GPU Vulkan runs on llvmpipe (mesa-vulkan-drivers)
	if ! command -v xvfb-run > /dev/null; then
		echo "No display, install xvfb (and mesa-vulkan-drivers on a machine without a GPU)"
		exit 2
	fi
	if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
		export XDG_RUNTIME_DIR="$(mktemp -d)"
	fi
	RUN+=(xvfb-run -a -s "-screen 0 1280x720x24")
fi
if command -v stdbuf > /dev/null; then
	RUN+=(stdbuf -oL -eL) # Keep the log when the app is killed by the timeout
fi

mkdir -p "$ROOT/paint/build/smoke"
cd "$OUT" || exit 2
STATUS=0
for SCRIPT in "${SCRIPTS[@]}"; do
	LOG="$ROOT/paint/build/smoke/$(basename "$SCRIPT" .c).log"
	rm -f "$CONFIG" # Each script starts from the default config
	"${RUN[@]}" "$APP" --background --script "../../../base/tests/paint_headless/$SCRIPT" 2>&1 | tee "$LOG"
	if grep -q -e "SMOKE PASS" -e "FIXTURE DONE" "$LOG"; then
		echo "[$SCRIPT] passed"
	else
		echo "[$SCRIPT] failed, log: $LOG"
		STATUS=1
	fi
done
exit $STATUS
