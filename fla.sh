#!/bin/sh

set -eu

FQBN=${FQBN:-m5stack:esp32:m5stack_stopwatch}
SKETCH_DIR=${SKETCH_DIR:-$(CDPATH= cd "$(dirname "$0")" && pwd)}
BUILD_DIR=${BUILD_DIR:-${TMPDIR:-/tmp}/counterclock-arduino-build}
PORT=${PORT:-}
COMPILE_ONLY=0

usage() {
	cat <<EOF
Usage: $0 [options]

Compile counterclock.ino and upload it to an attached M5Stack StopWatch.

Options:
  -p, --port PORT       Serial port to upload to, for example /dev/cu.usbmodem1101
  -b, --fqbn FQBN       Arduino board FQBN (default: $FQBN)
  -c, --compile-only    Compile without uploading
  -h, --help            Show this help

Environment:
  ARDUINO_CLI           Path to arduino-cli
  ARDUINO_LIBRARIES     Path to Arduino libraries
  BUILD_DIR             Build output directory (default: $BUILD_DIR)
  PORT                  Serial port, same as --port
  FQBN                  Board FQBN, same as --fqbn
EOF
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		-p|--port)
			if [ "$#" -lt 2 ]; then
				echo "Missing value for $1" >&2
				exit 2
			fi
			PORT=$2
			shift 2
			;;
		-b|--fqbn)
			if [ "$#" -lt 2 ]; then
				echo "Missing value for $1" >&2
				exit 2
			fi
			FQBN=$2
			shift 2
			;;
		-c|--compile-only)
			COMPILE_ONLY=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown option: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

find_arduino_cli() {
	if [ -n "${ARDUINO_CLI:-}" ]; then
		printf '%s\n' "$ARDUINO_CLI"
		return
	fi

	if command -v arduino-cli >/dev/null 2>&1; then
		command -v arduino-cli
		return
	fi

	for candidate in \
		"/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli" \
		"/opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli" \
		"/usr/local/bin/arduino-cli" \
		"/opt/homebrew/bin/arduino-cli"
	do
		if [ -x "$candidate" ]; then
			printf '%s\n' "$candidate"
			return
		fi
	done

	echo "Could not find arduino-cli. Install it or set ARDUINO_CLI=/path/to/arduino-cli." >&2
	exit 1
}

find_libraries() {
	if [ -n "${ARDUINO_LIBRARIES:-}" ]; then
		printf '%s\n' "$ARDUINO_LIBRARIES"
		return
	fi

	for candidate in \
		"$HOME/Library/Arduino/libraries" \
		"$HOME/Arduino/libraries"
	do
		if [ -d "$candidate" ]; then
			printf '%s\n' "$candidate"
			return
		fi
	done

	printf '%s\n' ""
}

first_existing_port() {
	for pattern in \
		/dev/cu.usbmodem* \
		/dev/cu.usbserial* \
		/dev/ttyACM* \
		/dev/ttyUSB*
	do
		for candidate in $pattern; do
			if [ -e "$candidate" ]; then
				printf '%s\n' "$candidate"
				return
			fi
		done
	done
}

ARDUINO_CLI_PATH=$(find_arduino_cli)
LIBRARIES=$(find_libraries)

compile_cmd() {
	if [ -n "$LIBRARIES" ]; then
		"$ARDUINO_CLI_PATH" compile --fqbn "$FQBN" --libraries "$LIBRARIES" --build-path "$BUILD_DIR" "$SKETCH_DIR"
	else
		"$ARDUINO_CLI_PATH" compile --fqbn "$FQBN" --build-path "$BUILD_DIR" "$SKETCH_DIR"
	fi
}

echo "Compiling $SKETCH_DIR"
echo "Board: $FQBN"
if [ -n "$LIBRARIES" ]; then
	echo "Libraries: $LIBRARIES"
else
	echo "Libraries: arduino-cli defaults"
fi
compile_cmd

if [ "$COMPILE_ONLY" -eq 1 ]; then
	exit 0
fi

if [ -z "$PORT" ]; then
	PORT=$(first_existing_port || true)
fi

if [ -z "$PORT" ]; then
	echo "No serial port found. Connect the board or pass --port /dev/..." >&2
	exit 1
fi

echo "Uploading to $PORT"
"$ARDUINO_CLI_PATH" upload --fqbn "$FQBN" --port "$PORT" --input-dir "$BUILD_DIR" "$SKETCH_DIR"
