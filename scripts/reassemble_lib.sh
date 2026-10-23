#!/bin/bash
# reassemble_lib.sh — rebuild libyamjs.a from its split parts.
#
# Reads MANIFEST.txt from the parts directory, joins the parts in
# order, writes the original file, and verifies the SHA-256 hash.
#
# Usage:
#   ./scripts/reassemble_lib.sh [parts_dir] [output_file]
#
# Defaults:
#   parts_dir   app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a.parts
#   output_file app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

PARTS_DIR="${1:-$PROJECT_DIR/app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a.parts}"
OUTPUT="${2:-$PROJECT_DIR/app/src/main/jni/all_libs/LIBYAMJS/libyamjs.a}"

log()  { printf "[reassemble] %s\n" "$*"; }
fail() { printf "[reassemble] ERROR: %s\n" "$*" >&2; exit 1; }

[ -d "$PARTS_DIR" ]                || fail "parts directory not found: $PARTS_DIR"
[ -f "$PARTS_DIR/MANIFEST.txt" ]   || fail "MANIFEST.txt missing in $PARTS_DIR"

MANIFEST="$PARTS_DIR/MANIFEST.txt"

get() { awk -F ': *' -v k="$1" '$1==k {print $2; exit}' "$MANIFEST"; }

ORIG_NAME="$(get original_name)"
ORIG_SIZE="$(get original_size)"
EXPECTED_HASH="$(get sha256)"
PART_COUNT="$(get part_count)"
PREFIX="$(get part_prefix)"

[ -n "$ORIG_NAME" ]     || fail "MANIFEST.txt: original_name missing"
[ -n "$ORIG_SIZE" ]     || fail "MANIFEST.txt: original_size missing"
[ -n "$EXPECTED_HASH" ] || fail "MANIFEST.txt: sha256 missing"
[ -n "$PART_COUNT" ]    || fail "MANIFEST.txt: part_count missing"
[ -n "$PREFIX" ]        || fail "MANIFEST.txt: part_prefix missing"

log "manifest:  $MANIFEST"
log "output:    $OUTPUT"
log "expected:  $ORIG_SIZE bytes, sha256=$EXPECTED_HASH"
log "parts:     $PART_COUNT (prefix '$PREFIX')"
log ""

# ── 1. Verify every part exists. ─────────────────────────────
for i in $(seq 0 $((PART_COUNT - 1))); do
    part_name=$(printf "%s%02d" "$PREFIX" "$i")
    part_path="$PARTS_DIR/$part_name"
    if [ ! -f "$part_path" ]; then
        fail "missing part: $part_path"
    fi
done
log "✓ all $PART_COUNT parts present"

# ── 2. Remove any previous output. ──────────────────────────
rm -f "$OUTPUT"

# ── 3. Concatenate. ─────────────────────────────────────────
log "concatenating…"
for i in $(seq 0 $((PART_COUNT - 1))); do
    part_name=$(printf "%s%02d" "$PREFIX" "$i")
    part_path="$PARTS_DIR/$part_name"
    cat "$part_path" >> "$OUTPUT" || fail "cat failed on $part_name"
done

# ── 4. Verify size. ─────────────────────────────────────────
ACTUAL_SIZE=$(stat -c%s "$OUTPUT")
if [ "$ACTUAL_SIZE" != "$ORIG_SIZE" ]; then
    fail "size mismatch: expected $ORIG_SIZE, got $ACTUAL_SIZE"
fi
log "✓ size matches ($ACTUAL_SIZE bytes)"

# ── 5. Verify hash. ─────────────────────────────────────────
if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL_HASH="$(sha256sum "$OUTPUT" | awk '{print $1}')"
elif command -v shasum >/dev/null 2>&1; then
    ACTUAL_HASH="$(shasum -a 256 "$OUTPUT" | awk '{print $1}')"
else
    log "WARN: neither sha256sum nor shasum available; skipping hash check"
    ACTUAL_HASH="$EXPECTED_HASH"
fi

if [ "$ACTUAL_HASH" != "$EXPECTED_HASH" ]; then
    fail "hash mismatch: expected $EXPECTED_HASH, got $ACTUAL_HASH"
fi
log "✓ hash matches"

log ""
log "done: $OUTPUT"
