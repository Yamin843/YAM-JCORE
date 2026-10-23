#!/bin/bash
# build_dex.sh — compile the Java sources under app/src/main/java to a
# single classes.dex, then embed it as a C header for the native library.
#
# Environment:
#   ANDROID_HOME    — path to the Android SDK (falls back to
#                     ANDROID_SDK_ROOT, then to common Termux paths).
#
# Tool chain used:
#   javac (JDK 8+)
#   d8    (from Android build-tools)
#   android.jar from the newest installed platform
#
# Output:
#   build/dex/classes.dex
#   app/src/main/jni/Generated/embedded_dex.h

set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
JAVA_SRC_DIR="$PROJECT_DIR/app/src/main/java"
OUT_HDR="$PROJECT_DIR/app/src/main/jni/Generated/embedded_dex.h"
BUILD_DIR="$PROJECT_DIR/build/dex"

log()  { printf "[build_dex] %s\n" "$*"; }
fail() { printf "[build_dex] ERROR: %s\n" "$*" >&2; exit 1; }

# ── 1. Resolve SDK location ─────────────────────────────────
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"

if [ -z "$SDK" ]; then
    for candidate in \
        "$HOME/Android/Sdk" \
        "$HOME/android-sdk" \
        "/data/data/com.termux/files/usr/opt/android-sdk" \
        "/data/data/com.termux/files/home/android-sdk" \
        "/opt/android-sdk"; do
        if [ -d "$candidate" ]; then
            SDK="$candidate"
            break
        fi
    done
fi

[ -n "$SDK" ]      || fail "ANDROID_HOME / ANDROID_SDK_ROOT is not set and no common path was found"
[ -d "$SDK" ]      || fail "SDK directory does not exist: $SDK"
[ -d "$SDK/build-tools" ] || fail "no build-tools in SDK: $SDK/build-tools"
[ -d "$SDK/platforms" ]   || fail "no platforms in SDK: $SDK/platforms"

# ── 2. Pick the newest build-tools and platform ─────────────
BUILD_TOOLS="$(ls -1 "$SDK/build-tools"    | sort -V | tail -n1)"
PLATFORM="$(ls -1 "$SDK/platforms"         | sort -V | tail -n1)"
ANDROID_JAR="$SDK/platforms/$PLATFORM/android.jar"
D8="$SDK/build-tools/$BUILD_TOOLS/d8"

[ -f "$ANDROID_JAR" ] || fail "android.jar not found: $ANDROID_JAR"
[ -x "$D8" ]          || fail "d8 not found or not executable: $D8"

log "SDK:           $SDK"
log "build-tools:   $BUILD_TOOLS"
log "platform:      $PLATFORM"
log "android.jar:   $ANDROID_JAR"
log "d8:            $D8"

# ── 3. javac must exist ────────────────────────────────────
command -v javac >/dev/null 2>&1 || fail "javac not found in PATH"
JAVAC="javac"

# ── 4. Clean and rebuild ───────────────────────────────────
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/classes" "$BUILD_DIR/dex"

# Collect every .java file under the source tree.
mapfile -t SOURCES < <(find "$JAVA_SRC_DIR" -type f -name '*.java')
[ "${#SOURCES[@]}" -gt 0 ] || fail "no .java files found under $JAVA_SRC_DIR"

log "compiling ${#SOURCES[@]} java source file(s)"

"$JAVAC" \
    -source 8 \
    -target 8 \
    -bootclasspath "$ANDROID_JAR" \
    -classpath "$ANDROID_JAR" \
    -d "$BUILD_DIR/classes" \
    "${SOURCES[@]}" \
    || fail "javac failed"

mapfile -t CLASSES < <(find "$BUILD_DIR/classes" -type f -name '*.class')
[ "${#CLASSES[@]}" -gt 0 ] || fail "javac produced no .class files"

log "produced ${#CLASSES[@]} .class file(s)"

# ── 5. Convert to DEX ──────────────────────────────────────
log "running d8"

"$D8" \
    --output "$BUILD_DIR/dex/" \
    --lib "$ANDROID_JAR" \
    --min-api 24 \
    --no-desugaring \
    "${CLASSES[@]}" \
    || fail "d8 failed"

[ -f "$BUILD_DIR/dex/classes.dex" ] || fail "classes.dex not produced"

DEX_SIZE="$(stat -c%s "$BUILD_DIR/dex/classes.dex")"
log "classes.dex: $DEX_SIZE bytes"

# ── 6. Embed as C header ───────────────────────────────────
log "generating $OUT_HDR"
python3 "$SCRIPT_DIR/dex_to_header.py" \
    "$BUILD_DIR/dex/classes.dex" \
    "$OUT_HDR" \
    || fail "dex_to_header.py failed"

log "done"
