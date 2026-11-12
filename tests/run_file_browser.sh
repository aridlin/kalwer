#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT
mkdir -p "$test_root/files" "$test_root/config"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/file_browser_gtk_test.cpp $(pkg-config --cflags --libs gtk+-3.0 libcurl json-glib-1.0 gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) -o "$test_root/browser-test"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/file_popup_placement_gtk_test.cpp $(pkg-config --cflags --libs gtk+-3.0 libcurl json-glib-1.0 gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) -o "$test_root/placement-test"
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/file_surface_test.cpp $(pkg-config --cflags --libs cairo) -o "$test_root/surface-test"
"$test_root/surface-test"
TEST_ROOT="$test_root/files" XDG_CONFIG_HOME="$test_root/config" GDK_BACKEND=x11 GDK_SCALE=1 GDK_DPI_SCALE=1 xvfb-run -a -s '-screen 0 1920x1080x24' bash -c '"$1" && "$2"' _ "$test_root/browser-test" "$test_root/placement-test"
