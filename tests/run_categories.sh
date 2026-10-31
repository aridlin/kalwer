#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT
g++ -std=c++20 -Wall -Wextra -Wpedantic tests/categories_test.cpp $(pkg-config --cflags --libs glib-2.0) -o "$test_root/categories-test"
"$test_root/categories-test"
make -j2
g++ -std=c++20 -O1 -pthread tests/categories_gtk_test.cpp protocols/toplevel-protocol.o build-koom/bundle.o $(pkg-config --cflags --libs wayland-client gtk+-3.0 json-glib-1.0 pangocairo epoxy vte-2.91) -lm -ldl -o "$test_root/categories-gtk-test"
GDK_BACKEND=x11 LIBGL_ALWAYS_SOFTWARE=1 XDG_CONFIG_HOME="$test_root/config" xvfb-run -a "$test_root/categories-gtk-test"
