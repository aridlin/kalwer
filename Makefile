CXX ?= g++
CXXFLAGS ?= -O2 -pipe
CXXFLAGS += -pthread -std=c++20 -Wall -Wextra -Wpedantic
PKGS = gtk+-3.0 json-glib-1.0 pangocairo epoxy vte-2.91
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all clean install

all: elephant-field

elephant-field: main.cpp elevation_linux.hpp system_file_index.hpp launcher_commands.hpp update_status.hpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags $(PKGS)) $< -o $@ $(shell pkg-config --libs $(PKGS)) -lm

install: elephant-field
	install -Dm755 elephant-field $(DESTDIR)$(BINDIR)/elephant-field
	ln -sfn elephant-field $(DESTDIR)$(BINDIR)/kalwer
	install -Dm644 assets/kalwer.svg $(DESTDIR)$(PREFIX)/share/icons/hicolor/scalable/apps/kalwer.svg
	install -Dm644 kalwer.desktop $(DESTDIR)$(PREFIX)/share/applications/kalwer.desktop

clean:
	rm -f elephant-field
