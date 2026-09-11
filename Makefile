CXX ?= g++
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -pthread -std=c++20 -Wall -Wextra -Wpedantic
PKGS = wayland-client gtk+-3.0 json-glib-1.0 pangocairo epoxy vte-2.91
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install

all: elephant-field

elephant-field: main.cpp build-koom/bundle.o appearance.hpp live_backdrop.hpp backdrop_linux.hpp protocols/toplevel-protocol.o peggle.hpp garden.hpp chess.hpp shop.hpp tetris.hpp breakout.hpp kar.hpp kar_physics.hpp kar_course.hpp game_trials.hpp koom.hpp calculator_format.hpp passive_koins.hpp games.hpp games_gtk.hpp gpu_game.hpp gpu_dither.hpp elevation_linux.hpp system_file_index.hpp launcher_commands.hpp update_status.hpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) $< protocols/toplevel-protocol.o build-koom/bundle.o -Wl,-z,noexecstack -o $@ $(shell pkg-config --libs $(PKGS)) -lm

install: elephant-field
	install -Dm755 elephant-field $(DESTDIR)$(BINDIR)/elephant-field
	ln -sfn elephant-field $(DESTDIR)$(BINDIR)/kalwer
	install -Dm644 assets/kalwer.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/kalwer.svg
	install -Dm644 kalwer.desktop $(DESTDIR)$(PREFIX)/share/applications/kalwer.desktop

clean:
	rm -f elephant-field

protocols/toplevel-protocol.o: protocols/toplevel-protocol.c
	$(CC) -c $< -o $@

build-koom/kalwer-koom: koom/host.c koom/audio.c koom/build.py $(wildcard vendor/doomgeneric/*.[ch]) $(wildcard vendor/miniaudio/*.h) $(wildcard vendor/TinySoundFont/*.h)
	python3 koom/build.py

build-koom/bundle.o: build-koom/kalwer-koom assets/koom/freedoom2.wad assets/koom/TimGM6mb.sf2
	ld -r -b binary assets/koom/freedoom2.wad assets/koom/TimGM6mb.sf2 build-koom/kalwer-koom -o $@
	objcopy --rename-section .data=.rodata,alloc,load,readonly,data,contents $@
