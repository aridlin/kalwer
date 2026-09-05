#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ -n "${JAVA_HOME:-}" ]; then PATH="$JAVA_HOME/bin:$PATH"; export PATH; fi
command -v javac >/dev/null || { echo 'A JDK is required; set JAVA_HOME to JDK 17 or 21.' >&2; exit 1; }
test_build=$(mktemp -d /tmp/kalwer-search-tests.XXXXXX)
trap 'rm -f "$test_build"/pl/aridlin/kalwer/*.class; rmdir "$test_build"/pl/aridlin/kalwer "$test_build"/pl/aridlin "$test_build"/pl "$test_build" 2>/dev/null || true' EXIT
javac -d "$test_build" app/src/main/java/pl/aridlin/kalwer/SearchLogic.java tests/SearchLogicTest.java
java -cp "$test_build" pl.aridlin.kalwer.SearchLogicTest
