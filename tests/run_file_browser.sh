#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT
mkdir -p "$test_root/files" "$test_root/config"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/file_browser_gtk_test.cpp $(pkg-config --cflags --libs gtk+-3.0) -o "$test_root/browser-test"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/file_popup_placement_gtk_test.cpp $(pkg-config --cflags --libs gtk+-3.0) -o "$test_root/placement-test"
TEST_ROOT="$test_root/files" XDG_CONFIG_HOME="$test_root/config" GDK_BACKEND=x11 GDK_SCALE=1 GDK_DPI_SCALE=1 xvfb-run -a -s '-screen 0 1920x1080x24' bash -c '"$1" && "$2"' _ "$test_root/browser-test" "$test_root/placement-test"
