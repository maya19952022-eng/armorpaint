#!/usr/bin/env bash
# Compiles ArmorPaint for every CI target this host can build, see base/docs/building_cloud.md
# Usage: scripts/ci_compile.sh [--clean] [--smoke] [--max-warnings N]
#   --clean           remove paint/build first, so every file is compiled and its warnings counted
#   --smoke           run the headless smoke test after a successful build (Linux and Windows)
#   --max-warnings N  fail when the build prints more than N compiler warnings
# CI targets: linux (Vulkan), macos (Metal), windows (Direct3D12), each one needs its own host OS.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLEAN=0
SMOKE=0
MAX_WARNINGS=-1
while [ $# -gt 0 ]; do
	case "$1" in
	--clean) CLEAN=1 ;;
	--smoke) SMOKE=1 ;;
	--max-warnings)
		shift
		MAX_WARNINGS="${1:-0}"
		;;
	*)
		echo "Unknown option $1"
		exit 2
		;;
	esac
	shift
done

case "$OSTYPE" in
linux-gnu*) HOST=linux ;;
darwin*) HOST=macos ;;
msys* | cygwin* | win32*) HOST=windows ;;
*)
	echo "Unsupported host $OSTYPE"
	exit 2
	;;
esac

if [ $CLEAN -eq 1 ]; then
	rm -rf "$ROOT/paint/build"
fi

LOG="$ROOT/paint/build_logs/$HOST.log" # Outside paint/build, which --clean removes, ignored by git
WARNING="warning( C[0-9]+)?:"              # clang "warning:", msvc "warning C4996:"
ERROR="(error|fatal error)( [A-Z]+[0-9]+)?:"
STATUS=0
for TARGET in linux macos windows; do
	if [ "$TARGET" != "$HOST" ]; then
		echo "[$TARGET] skipped, needs a $TARGET host (CI: .github/workflows)"
		continue
	fi
	mkdir -p "$(dirname "$LOG")"
	echo "[$TARGET] compiling, log: $LOG"
	START=$(date +%s)
	if [ "$HOST" = "windows" ]; then
		(cd "$ROOT/paint" && cmd //c "..\\base\\make.bat --compile") > "$LOG" 2>&1
	else
		(cd "$ROOT/paint" && ../base/make --compile) > "$LOG" 2>&1
	fi
	RESULT=$?
	WARNINGS=$(grep -c -E "$WARNING" "$LOG")
	echo "[$TARGET] exit $RESULT, $WARNINGS warnings, $(($(date +%s) - START))s"
	grep -E "$WARNING" "$LOG" | sed 's/^/    /' | head -50
	if [ $RESULT -ne 0 ]; then
		grep -E "$ERROR" "$LOG" | sed 's/^/    /' | head -50
		STATUS=1
		continue
	fi
	if [ "$MAX_WARNINGS" -ge 0 ] && [ "$WARNINGS" -gt "$MAX_WARNINGS" ]; then
		echo "[$TARGET] more than $MAX_WARNINGS warnings"
		STATUS=1
	fi
	if [ $SMOKE -eq 1 ]; then
		if [ "$HOST" = "windows" ]; then
			cmd //c "$(cygpath -w "$ROOT/base/tests/paint_headless/run.bat")" || STATUS=1
		elif [ "$HOST" = "linux" ]; then
			"$ROOT/base/tests/paint_headless/run.sh" > /dev/null || STATUS=1
			echo "[$TARGET] smoke test: $(grep -E 'checks,' "$ROOT/paint/build/smoke/run.log")"
		else
			echo "[$TARGET] smoke test skipped, not supported on macOS yet"
		fi
	fi
done
exit $STATUS
