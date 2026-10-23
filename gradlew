#!/bin/sh
# Minimal gradlew wrapper. Works on Linux, macOS, and Termux.

set -u

APP_HOME=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
CLASSPATH="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

if [ ! -f "$CLASSPATH" ]; then
    echo "ERROR: missing gradle/wrapper/gradle-wrapper.jar" >&2
    echo "       Run 'gradle wrapper' once with a system Gradle install," >&2
    echo "       or copy the jar from another project." >&2
    exit 1
fi

if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/java" ]; then
    JAVACMD="$JAVA_HOME/bin/java"
else
    JAVACMD="$(command -v java)"
fi

if [ -z "$JAVACMD" ]; then
    echo "ERROR: java not found in PATH and JAVA_HOME is not set" >&2
    exit 1
fi

exec "$JAVACMD" \
    -Dorg.gradle.appname=gradlew \
    -classpath "$CLASSPATH" \
    org.gradle.wrapper.GradleWrapperMain \
    "$@"
