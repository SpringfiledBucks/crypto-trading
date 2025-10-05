#!/usr/bin/env bash
# Wrapper script to build and run the test_sse_client tool
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build"
LOG_DIR="$ROOT_DIR/logs"

usage(){
  cat <<EOF
usage: test_sse.sh [--build] [--help] [-- <args>]

Options:
  --build    run cmake/make to (re)build test_sse_client before running
  --help     show this help

All args after '--' are passed to the test binary (e.g. --host, --port, --log, --delay, --timeout)

Examples:
  # build and run with defaults
  ./scripts/test_sse.sh --build --

  # run against custom host/port/logpath
  ./scripts/test_sse.sh -- --host 127.0.0.1 --port 8080 --log $ROOT_DIR/logs/runtime.log --timeout 15
EOF
}

BUILD=false
POSITIONAL=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build) BUILD=true; shift ;;
    --help|-h) usage; exit 0 ;;
    --) shift; POSITIONAL+=("$@"); break ;;
    *) echo "Unknown arg: $1"; usage; exit 2 ;;
  esac
done

if [ "$BUILD" = true ]; then
  mkdir -p "$BUILD_DIR"
  pushd "$BUILD_DIR" >/dev/null
  cmake ..
  make -j test_sse_client
  popd >/dev/null
fi

mkdir -p "$LOG_DIR"

TEST_BIN="$BUILD_DIR/test_sse_client"
if [ ! -x "$TEST_BIN" ]; then
  echo "test binary not found: $TEST_BIN" >&2
  echo "Run with --build first or build the project." >&2
  exit 3
fi

exec "$TEST_BIN" "${POSITIONAL[@]}"
