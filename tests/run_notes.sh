#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT
mkdir -p "$test_root/bin" "$test_root/data"
ln -s "$PWD/tests/notes_mock.py" "$test_root/bin/curl"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/notes_test.cpp $(pkg-config --cflags --libs json-glib-1.0 gio-2.0) -o "$test_root/notes-test"
for mode in ok auth offline invalid; do
    PATH="$test_root/bin:$PATH" XDG_DATA_HOME="$test_root/data" NOTES_TEST_MODE="$mode" "$test_root/notes-test" "$mode"
done
