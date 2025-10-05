#!/usr/bin/env bash
set -euo pipefail
# rotate_logs.sh - compress selected logs into archive and truncate originals
# Usage: ./scripts/rotate_logs.sh [days_to_keep]

KEEP=${1:-7}
BASEDIR="$(cd "$(dirname "$0")/.." && pwd)"
LOGDIR="$BASEDIR/logs"
ARCHIVE_DIR="$BASEDIR/archive"
mkdir -p "$ARCHIVE_DIR"

TS=$(date -u +%Y%m%dT%H%M%SZ)
ARCHIVE_NAME="$ARCHIVE_DIR/logs-$TS.tgz"

# files to include in rotation
FILES=("$LOGDIR/crypto_trading.out" "$LOGDIR/runtime.log" "$LOGDIR/proxy_traffic.log" "$LOGDIR/refresh_monitor.log")
EXISTING=()
for f in "${FILES[@]}"; do
  if [ -f "$f" ] && [ -s "$f" ]; then
    EXISTING+=("$f")
  fi
done

if [ ${#EXISTING[@]} -eq 0 ]; then
  echo "rotate_logs: no log files to archive"
  exit 0
fi

tar -czf "$ARCHIVE_NAME" -C / "$(printf "%s\n" "${EXISTING[@]}" | sed -e 's#^/#/#')" || true
# create compressed tarball of existing logs
# Use relative paths to avoid tar complaining about absolute names; copy files to a temp dir if necessary
TMPDIR=$(mktemp -d)
for f in "${EXISTING[@]}"; do cp -p "$f" "$TMPDIR/"; done
tar -C "$TMPDIR" -czf "$ARCHIVE_NAME" . || true
rm -rf "$TMPDIR"
# confirm size
du -h "$ARCHIVE_NAME" | awk '{print "created archive: " $2 " size=" $1}'

# truncate original files (copytruncate semantics)
for f in "${EXISTING[@]}"; do
  # preserve permissions
  : > "$f"
done

# prune old archives (keep most recent $KEEP)
cd "$ARCHIVE_DIR"
ls -1t logs-*.tgz 2>/dev/null | tail -n +$((KEEP+1)) | xargs -r rm -f || true

echo "rotate_logs: completed, kept $KEEP archives in $ARCHIVE_DIR"
